# Cluster LOD hierarchy JSON schema

Contract for all generators and views. Export tools must emit this format; viewers consume it.

## Root

- **mesh** (optional): Mesh geometry for views that need it.
  - **vertices**: `number[][]` — array of `[x, y, z]` positions.
  - **indices**: `number[]` — flat uint32 index buffer (triangles, 3 indices per triangle).
- **groups**: `Group[]` — one entry per emitted group (order = group id).
- **clusters**: `Cluster[]` — flat list of all clusters (order preserved from emission).

## Group

- **depth**: `number` — DAG level.
- **bounds**: **Bounds** — simplified group bounds.

## Bounds

- **center**: `[number, number, number]` — sphere center.
- **radius**: `number`.
- **error**: `number` — simplification error (use `Infinity` for terminal groups).

## Cluster

- **groupId**: `number` — index into `groups[]` (which group this cluster was emitted with).
- **refined**: `number` — group id of the more detailed group this cluster refines to, or `-1` for leaves.
- **bounds**: **Bounds** — cluster bounds.
- **indexCount**: `number`.
- **vertexCount**: `number`.
- **indices** (optional): `number[]` — triangle indices into `mesh.vertices`; omit if `--no-geometry`.
- **boundaryInner** (optional): `number` — total length of edges shared with other clusters in the same group (for DAG Structure View).
- **boundaryOuter** (optional): `number` — total length of edges not shared within the group.
