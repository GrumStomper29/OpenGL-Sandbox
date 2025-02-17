#version 420 core

layout (binding = 0) uniform sampler2D inColor;
layout (binding = 1) uniform sampler2D inNorm;

layout(binding = 2) uniform sampler2D hiZ;
uniform int hiZLevel;

layout (location = 0) out vec4 outColor;

void main()
{
	ivec2 coords = ivec2(gl_FragCoord.xy);

	outColor = texelFetch(inColor, coords, 0);

	const vec3 lightDir = normalize(const vec3(-2.0f, 8.0f, 1.0f));
	const vec3 lightCol = const vec3(0.99f, 0.98f, 0.83f);
	const vec3 ambientCol = const vec3(0.82f, 0.90f, 1.0f);
	const float ambientStrength = 0.7f;

	const vec3 ambient = ambientStrength * ambientCol;

	float diffuse = max(dot(normalize(texelFetch(inNorm, coords, 0).xyz), lightDir), 0.0f);

	outColor = vec4((diffuse * lightCol + ambient), 1.0f) * outColor;

	if (hiZLevel != 0)
	{
		outColor = vec4(texelFetch(hiZ, coords / int(pow(2, hiZLevel)), hiZLevel).r);
	}
}