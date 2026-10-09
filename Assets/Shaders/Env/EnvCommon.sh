#ifndef __ENVCOMMON_SH__
#define __ENVCOMMON_SH__
// Shared helpers for environment capture and filtering. Faces are addressed through gl_FragCoord, so
// the same code works whichever way a backend orients render targets: on every backend the first
// row in memory is the face's t = 0 row.

#define ENV_PI 3.14159265359

#if BGFX_SHADER_LANGUAGE_GLSL >= 130
#	define envCubeLod(_sampler, _dir, _lod) textureLod(_sampler, _dir, _lod)
#else
#	define envCubeLod(_sampler, _dir, _lod) textureCubeLod(_sampler, _dir, _lod)
#endif

uniform vec4 u_envFace;    // x face (+X -X +Y -Y +Z -Z), y roughness, z source size, w sample count

// Direction through texel (s, t) of a cube face (D3D / GL cube map conventions).
vec3 cubeFaceDirection(float _face, vec2 _st)
{
	vec2 c = _st * 2.0 - 1.0;
	vec3 dir;
	if (_face < 0.5)      dir = vec3( 1.0, -c.y, -c.x);
	else if (_face < 1.5) dir = vec3(-1.0, -c.y,  c.x);
	else if (_face < 2.5) dir = vec3( c.x,  1.0,  c.y);
	else if (_face < 3.5) dir = vec3( c.x, -1.0, -c.y);
	else if (_face < 4.5) dir = vec3( c.x, -c.y,  1.0);
	else                  dir = vec3(-c.x, -c.y, -1.0);
	return normalize(dir);
}

// _fragCoord is gl_FragCoord.xy (only readable in main).
vec3 currentFaceDirection(vec2 _fragCoord)
{
	return cubeFaceDirection(u_envFace.x, _fragCoord * u_viewTexel.xy);
}

// Van der Corput radical inverse without integer ops (up to 1024 samples).
float radicalInverse(float _i)
{
	float result = 0.0;
	float fraction = 0.5;
	float n = _i;
	for (int bit = 0; bit < 10; ++bit)
	{
		result += fraction * mod(n, 2.0);
		n = floor(n * 0.5);
		fraction *= 0.5;
	}
	return result;
}

vec2 hammersley(float _i, float _count)
{
	return vec2((_i + 0.5) / _count, radicalInverse(_i));
}

void tangentFrame(vec3 _n, out vec3 _t, out vec3 _b)
{
	vec3 up = abs(_n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	_t = normalize(cross(up, _n));
	_b = cross(_n, _t);
}

// GGX half vector around _n for alpha _a.
vec3 importanceSampleGGX(vec2 _xi, vec3 _n, float _a)
{
	float phi = 2.0 * ENV_PI * _xi.x;
	float cosTheta = sqrt((1.0 - _xi.y) / (1.0 + (_a * _a - 1.0) * _xi.y));
	float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
	vec3 t;
	vec3 b;
	tangentFrame(_n, t, b);
	return normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + _n * cosTheta);
}

float envD_GGX(float _NoH, float _a)
{
	float a2 = _a * _a;
	float f = (_NoH * a2 - _NoH) * _NoH + 1.0;
	return a2 / (ENV_PI * f * f + 1e-7);
}

#endif // __ENVCOMMON_SH__
