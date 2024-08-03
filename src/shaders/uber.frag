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

layout (binding = 1, std430) readonly buffer MaterialBlock
{
	Material materials[];
};

uniform vec3 meshletCol;

uniform int materialIndex;


out vec4 outColor;
out vec4 outNorm;
out vec4 outPos;


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

vec3 perturbNormal(vec3 normal, vec3 viewspacePos, vec2 uv)
{
	vec3 map = texture(sampler2D(materials[materialIndex].normalTex), uv).rgb;
	map = map * 2.0f - 1.0f;
	mat3 tbn = cotangentFrame(normal, -viewspacePos, uv);
	return normalize(tbn * map);
}

void main()
{
	if (materials[materialIndex].hasColorTex)
	{
		outColor = (texture(sampler2D(materials[materialIndex].baseColorTex), fsIn.uv)) * (materials[materialIndex].colorFactor);
	}
	else
	{
		outColor = materials[materialIndex].colorFactor;
	}

	if (materials[materialIndex].alphaMask)
	{
		if (outColor.a < materials[materialIndex].alphaCutoff)
		{
			discard;
		}
	}

	outNorm = vec4(normalize(fsIn.norm), 0.0f);
	if (materials[materialIndex].hasNormalTex)
	{
		outNorm = vec4(perturbNormal(outNorm.xyz, fsIn.camPosMinusWorldVert, fsIn.uv), 1.0f);
	}

	// Todo: remove
	outPos = vec4(0.0f, 0.0f, 0.0f, 0.0f);

	//outColor = vec4(meshletCol, 1.0f);
}