#version 420 core
#extension GL_ARB_bindless_texture : require

in VsOut
{
	vec3 norm;
	vec2 uv;
	vec3 camPosMinusWorldVert;
} fsIn;



layout (std140, binding = 1) uniform Material
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



out vec4 outColor;


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
	vec3 map = texture(sampler2D(normalTex), uv).rgb;
	map = map * 2.0f - 1.0f;
	mat3 tbn = cotangentFrame(normal, -viewspacePos, uv);
	return normalize(tbn * map);
}

void main()
{
	if (hasColorTex)
	{
		outColor = (texture(sampler2D(baseColorTex), fsIn.uv)) * (colorFactor);
	}
	else
	{
		outColor = colorFactor;
	}

	if (alphaMask)
	{
		if (outColor.a < alphaCutoff)
		{
			discard;
		}
	}

	vec3 fragNorm = normalize(fsIn.norm);
	fragNorm = perturbNormal(fragNorm, fsIn.camPosMinusWorldVert, fsIn.uv);

	const vec3 lightDir = normalize(const vec3(-2.0f, 8.0f, 1.0f));
	const vec3 lightCol = const vec3(1.0f);

	const float ambientStrength = 0.4f;
	const vec3 ambient = ambientStrength * lightCol;

	float diffuse = max(dot(normalize(fragNorm), lightDir), -1.0f); // changed to -1

	//outColor = vec4((diffuse * lightCol + ambient), 1.0f) * outColor;

	diffuse = (diffuse + 1.0f) / 2.0f;
	outColor = vec4((diffuse * lightCol), 1.0f) * outColor;
}