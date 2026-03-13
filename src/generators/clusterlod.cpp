// Include clusterlod.h with implementation first so its definitions are compiled here.
// generator.h also includes clusterlod.h; the include guard makes that a no-op.
#include "meshoptimizer.h"
#define CLUSTERLOD_IMPLEMENTATION
#include "clusterlod.h"

#include "generator.h"
#include "model_loader.h"

#include <math.h>
#include <algorithm>
#include <map>
#include <vector>

static float edgeLength(const float* positions, size_t stride, unsigned int a, unsigned int b)
{
	float dx = positions[a * stride + 0] - positions[b * stride + 0];
	float dy = positions[a * stride + 1] - positions[b * stride + 1];
	float dz = positions[a * stride + 2] - positions[b * stride + 2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void computeBoundaryStats(const float* positions, size_t positions_stride_floats,
    const std::vector<std::vector<unsigned int>>& group_indices,
    std::vector<float>& boundary_inner, std::vector<float>& boundary_outer)
{
	boundary_inner.assign(group_indices.size(), 0.f);
	boundary_outer.assign(group_indices.size(), 0.f);

	typedef std::pair<unsigned int, unsigned int> Edge;
	std::map<Edge, std::vector<size_t>> edge_to_clusters;

	for (size_t c = 0; c < group_indices.size(); ++c)
	{
		const std::vector<unsigned int>& ind = group_indices[c];
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

bool runClusterlod(const Mesh& mesh, const GeneratorOptions& opts,
    std::vector<ExportedGroup>& groups, std::vector<ExportedCluster>& clusters)
{
	groups.clear();
	clusters.clear();

	const float attribute_weights[3] = {0.5f, 0.5f, 0.5f};
	clodMesh clod_mesh = {};
	clod_mesh.indices = mesh.indices.data();
	clod_mesh.index_count = mesh.indices.size();
	clod_mesh.vertex_count = mesh.vertices.size();
	clod_mesh.vertex_positions = &mesh.vertices[0].px;
	clod_mesh.vertex_positions_stride = sizeof(Vertex);
	clod_mesh.vertex_attributes = &mesh.vertices[0].nx;
	clod_mesh.vertex_attributes_stride = sizeof(Vertex);
	clod_mesh.attribute_weights = attribute_weights;
	clod_mesh.attribute_count = 3;
	clod_mesh.attribute_protect_mask = (1u << 3) | (1u << 4);

	clodConfig config = clodDefaultConfig(128);

	std::vector<std::vector<unsigned int>> current_group_indices;

	clodBuild(config, clod_mesh, [&](clodGroup group, const clodCluster* cluster_list, size_t cluster_count) -> int {
		int group_id = (int)groups.size();
		groups.push_back({group.depth, group.simplified});

		current_group_indices.clear();
		current_group_indices.resize(cluster_count);

		for (size_t i = 0; i < cluster_count; ++i)
		{
			ExportedCluster c;
			c.groupId = group_id;
			c.refined = cluster_list[i].refined;
			c.bounds = cluster_list[i].bounds;
			c.index_count = cluster_list[i].index_count;
			c.vertex_count = cluster_list[i].vertex_count;
			c.boundary_inner = 0.f;
			c.boundary_outer = 0.f;
			if (!opts.no_geometry)
				c.indices.assign(cluster_list[i].indices, cluster_list[i].indices + cluster_list[i].index_count);
			current_group_indices[i].assign(cluster_list[i].indices, cluster_list[i].indices + cluster_list[i].index_count);
			clusters.push_back(std::move(c));
		}

		std::vector<unsigned int> remap(mesh.vertices.size());
		meshopt_generatePositionRemap(&remap[0], &mesh.vertices[0].px, mesh.vertices.size(), sizeof(Vertex));
		for (size_t i = 0; i < current_group_indices.size(); ++i)
			for (size_t j = 0; j < current_group_indices[i].size(); ++j)
				current_group_indices[i][j] = remap[current_group_indices[i][j]];

		std::vector<float> canon_pos(mesh.vertices.size() * 3);
		for (size_t v = 0; v < mesh.vertices.size(); ++v)
		{
			unsigned int r = remap[v];
			canon_pos[r * 3 + 0] = mesh.vertices[v].px;
			canon_pos[r * 3 + 1] = mesh.vertices[v].py;
			canon_pos[r * 3 + 2] = mesh.vertices[v].pz;
		}

		std::vector<float> inner(cluster_count), outer(cluster_count);
		computeBoundaryStats(&canon_pos[0], 3, current_group_indices, inner, outer);

		for (size_t i = 0; i < cluster_count; ++i)
		{
			clusters[clusters.size() - cluster_count + i].boundary_inner = inner[i];
			clusters[clusters.size() - cluster_count + i].boundary_outer = outer[i];
		}

		return group_id;
	});

	return true;
}
