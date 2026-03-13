# Generator output and cutting behavior

This document describes the **clusterlod** (meshoptimizer), **trichi**, and **nv_cluster_lod_builder** output structures and cutting semantics, and why non-clusterlod generators are converted to a common schema. Use it when implementing or updating adapters so that all generators emit a consistent structure and cutting logic can be applied uniformly.

The canonical consumer schema is in [../schema.md](../schema.md). All generators fill `ExportedGroup` / `ExportedCluster` (see `src/generator.h`) and the JSON writer emits the schema format.

---

## 1. Target schema for all generators

Every generator must fill:

- **ExportedGroup** (one per “group”): `depth`, `bounds` (center, radius, error).
- **ExportedCluster** (one per cluster): `groupId`, `refined`, `bounds`, `index_count`, `vertex_count`, `indices` (unless `no_geometry`), **boundary_inner**, **boundary_outer**.

Semantics to preserve:

- **refined** = group id of the **more refined** (finer) group that this cluster refines to, or `-1` for leaves/original.
- **Groups** emitted in an order that allows cut logic to use `groups[cluster.refined]` (e.g. finest-first by depth).
- **Boundary**: inner = length of edges shared with other clusters (in the same group or in the same “level”); outer = length of edges on the mesh boundary. If the library doesn’t provide this, compute it from cluster index buffers and mesh positions (see clusterlod/trichi/nv_cluster_lod_builder).

This schema is the contract between all generators and downstream tools (JSON writer, viewers, analysis scripts). Adapters for trichi and nv_cluster_lod_builder exist purely to convert their native representations into this shape while preserving the semantics above.

---

## 2. Clusterlod (meshoptimizer) output and cutting

### 2.1 Native output structure

- **Groups**  
  The build runs in **depth order** (finest first). Each callback receives one **group** and its clusters. A group has:
  - `depth`: DAG level (0 = finest, higher = coarser).
  - `simplified`: `clodBounds` (center, radius, **error**). This is the aggregate/simplified bounds for that level; `error` is the simplification error (or `FLT_MAX` for terminal groups).

- **Clusters**  
  Each cluster in that group has:
  - `refined`: **index of the more refined group** (the group that was simplified to produce this cluster), or `-1` for original geometry.
  - `bounds`, `indices`, `index_count`, `vertex_count`.

So the hierarchy is **group → group**: every cluster in group G has the same `refined` semantics (they came from simplifying the same finer group). All children of a given “parent group” are in one group; the build never emits a cluster in two groups.

### 2.2 Cutting behavior

Cut logic (see `deps/meshoptimizer/demo/nanite.cpp`) runs in the **same order** as the build (groups in depth order). For each group, for each cluster:

- **Level cut** (`cut_level >= 0`): add cluster to cut if `group.depth == cut_level` or if coarser and terminal (`group.simplified.error == FLT_MAX`).
- **Error cut** (`cut_level == -1`): add cluster to cut iff  
  `(cluster.refined < 0 || boundsError(groups[cluster.refined], …) <= threshold)`  
  **and**  
  `boundsError(group.simplified, …) > threshold`.

So a cluster is drawn when the **finer** group (its `refined`) is below the error threshold and the **current** group is above it. For a fixed view and threshold, each group has one error value, so you never draw both a group and the clusters that refine it. That gives a **single decision per group** and avoids parent+child both being drawn.

### 2.3 Boundary

Clusterlod’s demo computes **boundary_inner** and **boundary_outer** from the current group’s cluster index buffers (after a position remap):

- **Inner**: edges that appear in exactly one triangle in each of **two or more** clusters in the group; edge length is split among those clusters.
- **Outer**: edges that appear in exactly one triangle in **one** cluster (mesh boundary); full length assigned to that cluster.

So boundary is **per cluster**, derived from triangle edges and group-local edge counts.

---

## 3. Trichi output and cutting

### 3.1 Native output structure

Trichi’s `ClusterHierarchy` is a **DAG of nodes**; there are no built-in “groups”:

