#version 430 core
#extension GL_ARB_bindless_texture : require

in Interpolant
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

	uint viewId;

	uint vertexCount;

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

layout (binding = 1, std430) readonly buffer MaterialBlock
{
	Material materials[];
};

uniform vec3 lightDir;



layout (location = 0) out vec4 outNorm;
layout (location = 1) out vec4 outRadiantFlux;



// From http://www.thetenthplanet.de/archives/1180
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv)
{
	vec3 dp1 = dFdx(p);
	vec3 dp2 = dFdy(p);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

vec3 perturbNormal(vec3 normal, vec3 viewspacePos, int materialIndex, vec2 uv)
{
	vec3 map = texture(sampler2D(materials[materialIndex].normalTex), uv).rgb;
	map = map * 2.0f - 1.0f;
	mat3 tbn = cotangentFrame(normal, -viewspacePos, uv);
	return normalize(tbn * map);
}



const float PI = 3.1415926535897932384626433832795;

// Potentially ignore normal maps for optimization
void main()
{
	// This is cancer
	const vec3 lightCol = const vec3(1.0f, 0.851f, 0.713f) * 5.0f;

	int materialIndex = clusters[fsIn.clusterId].materialIndex;

	if (materials[materialIndex].hasColorTex)
	{
		outRadiantFlux = (texture(sampler2D(materials[materialIndex].baseColorTex), fsIn.uv)) * materials[materialIndex].colorFactor;
	}
	else
	{
		outRadiantFlux = materials[materialIndex].colorFactor;
	}

	// todo: will writes be canceled?
	if (materials[materialIndex].alphaMask)
	{
		if (outRadiantFlux.a < materials[materialIndex].alphaCutoff)
		{
			// Causes shadows to disappear at further cascades
			//discard;
		}
	}

	outNorm = vec4(normalize(fsIn.norm), 0.0f);
	if (materials[materialIndex].hasNormalTex)
	{
		outNorm = vec4(perturbNormal(outNorm.xyz, fsIn.camPosMinusWorldVert, materialIndex, fsIn.uv), 1.0f);
	}

	const float lambert = 1.0f / PI;
	float NoL = clamp(dot(outNorm.xyz, lightDir), 0.0f, 1.0f);
	//outRadiantFlux = (outRadiantFlux * lambert) * NoL;
	outRadiantFlux = (outRadiantFlux * lambert) * NoL + outRadiantFlux * vec4(0.765f, 0.820f, 1.0f, 1.0f) * 0.7f * 1.0f;
	//outRadiantFlux = (outRadiantFlux * lambert) * NoL + outRadiantFlux * vec4(0.5f, 0.5f, 0.5f, 1.0f) * 1.0f * 1.0f;
}