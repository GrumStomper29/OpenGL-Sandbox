#version 430 core

layout (binding = 0) uniform sampler2D inColor;
layout (binding = 1) uniform sampler2D inNorm;
layout (binding = 2) uniform sampler2D inDepth;
layout (binding = 6) uniform sampler2D inMetallicRoughness;

layout (binding = 3) uniform sampler2DArray hiZ;
uniform int hiZLevel;

layout (binding = 4) uniform sampler2DArrayShadow shadowMap;

layout (binding = 5) uniform sampler2DArray shadowDepthMap;
layout (binding = 7) uniform sampler2DArray	shadowNormalMap;
layout (binding = 8) uniform sampler2DArray	shadowRadiantFluxMap;

uniform mat4 invViewProj;
uniform mat4 view;
uniform vec3 camPos;

uniform vec3 lightDir;

struct LightMatrices
{
	mat4 viewProj;
	mat4 radius;
};
layout (binding = 5, std430) readonly buffer LightMatricesBuffer
{
	LightMatrices lightMatrices[];
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
vec3 reconstructFragmentWorldPositionFromDepth(vec2 fragCoord, float depth, vec2 screenSize, mat4 invViewProj)
{
    //float z = depth * 2.0 - 1.0; // [0, 1] -> [-1, 1]
	float z = depth;
    vec2 position_cs = fragCoord / (screenSize - 1); // [0.5, screenSize] -> [0, 1]
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


// from https://64.github.io/tonemapping/
float luminance(vec3 v)
{
    return dot(v, vec3(0.2126f, 0.7152f, 0.0722f));
}
vec3 change_luminance(vec3 c_in, float l_out)
{
    float l_in = luminance(c_in);
    return c_in * (l_out / l_in);
}
vec3 reinhard_extended_luminance(vec3 v, float max_white_l)
{
    float l_old = luminance(v);
    float numerator = l_old * (1.0f + (l_old / (max_white_l * max_white_l)));
    float l_new = numerator / (1.0f + l_old);
    return change_luminance(v, l_new);
}



// PBR functionality from https://google.github.io/filament/Filament.html
const float PI = 3.1415926535897932384626433832795;
float D_GGX(float NoH, float roughness) 
{
    float a = NoH * roughness;
    float k = roughness / (1.0 - NoH * NoH + a * a);
    return k * k * (1.0 / PI);
}
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness)
{
    float a2 = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}
vec3 F_Schlick(float u, vec3 f0) 
{
	float f = pow(1.0 - u, 5.0);
    return f + f0 * (1.0 - f);
}

float Fd_Lambert() 
{
    return 1.0 / PI;
}

vec3 brdf(vec3 v, vec3 l, vec3 n, vec3 f0, vec3 baseColor, float metallic, float perceptualRoughness) 
{
	vec3 diffuseColor = (1.0f - metallic) * baseColor;

    vec3 h = normalize(v + l);

    float NoV = abs(dot(n, v)) + 1e-5;
    float NoL = clamp(dot(n, l), 0.0, 1.0);
    float NoH = clamp(dot(n, h), 0.0, 1.0);
    float LoH = clamp(dot(l, h), 0.0, 1.0);

    float roughness = perceptualRoughness * perceptualRoughness;
	
    float D = D_GGX(NoH, roughness);
    vec3  F = F_Schlick(LoH, f0);
    float V = V_SmithGGXCorrelated(NoV, NoL, roughness);

    // specular BRDF
    vec3 Fr = ((D * V) * F);
	//Fr = vec3(0.0f);

    // diffuse BRDF
    vec3 Fd = diffuseColor * Fd_Lambert();

	return Fr + Fd;
}



// Reflective Shadow Maps. Carsten Dachsbacher, Marc Stamminger. 2005. 
vec3 pixelLightContrib(vec3 fragPos, vec3 fragNorm, vec3 plPos, vec3 plNorm, vec3 radiantFlux)
{
	float geometry = max(0.0f, dot(plNorm,   (fragPos - plPos  )))
				   * max(0.0f, dot(fragNorm, (plPos   - fragPos)));

	float d = max(distance(fragPos, plPos), 0.01f);

	return radiantFlux * (geometry / pow(d, 4));
}

// From https://github.com/JuanDiegoMontoya/Fwog/blob/main/example/shaders/rsm/Indirect.comp.glsl
vec2 hammersley(uint i, uint N)
{
  return vec2(float(i) / float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

const int rsmSamples = 100;

vec3 computeRSM(vec3 fragPos, vec3 fragNorm, vec2 rsmUv, int cascade, mat4 lightInvViewProj)
{
	float rsmRMax = 0.5f;

	float rMaxScale = lightMatrices[0].radius[0][0] / lightMatrices[cascade].radius[0][0];
	rsmRMax *= rMaxScale;
	
	vec3 p0 = reconstructFragmentWorldPositionFromDepth(rsmUv * textureSize(shadowDepthMap, 0).xy, 
		0.0f, ivec2(textureSize(shadowDepthMap, 0).xy), lightInvViewProj);
	vec3 p1 = reconstructFragmentWorldPositionFromDepth(vec2(rsmUv.x + rsmRMax, rsmUv.y) * textureSize(shadowDepthMap, 0).xy, 
		0.0f, ivec2(textureSize(shadowDepthMap, 0).xy), lightInvViewProj);

	float rMaxWorld = distance(p0, p1);
	
    float normalizationFactor = 2.0f * rMaxWorld * rMaxWorld;

	vec3 outLight = vec3(0.0f);
  
	for (int i = 0; i < rsmSamples; ++i)
	{
	
		vec2 xi = hammersley(i, rsmSamples);
		float r = xi.x;
		float theta = xi.y * (2.0f * PI);

		vec2 plUv = rsmUv + vec2(r * cos(theta), r * sin(theta)) * rsmRMax;
		float weight = xi.x;

		float depth = texture(shadowDepthMap, vec3(plUv, cascade)).x;
		vec3 plPos = reconstructFragmentWorldPositionFromDepth(plUv * textureSize(shadowDepthMap, 0).xy, 
		depth, ivec2(textureSize(shadowDepthMap, 0).xy), lightInvViewProj);

		vec3 plNorm = texture(shadowNormalMap, vec3(plUv, cascade)).xyz;

		//plUv = rsmUv;
		vec3 radiantFlux = texture(shadowRadiantFluxMap, vec3(plUv, cascade)).rgb;

		outLight += pixelLightContrib(fragPos, fragNorm, plPos, plNorm, radiantFlux) * weight;
		//outLight = radiantFlux;
	}
	//return outLight / rsmSamples;
	return normalizationFactor * outLight / rsmSamples;
}



// From https://therealmjp.github.io/posts/storing-normals-using-spherical-coordinates/
vec3 sphericalToCartesian(vec2 spherical)
{
    spherical = spherical * 2.0f - 1.0f;

	vec2 sinCosTheta = vec2(sin(spherical.x * 3.1415926f), cos(spherical.x * 3.1415926f));
	vec2 sinCosPhi   = vec2(sqrt(1.0f - spherical.x * spherical.y), spherical.y);

    return vec3(sinCosTheta.y * sinCosPhi.x, sinCosTheta.x * sinCosPhi.x, sinCosPhi.y);    
}




void main()
{
	ivec2 coords = ivec2(gl_FragCoord.xy);

	vec4 baseColor = texelFetch(inColor, coords, 0);
	//baseColor = vec4(0.5f, 0.0f, 0.0f, 1.0f);
	baseColor = vec4(pow(baseColor.rgb, vec3(2.2f)), baseColor.a);

	vec3 worldPos = reconstructFragmentWorldPositionFromDepth(texelFetch(inDepth, coords, 0).r, vec2(1440, 810), invViewProj);

	vec3 norm		= texelFetch(inNorm, coords, 0).xyz;
	vec3 gWorldNorm = texelFetch(inNorm, coords, 0).zww; // Hopefully the cache loves me
	norm = normalize(sphericalToCartesian(norm.xy));
	gWorldNorm = normalize(sphericalToCartesian(gWorldNorm.xy));

	vec2 metallicRoughness = texelFetch(inMetallicRoughness, coords, 0).rg;

	const vec3 lightCol = const vec3(1.0f, 0.851f, 0.713f) * 5.0f;

	const vec3 skyAmbientCol = const vec3(0.765f, 0.820f, 1.0f);
	float skyAmbientStrength = 0.075f;
	//skyAmbientStrength = 1.0f;

	vec3 ambient = skyAmbientStrength * skyAmbientCol;

	float cosLightAngle = dot(lightDir, gWorldNorm);
	float normalOffsetScale = clamp(1 - cosLightAngle, 0.0f, 1.0f);
	normalOffsetScale *= 0.5f;

	vec3 shadowReceiverPos = worldPos + gWorldNorm * (normalOffsetScale + 0.0001f);

	vec4 viewPos = view * vec4(shadowReceiverPos, 1.0f);
	float dither = dither(coords);
	//dither = 0;
	viewPos.z += dither * 2.0f;
	float depth = (viewPos.z) + dither * 2.0f;
	depth = viewPos.z;

	int shadowCascade = 2;
	if (depth > -36.0f)
	{
		shadowCascade = 1;
	}
	if (depth > -9.0f)
	{
		shadowCascade = 0;
	}

	vec4 lightSpacePos = lightMatrices[shadowCascade].viewProj * vec4(shadowReceiverPos, 1.0f);
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

			shadow += texture(shadowMap, vec4(projCoords.xy + offset, shadowCascade, projCoords.z + 0.00008f));
		}
	}
	shadow /= 9;

	float a = 0.5f + 0.5f * dot(norm, vec3(0.0f, 1.0f, 0.0f));
	ambient *= a * 0.5f;
	ambient += computeRSM(worldPos, norm, projCoords.xy, shadowCascade, inverse(lightMatrices[shadowCascade].viewProj));


	float reflectance = 0.4f;
	vec3 f0 = 0.16f * reflectance * reflectance * (1.0f - metallicRoughness.r) + baseColor.rgb * metallicRoughness.r;

	float NoL = clamp(dot(norm, lightDir), 0.0f, 1.0f);
	
	outColor.rgb = (1.0f - shadow) * (brdf(normalize(camPos - worldPos), lightDir, norm, f0, baseColor.rgb, metallicRoughness.r, metallicRoughness.g)
	* NoL * lightCol) + ambient * baseColor.rgb;


	// todo: exposure parameter in the gui
	float whiteValue = luminance(lightCol);
	outColor = vec4(reinhard_extended_luminance(outColor.rgb, whiteValue), 1.0f);
	//outColor = baseColor;
	//outColor = vec4(baseColor.rgb, 1.0f);
	//outColor = vec4(ambient * baseColor.rgb, 1.0f);
	//outColor = vec4(ambient, 1.0f);

	gl_FragDepth = texelFetch(inDepth, coords, 0).r;
}