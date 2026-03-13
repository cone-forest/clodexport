/**
 * Generator interface: shared types for all generators.
 * Generators fill ExportedGroup/ExportedCluster; json_writer emits them.
 */
#pragma once

#include <stddef.h>
#include <vector>

#include "clusterlod.h"

struct ExportedGroup
{
	int depth;
	clodBounds bounds;
};

struct ExportedCluster
{
	int groupId;
	int refined;
	clodBounds bounds;
	size_t index_count;
	size_t vertex_count;
	std::vector<unsigned int> indices;
	float boundary_inner;
	float boundary_outer;
};

struct GeneratorOptions
{
	bool no_geometry = false;
};
