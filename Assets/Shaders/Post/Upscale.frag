$input v_texcoord0

// Render scaling (LOOK-S1): the camera's internal-resolution image, upscaled to its output.
//   0 Bilinear   smooth, cheap
//   1 Nearest    blocky at any ratio (texel centres, no blending)
//   2 Pixel      integer nearest for pixel art: every texel is an exact N x N block
//   3 Sharp      Catmull-Rom bicubic, then contrast-adaptive sharpening (FSR1-style spatial)
#include "../Common.sh"
SAMPLER2D(s_input0, 0);
uniform vec4 u_upscaleParams; // x filter, y sharpness 0..1, z integer factor (Pixel)
uniform vec4 u_upscaleSize;   // xy input size (texels), zw output rect size (pixels)

vec3 fetchColor(vec2 _uv)
{
	return texture2DLod(s_input0, _uv, 0.0).rgb;
}

// Catmull-Rom in 9 bilinear taps (the weights folded into fractional offsets).
vec3 catmullRom(vec2 _uv, vec2 _size)
{
	vec2 texelSize = 1.0 / _size;
	vec2 samplePos = _uv * _size;
	vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
	vec2 f = samplePos - texPos1;
	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);
	vec2 w12 = w1 + w2;
	vec2 offset12 = w2 / w12;
	vec2 texPos0 = (texPos1 - 1.0) * texelSize;
	vec2 texPos3 = (texPos1 + 2.0) * texelSize;
	vec2 texPos12 = (texPos1 + offset12) * texelSize;
	vec3 result = vec3_splat(0.0);
	result += fetchColor(vec2(texPos0.x,  texPos0.y))  * w0.x  * w0.y;
	result += fetchColor(vec2(texPos12.x, texPos0.y))  * w12.x * w0.y;
	result += fetchColor(vec2(texPos3.x,  texPos0.y))  * w3.x  * w0.y;
	result += fetchColor(vec2(texPos0.x,  texPos12.y)) * w0.x  * w12.y;
	result += fetchColor(vec2(texPos12.x, texPos12.y)) * w12.x * w12.y;
	result += fetchColor(vec2(texPos3.x,  texPos12.y)) * w3.x  * w12.y;
	result += fetchColor(vec2(texPos0.x,  texPos3.y))  * w0.x  * w3.y;
	result += fetchColor(vec2(texPos12.x, texPos3.y))  * w12.x * w3.y;
	result += fetchColor(vec2(texPos3.x,  texPos3.y))  * w3.x  * w3.y;
	return max(result, vec3_splat(0.0));
}

void main()
{
	float mode = u_upscaleParams.x;
	vec2 inSize = u_upscaleSize.xy;
	vec2 uv = v_texcoord0;
	vec3 color;
	if (mode > 2.5)
	{
		color = catmullRom(uv, inSize);
		// Contrast-adaptive sharpening (AMD CAS): a negative-lobe cross filter whose strength
		// falls off where the neighbourhood already has contrast, so edges don't ring.
		vec2 t = 1.0 / inSize;
		vec3 n = fetchColor(uv + vec2(0.0, -t.y));
		vec3 s = fetchColor(uv + vec2(0.0, t.y));
		vec3 e = fetchColor(uv + vec2(t.x, 0.0));
		vec3 w = fetchColor(uv + vec2(-t.x, 0.0));
		vec3 mn = min(color, min(min(n, s), min(e, w)));
		vec3 mx = max(color, max(max(n, s), max(e, w)));
		vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3_splat(0.0001)), 0.0, 1.0));
		vec3 lobe = -amp * mix(1.0 / 8.0, 1.0 / 5.0, clamp(u_upscaleParams.y, 0.0, 1.0));
		color = clamp((color + lobe * (n + s + e + w)) / (1.0 + 4.0 * lobe), 0.0, 1.0);
	}
	else if (mode > 1.5)
	{
		// Output pixel -> its N x N block's texel (the image is anchored at the rect's top left).
		vec2 pixel = floor(uv * u_upscaleSize.zw);
		vec2 texel = floor(pixel / max(u_upscaleParams.z, 1.0));
		color = fetchColor((texel + 0.5) / inSize);
	}
	else if (mode > 0.5)
	{
		color = fetchColor((floor(uv * inSize) + 0.5) / inSize);
	}
	else
	{
		color = fetchColor(uv);
	}
	gl_FragColor = vec4(color, 1.0);
}
