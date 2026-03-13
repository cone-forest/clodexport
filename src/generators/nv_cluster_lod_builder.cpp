#ifdef CLODEXPORT_HAS_NVCLUSTERLOD

#include "generators/nv_cluster_lod_builder.h"
#include "generator.h"
#include "obj_loader.h"

#include <nvcluster/nvcluster.h>
#include <nvclusterlod/nvclusterlod_common.h>
#include <nvclusterlod/nvclusterlod_mesh.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

// nv_cluster_lod_builder produces a DAG of groups of clusters over LOD levels.
// This adapter maps that structure into the shared ExportedGroup/ExportedCluster
// schema used by all generators:
// - Groups correspond to entries in groupClusterRanges/lodLevelGroupRanges
//   (LOD 0 = finest); ExportedGroup.depth is the LOD index and bounds/error
//   are aggregated per group.
// - For each cluster, ExportedCluster.refined is the group id in
//   clusterGeneratingGroups[c] that was decimated to produce this cluster,
//   or -1 for NVCLUSTERLOD_ORIGINAL_MESH_GROUP. This points to a *finer* group.
// - groupQuadricErrors[g] is used as ExportedGroup.bounds.error; per-cluster
//   error is groupQuadricErrors[clusterGeneratingGroups[c]].
// Note that, unlike meshoptimizer's clusterlod, groups may contain clusters
// originating from multiple refined groups; refined is therefore a per-cluster
// property and not uniform within a group.

