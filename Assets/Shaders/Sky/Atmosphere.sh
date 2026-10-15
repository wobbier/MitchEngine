#ifndef __ATMOSPHERE_SH__
#define __ATMOSPHERE_SH__
#include "../SolarEclipse.sh"
// Physically based atmosphere after Hillaire 2020 ("A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique"): Rayleigh and Mie scattering plus ozone absorption over a
// spherical planet, with lookup tables for the transmittance, the multiple scattering and the sky as
// seen from the camera. Distances are kilometres; the planet centre is the origin and +Y is up at
// the camera. Light values are the engine's linear HDR units (DynamicSky sets the sun and moon
// illuminance outside the atmosphere).

#define SKY_PI 3.14159265359
#define SKY_BOTTOM_RADIUS 6360.0
#define SKY_TOP_RADIUS 6460.0
#define SKY_TRANSMITTANCE_WIDTH 256.0
#define SKY_TRANSMITTANCE_HEIGHT 64.0
#define SKY_MULTISCATTER_SIZE 32.0
#define SKY_VIEW_WIDTH 256.0
#define SKY_VIEW_HEIGHT 128.0

#if BGFX_SHADER_LANGUAGE_GLSL >= 130
#	define skyTexture3DLod(_sampler, _coord, _lod) textureLod(_sampler, _coord, _lod)
#else
#	define skyTexture3DLod(_sampler, _coord, _lod) texture3DLod(_sampler, _coord, _lod)
#endif

uniform vec4 u_skySun;          // xyz unit direction towards the sun, w disc angular radius (radians)
uniform vec4 u_skySunColor;     // rgb illuminance outside the atmosphere, w disc luminance per unit illuminance
uniform vec4 u_skyMoon;         // xyz unit direction towards the moon, w disc angular radius (radians)
uniform vec4 u_skyMoonColor;    // rgb moonlight illuminance outside the atmosphere, w disc luminance per unit illuminance
uniform vec4 u_skyAtmosphere;   // x aerosol density scale, y camera altitude (km), z ground albedo, w night sky luminance
uniform vec4 u_skyCamera;       // xyz camera position (km: world x, altitude, world z), w 1 = clouds enabled
uniform vec4 u_skyScattering;   // x sky brightness, y cloud brightness, z solar visibility at camera

SAMPLER2D(s_skyTransmittance, 0);
SAMPLER2D(s_skyMultiScatter, 1);
SAMPLER2D(s_skyView, 2);

// Atmospheric positions are km from the planet centre; the eclipse uses world metres.
float skySolarVisibility(vec3 _planetPosition)
{
    return solarVisibility((_planetPosition - vec3(0.0, SKY_BOTTOM_RADIUS, 0.0)) * 1000.0);
}

// Scattering and extinction (per km) at an altitude (km).
void skyMedium(float _altitude, out vec3 _rayleigh, out vec3 _mie, out vec3 _extinction)
{
	float h = max(_altitude, 0.0);
	float mieDensity = exp(-h / 1.2) * u_skyAtmosphere.x;
	float ozoneDensity = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
	_rayleigh = vec3(5.802e-3, 13.558e-3, 33.1e-3) * exp(-h / 8.0);
	_mie = vec3_splat(3.996e-3 * mieDensity);
	_extinction = _rayleigh + vec3_splat(4.44e-3 * mieDensity) + vec3(0.650e-3, 1.881e-3, 0.085e-3) * ozoneDensity;
}

float skyRayleighPhase(float _cosTheta)
{
	return 3.0 / (16.0 * SKY_PI) * (1.0 + _cosTheta * _cosTheta);
}

// Cornette-Shanks: a Henyey-Greenstein lobe with a Rayleigh-like tail, for the aerosols.
float skyMiePhase(float _cosTheta)
{
	const float g = 0.8;
	float k = 3.0 / (8.0 * SKY_PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + _cosTheta * _cosTheta) / pow(max(1.0 + g * g - 2.0 * g * _cosTheta, 1e-4), 1.5);
}

// Nearest non-negative distance along a unit ray to a sphere around the planet centre, -1 if none.
float skyRaySphere(vec3 _origin, vec3 _dir, float _radius)
{
	float b = dot(_origin, _dir);
	float c = dot(_origin, _origin) - _radius * _radius;
	float discriminant = b * b - c;
	if (discriminant < 0.0)
	{
		return -1.0;
	}
	float s = sqrt(discriminant);
	if (-b - s >= 0.0)
	{
		return -b - s;
	}
	return -b + s >= 0.0 ? -b + s : -1.0;
}

// Fraction of a light disc of angular radius _radius at cosine _mu from the zenith (seen from
// radius _r) that the planet doesn't hide: a soft terminator rather than a hard shadow line.
float skyPlanetShadow(float _r, float _mu, float _radius)
{
	float sinHorizon = SKY_BOTTOM_RADIUS / _r;
	float cosHorizon = -sqrt(max(1.0 - sinHorizon * sinHorizon, 0.0));
	float radius = max(_radius, 1e-4);
	return smoothstep(-radius, radius, _mu - cosHorizon);
}

// Transmittance table (Bruneton 2017 parametrisation): u the distance to the top of the atmosphere
// between its minimum and the horizon's, v the altitude. Texel centres span the unit range.
vec2 skyTransmittanceUv(float _r, float _mu)
{
	float H = sqrt(SKY_TOP_RADIUS * SKY_TOP_RADIUS - SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS);
	float rho = sqrt(max(_r * _r - SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS, 0.0));
	float discriminant = _r * _r * (_mu * _mu - 1.0) + SKY_TOP_RADIUS * SKY_TOP_RADIUS;
	float d = max(0.0, -_r * _mu + sqrt(max(discriminant, 0.0)));
	float dMin = SKY_TOP_RADIUS - _r;
	float dMax = rho + H;
	vec2 unit = vec2((d - dMin) / max(dMax - dMin, 1e-4), rho / H);
	vec2 size = vec2(SKY_TRANSMITTANCE_WIDTH, SKY_TRANSMITTANCE_HEIGHT);
	return saturate(unit) * (1.0 - 1.0 / size) + 0.5 / size;
}

