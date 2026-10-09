// Shared helpers for post-processing passes.

// Interleaved gradient noise (Jimenez 2014): cheap screen-space dither.
float interleavedGradientNoise(vec2 _pixel)
{
	return fract(52.9829189 * fract(dot(_pixel, vec2(0.06711056, 0.00583715))));
}

float lumaRec709(vec3 _rgb)
{
	return dot(_rgb, vec3(0.2126, 0.7152, 0.0722));
}

// ACES fitted curve (Stephen Hill), more accurate than the Narkowicz approximation.
vec3 tonemapAcesFitted(vec3 _color)
{
	mat3 inputMat = mtxFromRows(
		vec3(0.59719, 0.35458, 0.04823),
		vec3(0.07600, 0.90834, 0.01566),
		vec3(0.02840, 0.13383, 0.83777));
	mat3 outputMat = mtxFromRows(
		vec3( 1.60475, -0.53108, -0.07367),
		vec3(-0.10208,  1.10813, -0.00605),
		vec3(-0.00327, -0.07276,  1.07602));
	vec3 v = mul(inputMat, _color);
	vec3 a = v * (v + 0.0245786) - 0.000090537;
	vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return clamp(mul(outputMat, a / b), 0.0, 1.0);
}

// AgX (Troy Sobotka), minimal fit by Benjamin Wrensch; output is display-encoded already.
vec3 agxDefaultContrast(vec3 _x)
{
	vec3 x2 = _x * _x;
	vec3 x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * _x + 31.96 * x4 - 6.868 * x2 * _x + 0.4298 * x2 + 0.1191 * _x - 0.00232;
}

vec3 tonemapAgX(vec3 _color)
{
	mat3 agxMat = mtxFromRows(
		vec3(0.842479062253094, 0.0784335999999992, 0.0792237451477643),
		vec3(0.0423282422610123, 0.878468636469772, 0.0791661274605434),
		vec3(0.0423756549057051, 0.0784336, 0.879142973793104));
	mat3 agxInvMat = mtxFromRows(
		vec3(1.19687900512017, -0.0980208811401368, -0.0990297440797205),
		vec3(-0.0528968517574562, 1.15190312990417, -0.0989611768448433),
		vec3(-0.0529716355144438, -0.0980434501171241, 1.15107367264116));
	const float minEv = -12.47393;
	const float maxEv = 4.026069;
	vec3 x = mul(agxMat, max(_color, vec3_splat(1e-10)));
	x = clamp((log2(x) - minEv) / (maxEv - minEv), 0.0, 1.0);
	x = agxDefaultContrast(x);
	x = mul(agxInvMat, x);
	// The curve already encodes for display; return linear so the shared gamma step applies once.
	return pow(max(x, vec3_splat(0.0)), vec3_splat(2.2));
}

vec3 tonemapReinhardLuma(vec3 _color)
{
	float l = lumaRec709(_color);
	return _color / (1.0 + l);
}

// Approximate white balance in linear sRGB from temperature/tint offsets (-1..1).
vec3 whiteBalance(vec3 _color, float _temperature, float _tint)
{
	vec3 scale = vec3(1.0 + 0.2 * _temperature, 1.0 - 0.1 * _tint, 1.0 - 0.2 * _temperature);
	return _color * scale;
}