static float edgeLength(const float* positions, size_t positions_stride_floats,
    unsigned int a, unsigned int b)
{
	float dx = positions[a * positions_stride_floats + 0] - positions[b * positions_stride_floats + 0];
	float dy = positions[a * positions_stride_floats + 1] - positions[b * positions_stride_floats + 1];
	float dz = positions[a * positions_stride_floats + 2] - positions[b * positions_stride_floats + 2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void computeBoundaryStats(const float* positions, size_t positions_stride_floats,
    const std::vector<std::vector<unsigned int>>& cluster_indices,
    std::vector<float>& boundary_inner, std::vector<float>& boundary_outer)
{
	boundary_inner.assign(cluster_indices.size(), 0.f);
	boundary_outer.assign(cluster_indices.size(), 0.f);

	typedef std::pair<unsigned int, unsigned int> Edge;
	std::map<Edge, std::vector<size_t>> edge_to_clusters;

	for (size_t c = 0; c < cluster_indices.size(); ++c)
	{
		const std::vector<unsigned int>& ind = cluster_indices[c];
		for (size_t t = 0; t + 2 < ind.size(); t += 3)
		{
			unsigned int i0 = ind[t], i1 = ind[t + 1], i2 = ind[t + 2];
			Edge e01 = (i0 < i1) ? Edge{i0, i1} : Edge{i1, i0};
			Edge e12 = (i1 < i2) ? Edge{i1, i2} : Edge{i2, i1};
			Edge e20 = (i2 < i0) ? Edge{i2, i0} : Edge{i0, i2};
			edge_to_clusters[e01].push_back(c);
			edge_to_clusters[e12].push_back(c);
			edge_to_clusters[e20].push_back(c);
		}
	}

	for (const auto& kv : edge_to_clusters)
	{
		unsigned int a = kv.first.first, b = kv.first.second;
		float len = edgeLength(positions, positions_stride_floats, a, b);
		const std::vector<size_t>& clusters_with_edge = kv.second;
		std::map<size_t, size_t> cluster_count;
		for (size_t c : clusters_with_edge)
			cluster_count[c]++;
		std::vector<size_t> boundary_clusters;
		for (const auto& cc : cluster_count)
		{
			if (cc.second == 1)
				boundary_clusters.push_back(cc.first);
		}
		size_t n = boundary_clusters.size();
		if (n >= 2)
		{
			for (size_t c : boundary_clusters)
				boundary_inner[c] += len / (float)n;
		}
		else if (n == 1)
			boundary_outer[boundary_clusters[0]] += len;
	}
}

static int getGroupDepth(uint32_t groupIndex, uint32_t groupCount,
    const nvcluster_Range* lodLevelGroupRanges, uint32_t lodLevelCount)
{
	for (uint32_t l = 0; l < lodLevelCount; ++l)
	{
		const nvcluster_Range& r = lodLevelGroupRanges[l];
		if (groupIndex >= r.offset && groupIndex < r.offset + r.count)
			return (int)l;
	}
	return 0;
}

static uint32_t getClusterGroupId(uint32_t clusterIndex, uint32_t groupCount,
    const nvcluster_Range* groupClusterRanges)
{
	for (uint32_t g = 0; g < groupCount; ++g)
	{
		const nvcluster_Range& r = groupClusterRanges[g];
		if (clusterIndex >= r.offset && clusterIndex < r.offset + r.count)
			return g;
	}
	return 0;
}

bool runNvClusterLod(const Mesh& mesh, const GeneratorOptions& opts,
    std::vector<ExportedGroup>& groups, std::vector<ExportedCluster>& clusters)
{
	groups.clear();
	clusters.clear();

	if (mesh.vertices.empty() || mesh.indices.size() < 3 || (mesh.indices.size() % 3) != 0)
		return false;

	// Create nvcluster context
	nvcluster_ContextCreateInfo clusterInfo = nvcluster_defaultContextCreateInfo();
	nvcluster_Context clusterCtx = nullptr;
	if (nvclusterCreateContext(&clusterInfo, &clusterCtx) != NVCLUSTER_SUCCESS)
		return false;

	// Create nvclusterlod context (uses cluster context)
	nvclusterlod_ContextCreateInfo lodInfo = {};
	lodInfo.version = NVCLUSTERLOD_VERSION;
	lodInfo.parallelize = NVCLUSTER_TRUE;
	lodInfo.clusterContext = clusterCtx;

	nvclusterlod_Context lodCtx = nullptr;
	nvclusterlod_Result result = nvclusterlodCreateContext(&lodInfo, &lodCtx);
	if (result != NVCLUSTERLOD_SUCCESS)
	{
		nvclusterDestroyContext(clusterCtx);
		return false;
	}

	// Build mesh input: triangles from indices, positions from vertices (interleaved)
	nvclusterlod_MeshInput meshInput = {};
	meshInput.triangleVertices = reinterpret_cast<const nvclusterlod_Vec3u*>(mesh.indices.data());
	meshInput.triangleCount = (uint32_t)(mesh.indices.size() / 3);
	meshInput.vertexPositions = reinterpret_cast<const nvcluster_Vec3f*>(&mesh.vertices[0].px);
	meshInput.vertexCount = (uint32_t)mesh.vertices.size();
	meshInput.vertexStride = (uint32_t)sizeof(Vertex);
	meshInput.clusterConfig = nvclusterlod_defaultClusterConfig();
	meshInput.groupConfig = nvclusterlod_defaultGroupConfig();
	meshInput.decimationFactor = 0.5f;

	// Get requirements and allocate output
	nvclusterlod_MeshCounts counts = {};
	result = nvclusterlodGetMeshRequirements(lodCtx, &meshInput, &counts);
	if (result != NVCLUSTERLOD_SUCCESS)
	{
		nvclusterlodDestroyContext(lodCtx);
		nvclusterDestroyContext(clusterCtx);
		return false;
	}

	std::vector<nvcluster_Range> clusterTriangleRanges(counts.clusterCount);
	std::vector<nvclusterlod_Vec3u> triangleVertices(counts.triangleCount);
	std::vector<uint32_t> clusterGeneratingGroups(counts.clusterCount);
	std::vector<nvclusterlod_Sphere> clusterBoundingSpheres(counts.clusterCount);
	std::vector<float> groupQuadricErrors(counts.groupCount);
	std::vector<nvcluster_Range> groupClusterRanges(counts.groupCount);
	std::vector<nvcluster_Range> lodLevelGroupRanges(counts.lodLevelCount);

	nvclusterlod_MeshOutput meshOutput = {};
	meshOutput.clusterTriangleRanges = clusterTriangleRanges.data();
	meshOutput.triangleVertices = triangleVertices.data();
	meshOutput.clusterGeneratingGroups = clusterGeneratingGroups.data();
	meshOutput.clusterBoundingSpheres = clusterBoundingSpheres.data();
	meshOutput.groupQuadricErrors = groupQuadricErrors.data();
	meshOutput.groupClusterRanges = groupClusterRanges.data();
	meshOutput.lodLevelGroupRanges = lodLevelGroupRanges.data();
	meshOutput.clusterCount = counts.clusterCount;
	meshOutput.groupCount = counts.groupCount;
	meshOutput.lodLevelCount = counts.lodLevelCount;
	meshOutput.triangleCount = counts.triangleCount;

	result = nvclusterlodBuildMesh(lodCtx, &meshInput, &meshOutput);
	nvclusterlodDestroyContext(lodCtx);
	nvclusterDestroyContext(clusterCtx);

	if (result != NVCLUSTERLOD_SUCCESS)
		return false;

	// Library may have written actual counts back into meshOutput
	uint32_t numGroups = meshOutput.groupCount;
	uint32_t numClusters = meshOutput.clusterCount;
	uint32_t numLodLevels = meshOutput.lodLevelCount;

	// Build ExportedGroups: one per nv group, depth = LOD level
	groups.reserve(numGroups);
	for (uint32_t g = 0; g < numGroups; ++g)
	{
		ExportedGroup gr = {};
		gr.depth = getGroupDepth(g, numGroups,
		    lodLevelGroupRanges.data(), numLodLevels);
		// Aggregate bounds from clusters in this group. We compute a loose
		// bounding sphere from the AABBs of all cluster spheres.
		gr.bounds.center[0] = 0.f;
		gr.bounds.center[1] = 0.f;
		gr.bounds.center[2] = 0.f;
		gr.bounds.radius = 0.f;
		gr.bounds.error = g < groupQuadricErrors.size() ? groupQuadricErrors[g] : 0.f;
		const nvcluster_Range& cr = groupClusterRanges[g];
		if (cr.count > 0 && (cr.offset + cr.count) <= clusterBoundingSpheres.size())
		{
			float min_xyz[3] = {0.f, 0.f, 0.f};
			float max_xyz[3] = {0.f, 0.f, 0.f};
			bool first = true;

			for (uint32_t i = 0; i < cr.count; ++i)
			{
				uint32_t ci = cr.offset + i;
				const nvclusterlod_Sphere& s = clusterBoundingSpheres[ci];
				float cx = s.center.x;
				float cy = s.center.y;
				float cz = s.center.z;
				float r = s.radius;

				float sx_min[3] = {cx - r, cy - r, cz - r};
				float sx_max[3] = {cx + r, cy + r, cz + r};

				if (first)
				{
					for (int k = 0; k < 3; ++k)
					{
						min_xyz[k] = sx_min[k];
						max_xyz[k] = sx_max[k];
					}
					first = false;
				}
				else
				{
					for (int k = 0; k < 3; ++k)
					{
						if (sx_min[k] < min_xyz[k]) min_xyz[k] = sx_min[k];
						if (sx_max[k] > max_xyz[k]) max_xyz[k] = sx_max[k];
					}
				}
			}

			if (!first)
			{
				float cx = 0.5f * (min_xyz[0] + max_xyz[0]);
				float cy = 0.5f * (min_xyz[1] + max_xyz[1]);
				float cz = 0.5f * (min_xyz[2] + max_xyz[2]);
				float dx = max_xyz[0] - min_xyz[0];
				float dy = max_xyz[1] - min_xyz[1];
				float dz = max_xyz[2] - min_xyz[2];
				float r = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);

				gr.bounds.center[0] = cx;
				gr.bounds.center[1] = cy;
				gr.bounds.center[2] = cz;
				gr.bounds.radius = r;
			}
		}
		groups.push_back(gr);
	}

	// Build ExportedClusters: one per cluster
	clusters.reserve(numClusters);
	for (uint32_t c = 0; c < numClusters; ++c)
	{
		ExportedCluster ec = {};
		ec.groupId = (int)getClusterGroupId(c, numGroups, groupClusterRanges.data());
		uint32_t gen = clusterGeneratingGroups[c];
		if (gen != NVCLUSTERLOD_ORIGINAL_MESH_GROUP && gen >= numGroups)
			return false;
		ec.refined = (gen == NVCLUSTERLOD_ORIGINAL_MESH_GROUP) ? -1 : (int)gen;

		if (c < clusterBoundingSpheres.size())
		{
			const nvclusterlod_Sphere& s = clusterBoundingSpheres[c];
			ec.bounds.center[0] = s.center.x;
			ec.bounds.center[1] = s.center.y;
			ec.bounds.center[2] = s.center.z;
			ec.bounds.radius = s.radius;
			ec.bounds.error = (gen < groupQuadricErrors.size()) ? groupQuadricErrors[gen] : 0.f;
		}
		else
		{
			ec.bounds.center[0] = ec.bounds.center[1] = ec.bounds.center[2] = 0.f;
			ec.bounds.radius = 0.f;
			ec.bounds.error = 0.f;
		}

		const nvcluster_Range& tr = clusterTriangleRanges[c];
		ec.index_count = tr.count * 3;
		ec.vertex_count = 0;  // not provided by nv output; leave 0 or compute from unique indices
		ec.boundary_inner = 0.f;
		ec.boundary_outer = 0.f;

		if (!opts.no_geometry && tr.count > 0 &&
		    tr.offset + tr.count <= triangleVertices.size())
		{
			ec.indices.reserve(ec.index_count);
			for (uint32_t t = 0; t < tr.count; ++t)
			{
				const nvclusterlod_Vec3u& tri = triangleVertices[tr.offset + t];
				ec.indices.push_back(tri.x);
				ec.indices.push_back(tri.y);
				ec.indices.push_back(tri.z);
			}
			// vertex_count = number of unique vertex indices in this cluster
			std::vector<uint32_t> uniq(ec.indices.begin(), ec.indices.end());
			std::sort(uniq.begin(), uniq.end());
			uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
			ec.vertex_count = uniq.size();
		}

		clusters.push_back(std::move(ec));
	}

	// Compute boundary statistics per cluster from triangle indices and mesh
	// positions, matching clusterlod/trichi semantics. This runs regardless of
	// opts.no_geometry so that boundary metrics are always available.
	if (!mesh.vertices.empty())
	{
		const float* positions = &mesh.vertices[0].px;
		size_t positions_stride_floats = sizeof(Vertex) / sizeof(float);

		std::vector<float> boundary_inner(clusters.size(), 0.f);
		std::vector<float> boundary_outer(clusters.size(), 0.f);

		for (uint32_t g = 0; g < numGroups; ++g)
		{
			const nvcluster_Range& cr = groupClusterRanges[g];
			if (cr.count == 0)
				continue;

			// Build per-cluster index lists for clusters in this group, using
			// original-mesh vertex indices from triangleVertices.
			std::vector<std::vector<unsigned int>> group_indices;
			group_indices.resize(cr.count);

			for (uint32_t i = 0; i < cr.count; ++i)
			{
				uint32_t cidx = cr.offset + i;
				if (cidx >= numClusters)
					continue;

				const nvcluster_Range& tr = clusterTriangleRanges[cidx];
				if (tr.count == 0 || tr.offset + tr.count > triangleVertices.size())
					continue;

				std::vector<unsigned int>& ind = group_indices[i];
				ind.reserve(tr.count * 3);
				for (uint32_t t = 0; t < tr.count; ++t)
				{
					const nvclusterlod_Vec3u& tri = triangleVertices[tr.offset + t];
					ind.push_back(tri.x);
					ind.push_back(tri.y);
					ind.push_back(tri.z);
				}
			}

			std::vector<float> inner(cr.count), outer(cr.count);
			computeBoundaryStats(positions, positions_stride_floats, group_indices, inner, outer);

			for (uint32_t i = 0; i < cr.count; ++i)
			{
				uint32_t cidx = cr.offset + i;
				if (cidx >= clusters.size())
					continue;
				boundary_inner[cidx] = inner[i];
				boundary_outer[cidx] = outer[i];
			}
		}

		for (size_t i = 0; i < clusters.size(); ++i)
		{
			clusters[i].boundary_inner = boundary_inner[i];
			clusters[i].boundary_outer = boundary_outer[i];
		}
	}

	return true;
}

#endif
