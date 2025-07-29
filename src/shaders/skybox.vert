#version 430 core

layout (location = 0) in vec3 inPos;

out vec3 texCoord;

uniform mat4 proj;
uniform mat4 view;

void main()
{
	texCoord = inPos;
	gl_Position = proj * view * vec4(inPos, 1.0f);
}