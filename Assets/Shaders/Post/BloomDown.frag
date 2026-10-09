$input v_texcoord0

// Bloom downsample: 13-tap filter (Jimenez, CoD: Advanced Warfare). The first pass (u_bloom.w = 1)
// applies the soft-knee threshold and a Karis average to suppress fireflies.
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_input0, 0);
uniform vec4 u_bloom; // x threshold, y knee, z unused, w first pass

vec3 prefilter(vec3 _color)
{
	float brightness = max(_color.r, max(_color.g, _color.b));
	float knee = u_bloom.x * u_bloom.y + 1e-4;
	float soft = clamp(brightness - u_bloom.x + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee);
	float contribution = max(soft, brightness - u_bloom.x) / max(brightness, 1e-4);
	return _color * contribution;
}

float karisWeight(vec3 _color)
{
	return 1.0 / (1.0 + lumaRec709(_color));
}

void main()
{
	vec2 t = u_viewTexel.xy;   // texel of the destination; source is twice the size
	vec2 uv = v_texcoord0;
	vec3 a = texture2D(s_input0, uv + t * vec2(-1.0, -1.0)).rgb;
	vec3 b = texture2D(s_input0, uv + t * vec2( 0.0, -1.0)).rgb;
	vec3 c = texture2D(s_input0, uv + t * vec2( 1.0, -1.0)).rgb;
	vec3 d = texture2D(s_input0, uv + t * vec2(-0.5, -0.5)).rgb;
	vec3 e = texture2D(s_input0, uv + t * vec2( 0.5, -0.5)).rgb;
	vec3 f = texture2D(s_input0, uv + t * vec2(-1.0,  0.0)).rgb;
	vec3 g = texture2D(s_input0, uv).rgb;
	vec3 h = texture2D(s_input0, uv + t * vec2( 1.0,  0.0)).rgb;
	vec3 i = texture2D(s_input0, uv + t * vec2(-0.5,  0.5)).rgb;
	vec3 j = texture2D(s_input0, uv + t * vec2( 0.5,  0.5)).rgb;
	vec3 k = texture2D(s_input0, uv + t * vec2(-1.0,  1.0)).rgb;
	vec3 l = texture2D(s_input0, uv + t * vec2( 0.0,  1.0)).rgb;
	vec3 m = texture2D(s_input0, uv + t * vec2( 1.0,  1.0)).rgb;

	vec3 color;
	if (u_bloom.w > 0.5)
	{
		vec3 g0 = (d + e + i + j) * 0.25;
		vec3 g1 = (a + b + f + g) * 0.25;
		vec3 g2 = (b + c + g + h) * 0.25;
		vec3 g3 = (f + g + k + l) * 0.25;
		vec3 g4 = (g + h + l + m) * 0.25;
		float w0 = karisWeight(g0) * 0.5;
		float w1 = karisWeight(g1) * 0.125;
		float w2 = karisWeight(g2) * 0.125;
		float w3 = karisWeight(g3) * 0.125;
		float w4 = karisWeight(g4) * 0.125;
		color = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1e-4);
		color = prefilter(color);
	}
	else
	{
		color = (d + e + i + j) * 0.125;
		color += (a + b + f + g) * 0.03125;
		color += (b + c + g + h) * 0.03125;
		color += (f + g + k + l) * 0.03125;
		color += (g + h + l + m) * 0.03125;
	}
	gl_FragColor = vec4(max(color, vec3_splat(0.0)), 1.0);
}
