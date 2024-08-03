#version 430 core
#extension GL_ARB_bindless_texture : require

in VsOut
{
	vec3 norm;
	vec2 uv;
	vec3 camPosMinusWorldVert;
} fsIn;



struct Material
{
	vec4 colorFactor;
	float metallicFactor;
	float roughnessFactor;

	bool hasColorTex;
	uvec2 baseColorTex;

	bool hasMetallicRoughnessTex;
	uvec2 metallicRoughnessTex;

	bool hasNormalTex;
	uvec2 normalTex;

	bool alphaMask;
	float alphaCutoff;
};

layout(binding = 1, std430) readonly buffer MaterialBlock
{
	Material materials[];
};

uniform int materialIndex;



layout (location = 0) out vec4 accum;
layout (location = 1) out float reveal;



void main()
{
	vec4 outColor = vec4(0.0f);

	if (materials[materialIndex].hasColorTex)
	{
		outColor = (texture(sampler2D(materials[materialIndex].baseColorTex), fsIn.uv)) * (materials[materialIndex].colorFactor);
	}
	else
	{
		outColor = materials[materialIndex].colorFactor;
	}
	/*
	const vec3 lightDir = normalize(const vec3(-2.0f, 8.0f, 1.0f));
	const vec3 lightCol = const vec3(1.0f);

	const float ambientStrength = 0.4f;
	const vec3 ambient = ambientStrength * lightCol;

	float diffuse = max(dot(normalize(fsIn.norm), lightDir), -1.0f); // changed to -1

	diffuse = (diffuse + 1.0f) / 2.0f;
	outColor = vec4((diffuse * lightCol), 1.0f) * outColor;
	*/

	float weight = clamp(pow(min(1.0, outColor.a * 10.0) + 0.01, 3.0) * 1e8 *
		pow(1.0 - gl_FragCoord.z * 0.9, 3.0), 1e-2, 3e3);
	
	accum = vec4(outColor.rgb * outColor.a, outColor.a) * weight;
	reveal = outColor.a;
}