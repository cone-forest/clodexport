#pragma once

#include "generator.h"
#include "obj_loader.h"
#include <vector>

bool runNvClusterLod(const Mesh& mesh, const GeneratorOptions& opts,
    std::vector<ExportedGroup>& groups, std::vector<ExportedCluster>& clusters);
