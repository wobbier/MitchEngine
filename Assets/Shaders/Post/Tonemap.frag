$input v_texcoord0

// HDR -> display: exposure, bloom add, white balance, tonemapping, grading, vignette, dithered
// gamma encode. Writes luma to alpha when FXAA follows.
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_hdrColor, 0);
SAMPLER2D(s_bloom, 1);
SAMPLER2D(s_exposure, 2);  // adapted average luminance (1x1), when auto exposure is on

uniform vec4 u_tonemap;   // x exposure (linear multiplier), y operator (0 clamp, 1 ACES, 2 AgX, 3 Reinhard), z bloom intensity, w vignette intensity
uniform vec4 u_grading;   // x saturation, y contrast, z temperature, w tint
uniform vec4 u_grading2;  // x vignette smoothness, y gamma, z lift, w gain
uniform vec4 u_exposure;  // x auto exposure on, y compensation multiplier (2^EV), z luma in alpha (FXAA follows)

void main()
{
	vec3 color = texture2D(s_hdrColor, v_texcoord0).rgb;
	color = mix(color, texture2D(s_bloom, v_texcoord0).rgb, u_tonemap.z);
	float exposure = u_tonemap.x;
	if (u_exposure.x > 0.5)
	{
		// Key value 0.18 (mid grey) over the adapted scene average.
		exposure = 0.18 / max(texture2D(s_exposure, vec2_splat(0.5)).r, 0.0001) * u_exposure.y;
	}
	color *= exposure;
	color = whiteBalance(color, u_grading.z, u_grading.w);

	float op = u_tonemap.y;
	if (op > 2.5)
	{
		color = tonemapReinhardLuma(color);
	}
	else if (op > 1.5)
	{
		color = tonemapAgX(color);
	}
	else if (op > 0.5)
	{
		color = tonemapAcesFitted(color);
	}
	color = clamp(color, 0.0, 1.0);

	// Grading in linear display space: saturation and contrast around mid grey, lift/gain/gamma.
	float l = lumaRec709(color);
	color = mix(vec3_splat(l), color, u_grading.x);
	color = (color - 0.18) * u_grading.y + 0.18;
	color = color * u_grading2.w + u_grading2.z * (1.0 - color);
	color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / max(u_grading2.y, 0.01)));

	// Vignette.
	vec2 centered = v_texcoord0 - 0.5;
	float vignette = 1.0 - u_tonemap.w * smoothstep(0.2, 0.2 + u_grading2.x, dot(centered, centered) * 2.0);
	color *= vignette;

	vec3 display = toGammaAccurate(clamp(color, 0.0, 1.0));
	// +-0.5 LSB of dither hides 8-bit banding in gradients (sky, fog).
	display += (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
	// FXAA reads luma from alpha; when tonemapping straight to the output the image must stay opaque.
	gl_FragColor = vec4(display, u_exposure.z > 0.5 ? dot(display, vec3(0.299, 0.587, 0.114)) : 1.0);
}
