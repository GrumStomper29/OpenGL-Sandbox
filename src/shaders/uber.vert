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

out VsOut
{
	vec3 norm;
	vec2 uv;
	vec3 camPosMinusWorldVert;
	flat uint clusterId;
} vsOut;

uniform mat4 transform;
//uniform mat4 model;
uniform mat4 view;
uniform vec3 camPos;

void main()
{
	uint clusterId = bitfieldExtract(gl_VertexID, 7, 25);
	vsOut.clusterId = clusterId;
	Vertex vertex = vertices[bitfieldExtract(gl_VertexID, 0, 7) + clusters[clusterId].vertexOffset];
	//Vertex vertex = vertices[gl_VertexID];

	gl_Position = transform * transforms[clusters[clusterId].transformIndex] * vec4(vertex.pos, 1.0f);
	//gl_Position = transform * vec4(vertex.pos, 1.0f);

	mat3 normalTransform = inverse(transpose(mat3(transforms[clusters[clusterId].transformIndex])));
	vsOut.norm = normalTransform * vertex.normal;

	vsOut.uv.x = vertex.u;
	vsOut.uv.y = vertex.v;

	vsOut.camPosMinusWorldVert = camPos - (transforms[clusters[clusterId].transformIndex] * vec4(vertex.pos, 1.0f)).xyz;
}