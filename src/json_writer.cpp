#include "json_writer.h"

#include <float.h>
#include <stdio.h>

static void writeFloat(FILE* f, float v)
{
	if (v != v)
		fprintf(f, "null");
	else if (v == FLT_MAX || v > 1e30f)
		fprintf(f, "null");
	else
		fprintf(f, "%.9g", v);
}

static void escapeJsonString(FILE* f, const char* s)
{
	fputc('"', f);
	for (; *s; ++s)
	{
		if (*s == '"' || *s == '\\')
			fprintf(f, "\\%c", *s);
		else if ((unsigned char)*s < 32)
			fprintf(f, "\\u%04x", (unsigned char)*s);
		else
			fputc(*s, f);
	}
	fputc('"', f);
}

bool writeJson(const char* path,
    const std::vector<ExportedGroup>& groups,
    const std::vector<ExportedCluster>& clusters,
    const Mesh* mesh,
    bool include_geometry)
{
	FILE* f = fopen(path, "w");
	if (!f)
	{
		fprintf(stderr, "Error: cannot write %s\n", path);
		return false;
	}

	fprintf(f, "{\n");

	if (include_geometry && mesh)
	{
		fprintf(f, "  \"mesh\": {\n    \"vertices\": [");
		for (size_t i = 0; i < mesh->vertices.size(); ++i)
		{
			if (i)
				fputc(',', f);
			fprintf(f, "[%.9g,%.9g,%.9g]", mesh->vertices[i].px, mesh->vertices[i].py, mesh->vertices[i].pz);
		}
		fprintf(f, "],\n    \"indices\": [");
		for (size_t i = 0; i < mesh->indices.size(); ++i)
		{
			if (i)
				fputc(',', f);
			fprintf(f, "%u", mesh->indices[i]);
		}
		fprintf(f, "]},\n");
	}

	fprintf(f, "  \"groups\": [");
	for (size_t g = 0; g < groups.size(); ++g)
	{
		if (g)
			fprintf(f, ",");
		const ExportedGroup& gr = groups[g];
		fprintf(f, "{\"depth\":%d,\"bounds\":{\"center\":[%.9g,%.9g,%.9g],\"radius\":%.9g,\"error\":",
		    gr.depth, gr.bounds.center[0], gr.bounds.center[1], gr.bounds.center[2], gr.bounds.radius);
		writeFloat(f, gr.bounds.error);
		fprintf(f, "}}");
	}
	fprintf(f, "],\n");

	fprintf(f, "  \"clusters\": [");
	for (size_t c = 0; c < clusters.size(); ++c)
	{
		if (c)
			fprintf(f, ",");
		const ExportedCluster& cl = clusters[c];
		fprintf(f, "{\"groupId\":%d,\"refined\":%d,\"bounds\":{\"center\":[%.9g,%.9g,%.9g],\"radius\":%.9g,\"error\":",
		    cl.groupId, cl.refined, cl.bounds.center[0], cl.bounds.center[1], cl.bounds.center[2], cl.bounds.radius);
		writeFloat(f, cl.bounds.error);
		fprintf(f, "},\"indexCount\":%zu,\"vertexCount\":%zu",
		    (size_t)cl.index_count, (size_t)cl.vertex_count);
		if (!cl.indices.empty())
		{
			fprintf(f, ",\"indices\":[");
			for (size_t i = 0; i < cl.indices.size(); ++i)
			{
				if (i)
					fputc(',', f);
				fprintf(f, "%u", cl.indices[i]);
			}
			fprintf(f, "]");
		}
		fprintf(f, ",\"boundaryInner\":%.9g,\"boundaryOuter\":%.9g}", cl.boundary_inner, cl.boundary_outer);
	}
	fprintf(f, "]\n}\n");

	fclose(f);
	return true;
}
