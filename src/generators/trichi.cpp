#ifdef CLODEXPORT_HAS_TRICHI

#include "generators/trichi.h"
#include "generator.h"

#include <trichi.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <vector>
#include <cstring>

// Compute depth from leaves (finest = 0) toward roots (coarsest = max).
// Trichi's rootNodes can be empty when the last LOD adds 0 clusters, so we always
// derive depth from leaf nodes to match the schema (depth 0 = finest LOD).
// A node can have multiple parents in the DAG, so we must update all parents when processing.
static void computeNodeDepths(const trichi::ClusterHierarchy& hierarchy,
    std::vector<int>& node_depth)
{
	const size_t n = hierarchy.nodes.size();
	node_depth.assign(n, -1);

	// children_of[p] = list of children of node p
	std::vector<std::vector<size_t>> children_of(n);
	// parents_of[u] = list of nodes that have u as child (so we update all when u is processed)
	std::vector<std::vector<size_t>> parents_of(n);
	for (size_t i = 0; i < n; ++i)
		for (size_t c : hierarchy.nodes[i].childNodeIndices)
		{
			children_of[i].push_back(c);
			parents_of[c].push_back(i);
		}

	// Leaves = nodes with no children (finest LOD). They get depth 0.
	std::queue<size_t> q;
	for (size_t i = 0; i < n; ++i)
		if (children_of[i].empty())
		{
			node_depth[i] = 0;
			q.push(i);
		}

	// When all children of p are processed, we set depth[p] and push p.
	std::vector<int> pending_count(n, 0);
	for (size_t p = 0; p < n; ++p)
		pending_count[p] = (int)children_of[p].size();

	// BFS from leaves toward roots: when we pop u, update every parent of u.
	while (!q.empty())
	{
		size_t u = q.front();
		q.pop();
		for (size_t p : parents_of[u])
		{
			--pending_count[p];
			if (pending_count[p] != 0)
				continue;
			int max_child_depth = 0;
			for (size_t c : children_of[p])
				if (node_depth[c] > max_child_depth)
					max_child_depth = node_depth[c];
			node_depth[p] = max_child_depth + 1;
			q.push(p);
		}
	}

	// Disconnected or roots (no children, or never reached): assign max depth so far + 1
	int max_d = 0;
	for (size_t i = 0; i < n; ++i)
		if (node_depth[i] > max_d)
			max_d = node_depth[i];
	for (size_t i = 0; i < n; ++i)
		if (node_depth[i] < 0)
			node_depth[i] = max_d + 1;
}

