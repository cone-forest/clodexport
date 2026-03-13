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

/** Load OBJ from path. Returns empty mesh on error. */
Mesh parseObj(const char* path);
