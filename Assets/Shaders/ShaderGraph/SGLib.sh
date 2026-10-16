#ifndef SHADER_GRAPH_LIB_SH
#define SHADER_GRAPH_LIB_SH

// Functions the shader graph nodes call (Engine/Source/ShaderGraph/Nodes). Everything here must
// compile in both bgfx dialect paths (GLSL, and the HLSL path used for SPIR-V, Metal and D3D):
// no implicit scalar->vector arguments to overloaded helpers, no scalar swizzles, float literals
// with a decimal point. Functions using derivatives only exist in fragment shaders.

#define SG_PI 3.14159265359
#define SG_TAU 6.28318530718

// ---------------------------------------------------------------------------------------------
// Vectors

vec3 sgSafeNormalize(vec3 _v)
{
	float lengthSq = dot(_v, _v);
	return lengthSq > 1e-12 ? _v * inversesqrt(lengthSq) : vec3(0.0, 0.0, 1.0);
}

vec2 sgSafeNormalize2(vec2 _v)
{
	float lengthSq = dot(_v, _v);
	return lengthSq > 1e-12 ? _v * inversesqrt(lengthSq) : vec2(1.0, 0.0);
}

// Rodrigues' rotation of _in about _axis (radians).
vec3 sgRotateAboutAxis(vec3 _in, vec3 _axis, float _angle)
{
	vec3 axis = sgSafeNormalize(_axis);
	float s = sin(_angle);
	float c = cos(_angle);
	return _in * c + cross(axis, _in) * s + axis * dot(axis, _in) * (1.0 - c);
}

// ---------------------------------------------------------------------------------------------
// Normals

// A tangent-space normal from a normal-map sample (z rebuilt, so two-channel maps work).
vec3 sgUnpackNormal(vec4 _sample)
{
	vec3 n;
	n.xy = _sample.xy * 2.0 - 1.0;
	n.z = sqrt(max(1.0 - dot(n.xy, n.xy), 0.0));
	return n;
}

vec3 sgNormalReconstructZ(vec2 _xy)
{
	return vec3(_xy, sqrt(max(1.0 - dot(_xy, _xy), 0.0)));
}

vec3 sgNormalStrength(vec3 _n, float _strength)
{
	return sgSafeNormalize(vec3(_n.xy * _strength, mix(1.0, _n.z, clamp(_strength, 0.0, 1.0))));
}

// Whiteout blend of two tangent-space normals.
vec3 sgNormalBlend(vec3 _a, vec3 _b)
{
	return sgSafeNormalize(vec3(_a.xy + _b.xy, _a.z * _b.z));
}

// Reoriented normal mapping (detail map _b follows the surface of _a).
vec3 sgNormalBlendReoriented(vec3 _a, vec3 _b)
{
	vec3 t = _a + vec3(0.0, 0.0, 1.0);
	vec3 u = _b * vec3(-1.0, -1.0, 1.0);
	return sgSafeNormalize(t * dot(t, u) - u * t.z);
}

vec3 sgTangentToWorld(vec3 _v, vec3 _tangent, vec3 _bitangent, vec3 _normal)
{
	return sgSafeNormalize(_tangent * _v.x + _bitangent * _v.y + _normal * _v.z);
}

vec3 sgWorldToTangent(vec3 _v, vec3 _tangent, vec3 _bitangent, vec3 _normal)
{
	return vec3(dot(_v, _tangent), dot(_v, _bitangent), dot(_v, _normal));
}

#if BGFX_SHADER_TYPE_FRAGMENT
// A tangent-space normal from a height value, through screen-space derivatives.
vec3 sgNormalFromHeight(float _height, float _strength, vec3 _position, vec3 _normal)
{
	vec3 dpdx = dFdx(_position);
	vec3 dpdy = dFdy(_position);
	float dhdx = dFdx(_height) * _strength;
	float dhdy = dFdy(_height) * _strength;
	vec3 r1 = cross(dpdy, _normal);
	vec3 r2 = cross(_normal, dpdx);
	float det = dot(dpdx, r1);
	vec3 grad = (r1 * dhdx + r2 * dhdy) * (det < 0.0 ? -1.0 : 1.0);
	return sgSafeNormalize(abs(det) * _normal - grad);
}
#endif

// ---------------------------------------------------------------------------------------------
// UV

vec2 sgRotateUV(vec2 _uv, vec2 _center, float _radians)
{
	vec2 d = _uv - _center;
	float s = sin(_radians);
	float c = cos(_radians);
	return vec2(d.x * c - d.y * s, d.x * s + d.y * c) + _center;
}

vec2 sgPolarCoordinates(vec2 _uv, vec2 _center, float _radialScale, float _lengthScale)
{
	vec2 d = _uv - _center;
	float radius = length(d) * 2.0 * _radialScale;
	float angle = atan2(d.x, d.y) * (1.0 / SG_TAU) * _lengthScale;
	return vec2(radius, angle);
}