void skyTransmittanceRMu(vec2 _uv, out float _r, out float _mu)
{
	vec2 size = vec2(SKY_TRANSMITTANCE_WIDTH, SKY_TRANSMITTANCE_HEIGHT);
	vec2 unit = saturate((_uv - 0.5 / size) / (1.0 - 1.0 / size));
	float H = sqrt(SKY_TOP_RADIUS * SKY_TOP_RADIUS - SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS);
	float rho = H * unit.y;
	_r = sqrt(rho * rho + SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS);
	float dMin = SKY_TOP_RADIUS - _r;
	float dMax = rho + H;
	float d = dMin + unit.x * (dMax - dMin);
	_mu = d < 1e-4 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * _r * d), -1.0, 1.0);
}

vec3 skyTransmittance(float _r, float _mu)
{
	return texture2DLod(s_skyTransmittance, skyTransmittanceUv(_r, _mu), 0.0).rgb;
}

// Light reaching _pos from a disc of angular radius _radius in direction _dir (atmosphere and planet).
vec3 skyLightTransmittance(vec3 _pos, vec3 _dir, float _radius)
{
	float r = length(_pos);
	float mu = dot(_pos, _dir) / r;
	return skyTransmittance(r, mu) * skyPlanetShadow(r, mu, _radius);
}

// Light scattered more than once, per unit scattering coefficient and unit illuminance (Hillaire's
// isotropic approximation); u the light's zenith cosine, v the altitude.
vec3 skyMultiScattering(float _r, float _muLight)
{
	vec2 unit = saturate(vec2(_muLight * 0.5 + 0.5, (_r - SKY_BOTTOM_RADIUS) / (SKY_TOP_RADIUS - SKY_BOTTOM_RADIUS)));
	return texture2DLod(s_skyMultiScatter, unit * (1.0 - 1.0 / SKY_MULTISCATTER_SIZE) + 0.5 / SKY_MULTISCATTER_SIZE, 0.0).rgb;
}

float skyCameraRadius()
{
	return SKY_BOTTOM_RADIUS + u_skyAtmosphere.y;
}

// Sky-view table: u the world azimuth (wrapping), v the zenith angle, squeezed towards the horizon
// (seen from radius _r) at v = 0.5 so the horizon stays sharp; above it the sky, below the ground.
vec2 skyViewUv(vec3 _dir, float _r)
{
	float beta = acos(clamp(sqrt(max(_r * _r - SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS, 0.0)) / _r, 0.0, 1.0));
	float zenithHorizonAngle = SKY_PI - beta;
	float viewZenithAngle = acos(clamp(_dir.y, -1.0, 1.0));
	float v;
	if (viewZenithAngle < zenithHorizonAngle)
	{
		v = (1.0 - sqrt(max(1.0 - viewZenithAngle / zenithHorizonAngle, 0.0))) * 0.5;
	}
	else
	{
		v = sqrt(saturate((viewZenithAngle - zenithHorizonAngle) / max(beta, 1e-4))) * 0.5 + 0.5;
	}
	float u = atan2(_dir.z, _dir.x) / (2.0 * SKY_PI) + 0.5;
	return vec2(u, v * (1.0 - 1.0 / SKY_VIEW_HEIGHT) + 0.5 / SKY_VIEW_HEIGHT);
}

vec3 skyViewDirection(vec2 _uv, float _r)
{
	float v = saturate((_uv.y - 0.5 / SKY_VIEW_HEIGHT) / (1.0 - 1.0 / SKY_VIEW_HEIGHT));
	float beta = acos(clamp(sqrt(max(_r * _r - SKY_BOTTOM_RADIUS * SKY_BOTTOM_RADIUS, 0.0)) / _r, 0.0, 1.0));
	float zenithHorizonAngle = SKY_PI - beta;
	float viewZenithAngle;
	if (v < 0.5)
	{
		float coord = 1.0 - 2.0 * v;
		viewZenithAngle = zenithHorizonAngle * (1.0 - coord * coord);
	}
	else
	{
		float coord = v * 2.0 - 1.0;
		viewZenithAngle = zenithHorizonAngle + beta * coord * coord;
	}
	float azimuth = (_uv.x - 0.5) * 2.0 * SKY_PI;
	float s = sin(viewZenithAngle);
	return vec3(s * cos(azimuth), cos(viewZenithAngle), s * sin(azimuth));
}

// The sky (sun and moon light scattered by the atmosphere, the ground below the horizon) seen from
// the camera in direction _dir.
vec3 skyViewLuminance(vec3 _dir)
{
	return texture2DLod(s_skyView, skyViewUv(_dir, skyCameraRadius()), 0.0).rgb;
}

// Faint natural light of the night sky (airglow and starlight), brighter towards the horizon.
vec3 skyNightGlow(vec3 _dir)
{
	float horizon = 1.0 - saturate(_dir.y);
	return u_skyAtmosphere.w * vec3(0.32, 0.42, 0.75) * (1.0 + 1.5 * horizon * horizon) * smoothstep(-0.05, 0.02, _dir.y);
}

#endif // __ATMOSPHERE_SH__
