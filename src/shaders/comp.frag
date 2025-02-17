// From learnopengl.com

#version 420 core

layout (binding = 0) uniform sampler2D accum;
layout (binding = 1) uniform sampler2D reveal;

layout (location = 0) out vec4 outColor;



const float epsilon = 0.00001f;



bool isApproximatelyEqual(float a, float b)
{
	return abs(a - b) <= (abs(a) < abs(b) ? abs(b) : abs(a)) * epsilon;
}

float max3(vec3 v)
{
	return max(max(v.x, v.y), v.z);
}

void main()
{
	ivec2 coords = ivec2(gl_FragCoord.xy);
	
	float revealage = texelFetch(reveal, coords, 0).r;

	if (isApproximatelyEqual(revealage, 1.0f))
	{
		discard;
	}

	vec4 accumulation = texelFetch(accum, coords, 0);

	if (isinf(max3(abs(accumulation.rgb))))
	{
		accumulation.rgb = vec3(accumulation.a);
	}

	// Prevent floating precision error
	vec3 average_color = accumulation.rgb / max(accumulation.a, epsilon);

	outColor = vec4(average_color, 1.0f - revealage);
}