vec2 sgTwirl(vec2 _uv, vec2 _center, float _strength, vec2 _offset)
{
	vec2 d = _uv - _center;
	float angle = _strength * length(d);
	float s = sin(angle);
	float c = cos(angle);
	return vec2(c * d.x - s * d.y, s * d.x + c * d.y) + _center + _offset;
}

vec2 sgSpherize(vec2 _uv, vec2 _center, float _strength, vec2 _offset)
{
	vec2 d = _uv - _center;
	float d2 = dot(d, d);
	return _uv + d * (d2 * d2) * _strength + _offset;
}

// Picks tile _tile (row-major from the top left) of a _columns x _rows sheet.
vec2 sgFlipbook(vec2 _uv, float _columns, float _rows, float _tile)
{
	float columns = max(_columns, 1.0);
	float rows = max(_rows, 1.0);
	float tile = mod(floor(_tile), columns * rows);
	vec2 cell = vec2(mod(tile, columns), floor(tile / columns));
	return (_uv + cell) / vec2(columns, rows);
}

// ---------------------------------------------------------------------------------------------
// Noise (hash-based, deterministic across backends)

float sgHash21(vec2 _p)
{
	vec3 p3 = fract(vec3(_p.x, _p.y, _p.x) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

vec2 sgHash22(vec2 _p)
{
	vec3 p3 = fract(vec3(_p.x, _p.y, _p.x) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

float sgValueNoise(vec2 _uv, float _scale)
{
	vec2 p = _uv * _scale;
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = sgHash21(i);
	float b = sgHash21(i + vec2(1.0, 0.0));
	float c = sgHash21(i + vec2(0.0, 1.0));
	float d = sgHash21(i + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Perlin-style gradient noise in 0..1.
float sgGradientNoise(vec2 _uv, float _scale)
{
	vec2 p = _uv * _scale;
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	vec2 ga = sgHash22(i) * 2.0 - 1.0;
	vec2 gb = sgHash22(i + vec2(1.0, 0.0)) * 2.0 - 1.0;
	vec2 gc = sgHash22(i + vec2(0.0, 1.0)) * 2.0 - 1.0;
	vec2 gd = sgHash22(i + vec2(1.0, 1.0)) * 2.0 - 1.0;
	float a = dot(ga, f);
	float b = dot(gb, f - vec2(1.0, 0.0));
	float c = dot(gc, f - vec2(0.0, 1.0));
	float d = dot(gd, f - vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.7071 + 0.5;
}

// 2D simplex noise in 0..1.
float sgSimplexNoise(vec2 _uv, float _scale)
{
	vec2 p = _uv * _scale;
	const float K1 = 0.366025404; // (sqrt(3) - 1) / 2
	const float K2 = 0.211324865; // (3 - sqrt(3)) / 6
	vec2 i = floor(p + (p.x + p.y) * K1);
	vec2 a = p - i + (i.x + i.y) * K2;
	float m = step(a.y, a.x);
	vec2 o = vec2(m, 1.0 - m);
	vec2 b = a - o + K2;
	vec2 c = a - 1.0 + 2.0 * K2;
	vec3 h = max(vec3(0.5, 0.5, 0.5) - vec3(dot(a, a), dot(b, b), dot(c, c)), vec3(0.0, 0.0, 0.0));
	vec3 n = h * h * h * h * vec3(dot(a, sgHash22(i) * 2.0 - 1.0), dot(b, sgHash22(i + o) * 2.0 - 1.0), dot(c, sgHash22(i + vec2(1.0, 1.0)) * 2.0 - 1.0));
	return dot(n, vec3(70.0, 70.0, 70.0)) * 0.5 + 0.5;
}

// Fractal sum of gradient noise; _octaves is clamped to 1..8.
float sgFractalNoise(vec2 _uv, float _scale, float _octaves, float _lacunarity, float _gain)
{
	float sum = 0.0;
	float amplitude = 0.5;
	float frequency = _scale;
	float total = 0.0;
	for (int i = 0; i < 8; ++i)
	{
		if (float(i) >= clamp(_octaves, 1.0, 8.0))
		{
			break;
		}
		sum += sgGradientNoise(_uv, frequency) * amplitude;
		total += amplitude;
		frequency *= _lacunarity;
		amplitude *= _gain;
	}
	return sum / max(total, 1e-4);
}

// x: distance to the nearest cell point, y: that cell's random value.
vec2 sgVoronoi(vec2 _uv, float _angleOffset, float _cellDensity)
{
	vec2 p = _uv * _cellDensity;
	vec2 cell = floor(p);
	vec2 f = fract(p);
	float best = 8.0;
	float value = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 lattice = vec2(float(x), float(y));
			vec2 h = sgHash22(cell + lattice);
			vec2 offset = vec2(sin(h.y * _angleOffset), cos(h.x * _angleOffset)) * 0.5 + 0.5;
			float d = distance(lattice + offset, f);
			if (d < best)
			{
				best = d;
				value = h.x;
			}
		}
	}
	return vec2(best, value);
}

// ---------------------------------------------------------------------------------------------
// Shapes (anti-aliased with derivatives in fragment shaders, hard-edged in vertex shaders)

float sgAntialias(float _distance)
{
#if BGFX_SHADER_TYPE_FRAGMENT
	float width = max(fwidth(_distance), 1e-5);
	return clamp(0.5 - _distance / width, 0.0, 1.0);
#else
	return _distance < 0.0 ? 1.0 : 0.0;
#endif
}

float sgEllipse(vec2 _uv, float _width, float _height)
{
	vec2 d = (_uv * 2.0 - 1.0) / max(vec2(_width, _height), vec2(1e-4, 1e-4));
	return sgAntialias((length(d) - 1.0) * min(_width, _height) * 0.5);
}

float sgRectangle(vec2 _uv, float _width, float _height)
{
	vec2 d = abs(_uv * 2.0 - 1.0) - vec2(_width, _height);
	return sgAntialias(max(d.x, d.y) * 0.5);
}

float sgRoundedRectangle(vec2 _uv, float _width, float _height, float _radius)
{
	float radius = max(min(min(abs(_radius * 2.0), abs(_width)), abs(_height)), 1e-5);
	vec2 d = abs(_uv * 2.0 - 1.0) - vec2(_width, _height) + radius;
	float distanceOut = length(max(d, vec2(0.0, 0.0))) + min(max(d.x, d.y), 0.0) - radius;
	return sgAntialias(distanceOut * 0.5);
}

float sgPolygon(vec2 _uv, float _sides, float _width, float _height)
{
	float sides = max(floor(_sides), 3.0);
	float pi = SG_PI;
	float aWidth = _width * cos(pi / sides);
	float aHeight = _height * cos(pi / sides);
	vec2 uv = (_uv * 2.0 - 1.0) / max(vec2(aWidth, aHeight), vec2(1e-4, 1e-4));
	uv.y *= -1.0;
	float pCoord = atan2(uv.x, uv.y);
	float r = 2.0 * pi / sides;
	float distanceOut = cos(floor(0.5 + pCoord / r) * r - pCoord) * length(uv);
	return sgAntialias((distanceOut - 1.0) * min(aWidth, aHeight) * 0.5);
}

vec3 sgCheckerboard(vec2 _uv, vec3 _colorA, vec3 _colorB, vec2 _frequency)
{
	vec2 checks = floor(_uv * _frequency);
	float checker = mod(checks.x + checks.y, 2.0);
	return mix(_colorA, _colorB, vec3(checker, checker, checker));
}

// ---------------------------------------------------------------------------------------------
// Colour

float sgLuminance(vec3 _rgb)
{
	return dot(_rgb, vec3(0.2126729, 0.7151522, 0.0721750));
}

vec3 sgRGBToHSV(vec3 _rgb)
{
	vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 P = mix(vec4(_rgb.bg, K.wz), vec4(_rgb.gb, K.xy), vec4_splat(step(_rgb.b, _rgb.g)));
	vec4 Q = mix(vec4(P.xyw, _rgb.r), vec4(_rgb.r, P.yzx), vec4_splat(step(P.x, _rgb.r)));
	float D = Q.x - min(Q.w, Q.y);
	float E = 1e-10;
	return vec3(abs(Q.z + (Q.w - Q.y) / (6.0 * D + E)), D / (Q.x + E), Q.x);
}

vec3 sgHSVToRGB(vec3 _hsv)
{
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 P = abs(fract(_hsv.xxx + K.xyz) * 6.0 - K.www);
	return _hsv.z * mix(K.xxx, clamp(P - K.xxx, 0.0, 1.0), vec3_splat(_hsv.y));
}

// _offset in turns (1.0 = a full turn of the hue wheel).
vec3 sgHueShift(vec3 _rgb, float _offset)
{
	vec3 hsv = sgRGBToHSV(_rgb);
	hsv.x = fract(hsv.x + _offset);
	return sgHSVToRGB(hsv);
}

vec3 sgSaturation(vec3 _rgb, float _saturation)
{
	float luma = sgLuminance(_rgb);
	return vec3_splat(luma) + (_rgb - vec3_splat(luma)) * _saturation;
}

vec3 sgContrast(vec3 _rgb, float _contrast)
{
	float midpoint = 0.21763764; // mid grey in linear
	return (_rgb - vec3_splat(midpoint)) * _contrast + vec3_splat(midpoint);
}

vec3 sgReplaceColor(vec3 _rgb, vec3 _from, vec3 _to, float _range, float _fuzziness)
{
	float d = distance(_from, _rgb);
	float t = clamp((d - _range) / max(_fuzziness, 1e-5), 0.0, 1.0);
	return mix(_to, _rgb, vec3_splat(t));
}

vec3 sgLinearToSRGB(vec3 _rgb)
{
	vec3 c = max(_rgb, vec3_splat(0.0));
	vec3 low = c * 12.92;
	vec3 high = 1.055 * pow(c, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(high, low, step(c, vec3_splat(0.0031308)));
}

vec3 sgSRGBToLinear(vec3 _rgb)
{
	vec3 c = max(_rgb, vec3_splat(0.0));
	vec3 low = c / 12.92;
	vec3 high = pow((c + 0.055) / 1.055, vec3_splat(2.4));
	return mix(high, low, step(c, vec3_splat(0.04045)));
}

// Photoshop-style blend modes (_base under _blend).
vec3 sgBlendBurn(vec3 _base, vec3 _blend) { return vec3_splat(1.0) - (vec3_splat(1.0) - _blend) / max(_base, vec3_splat(1e-5)); }
vec3 sgBlendDarken(vec3 _base, vec3 _blend) { return min(_blend, _base); }
vec3 sgBlendDifference(vec3 _base, vec3 _blend) { return abs(_blend - _base); }
vec3 sgBlendDodge(vec3 _base, vec3 _blend) { return _base / max(vec3_splat(1.0) - _blend, vec3_splat(1e-5)); }
vec3 sgBlendDivide(vec3 _base, vec3 _blend) { return _base / max(_blend, vec3_splat(1e-5)); }
vec3 sgBlendExclusion(vec3 _base, vec3 _blend) { return _blend + _base - 2.0 * _blend * _base; }
vec3 sgBlendLighten(vec3 _base, vec3 _blend) { return max(_blend, _base); }
vec3 sgBlendLinearDodge(vec3 _base, vec3 _blend) { return _base + _blend; }
vec3 sgBlendMultiply(vec3 _base, vec3 _blend) { return _base * _blend; }
vec3 sgBlendScreen(vec3 _base, vec3 _blend) { return vec3_splat(1.0) - (vec3_splat(1.0) - _blend) * (vec3_splat(1.0) - _base); }
vec3 sgBlendSubtract(vec3 _base, vec3 _blend) { return _base - _blend; }
vec3 sgBlendOverwrite(vec3 _base, vec3 _blend) { return _blend; }

vec3 sgBlendOverlay(vec3 _base, vec3 _blend)
{
	vec3 low = 2.0 * _base * _blend;
	vec3 high = vec3_splat(1.0) - 2.0 * (vec3_splat(1.0) - _base) * (vec3_splat(1.0) - _blend);
	return mix(low, high, step(vec3_splat(0.5), _base));
}

vec3 sgBlendHardLight(vec3 _base, vec3 _blend)
{
	return sgBlendOverlay(_blend, _base);
}

vec3 sgBlendSoftLight(vec3 _base, vec3 _blend)
{
	vec3 low = 2.0 * _base * _blend + _base * _base * (vec3_splat(1.0) - 2.0 * _blend);
	vec3 high = sqrt(max(_base, vec3_splat(0.0))) * (2.0 * _blend - vec3_splat(1.0)) + 2.0 * _base * (vec3_splat(1.0) - _blend);
	return mix(low, high, step(vec3_splat(0.5), _blend));
}

// ---------------------------------------------------------------------------------------------
// Texturing

#if BGFX_SHADER_TYPE_FRAGMENT
// World-space triplanar projection; _blend sharpens the transition between the three planes.
vec4 sgTriplanar(sampler2D _texture, vec3 _position, vec3 _normal, float _tile, float _blend)
{
	vec3 weights = pow(abs(_normal), vec3_splat(max(_blend, 1e-3)));
	weights /= max(weights.x + weights.y + weights.z, 1e-5);
	vec3 p = _position * _tile;
	return texture2D(_texture, p.zy) * weights.x
		+ texture2D(_texture, p.xz) * weights.y
		+ texture2D(_texture, p.xy) * weights.z;
}

// Parallax offset (bump offset): _viewTS is the tangent-space direction to the camera.
vec2 sgParallaxOffset(sampler2D _heightMap, vec2 _uv, float _amplitude, vec3 _viewTS)
{
	float height = texture2D(_heightMap, _uv).r - 0.5;
	return _uv + _viewTS.xy / max(_viewTS.z, 0.05) * height * _amplitude;
}
#endif

#endif // SHADER_GRAPH_LIB_SH