static float edgeLength(const float* positions, size_t stride_floats, unsigned int a, unsigned int b)
{
	float dx = positions[a * stride_floats + 0] - positions[b * stride_floats + 0];
	float dy = positions[a * stride_floats + 1] - positions[b * stride_floats + 1];
	float dz = positions[a * stride_floats + 2] - positions[b * stride_floats + 2];
	return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Inner boundary = edge length shared between two+ clusters (split by count). Outer = edge in only one cluster (mesh boundary).
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

// Union-Find for grouping: all children of a node in same group, all parents of a node in same group;
// if a cluster would belong to two groups, merge them (disjoint set).
static void buildGroupComponents(const trichi::ClusterHierarchy& hierarchy,
	std::vector<std::vector<size_t>>& children_of,
	std::vector<std::vector<size_t>>& parents_of,
	std::vector<int>& parent_uf)
{
	const size_t n = hierarchy.nodes.size();
	children_of.resize(n);
	parents_of.resize(n);
	for (size_t i = 0; i < n; ++i)
		for (size_t c : hierarchy.nodes[i].childNodeIndices)
		{
			children_of[i].push_back(c);
			parents_of[c].push_back(i);
		}

	parent_uf.resize(n);
	for (size_t i = 0; i < n; ++i)
		parent_uf[i] = (int)i;

	auto find = [&](size_t x) -> size_t {
		int px = (int)x;
		while (parent_uf[px] != px)
			px = parent_uf[px];
		return (size_t)px;
	};
	auto unite = [&](size_t a, size_t b) {
		a = find(a);
		b = find(b);
		if (a != b)
			parent_uf[(int)a] = (int)b;
	};

	// All children of the same parent -> same group
	for (size_t p = 0; p < n; ++p)
		for (size_t j = 1; j < children_of[p].size(); ++j)
			unite(children_of[p][0], children_of[p][j]);
	// All parents of the same child -> same group
	for (size_t c = 0; c < n; ++c)
		for (size_t j = 1; j < parents_of[c].size(); ++j)
			unite(parents_of[c][0], parents_of[c][j]);
}

bool runTrichi(const Mesh& mesh, const GeneratorOptions& opts,
    std::vector<ExportedGroup>& groups, std::vector<ExportedCluster>& clusters)
{
	groups.clear();
	clusters.clear();

	// trichi expects vertices as std::vector<float> with vertexStride floats per vertex (first 3 = position)
	std::vector<float> positions;
	positions.reserve(mesh.vertices.size() * 3);
	for (const Vertex& v : mesh.vertices)
	{
		positions.push_back(v.px);
		positions.push_back(v.py);
		positions.push_back(v.pz);
	}

	trichi::Params params;
	trichi::ClusterHierarchy hierarchy = trichi::buildClusterHierarchy(
	    mesh.indices, positions, 3 * sizeof(float), params);

	if (hierarchy.nodes.empty() || hierarchy.clusters.empty())
		return false;

	std::vector<int> node_depth;
	computeNodeDepths(hierarchy, node_depth);

	std::vector<std::vector<size_t>> children_of, parents_of;
	std::vector<int> parent_uf;
	buildGroupComponents(hierarchy, children_of, parents_of, parent_uf);

	const size_t n = hierarchy.nodes.size();
	// Resolve roots: each node's group = find(node)
	std::vector<size_t> root_of(n);
	for (size_t i = 0; i < n; ++i)
	{
		int r = (int)i;
		while (parent_uf[r] != r)
			r = parent_uf[r];
		root_of[i] = (size_t)r;
	}

	// Unique roots (components) -> group index; order by min depth in component (finest first)
	std::vector<size_t> roots_sorted;
	for (size_t r : root_of)
	{
		if (std::find(roots_sorted.begin(), roots_sorted.end(), r) == roots_sorted.end())
			roots_sorted.push_back(r);
	}
	std::sort(roots_sorted.begin(), roots_sorted.end(), [&](size_t a, size_t b) {
		int min_a = node_depth[a], min_b = node_depth[b];
		for (size_t i = 0; i < n; ++i)
		{
			if (root_of[i] == a && node_depth[i] < min_a) min_a = node_depth[i];
			if (root_of[i] == b && node_depth[i] < min_b) min_b = node_depth[i];
		}
		return min_a < min_b;
	});
	std::vector<int> root_to_group_id(n, -1);
	for (size_t g = 0; g < roots_sorted.size(); ++g)
		root_to_group_id[roots_sorted[g]] = (int)g;

	// Build one ExportedGroup per component: aggregate bounds and error
	groups.resize(roots_sorted.size());
	for (size_t g = 0; g < roots_sorted.size(); ++g)
	{
		ExportedGroup& gr = groups[g];
		gr.depth = 0;
		gr.bounds.center[0] = 0.f;
		gr.bounds.center[1] = 0.f;
		gr.bounds.center[2] = 0.f;
		gr.bounds.radius = 0.f;
		gr.bounds.error = 0.f;
		float max_error = 0.f;
		int min_depth = 0x7fffffff;
		int count = 0;
		for (size_t i = 0; i < n; ++i)
		{
			if (root_of[i] != roots_sorted[g])
				continue;
			if (node_depth[i] < min_depth)
				min_depth = node_depth[i];
			size_t ci = hierarchy.nodes[i].clusterIndex;
			if (ci >= hierarchy.clusters.size())
				continue;
			if (ci < hierarchy.errors.size())
				max_error = (hierarchy.errors[ci].clusterError.error > max_error)
					? hierarchy.errors[ci].clusterError.error : max_error;
			float cx[3], r;
			if (ci < hierarchy.errors.size())
			{
				const auto& eb = hierarchy.errors[ci].clusterError;
				cx[0] = eb.center[0]; cx[1] = eb.center[1]; cx[2] = eb.center[2]; r = eb.radius;
			}
			else if (ci < hierarchy.bounds.size())
			{
				const auto& b = hierarchy.bounds[ci];
				cx[0] = b.center[0]; cx[1] = b.center[1]; cx[2] = b.center[2]; r = b.radius;
			}
			else
				continue;
			if (count == 0)
			{
				gr.bounds.center[0] = cx[0];
				gr.bounds.center[1] = cx[1];
				gr.bounds.center[2] = cx[2];
				gr.bounds.radius = r;
			}
			else
			{
				float dx = cx[0] - gr.bounds.center[0], dy = cx[1] - gr.bounds.center[1], dz = cx[2] - gr.bounds.center[2];
				float dist = sqrtf(dx*dx + dy*dy + dz*dz) + r;
				if (dist > gr.bounds.radius)
					gr.bounds.radius = dist;
			}
			++count;
		}
		gr.depth = (min_depth < 0x7fffffff) ? min_depth : 0;
		gr.bounds.error = max_error;
	}

	// Packed positions for boundary length computation (stride 3 floats)
	std::vector<float> positions_packed(mesh.vertices.size() * 3);
	for (size_t v = 0; v < mesh.vertices.size(); ++v)
	{
		positions_packed[v * 3 + 0] = mesh.vertices[v].px;
		positions_packed[v * 3 + 1] = mesh.vertices[v].py;
		positions_packed[v * 3 + 2] = mesh.vertices[v].pz;
	}

	// Clusters: groupId = component, refined = group containing my children (all children in one group by construction)
	std::vector<std::vector<unsigned int>> cluster_indices;
	cluster_indices.reserve(n);
	for (size_t i = 0; i < n; ++i)
	{
		const trichi::Node& node = hierarchy.nodes[i];
		size_t ci = node.clusterIndex;
		if (ci >= hierarchy.clusters.size())
			continue;

		const trichi::Cluster& c = hierarchy.clusters[ci];
		ExportedCluster ec = {};
		int gid = root_to_group_id[root_of[i]];
		ec.groupId = (gid >= 0) ? gid : 0;
		// refined = group that contains this node's children, only if that group is strictly finer (smaller depth)
		if (children_of[i].empty())
			ec.refined = -1;
		else
		{
			int child_gid = root_to_group_id[root_of[children_of[i][0]]];
			bool child_is_finer = (child_gid >= 0 && child_gid != ec.groupId &&
			    (size_t)child_gid < groups.size() && (size_t)ec.groupId < groups.size() &&
			    groups[child_gid].depth < groups[ec.groupId].depth);
			ec.refined = child_is_finer ? child_gid : -1;
		}

		if (ci < hierarchy.errors.size())
		{
			const auto& eb = hierarchy.errors[ci].clusterError;
			ec.bounds.center[0] = eb.center[0];
			ec.bounds.center[1] = eb.center[1];
			ec.bounds.center[2] = eb.center[2];
			ec.bounds.radius = eb.radius;
			ec.bounds.error = eb.error;
		}
		else if (ci < hierarchy.bounds.size())
		{
			const auto& b = hierarchy.bounds[ci];
			ec.bounds.center[0] = b.center[0];
			ec.bounds.center[1] = b.center[1];
			ec.bounds.center[2] = b.center[2];
			ec.bounds.radius = b.radius;
			ec.bounds.error = 0.f;
		}

		ec.index_count = c.triangleCount * 3;
		ec.vertex_count = c.vertexCount;
		ec.boundary_inner = 0.f;
		ec.boundary_outer = 0.f;

		std::vector<unsigned int> ind;
		if (c.triangleCount > 0 && c.triangleOffset + c.triangleCount * 3 <= hierarchy.triangles.size() && c.vertexOffset + c.vertexCount <= hierarchy.vertices.size())
		{
			ind.reserve(c.triangleCount * 3);
			for (unsigned int t = 0; t < c.triangleCount; ++t)
			{
				size_t l0 = (size_t)hierarchy.triangles[c.triangleOffset + t * 3 + 0];
				size_t l1 = (size_t)hierarchy.triangles[c.triangleOffset + t * 3 + 1];
				size_t l2 = (size_t)hierarchy.triangles[c.triangleOffset + t * 3 + 2];
				if (l0 < c.vertexCount && l1 < c.vertexCount && l2 < c.vertexCount)
				{
					ind.push_back(hierarchy.vertices[c.vertexOffset + l0]);
					ind.push_back(hierarchy.vertices[c.vertexOffset + l1]);
					ind.push_back(hierarchy.vertices[c.vertexOffset + l2]);
				}
			}
		}
		cluster_indices.push_back(std::move(ind));

		if (!opts.no_geometry && !cluster_indices.back().empty())
			ec.indices = cluster_indices.back();

		clusters.push_back(std::move(ec));
	}

	std::vector<float> inner(clusters.size()), outer(clusters.size());
	computeBoundaryStats(positions_packed.data(), 3, cluster_indices, inner, outer);
	for (size_t i = 0; i < clusters.size(); ++i)
	{
		clusters[i].boundary_inner = inner[i];
		clusters[i].boundary_outer = outer[i];
	}

	// Order clusters by group then by node index
	std::vector<size_t> order(clusters.size());
	for (size_t i = 0; i < order.size(); ++i)
		order[i] = i;
	std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
		if (clusters[a].groupId != clusters[b].groupId)
			return clusters[a].groupId < clusters[b].groupId;
		return a < b;
	});
	std::vector<ExportedCluster> sorted;
	sorted.reserve(clusters.size());
	for (size_t i : order)
		sorted.push_back(std::move(clusters[i]));
	clusters = std::move(sorted);

	return true;
}

#endif
