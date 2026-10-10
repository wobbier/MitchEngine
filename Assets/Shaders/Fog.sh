#ifndef __FOG_SH__
#define __FOG_SH__
// Exponential height fog: density(y) = density * exp(-falloff * (y - base height)), extinction per
// metre. Shared by the fog passes (Post/FogScatter, Post/FogBlur, Post/Fog).

uniform vec4 u_fogParams;       // x density at the base height, y base height, z height falloff, w start distance
uniform vec4 u_fogColor;        // rgb ambient in-scattering (linear radiance), w max opacity
uniform vec4 u_fogVolume;       // x direct light intensity, y anisotropy, z raymarch distance, w steps (0 = analytic only)
uniform vec4 u_fogProjection;   // x P[2][2], y P[3][2], z 1 = orthographic, w 1 = depth range -1..1
uniform vec4 u_fogProjection2;  // x P[0][0], y P[1][1], z far distance (sky), w unused
uniform vec4 u_fogScreen;       // xy full-resolution size, zw half-resolution (raymarch) size
uniform mat4 u_fogInvView;      // view -> world
uniform vec4 u_fogForward;      // x 1 = forward-shaded surfaces drawn after the fog pass apply it themselves

// Henyey-Greenstein phase (normalised over the sphere); _cosTheta between the light's travel
// direction and the scattered direction, _g > 0 scatters forwards.
float henyeyGreenstein(float _cosTheta, float _g)
{
	float g2 = _g * _g;
	return (1.0 - g2) / (4.0 * 3.14159265359 * pow(max(1.0 + g2 - 2.0 * _g * _cosTheta, 1e-4), 1.5));
}

float fogDensity(float _y)
{
	return u_fogParams.x * exp(min(-u_fogParams.z * (_y - u_fogParams.y), 8.0));
}

// Optical depth of the fog from _origin along _dir (unit) for _distance metres (closed form).
float fogOpticalDepth(vec3 _origin, vec3 _dir, float _distance)
{
	float falloff = max(u_fogParams.z, 1e-4);
	float k = falloff * _dir.y;
	float integral = _distance;
	if (abs(k * _distance) > 1e-4)
	{
		integral = (1.0 - exp(min(-k * _distance, 30.0))) / k;
	}
	return fogDensity(_origin.y) * integral;
}

// View depth (distance along the camera's forward axis) of a depth-buffer value.
float fogViewDepth(float _rawDepth)
{
	if (_rawDepth >= 0.99999)
	{
		return u_fogProjection2.z;   // sky: as far as the camera sees
	}
	float ndc = u_fogProjection.w > 0.5 ? _rawDepth * 2.0 - 1.0 : _rawDepth;
	if (u_fogProjection.z > 0.5)
	{
		return (ndc - u_fogProjection.y) / u_fogProjection.x;
	}
	return u_fogProjection.y / (ndc - u_fogProjection.x);
}

// The ray from the camera through _uv (top-left origin) to the surface at view depth _viewZ.
// Returns the world-space origin; _dir, _distance and the view depth gained per metre come out.
vec3 fogRay(vec2 _uv, float _viewZ, out vec3 _dir, out float _distance, out float _viewZPerMetre, out float _originViewZ)
{
	vec2 ndc = vec2(_uv.x * 2.0 - 1.0, 1.0 - _uv.y * 2.0);
	vec2 scale = vec2(1.0 / u_fogProjection2.x, 1.0 / u_fogProjection2.y);
	vec3 originView = vec3_splat(0.0);
	vec3 pointView = vec3(ndc * scale * _viewZ, _viewZ);
	if (u_fogProjection.z > 0.5)
	{
		originView = vec3(ndc * scale, 0.0);
		pointView = vec3(ndc * scale, _viewZ);
	}
	vec3 origin = mul(u_fogInvView, vec4(originView, 1.0)).xyz;
	vec3 target = mul(u_fogInvView, vec4(pointView, 1.0)).xyz;
	vec3 ray = target - origin;
	_distance = max(length(ray), 1e-4);
	_dir = ray / _distance;
	_viewZPerMetre = (pointView.z - originView.z) / _distance;
	_originViewZ = originView.z;
	return origin;
}

// Fog between _start and _end metres along the ray, lit by the ambient colour and the unshadowed
// sun (_sunRadiance * phase): rgb in-scattered light, a transmittance.
vec4 fogAnalytic(vec3 _origin, vec3 _dir, float _start, float _end, vec3 _sunRadiance)
{
	float start = max(_start, u_fogParams.w);
	if (_end <= start)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	float transmittance = exp(-fogOpticalDepth(_origin + _dir * start, _dir, _end - start));
	return vec4((u_fogColor.rgb + _sunRadiance) * (1.0 - transmittance), transmittance);
}

// Caps the fog's opacity at u_fogColor.w, scaling the in-scattering with it.
vec4 fogClampOpacity(vec4 _fog)
{
	float opacity = 1.0 - _fog.a;
	float capped = min(opacity, u_fogColor.w);
	return vec4(_fog.rgb * (capped / max(opacity, 1e-5)), 1.0 - capped);
}

// Fog between the camera and a forward-shaded point drawn after the fog pass (transparent meshes,
// particles), analytic: rgb in-scattered light, a transmittance. _sunColor / _sunTravel are the
// first directional light's radiance and travel direction (zero colour for none).
vec4 fogForward(vec3 _cameraPos, vec3 _worldPos, vec3 _sunColor, vec3 _sunTravel)
{
	if (u_fogForward.x < 0.5)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	vec3 ray = _worldPos - _cameraPos;
	float distance = max(length(ray), 1e-4);
	vec3 dir = ray / distance;
	vec3 sunRadiance = _sunColor * henyeyGreenstein(dot(dir, -_sunTravel), u_fogVolume.y) * u_fogVolume.x;
	return fogClampOpacity(fogAnalytic(_cameraPos, dir, 0.0, distance, sunRadiance));
}

#endif // __FOG_SH__
