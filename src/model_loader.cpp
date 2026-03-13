#include "model_loader.h"

#include "meshoptimizer.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stdio.h>

Mesh loadModel(const char* path)
{
	Mesh result;

	if (!path)
	{
		fprintf(stderr, "Error: input path is null\n");
		return result;
	}

	Assimp::Importer importer;

	const unsigned int flags =
	    aiProcess_Triangulate |
	    aiProcess_JoinIdenticalVertices |
	    aiProcess_GenNormals |
	    aiProcess_ImproveCacheLocality |
	    aiProcess_SortByPType;

	const aiScene* scene = importer.ReadFile(path, flags);
	if (!scene || !scene->HasMeshes())
	{
		fprintf(stderr, "Error: cannot load %s: %s\n", path, importer.GetErrorString());
		return result;
	}

	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices;
	vertices.reserve(1024);
	indices.reserve(1024);

	unsigned int vertex_offset = 0;

	for (unsigned int m = 0; m < scene->mNumMeshes; ++m)
	{
		const aiMesh* mesh = scene->mMeshes[m];
		if (!mesh)
			continue;

		const bool has_normals = mesh->HasNormals();
		const bool has_uv0 = mesh->HasTextureCoords(0);

		const unsigned int base_vertex = vertex_offset;
		vertices.resize(vertices.size() + mesh->mNumVertices);

		for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
		{
			Vertex vert = {};
			const aiVector3D& p = mesh->mVertices[v];
			vert.px = p.x;
			vert.py = p.y;
			vert.pz = p.z;

			if (has_normals)
			{
				const aiVector3D& n = mesh->mNormals[v];
				vert.nx = n.x;
				vert.ny = n.y;
				vert.nz = n.z;
			}
			else
			{
				vert.nx = vert.ny = vert.nz = 0.f;
			}

			if (has_uv0)
			{
				const aiVector3D& t = mesh->mTextureCoords[0][v];
				vert.tx = t.x;
				vert.ty = t.y;
			}
			else
			{
				vert.tx = vert.ty = 0.f;
			}

			vertices[base_vertex + v] = vert;
		}

		vertex_offset += mesh->mNumVertices;

		for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
		{
			const aiFace& face = mesh->mFaces[f];
			if (face.mNumIndices != 3)
				continue;

			for (unsigned int i = 0; i < 3; ++i)
				indices.push_back(base_vertex + face.mIndices[i]);
		}
	}

	if (vertices.empty() || indices.empty())
		return result;

	std::vector<unsigned int> remap(vertices.size());
	size_t unique = meshopt_generateVertexRemap(&remap[0], &indices[0], indices.size(), &vertices[0], vertices.size(), sizeof(Vertex));

	result.indices.resize(indices.size());
	meshopt_remapIndexBuffer(&result.indices[0], &indices[0], indices.size(), &remap[0]);

	result.vertices.resize(unique);
	meshopt_remapVertexBuffer(&result.vertices[0], &vertices[0], vertices.size(), sizeof(Vertex), &remap[0]);

	return result;
}

