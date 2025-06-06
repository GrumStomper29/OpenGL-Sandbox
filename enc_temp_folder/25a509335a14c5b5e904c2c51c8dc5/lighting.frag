#version 430 core

layout (binding = 0) uniform sampler2D inColor;
layout (binding = 1) uniform sampler2D inNorm;
layout (binding = 2) uniform sampler2D inDepth;

layout(binding = 3) uniform sampler2DArray hiZ;
uniform int hiZLevel;

layout(binding = 4) uniform sampler2DArrayShadow shadowMap;

uniform float nearZ;
uniform float farZ;

uniform mat4 invViewProj;

uniform mat4 view;

layout (binding = 5, std430) readonly buffer LightMatrices
{
	mat4 lightMatrices[];
};

layout (location = 0) out vec4 outColor;

vec3 reconstructFragmentWorldPositionFromDepth(float depth, vec2 screenSize, mat4 invViewProj)
{
    //float z = depth * 2.0 - 1.0; // [0, 1] -> [-1, 1]
	float z = depth;
    vec2 position_cs = gl_FragCoord.xy / (screenSize - 1); // [0.5, screenSize] -> [0, 1]
    vec4 position_ndc = vec4(position_cs * 2.0 - 1.0, z, 1.0); // [0, 1] -> [-1, 1]

    // undo view + projection
    vec4 position_ws = invViewProj * position_ndc;
    position_ws /= position_ws.w;

    return position_ws.xyz;
}

float map(float value, float min1, float max1, float min2, float max2) 
{
  return min2 + (value - min1) * (max2 - min2) / (max1 - min1);
}

// from https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
float dither(ivec2 p)
{
    return mod(52.9829189f * mod(0.06711056f * float(p.x) + 0.00583715f * float(p.y), 1.0f), 1.0f);
}

void main()
{
	const vec3 lightDir = normalize(const vec3(4.0f, 4.0f, 1.0f));

	ivec2 coords = ivec2(gl_FragCoord.xy);

	vec3 worldPos = reconstructFragmentWorldPositionFromDepth(texelFetch(inDepth, coords, 0).r, vec2(1440, 810), invViewProj);
	vec3 worldNorm = normalize(cross(dFdx(worldPos), dFdy(worldPos)));

	float shadowBias = 0.0015f;
	float cosLightAngle = dot(lightDir, worldNorm);
	float normalOffsetScale = clamp(1 - cosLightAngle, 0.0f, 1.0f);

	worldPos += worldNorm * (normalOffsetScale + 0.1f);

	outColor = texelFetch(inColor, coords, 0);
	const vec3 lightCol = const vec3(0.99f, 0.98f, 0.83f);

	const vec3 skyAmbientCol = const vec3(0.78f, 0.90f, 0.99f);
	float skyAmbientStrength = 0.7f;

	vec3 ambient = skyAmbientStrength * skyAmbientCol;

	float diffuse = max(dot(normalize(texelFetch(inNorm, coords, 0).xyz), lightDir), 0.0f);

	vec4 viewPos = view * vec4(worldPos, 1.0f);
	float dither = dither(coords);
	float depth = (viewPos.z) + dither * 2.0f;

	int layer = 2;
	if (depth > -36.0f)
	{
		layer = 1;
	}
	if (depth > -9.0f)
	{
		layer = 0;
	}

	vec4 lightSpacePos = lightMatrices[layer] * vec4(worldPos, 1.0f);
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
	projCoords.xy = projCoords.xy * 0.5f + 0.5f;

	vec2 texelSize = 1.0f / textureSize(shadowMap, 0).xy;

	float shadow;
	for (int x = -1; x < 2; ++x)
	{
		for (int y = -1; y < 2; ++y)
		{
			vec2 offset = vec2((x * 2) - 1, (y * 2) - 1) * texelSize;
			offset = vec2(x, y) * texelSize;

			vec4 gather = textureGatherOffset(shadowMap, vec3(projCoords.xy, layer), projCoords.z, ivec2(x, y));

			shadow += texture(shadowMap, vec4(projCoords.xy + offset, layer, projCoords.z));
		}
	}
	shadow /= 9;

	//if (projCoords.x < 0.0f || projCoords.x > 1.0f
	//	|| projCoords.y < 0.0f || projCoords.y > 1.0f) outColor = vec4(0.0f, 0.0f, 1.0f, 1.0f);


	diffuse *= 1.0f - shadow;

	outColor = vec4((diffuse * lightCol + ambient), 1.0f) * outColor;
}