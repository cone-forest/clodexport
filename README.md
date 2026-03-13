# clodexport

Export cluster LOD hierarchy to JSON for visualization tools. Loads OBJ, runs a pluggable generator (clusterlod, trichi, or nvclusterlod), and writes JSON conforming to [schema.md](schema.md).

## Build

```bash
git submodule update --init --recursive
cmake -B build
cmake --build build
```

- **meshoptimizer** and **trichi** are git submodules under `deps/`. Run `git submodule update --init --recursive` to fetch them. If `deps/trichi` is not yet in the repo, add it with `git submodule add https://github.com/JolifantoBambla/trichi deps/trichi`. To build without the trichi generator: `cmake -B build -DCLODEXPORT_BUILD_TRICHI=OFF`.

- **nv_cluster_lod_builder** is an optional third generator (archived repo). Add it with `git submodule add https://github.com/nvpro-samples/nv_cluster_lod_builder deps/nv_cluster_lod_builder`, then run `git submodule update --init --recursive` in `deps/nv_cluster_lod_builder` to fetch its own submodule (nv_cluster_builder). Build with: `cmake -B build -DCLODEXPORT_BUILD_NVCLUSTERLOD=ON`. This enables the `nvclusterlod` generator and requires C++20.

## Usage

```bash
./build/clodexport <input.obj> -o <output.json> [--no-geometry] [--generator <name>]
```

- **--generator**: `clusterlod` (default), `trichi`, or `nvclusterlod` (if built in).
- **--no-geometry**: Omit mesh and cluster indices from JSON (schema still has groups/clusters with bounds and counts).

All generators emit the same [schema](schema.md).
