#pragma once

#include "generator.h"
#include "model_loader.h"
#include <vector>

bool runTrichi(const Mesh& mesh, const GeneratorOptions& opts,
    std::vector<ExportedGroup>& groups, std::vector<ExportedCluster>& clusters);
