$input v_texcoord0

// The sky seen from the camera's altitude in every direction (see skyViewUv): single scattering of
// the sun and the moon with the multiple-scattering table, the lit ground below the horizon.
#include "../Common.sh"
#include "Atmosphere.sh"

// Light scattered towards the camera at _pos from one light (direction _lightDir, illuminance _color).
vec3 skyInScattering(vec3 _pos, vec3 _up, float _r, vec3 _viewDir, vec3 _lightDir, float _radius, vec3 _color, vec3 _rayleigh, vec3 _mie)
{
	float cosTheta = dot(_viewDir, _lightDir);
	float muLight = dot(_up, _lightDir);
	vec3 direct = skyTransmittance(_r, muLight) * skyPlanetShadow(_r, muLight, _radius);
	vec3 single = direct * (_rayleigh * skyRayleighPhase(cosTheta) + _mie * skyMiePhase(cosTheta));
	return _color * (single + skyMultiScattering(_r, muLight) * (_rayleigh + _mie));
}

void main()
{
	float cameraRadius = skyCameraRadius();
	vec3 origin = vec3(0.0, cameraRadius, 0.0);
	vec3 dir = skyViewDirection(v_texcoord0, cameraRadius);
	vec3 sunDir = u_skySun.xyz;
	vec3 moonDir = u_skyMoon.xyz;
	bool moonLit = dot(u_skyMoonColor.rgb, vec3_splat(1.0)) > 0.0;

	float tGround = skyRaySphere(origin, dir, SKY_BOTTOM_RADIUS);
	float tMax = tGround > 0.0 ? tGround : skyRaySphere(origin, dir, SKY_TOP_RADIUS);
	tMax = max(tMax, 0.0);

	// Samples bunch up near the camera, where the air is densest (Hillaire's quadratic spacing).
	const int steps = 32;
	vec3 luminance = vec3_splat(0.0);
	vec3 throughput = vec3_splat(1.0);
	for (int i = 0; i < steps; ++i)
	{
		float t0 = float(i) / float(steps);
		float t1 = float(i + 1) / float(steps);
		t0 = tMax * t0 * t0;
		t1 = tMax * t1 * t1;
		float dt = t1 - t0;
		vec3 pos = origin + dir * (t0 + dt * 0.3);
		float r = length(pos);
		vec3 up = pos / r;
		vec3 rayleigh;
		vec3 mie;
		vec3 extinction;
		skyMedium(r - SKY_BOTTOM_RADIUS, rayleigh, mie, extinction);
		extinction = max(extinction, vec3_splat(1e-7));
		float sunlight = skySolarVisibility(pos + vec3(u_skyCamera.x, 0.0, u_skyCamera.z));
		vec3 source = skyInScattering(pos, up, r, dir, sunDir, u_skySun.w, u_skySunColor.rgb * sunlight, rayleigh, mie);
		if (moonLit)
		{
			source += skyInScattering(pos, up, r, dir, moonDir, u_skyMoon.w, u_skyMoonColor.rgb, rayleigh, mie);
		}
		vec3 stepTransmittance = exp(-extinction * dt);
		luminance += throughput * (source - source * stepTransmittance) / extinction;
		throughput *= stepTransmittance;
	}
	// The brightness scale is for the air's light only, not the ground's.
	luminance *= u_skyScattering.x;
	if (tGround > 0.0)
	{
		// The planet's surface, lit directly and by the sky (roughly a quarter of the sun's share).
		vec3 pos = origin + dir * tGround;
		vec3 normal = normalize(pos);
		vec3 lit = u_skySunColor.rgb * skySolarVisibility(pos + vec3(u_skyCamera.x, 0.0, u_skyCamera.z)) * skyLightTransmittance(pos, sunDir, u_skySun.w) * (saturate(dot(normal, sunDir)) + 0.08);
		if (moonLit)
		{
			lit += u_skyMoonColor.rgb * skyLightTransmittance(pos, moonDir, u_skyMoon.w) * (saturate(dot(normal, moonDir)) + 0.08);
		}
		luminance += throughput * lit * u_skyAtmosphere.z / SKY_PI;
	}
	gl_FragColor = vec4(luminance, 1.0);
}
