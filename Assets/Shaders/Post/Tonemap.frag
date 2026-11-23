$input v_texcoord0

// HDR -> display: lens distortion, exposure, bloom add, white balance, tonemapping, grading, vignette, gamma
// encode, LUT, film grain, dither. Writes luma to alpha when FXAA follows.
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_hdrColor, 0);
SAMPLER2D(s_bloom, 1);
SAMPLER2D(s_exposure, 2);  // adapted average luminance (1x1), when auto exposure is on
SAMPLER3D(s_lut, 3);       // .cube grading volume (sRGB in/out)
SAMPLER2D(s_lutStrip, 4);  // strip LUT: N slices of NxN side by side, blue picks the slice

uniform vec4 u_tonemap;   // x exposure (linear multiplier), y operator (0 clamp, 1 ACES, 2 AgX, 3 Reinhard), z bloom intensity, w vignette intensity
uniform vec4 u_grading;   // x saturation, y contrast, z temperature, w tint
uniform vec4 u_grading3;  // colours a desaturating grade keeps: x reds, y blues (0 none .. 1 all of it), z how far the reds reach into orange and yellow (0 pure red .. 1 fire)
uniform vec4 u_grading2;  // x vignette smoothness, y gamma, z lift, w gain
uniform vec4 u_exposure;  // x auto exposure on, y compensation multiplier (2^EV), z luma in alpha (FXAA follows), w dither (0 for pixel-perfect cameras)
uniform vec4 u_lut;       // x LUT size N, y contribution, z mode (0 off, 1 volume, 2 strip)
uniform vec4 u_grain;     // x film grain intensity (0 off), y cell size in pixels, z pattern seed
uniform vec4 u_lens;      // x fisheye half angle over the diagonal in radians (0 off), y colour fringe, z aspect (width / height)

// Where a fisheye lens of half angle u_lens.x over the diagonal sees this pixel in the straight
// (rectilinear) picture: the angle grows evenly with the distance from the middle, so the source
// distance is its tangent. The corners map to the corners; _spread widens or narrows the map a
// touch (the colour fringe).
vec2 lensUv(vec2 _uv, float _spread)
{
	vec2 p = (_uv - 0.5) * vec2(u_lens.z, 1.0);
	float corner = 0.5 * sqrt(u_lens.z * u_lens.z + 1.0);
	float r = length(p) / corner;
	float bent = tan(min(r * _spread, 1.0) * u_lens.x) / tan(u_lens.x);
	p *= bent / max(r, 0.0001);
	return p / vec2(u_lens.z, 1.0) + 0.5;
}

// A hash of a 2D cell (Dave Hoskins' hash12): no sine, so it's the same on every GPU.
float grainHash(vec2 _p)
{
	vec3 p3 = fract(vec3(_p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec3 color;
	if (u_lens.x > 0.0)
	{
		uv = lensUv(v_texcoord0, 1.0);
		color = texture2D(s_hdrColor, uv).rgb;
		if (u_lens.y > 0.0)
		{
			// Red lands a little inside, blue a little outside, more toward the edges.
			vec2 centered = v_texcoord0 - 0.5;
			float split = 0.012 * u_lens.y * dot(centered, centered) * 4.0;
			color.r = texture2D(s_hdrColor, lensUv(v_texcoord0, 1.0 - split)).r;
			color.b = texture2D(s_hdrColor, lensUv(v_texcoord0, 1.0 + split)).b;
		}
	}
	else
	{
		color = texture2D(s_hdrColor, uv).rgb;
	}
	color = mix(color, texture2D(s_bloom, uv).rgb, u_tonemap.z);
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
	// Kept colours (u_grading3): where the picture is red (out to orange and yellow by z) or blue,
	// a grade that takes the colour out leaves it in, and the LUT below leaves it alone: a black
	// and white picture with its emergency lights and its fire still in colour.
	float keep = 0.0;
	if (u_grading3.x + u_grading3.y > 0.0)
	{
		float top = max(max(color.r, color.g), color.b);
		float red = (color.r - max(color.g, color.b)) / max(color.r, 1e-4);
		float warm = color.g <= color.r ? (color.r - color.b) / max(color.r, 1e-4) : 0.0;
		float blue = (color.b - max(color.r, color.g)) / max(color.b, 1e-4);
		// The wider the reds reach, the more colour it takes to count: skin and warm white stay grey.
		float from = mix(0.12, 0.4, u_grading3.z);
		keep = max(u_grading3.x * smoothstep(from, from + 0.33, mix(red, warm, u_grading3.z)), u_grading3.y * smoothstep(0.12, 0.45, blue));
		keep *= smoothstep(0.02, 0.08, top);
	}
	color = mix(vec3_splat(l), color, mix(u_grading.x, max(u_grading.x, 1.0), keep));
	color = (color - 0.18) * u_grading.y + 0.18;
	color = color * u_grading2.w + u_grading2.z * (1.0 - color);
	color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / max(u_grading2.y, 0.01)));

	// Vignette.
	vec2 centered = v_texcoord0 - 0.5;
	float vignette = 1.0 - u_tonemap.w * smoothstep(0.2, 0.2 + u_grading2.x, dot(centered, centered) * 2.0);
	color *= vignette;

	vec3 display = toGammaAccurate(clamp(color, 0.0, 1.0));

	// Grading LUT, display-referred (sRGB in and out), sampled at texel centres.
	if (u_lut.z > 1.5)
	{
		float n = u_lut.x;
		vec3 c = clamp(display, 0.0, 1.0) * (n - 1.0);
		float slice = floor(c.b);
		float next = min(slice + 1.0, n - 1.0);
		vec2 cell = vec2((c.r + 0.5) / (n * n), (c.g + 0.5) / n);
		vec3 lutA = texture2DLod(s_lutStrip, vec2(cell.x + slice / n, cell.y), 0.0).rgb;
		vec3 lutB = texture2DLod(s_lutStrip, vec2(cell.x + next / n, cell.y), 0.0).rgb;
		display = mix(display, mix(lutA, lutB, c.b - slice), u_lut.y * (1.0 - keep));
	}
	else if (u_lut.z > 0.5)
	{
		float n = u_lut.x;
		vec3 coord = clamp(display, 0.0, 1.0) * ((n - 1.0) / n) + 0.5 / n;
		display = mix(display, texture3DLod(s_lut, coord, 0.0).rgb, u_lut.y * (1.0 - keep));
	}

	// Film grain: monochrome, triangular (two hashes summed, -1..1), strongest in the midtones like
	// film's, so blacks and blown highlights keep some grain without being swamped.
	if (u_grain.x > 0.0)
	{
		vec2 cell = floor(gl_FragCoord.xy / u_grain.y);
		vec2 seed = vec2(u_grain.z * 37.0, u_grain.z * 61.0);
		float grain = grainHash(cell + seed) + grainHash(cell + seed + vec2(113.0, 271.0)) - 1.0;
		float luma = clamp(dot(display, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
		display = clamp(display + grain * u_grain.x * mix(0.35, 1.0, 4.0 * luma * (1.0 - luma)), 0.0, 1.0);
	}

	// +-0.5 LSB of dither hides 8-bit banding in gradients (sky, fog). Pixel-perfect cameras skip it:
	// screen-fixed noise would change under art that moves in whole texels.
	display += (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0 * u_exposure.w;
	// FXAA reads luma from alpha; when tonemapping straight to the output the image must stay opaque.
	gl_FragColor = vec4(display, u_exposure.z > 0.5 ? dot(display, vec3(0.299, 0.587, 0.114)) : 1.0);
}
