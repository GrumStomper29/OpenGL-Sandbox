#version 420 core

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNorm;
layout (location = 2) in vec2 inUv;

out VsOut
{
	vec3 norm;
	vec2 uv;
	vec3 camPosMinusWorldVert;
} vsOut;

uniform mat4 transform;
uniform mat4 model;
uniform mat4 view;
uniform vec3 camPos;

void main()
{
	gl_Position = transform * model * vec4(inPos, 1.0f);

	mat3 normalTransform = inverse(transpose(mat3(model)));
	vsOut.norm = normalTransform * inNorm;
	vsOut.uv = inUv;

	vsOut.camPosMinusWorldVert = camPos - (model * vec4(inPos, 1.0f)).xyz;
}