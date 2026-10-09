$input v_texcoord0

// FXAA (Lottes), compact single-pass variant on a gamma-space image with luma in alpha.
#include "../Common.sh"

SAMPLER2D(s_ldrColor, 0);

#define FXAA_REDUCE_MIN (1.0 / 128.0)
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_SPAN_MAX 8.0

void main()
{
	vec2 texel = u_viewTexel.xy;
	vec2 uv = v_texcoord0;

	float lumaNW = texture2D(s_ldrColor, uv + vec2(-1.0, -1.0) * texel).a;
	float lumaNE = texture2D(s_ldrColor, uv + vec2( 1.0, -1.0) * texel).a;
	float lumaSW = texture2D(s_ldrColor, uv + vec2(-1.0,  1.0) * texel).a;
	float lumaSE = texture2D(s_ldrColor, uv + vec2( 1.0,  1.0) * texel).a;
	vec4 center = texture2D(s_ldrColor, uv);
	float lumaM = center.a;

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125))
	{
		gl_FragColor = vec4(center.rgb, 1.0);
		return;
	}

	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, vec2_splat(-FXAA_SPAN_MAX), vec2_splat(FXAA_SPAN_MAX)) * texel;

	vec3 rgbA = 0.5 * (texture2D(s_ldrColor, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture2D(s_ldrColor, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
	vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(s_ldrColor, uv - dir * 0.5).rgb + texture2D(s_ldrColor, uv + dir * 0.5).rgb);
	float lumaB = dot(rgbB, vec3(0.299, 0.587, 0.114));
	gl_FragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
