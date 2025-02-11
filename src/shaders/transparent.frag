#version 430 core
#extension GL_ARB_bindless_texture : require

in VsOut
{
	vec3 norm;
	vec2 uv;
	vec3 camPosMinusWorldVert;
	flat uint clusterId;
} fsIn;

struct Cluster
{
	vec4 boundingSphere;

	uint transformIndex;
	int materialIndex;

	uint indexCount;
	uint firstIndex;
	int vertexOffset;

	int padding0;
	int padding1;
	int padding2;
};
layout(binding = 0, std430) readonly buffer ClusterBuffer
{
	Cluster clusters[];
};

struct Material
{
	vec4 colorFactor;

	uvec2 baseColorTex;
	uvec2 metallicRoughnessTex;
	uvec2 normalTex;

	float metallicFactor;
	float roughnessFactor;

	bool hasColorTex;
	bool hasMetallicRoughnessTex;
	bool hasNormalTex;

	bool alphaMask;
	float alphaCutoff;
	bool alphaBlend;

	uvec2 padding;
};

layout(binding = 1, std430) readonly buffer MaterialBlock
{
	Material materials[];
};

//uniform int materialIndex;



layout (location = 0) out vec4 accum;
layout (location = 1) out float reveal;



void main()
{
	int materialIndex = clusters[fsIn.clusterId].materialIndex;

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