- **Nodes**: one per cluster. Each node has `childNodeIndices` (finer clusters that were merged/simplified to form this one). So edges are **cluster → cluster** (parent = coarser, children = finer).
- **Multi-parent**: a node can have **multiple parents** (one fine cluster can contribute to several coarser clusters).
- **Clusters / geometry**: `clusters[]`, `vertices[]`, `triangles[]` (local indices per cluster; global mesh index = `vertices[cluster.vertexOffset + localIndex]`). Per-cluster bounds/errors in `errors[]` / `bounds[]`.

So natively there are no “groups”, only a DAG of clusters. Level is implied by depth-from-leaves (e.g. leaves = depth 0).

### 3.2 Native cutting (demo)

Trichi’s demo does **per-cluster** LOD selection using `parent_error` and `cluster_error` in a **flat** pass (no explicit graph traversal). As a result, both a parent and a child can be drawn for the same screen region, which diverges from the “one decision per node” idea in the Nanite-style cut above.

### 3.3 Why convert trichi to a clusterlod-like schema

- **Unified cutting**: To run the **same** meshoptimizer-style cut (error-based, one decision per group), we need a **group** per “level” and **refined** = “group id of the finer level”. Trichi doesn’t have groups, so we have to **define** groups from the DAG.
- **Consistent schema**: All generators should emit the same JSON shape (groups, clusters with `groupId`, `refined`, bounds, boundary) so viewers and tools don’t need generator-specific logic.

### 3.4 How trichi is converted (adapter)

- **Groups**  
  We want: “all children of a node in the same group” and “all parents of a node in the same group”. That would force some clusters into two groups; so we **merge** those groups (disjoint set / union-find):
  - For each parent node P: union all children of P.
  - For each child node C: union all parents of C.  
  The **groups** are the resulting connected components. Group order is by **minimum node depth** in the component (finest first).

- **refined**  
  For each cluster (node), `refined` = **group id of the group that contains its children** (all children lie in one group by construction). If that group is the same as the cluster’s group (possible after merging) or the node has no children, `refined = -1`. So `refined` always points to the **finer** group, matching clusterlod semantics.

- **Group bounds / error**  
  Per group: aggregate bounds from all clusters in the group; error = max of cluster errors (or similar). So each group has a single `bounds` (and effective error) for cut logic.

- **Boundary**  
  Trichi’s API doesn’t expose boundary lengths. The adapter **computes** them the same way as clusterlod: from each cluster’s **original-mesh** triangle indices (using `vertices[cluster.vertexOffset + localIndex]` for correct indices), build edge→cluster counts and assign inner (shared) vs outer (mesh boundary) lengths.

- **Indices**  
  Triangle indices are stored **local** to the cluster (`vertexOffset` + local index). The adapter resolves them to **original mesh** vertex indices when writing `ec.indices` and when building the index list for boundary.

With this conversion, the trichi output **looks like** clusterlod (groups, refined = finer group, boundary), and the same error-based cut logic can be applied.

---

## 4. nv_cluster_lod_builder output and cutting

### 4.1 Native output structure

`nv_cluster_lod_builder` builds LODs over a **DAG of groups of clusters of triangles** and exposes them via `nvclusterlod_MeshOutput`:

- **Groups and LOD levels**  
  - `groupClusterRanges[g]`: range of clusters belonging to group `g`.  
  - `lodLevelGroupRanges[l]`: range of groups belonging to LOD level `l` (LOD 0 = finest, comprised of clusters of the original mesh; subsequent levels are coarser).
- **Clusters and geometry**  
  - `clusterTriangleRanges[c]`: range into the global triangle list for cluster `c`.  
  - `triangleVertices[]`: global triangle indices; each element is a `Vec3u` of **original mesh** vertex indices.  
  - `clusterBoundingSpheres[c]`: bounding sphere (center, radius) for cluster `c`.
