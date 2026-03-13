/**
 * clodexport - Export cluster LOD hierarchy to JSON.
 * Loads a model via Assimp, runs one generator (clusterlod, trichi, nvclusterlod), writes JSON.
 */
#include "generator.h"
#include "json_writer.h"
#include "model_loader.h"
#include "generators/clusterlod.h"
#ifdef CLODEXPORT_HAS_TRICHI
#include "generators/trichi.h"
#endif
#ifdef CLODEXPORT_HAS_NVCLUSTERLOD
#include "generators/nv_cluster_lod_builder.h"
#endif

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
	const char* input_path = nullptr;
	const char* output_path = nullptr;
	bool no_geometry = false;
	const char* generator_name = "clusterlod";

	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			output_path = argv[++i];
		else if (strcmp(argv[i], "--no-geometry") == 0)
			no_geometry = true;
		else if (strcmp(argv[i], "--generator") == 0 && i + 1 < argc)
			generator_name = argv[++i];
		else if (argv[i][0] != '-')
			input_path = argv[i];
	}

	if (!input_path || !output_path)
	{
		fprintf(stderr, "Usage: clodexport <input_model> -o <output.json> [--no-geometry] [--generator clusterlod|trichi|nvclusterlod]\n");
		return 1;
	}

	Mesh mesh = loadModel(input_path);
	if (mesh.vertices.empty())
		return 1;

	GeneratorOptions opts;
	opts.no_geometry = no_geometry;

	std::vector<ExportedGroup> groups;
	std::vector<ExportedCluster> clusters;

	bool ok = false;
	if (strcmp(generator_name, "clusterlod") == 0)
	{
		ok = runClusterlod(mesh, opts, groups, clusters);
		if (!ok)
			fprintf(stderr, "Error: clusterlod generator failed\n");
	}
#ifdef CLODEXPORT_HAS_TRICHI
	else if (strcmp(generator_name, "trichi") == 0)
	{
		ok = runTrichi(mesh, opts, groups, clusters);
		if (!ok)
			fprintf(stderr, "Error: trichi generator failed\n");
	}
#endif
#ifdef CLODEXPORT_HAS_NVCLUSTERLOD
	else if (strcmp(generator_name, "nvclusterlod") == 0)
	{
		ok = runNvClusterLod(mesh, opts, groups, clusters);
		if (!ok)
			fprintf(stderr, "Error: nvclusterlod generator failed\n");
	}
#endif
	else
	{
		fprintf(stderr, "Error: unknown generator '%s' (supported: clusterlod"
#ifdef CLODEXPORT_HAS_TRICHI
			", trichi"
#endif
#ifdef CLODEXPORT_HAS_NVCLUSTERLOD
			", nvclusterlod"
#endif
			")\n", generator_name);
		return 1;
	}

	if (!ok)
		return 1;

	if (!writeJson(output_path, groups, clusters, no_geometry ? nullptr : &mesh, !no_geometry))
		return 1;

	printf("Wrote %s: %zu groups, %zu clusters\n", output_path, groups.size(), clusters.size());
	return 0;
}
