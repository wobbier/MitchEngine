#ifndef __CLOUDS_SH__
#define __CLOUDS_SH__
// Volumetric clouds in a shell above the planet, after Schneider 2015 ("The Real-time Volumetric
// Cloudscapes of Horizon Zero Dawn") with Hillaire's 2016 energy-conserving integration and
// Wrenninge's multiple-scattering octaves: a weather map places the clouds, tiling Perlin-Worley and
// Worley volumes (DynamicSky generates them) carve their shapes, and every sample is lit by the sun
// and the moon through the cloud above it plus the sky's ambient light. Needs Atmosphere.sh.

uniform vec4 u_cloudLayer;  // x base altitude (km), y top altitude (km), z coverage 0..1, w extinction per km at full density
uniform vec4 u_cloudWind;   // xy horizontal distance the wind has carried the clouds (km), z shape evolution (km), w ambient strength
uniform vec4 u_cloudMarch;  // x view samples, y light samples, z longest march (km), w fade distance (km)

SAMPLER3D(s_cloudShape, 4);
SAMPLER3D(s_cloudDetail, 5);
SAMPLER2D(s_cloudWeather, 6);

#define CLOUD_WEATHER_TILE 60.0   // km covered by the weather map
#define CLOUD_SHAPE_TILE 9.0      // km covered by the shape volume
#define CLOUD_DETAIL_TILE 0.65    // km covered by the detail volume

float cloudRemap(float _value, float _low, float _high)
{
	return (_value - _low) / max(_high - _low, 1e-4);
}

// Fraction of the way from the cloud base (0) to the top (1).
float cloudHeight(vec3 _pos)
{
	return (length(_pos) - SKY_BOTTOM_RADIUS - u_cloudLayer.x) / (u_cloudLayer.y - u_cloudLayer.x);
}

// Vertical profile by cloud type (0 low stratus .. 1 towering cumulus): flat bases, rounded tops.
float cloudProfile(float _height, float _type)
{
	float top = mix(0.3, 1.0, _type);
	return smoothstep(0.0, 0.07, _height) * (1.0 - smoothstep(top * 0.45, top, _height));
}

// Density (0..1) at _pos (km from the planet centre) and layer height _height.
float cloudDensity(vec3 _pos, float _height, bool _detail)
{
	vec2 xz = _pos.xz + u_cloudWind.xy;
	vec2 weather = texture2DLod(s_cloudWeather, xz / CLOUD_WEATHER_TILE, 0.0).rg;
	// Local coverage: the weather pattern pushed up or down by the coverage setting.
	float coverage = saturate(weather.r + u_cloudLayer.z * 2.0 - 1.0);
	float profile = cloudProfile(_height, weather.g);
	if (coverage <= 0.0 || profile <= 0.0)
	{
		return 0.0;
	}
	float altitude = _height * (u_cloudLayer.y - u_cloudLayer.x);
	vec3 shapePos = vec3(xz.x, altitude + u_cloudWind.z, xz.y) / CLOUD_SHAPE_TILE;
	float shape = skyTexture3DLod(s_cloudShape, shapePos, 0.0).r;
	float density = saturate(cloudRemap(shape * profile, 1.0 - coverage, 1.0));
	if (_detail && density > 0.0)
	{
		vec3 detailPos = vec3(xz.x, altitude + u_cloudWind.z * 3.0, xz.y) / CLOUD_DETAIL_TILE;
		float detail = skyTexture3DLod(s_cloudDetail, detailPos, 0.0).r;
		// Wispy towards the base, billowing towards the top.
		float erosion = mix(detail, 1.0 - detail, saturate(_height * 4.0));
		density = saturate(cloudRemap(density, erosion * 0.5, 1.0));
	}
	return density;
}

float cloudHenyeyGreenstein(float _cosTheta, float _g)
{
	float g2 = _g * _g;
	return (1.0 - g2) / (4.0 * SKY_PI * pow(max(1.0 + g2 - 2.0 * _g * _cosTheta, 1e-4), 1.5));
}

