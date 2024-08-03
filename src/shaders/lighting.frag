#version 420 core

layout (binding = 0) uniform sampler2D inColor;
layout (binding = 1) uniform sampler2D inNorm;
layout (binding = 2) uniform sampler2D inPos;

layout (location = 0) out vec4 outColor;

void main()
{
	ivec2 coords = ivec2(gl_FragCoord.xy);

	outColor = texelFetch(inColor, coords, 0);

	const vec3 lightDir = normalize(const vec3(-2.0f, 8.0f, 1.0f));
	const vec3 lightCol = const vec3(1.0f);

	const float ambientStrength = 0.4f;
	const vec3 ambient = ambientStrength * lightCol;

	float diffuse = max(dot(normalize(texelFetch(inNorm, coords, 0).xyz), lightDir), -1.0f); // changed to -1

	//outColor = vec4((diffuse * lightCol + ambient), 1.0f) * outColor;

	diffuse = (diffuse + 1.0f) / 2.0f;
	outColor = vec4((diffuse * lightCol), 1.0f) * outColor;
}