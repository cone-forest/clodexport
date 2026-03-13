#include "obj_loader.h"
#include "meshoptimizer.h"

#include <stdio.h>

#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

Mesh parseObj(const char* path)
{
	Mesh result;
	fastObjMesh* obj = fast_obj_read(path);
	if (!obj)
	{
		fprintf(stderr, "Error: cannot read %s\n", path);
		return result;
	}

	size_t total_indices = 0;
	for (unsigned int i = 0; i < obj->face_count; ++i)
		if (obj->face_vertices[i] > 2)
			total_indices += 3 * (obj->face_vertices[i] - 2);

	std::vector<Vertex> vertices(total_indices);
	size_t vertex_offset = 0;
	size_t index_offset = 0;

	for (unsigned int i = 0; i < obj->face_count; ++i)
	{
		if (obj->face_vertices[i] <= 2)
			continue;

		for (unsigned int j = 0; j < obj->face_vertices[i]; ++j)
		{
			fastObjIndex gi = obj->indices[index_offset + j];
			Vertex v = {
			    (float)obj->positions[gi.p * 3 + 0],
			    (float)obj->positions[gi.p * 3 + 1],
			    (float)obj->positions[gi.p * 3 + 2],
			    (float)obj->normals[gi.n * 3 + 0],
			    (float)obj->normals[gi.n * 3 + 1],
			    (float)obj->normals[gi.n * 3 + 2],
			    (float)obj->texcoords[gi.t * 2 + 0],
			    (float)obj->texcoords[gi.t * 2 + 1],
			};

			if (j >= 3)
			{
				vertices[vertex_offset + 0] = vertices[vertex_offset - 3];
				vertices[vertex_offset + 1] = vertices[vertex_offset - 1];
				vertex_offset += 2;
			}
			vertices[vertex_offset] = v;
			vertex_offset++;
		}
		index_offset += obj->face_vertices[i];
	}

	fast_obj_destroy(obj);

	if (vertex_offset == 0)
		return result;

	std::vector<unsigned int> remap(vertex_offset);
	size_t unique = meshopt_generateVertexRemap(&remap[0], NULL, vertex_offset, &vertices[0], vertex_offset, sizeof(Vertex));

	result.indices.resize(vertex_offset);
	meshopt_remapIndexBuffer(&result.indices[0], NULL, vertex_offset, &remap[0]);
	result.vertices.resize(unique);
	meshopt_remapVertexBuffer(&result.vertices[0], &vertices[0], vertex_offset, sizeof(Vertex), &remap[0]);

	return result;
}