// Light scattered towards the eye after crossing optical depth _depth of cloud to reach the sample:
// Beer-Lambert plus octaves of approximate multiple scattering, each reaching deeper, less strongly
// and less directionally (Wrenninge 2013). Two phase lobes: the forward silver lining and some
// back-scattering. Multiple scattering needs some cloud to build up in, so it fades out towards
// the sunlit surface (Schneider's "powder"): the crevices and edges darken and the clouds look
// lumpy rather than flat.
float cloudLightScattering(float _depth, float _cosTheta)
{
	float scattering = 0.0;
	float attenuation = 1.0;
	float contribution = 1.0;
	float eccentricity = 1.0;
	float powder = 1.0 - 0.75 * exp(-_depth * 1.5);
	for (int i = 0; i < 4; ++i)
	{
		float phase = mix(cloudHenyeyGreenstein(_cosTheta, -0.3 * eccentricity), cloudHenyeyGreenstein(_cosTheta, 0.8 * eccentricity), 0.6);
		scattering += contribution * phase * exp(-_depth * attenuation) * (i == 0 ? 1.0 : powder);
		attenuation *= 0.45;
		contribution *= 0.7;
		eccentricity *= 0.5;
	}
	return scattering * u_skyScattering.y;
}

// Optical depth of cloud between _pos and the light (samples spaced further apart as they go).
float cloudLightDepth(vec3 _pos, vec3 _lightDir)
{
	float stepLength = (u_cloudLayer.y - u_cloudLayer.x) * 0.04;
	float depth = 0.0;
	vec3 pos = _pos;
	for (int i = 0; i < 8; ++i)
	{
		if (float(i) >= u_cloudMarch.y)
		{
			break;
		}
		pos += _lightDir * stepLength;
		float height = cloudHeight(pos);
		if (height < 0.0 || height > 1.0)
		{
			break;
		}
		depth += cloudDensity(pos, height, i < 2) * stepLength;
		stepLength *= 1.8;
	}
	return depth * u_cloudLayer.w;
}

// Where a ray crosses the cloud layer; false when it misses it or the ground is in the way.
bool cloudSegment(vec3 _origin, vec3 _dir, out float _start, out float _end)
{
	float baseRadius = SKY_BOTTOM_RADIUS + u_cloudLayer.x;
	float topRadius = SKY_BOTTOM_RADIUS + u_cloudLayer.y;
	float r = length(_origin);
	float tBase = skyRaySphere(_origin, _dir, baseRadius);
	float tTop = skyRaySphere(_origin, _dir, topRadius);
	_start = 0.0;
	_end = 0.0;
	if (r < baseRadius)
	{
		if (skyRaySphere(_origin, _dir, SKY_BOTTOM_RADIUS) >= 0.0)
		{
			return false;
		}
		_start = tBase;
		_end = tTop;
	}
	else if (r > topRadius)
	{
		if (tTop < 0.0)
		{
			return false;
		}
		_start = tTop;
		if (tBase >= 0.0)
		{
			_end = tBase;
		}
		else
		{
			// Through the top of the shell and out again.
			float b = dot(_origin, _dir);
			_end = -b + sqrt(max(b * b - dot(_origin, _origin) + topRadius * topRadius, 0.0));
		}
	}
	else
	{
		_end = tBase >= 0.0 ? tBase : tTop;
	}
	return _end > _start;
}

