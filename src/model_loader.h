#pragma once

#include <vector>

struct Vertex
{
	float px, py, pz;
	float nx, ny, nz;
	float tx, ty;
};

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
};

/** Load model from path using Assimp. Returns empty mesh on error. */
Mesh loadModel(const char* path);

