#version 430 core

layout (binding = 0) uniform sampler2D inColor;
layout (binding = 1) uniform sampler2D inNorm;
layout (binding = 2) uniform sampler2D inDepth;

layout(binding = 3) uniform sampler2DArray hiZ;
uniform int hiZLevel;

layout(binding = 4) uniform sampler2DArray shadowMap;

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

void main()
{
	ivec2 coords = ivec2(gl_FragCoord.xy);

	vec3 worldPos = reconstructFragmentWorldPositionFromDepth(texelFetch(inDepth, coords, 0).r, vec2(1440, 810), invViewProj);

	outColor = texelFetch(inColor, coords, 0);

	const vec3 lightDir = normalize(const vec3(-2.0f, 8.0f, 1.0f));
	const vec3 lightCol = const vec3(0.99f, 0.98f, 0.83f);
	const vec3 ambientCol = const vec3(0.82f, 0.90f, 1.0f);
	const float ambientStrength = 0.7f;

	const vec3 ambient = ambientStrength * ambientCol;

	float diffuse = max(dot(normalize(texelFetch(inNorm, coords, 0).xyz), lightDir), 0.0f);



	vec4 viewPos = view * vec4(worldPos, 1.0f);
	float depth = (viewPos.z);
	int layer = 2;
	if (depth > -80.0f)
	{
		layer = 1;
	}
	if (depth > -20.0f)
	{
		layer = 0;
	}

	vec4 lightSpacePos = lightMatrices[layer] * vec4(worldPos, 1.0f);
	vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
	projCoords.xy = projCoords.xy * 0.5f + 0.5f;
	float shadow = texture(shadowMap, vec3(projCoords.xy, layer)).r;


	if (projCoords.z + 0.001f < shadow) diffuse = 0.0f;



	outColor = vec4((diffuse * lightCol + ambient), 1.0f) * outColor;

	//outColor = vec4(worldPos.xyz, 0.0f);

	//outColor = vec4(1.0f, 0.0f, 0.0f, 1.0f);
	//if (depth > -80) outColor = vec4(0.0f, 1.0f, 0.0f, 1.0f);
	//if (depth > -20) outColor = vec4(0.0f, 0.0f, 1.0f, 1.0f);

	if (hiZLevel != 0)
	{
	    float depth = texelFetch(hiZ, ivec3(coords, hiZLevel), 0).r;
		//depth = 2.0f * nearZ * farZ 
		outColor = vec4(depth, 0, 0, 0);
		//outColor = vec4(texelFetch(hiZ, ivec2(coords) / int(pow(2, hiZLevel)), hiZLevel).r);
	}
}