// Clouds along a ray: rgb light scattered towards the camera (and the haze in front of far clouds),
// a transmittance, so the sky composites as sky * a + rgb. _jitter (0..1) offsets the samples so
// successive frames integrate different points.
vec4 cloudMarch(vec3 _origin, vec3 _dir, float _jitter)
{
	float start;
	float end;
	if (u_cloudLayer.z <= 0.0 || !cloudSegment(_origin, _dir, start, end))
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	end = min(end, start + u_cloudMarch.z);

	vec3 sunDir = u_skySun.xyz;
	vec3 moonDir = u_skyMoon.xyz;
	bool moonLit = dot(u_skyMoonColor.rgb, vec3_splat(1.0)) > 0.0;
	float cosSun = dot(_dir, sunDir);
	float cosMoon = dot(_dir, moonDir);

	// Ambient: the sky above (zenith and around the horizon) and light bounced off the ground below.
	vec3 skyAbove = skyViewLuminance(vec3(0.0, 1.0, 0.0)) * 0.5
		+ (skyViewLuminance(normalize(vec3(sunDir.x, 0.3, sunDir.z))) + skyViewLuminance(normalize(vec3(-sunDir.x, 0.3, -sunDir.z)))) * 0.25;
	vec3 groundIrradiance = u_skySunColor.rgb * skyTransmittance(SKY_BOTTOM_RADIUS + 0.01, sunDir.y) * saturate(sunDir.y) + skyAbove * SKY_PI;
	if (moonLit)
	{
		groundIrradiance += u_skyMoonColor.rgb * skyTransmittance(SKY_BOTTOM_RADIUS + 0.01, moonDir.y) * saturate(moonDir.y);
	}
	vec3 groundBelow = groundIrradiance * u_skyAtmosphere.z / SKY_PI;
	skyAbove *= u_cloudWind.w;
	groundBelow *= u_cloudWind.w;

	float steps = u_cloudMarch.x;
	float dt = (end - start) / steps;
	float t = start + dt * _jitter;
	vec3 luminance = vec3_splat(0.0);
	float transmittance = 1.0;
	float distanceSum = 0.0;
	float distanceWeight = 0.0;
	for (int i = 0; i < 128; ++i)
	{
		if (float(i) >= steps || transmittance < 0.01)
		{
			break;
		}
		vec3 pos = _origin + _dir * t;
		float height = cloudHeight(pos);
		float density = cloudDensity(pos, height, true);
		if (density > 0.0)
		{
			float extinction = density * u_cloudLayer.w;
			vec3 light = u_skySunColor.rgb * skyLightTransmittance(pos, sunDir, u_skySun.w)
				* cloudLightScattering(cloudLightDepth(pos, sunDir), cosSun);
			if (moonLit)
			{
				light += u_skyMoonColor.rgb * skyLightTransmittance(pos, moonDir, u_skyMoon.w)
					* cloudLightScattering(cloudLightDepth(pos, moonDir), cosMoon);
			}
			// The cloud above hides part of the sky from its lower reaches.
			vec3 source = light + skyAbove * mix(0.3, 1.0, saturate(height)) + groundBelow * saturate(1.0 - height);
			float stepTransmittance = exp(-extinction * dt);
			float absorbed = transmittance * (1.0 - stepTransmittance);
			luminance += source * absorbed;
			distanceSum += t * absorbed;
			distanceWeight += absorbed;
			transmittance *= stepTransmittance;
		}
		t += dt;
	}

	// The march stops once almost nothing gets through: that cloud is opaque, and the light its last
	// sliver would have scattered is like what came before. Otherwise the 1% left over lets the moon
	// and stars, far brighter than moonlit cloud, show through the thickest cloud.
	if (transmittance < 0.01)
	{
		luminance /= 1.0 - transmittance;
		transmittance = 0.0;
	}

	// Far clouds dissolve into the haze in front of them: the sky's light, added where the cloud hides
	// the sky. The cloud stays as opaque as it is, so what lies beyond the atmosphere (the sun, the
	// moon and the stars) doesn't show through it.
	float distance = distanceWeight > 0.0 ? distanceSum / distanceWeight : end;
	float fade = exp(-distance / u_cloudMarch.w);
	vec3 haze = (skyViewLuminance(_dir) + skyNightGlow(_dir)) * ((1.0 - fade) * (1.0 - transmittance));
	return vec4(luminance * fade + haze, transmittance);
}

#endif // __CLOUDS_SH__