- **Error and hierarchy linkage**  
  - `clusterGeneratingGroups[c]`: the group that was decimated to produce cluster `c`, or `NVCLUSTERLOD_ORIGINAL_MESH_GROUP` for original mesh clusters. This forms a DAG between **groups across LOD levels**.  
  - `groupQuadricErrors[g]`: error metric accumulated when decimating geometry in group `g`; used at runtime as `groupQuadricErrors[clusterGeneratingGroups[c]]` for clusters.

So natively there are **groups of clusters per LOD level**, with explicit per-cluster links back to the generating (finer) group, plus per-group decimation error and per-cluster bounds.

### 4.2 Adapter mapping to the shared schema

The nv adapter in `src/generators/nv_cluster_lod_builder.cpp` maps this native structure into `ExportedGroup` / `ExportedCluster` as follows:

- **Groups and depth**  
  - One `ExportedGroup` per nv group.  
  - `ExportedGroup.depth` = LOD level index found by scanning `lodLevelGroupRanges` (0 = finest).  
  - Group bounds are aggregated from all cluster bounding spheres in the group by building a loose enclosing sphere (AABB over all cluster spheres, then a sphere enclosing that AABB).  
  - `ExportedGroup.bounds.error` = `groupQuadricErrors[g]` (per-group decimation error).

- **refined mapping**  
  - nv stores, for each cluster, its **generating group** in `clusterGeneratingGroups[c]` (or `NVCLUSTERLOD_ORIGINAL_MESH_GROUP` for original mesh).  
  - The adapter maps this to `ExportedCluster.refined` as:
    - `refined = -1` if `clusterGeneratingGroups[c] == NVCLUSTERLOD_ORIGINAL_MESH_GROUP`.  
    - `refined = clusterGeneratingGroups[c]` otherwise, which always points to a **finer** group (an earlier LOD level).  
  - Unlike meshoptimizer’s clusterlod, nv groups may contain clusters with **different** generating groups, so `refined` is a per-cluster property and not uniform within a group, but it still obeys the “points to finer group” contract required by the schema.

- **Cluster bounds and error**  
  - `ExportedCluster.bounds.center` / `radius` come directly from `clusterBoundingSpheres[c]`.  
  - `ExportedCluster.bounds.error` = `groupQuadricErrors[clusterGeneratingGroups[c]]` (error of the generating group), mirroring how nv recommends using the quadric errors.

- **Indices and vertex counts**  
  - For each cluster `c`, the adapter uses `clusterTriangleRanges[c]` to gather triangles from `triangleVertices`, which already index **original mesh** vertices.  
  - These indices are flattened into `ExportedCluster.indices` (unless `no_geometry` is set).  
  - `ExportedCluster.index_count` = `triangleCount * 3` from the range.  
  - `ExportedCluster.vertex_count` is derived from the number of unique vertex indices used by that cluster.

### 4.3 Boundary (group-local)

nv’s API does not expose boundary lengths, so the adapter **computes** `boundary_inner` and `boundary_outer` from triangle indices and mesh positions, using the same edge-based rule as clusterlod/trichi but applied **per group**:

- For each nv group `g`:
  - Use `groupClusterRanges[g]` to enumerate its clusters.  
  - For each cluster in the group, build a local index list from `triangleVertices` / `clusterTriangleRanges` using **original mesh** vertex indices.  
  - Run the standard classifier on these per-cluster index lists:
    - For every undirected edge, count how many clusters in this group use that edge in exactly one triangle.  
    - If the edge is used in exactly one such triangle in **2+ clusters in this group**, add its length split evenly across those clusters → contributes to `boundary_inner`.  
    - If it is used in exactly one triangle in **1 cluster in this group**, add its full length to that cluster → contributes to `boundary_outer`.

This means boundary is measured as “border within the group”: edges shared only with clusters in *other* groups at the same LOD are treated as outer for every cluster in this group. This matches how nv’s groups are the unit of decimation and keeps the implementation consistent with the per-group boundary logic used by clusterlod.

With this mapping, the nv_cluster_lod_builder output is structurally and semantically aligned with clusterlod and trichi (groups, `refined` = finer group, group-local boundary), so a single cutting and visualization path can handle all generators.
