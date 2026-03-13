#pragma once

#include "generator.h"
#include "model_loader.h"

#include <vector>

/** Write groups, clusters, and optional mesh to JSON file. Returns false on error. */
bool writeJson(const char* path,
    const std::vector<ExportedGroup>& groups,
    const std::vector<ExportedCluster>& clusters,
    const Mesh* mesh,
    bool include_geometry);
