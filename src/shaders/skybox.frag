#version 430 core

in vec3 texCoord;

out vec4 outColor;
layout (depth_less) out float gl_FragDepth;

uniform layout(binding = 0) samplerCube skybox;

void main()
{
	outColor = texture(skybox, texCoord);
	gl_FragDepth = 0;
}