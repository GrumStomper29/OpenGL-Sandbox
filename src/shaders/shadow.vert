#version 430 core

struct Cluster
{
	vec4 boundingSphere;

	uint transformIndex;
	int materialIndex;

	uint indexCount;
	uint firstIndex;
	int vertexOffset;

	uint viewId;
	int padding1;
	int padding2;
};
layout(binding = 0, std430) readonly buffer ClusterBuffer
{
	Cluster clusters[];
};

struct Vertex
{
	vec3 pos;
	float u;
	vec3 normal;
	float v;
};

layout(binding = 2, std430) readonly buffer VertexBuffer
{
	Vertex vertices[];
};

layout(binding = 3, std430) readonly buffer TransformBuffer
{
	mat4 transforms[];
};

uniform mat4 transform;

void main()
{
	uint clusterId = bitfieldExtract(gl_VertexID, 7, 25);

	Vertex vertex = vertices[bitfieldExtract(gl_VertexID, 0, 7) + clusters[clusterId].vertexOffset];

	gl_Position = transform * transforms[clusters[clusterId].transformIndex] * vec4(vertex.pos, 1.0f);
}