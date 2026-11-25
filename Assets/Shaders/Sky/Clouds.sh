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
uniform vec4 u_cloudConvection; // x convection 0..1 (heaped tops, cumulonimbus towers), y wind angle (radians from +Z towards +X), z clearing 0..1 round the viewer, w clearing radius (km)

SAMPLER3D(s_cloudShape, 4);
SAMPLER3D(s_cloudDetail, 5);
SAMPLER2D(s_cloudWeather, 6);

#define CLOUD_WEATHER_TILE 60.0   // km covered by the weather map
#define CLOUD_SHAPE_TILE 9.0      // km covered by the shape volume
#define CLOUD_DETAIL_TILE 0.65    // km covered by the detail volume
// A convective sky: the layer's shapes come smaller, and every heap has its own top.
#define CLOUD_CONVECTIVE_SHAPE_TILE 3.2
#define CLOUD_HEAP_TILE 2.6
// Cumulonimbus: at most one in each cell of a grid (km), far from the viewer, its anvil inside its cell.
#define CLOUD_TOWER_CELL 36.0
#define CLOUD_TOWER_NEAREST 14.0  // km: none closer to the viewer than this (smaller ones over the next 10 km)
#define CLOUD_TOWER_TILE 5.0      // km covered by the shape volume on a tower
#define CLOUD_TOWER_REACH 220.0   // km marched through towers at most
#define CLOUD_TOWER_STEPS 14
#define CLOUD_TOWER_CELLS 14

float cloudRemap(float _value, float _low, float _high)
{
	return (_value - _low) / max(_high - _low, 1e-4);
}

// Three values in 0..1 from a small-integer cell (Dave Hoskins' hash without sines).
vec3 cloudHash(vec3 _p)
{
	_p = fract(_p * vec3(0.1031, 0.1030, 0.0973));
	_p += dot(_p, _p.yxz + 33.33);
	return fract((_p.xxy + _p.yxx) * _p.zyx);
}

// How much of the cover the clearing round the viewer takes away at a point (km, planet-centred).
float cloudClearing(vec3 _pos)
{
	if (u_cloudConvection.z <= 0.0)
	{
		return 0.0;
	}
	float d = length(_pos.xz - u_skyCamera.xz) / max(u_cloudConvection.w, 1e-3);
	return u_cloudConvection.z * (1.0 - smoothstep(0.35, 1.6, d));
}

// Fraction of the way from the cloud base (0) to the top (1).
float cloudHeight(vec3 _pos)
{
	return (length(_pos) - SKY_BOTTOM_RADIUS - u_cloudLayer.x) / (u_cloudLayer.y - u_cloudLayer.x);
}

// Vertical profile by cloud type (0 low stratus .. 1 towering cumulus): flat bases, rounded tops
// (sharper ones, heaped, in a convective sky).
float cloudProfile(float _height, float _type)
{
	float top = mix(0.3, 1.0, _type);
	float shoulder = mix(0.45, 0.72, u_cloudConvection.x);
	return smoothstep(0.0, 0.07, _height) * (1.0 - smoothstep(top * shoulder, top, _height));
}

// Density (0..1) at _pos (km from the planet centre) and layer height _height.
float cloudDensity(vec3 _pos, float _height, bool _detail)
{
	vec2 xz = _pos.xz + u_cloudWind.xy;
	vec2 weather = texture2DLod(s_cloudWeather, xz / CLOUD_WEATHER_TILE, 0.0).rg;
	// Local coverage: the weather pattern pushed up or down by the coverage setting.
	float coverage = saturate(weather.r + u_cloudLayer.z * 2.0 - 1.0 - cloudClearing(_pos));
	float convection = u_cloudConvection.x;
	float type = weather.g;
	if (convection > 0.0)
	{
		// Heaps a kilometre or two across, each with its own top: mounds beside tall domes.
		float heap = texture2DLod(s_cloudWeather, xz / CLOUD_HEAP_TILE + vec2(0.31, 0.67), 0.0).g;
		type = mix(type, saturate(heap * 1.3 - 0.1), convection);
	}
	float profile = cloudProfile(_height, type);
	if (coverage <= 0.0 || profile <= 0.0)
	{
		return 0.0;
	}
	float altitude = _height * (u_cloudLayer.y - u_cloudLayer.x);
	vec3 shapePos = vec3(xz.x, altitude + u_cloudWind.z, xz.y) / mix(CLOUD_SHAPE_TILE, CLOUD_CONVECTIVE_SHAPE_TILE, convection);
	float shape = skyTexture3DLod(s_cloudShape, shapePos, 0.0).r;
	float density = saturate(cloudRemap(shape * profile, 1.0 - coverage, 1.0));
	if (_detail && density > 0.0)
	{
		vec3 detailPos = vec3(xz.x, altitude + u_cloudWind.z * 3.0, xz.y) / mix(CLOUD_DETAIL_TILE, CLOUD_DETAIL_TILE * 0.55, convection);
		float detail = skyTexture3DLod(s_cloudDetail, detailPos, 0.0).r;
		// Wispy towards the base, billowing towards the top (cauliflower heads, convective).
		float erosion = mix(detail, 1.0 - detail, saturate(_height * 4.0));
		density = saturate(cloudRemap(density, erosion * mix(0.5, 0.68, convection), 1.0));
	}
	return density;
}

// --- Cumulonimbus ------------------------------------------------------------------------------
// Towers standing up out of the layer far from the viewer: at most one in each CLOUD_TOWER_CELL
// cell, its core a few kilometres across rising to 6..11 km, most of them spreading into an anvil
// sheared downwind at the top. They don't drift with the wind (they're tens of kilometres off).

// The cell's tower: xy its centre (km), z core radius (km), w top altitude (km); z 0 when there is
// none. _anvil: x anvil radius (km), y anvil depth (km), z how strongly it leans downwind.
vec4 cloudTower(vec2 _cell, out vec3 _anvil)
{
	_anvil = vec3_splat(0.0);
	vec3 a = cloudHash(vec3(_cell + 101.0, 7.0));
	vec3 b = cloudHash(vec3(_cell + 101.0, 19.0));
	// The centre keeps 0.32 of a cell clear all round: the widest anvil's bounds stay in the cell.
	vec2 centre = (_cell + 0.32 + 0.36 * a.yz) * CLOUD_TOWER_CELL;
	float away = length(centre - u_skyCamera.xz);
	if (a.x > u_cloudConvection.x * 0.8 || away < CLOUD_TOWER_NEAREST)
	{
		return vec4(centre, 0.0, 0.0);
	}
	// The nearest are younger, smaller ones (they don't fade in: that would leave a ghost).
	float grown = mix(0.55, 1.0, smoothstep(CLOUD_TOWER_NEAREST, CLOUD_TOWER_NEAREST + 10.0, away));
	float core = (2.2 + 1.5 * b.x) * grown;
	bool mature = b.z > 0.22;
	float top = u_cloudLayer.x + ((mature ? 7.5 + 3.5 * b.y : 4.5 + 2.0 * b.y) - u_cloudLayer.x) * grown;
	_anvil = mature ? vec3(min(core * (1.5 + 0.7 * fract(b.z * 7.3)), 5.0), (1.4 + 1.0 * fract(b.y * 5.1)) * grown, 0.85) : vec3(core, 0.0, 0.0);
	return vec4(centre, core, top);
}

// A tower's density (0..1) at _pos (km, planet-centred), _altitude above the ground. Its body is
// broad from the cloud sea up, with a lower turret of towering cumulus beside it; a mature one's
// top flares into a flat anvil blown out downwind, smooth and thin toward its edge.
float cloudTowerDensity(vec3 _pos, float _altitude, vec4 _tower, vec3 _anvil, bool _detail)
{
	float base = u_cloudLayer.x;
	float top = _tower.w;
	if (_altitude <= base || _altitude >= top + 0.7)
	{
		return 0.0;
	}
	float f = (_altitude - base) / (top - base);
	vec2 downwind = vec2(sin(u_cloudConvection.y), cos(u_cloudConvection.y));
	vec2 across = vec2(downwind.y, -downwind.x);
	// Into the anvil over the top _anvil.y kilometres or so: widest at the flat top (an upturned
	// cone, its underside sloping up to a thin edge), most of it downwind of the core.
	float anvil = _anvil.y > 0.0 ? smoothstep(top - _anvil.y * 2.2, top - _anvil.y * 0.1, _altitude) : 0.0;
	vec2 local = _pos.xz - _tower.xy - downwind * ((_anvil.x - _tower.z) * _anvil.z * anvil);
	// Drawn out along the wind, narrower across it.
	local = vec2(dot(local, downwind) / (1.0 + 0.45 * anvil), dot(local, across) / (1.0 - 0.2 * anvil));
	float column = _tower.z * (0.9 + 0.2 * smoothstep(0.0, 0.25, f) - 0.12 * smoothstep(0.45, 0.9, f));
	float radius = mix(column, _anvil.x, anvil);
	float r2 = dot(local, local);
	// A turret of towering cumulus beside the core, a third to two thirds as tall.
	vec3 t = cloudHash(vec3(_tower.xy, 3.0));
	float angle = t.x * 6.2832;
	vec2 turretCentre = _tower.xy + vec2(cos(angle), sin(angle)) * _tower.z * (1.0 + 0.5 * t.y);
	float turretRadius = _tower.z * (0.5 + 0.2 * t.z);
	float turretTop = base + (top - base) * (0.35 + 0.3 * t.y);
	vec2 turretLocal = _pos.xz - turretCentre;
	float turretR2 = dot(turretLocal, turretLocal);
	// Clear of both (lumps and all): no need to look up the noise.
	if (r2 > radius * radius * 1.6 && (turretR2 > turretRadius * turretRadius * 1.7 || _altitude > turretTop + 0.6))
	{
		return 0.0;
	}
	vec3 shapePos = vec3(_pos.x, _altitude * 0.8 - u_cloudWind.z, _pos.z) / CLOUD_TOWER_TILE;
	float shape = skyTexture3DLod(s_cloudShape, shapePos, 0.0).r;
	// The core overshoots the anvil's flat top in a dome.
	float core = saturate(1.0 - r2 / (_tower.z * _tower.z * 1.3));
	float ceiling = top + (0.65 - 0.3 * anvil) * core;
	// Lumpy outline, the heads bulging out and folding in; the anvil far smoother.
	float lumps = mix(0.55, 0.18, anvil * (1.0 - core));
	float d = sqrt(r2) + (0.55 - shape) * radius * lumps;
	float softTop = mix(0.45, 0.18, anvil * (1.0 - core));
	float mask = (1.0 - smoothstep(radius * 0.62, radius, d)) * smoothstep(base, base + 0.35, _altitude) * (1.0 - smoothstep(ceiling - softTop, ceiling, _altitude));
	// The anvil thins toward its edge.
	mask *= mix(1.0, 0.35 + 0.65 * saturate((1.0 - d / radius) * 1.6), anvil * (1.0 - core));

	float turretHeight = saturate((_altitude - base) / (turretTop - base));
	float turretDome = saturate(1.0 - turretR2 / (turretRadius * turretRadius));
	float td = sqrt(turretR2) + (0.55 - shape) * turretRadius * 0.6;
	float turret = (1.0 - smoothstep(turretRadius * (0.62 + 0.2 * turretHeight), turretRadius, td)) * smoothstep(base, base + 0.35, _altitude)
		* (1.0 - smoothstep(turretTop - 0.5, turretTop + 0.5 * turretDome, _altitude));
	mask = max(mask, turret);

	float density = saturate(cloudRemap(shape, 1.0 - mask, 1.0)) * mask;
	if (_detail && density > 0.0)
	{
		vec3 detailPos = vec3(_pos.x, _altitude - u_cloudWind.z * 3.0, _pos.z) / (CLOUD_DETAIL_TILE * 2.5);
		float detail = skyTexture3DLod(s_cloudDetail, detailPos, 0.0).r;
		density = saturate(cloudRemap(density, (1.0 - detail) * mix(0.35, 0.2, anvil), 1.0));
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

// The light a cloud sample scatters towards the eye from the sun and the moon, given the optical
// depth of cloud between it and each.
vec3 cloudDirectLight(vec3 _pos, float _sunDepth, float _moonDepth, float _cosSun, float _cosMoon, bool _moonLit)
{
	vec3 light = u_skySunColor.rgb * skySolarVisibility(_pos) * skyLightTransmittance(_pos, u_skySun.xyz, u_skySun.w)
		* cloudLightScattering(_sunDepth, _cosSun);
	if (_moonLit)
	{
		light += u_skyMoonColor.rgb * skyLightTransmittance(_pos, u_skyMoon.xyz, u_skyMoon.w)
			* cloudLightScattering(_moonDepth, _cosMoon);
	}
	return light;
}

// Far clouds dissolve into the haze in front of them: the sky's light, added where the cloud hides
// the sky. The cloud stays as opaque as it is, so what lies beyond the atmosphere (the sun, the
// moon and the stars) doesn't show through it. The march stops once almost nothing gets through:
// that cloud is opaque, and the light its last sliver would have scattered is like what came
// before. Otherwise the 1% left over lets the moon and stars, far brighter than moonlit cloud,
// show through the thickest cloud.
vec4 cloudFinish(vec3 _dir, vec3 _luminance, float _transmittance, float _distance)
{
	if (_transmittance < 0.01)
	{
		_luminance /= 1.0 - _transmittance;
		_transmittance = 0.0;
	}
	float fade = exp(-_distance / u_cloudMarch.w);
	vec3 haze = (skyViewLuminance(_dir) + skyNightGlow(_dir)) * ((1.0 - fade) * (1.0 - _transmittance));
	return vec4(_luminance * fade + haze, _transmittance);
}

// The layer along a ray (see cloudMarch); _distance where along the ray it is, on average.
vec4 cloudLayerMarch(vec3 _origin, vec3 _dir, float _jitter, vec3 _skyAbove, vec3 _groundBelow, out float _distance)
{
	_distance = 1e6;
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
			vec3 light = cloudDirectLight(pos, cloudLightDepth(pos, sunDir), moonLit ? cloudLightDepth(pos, moonDir) : 0.0, cosSun, cosMoon, moonLit);
			// The cloud above hides part of the sky from its lower reaches.
			vec3 source = light + _skyAbove * mix(0.3, 1.0, saturate(height)) + _groundBelow * saturate(1.0 - height);
			float stepTransmittance = exp(-extinction * dt);
			float absorbed = transmittance * (1.0 - stepTransmittance);
			luminance += source * absorbed;
			distanceSum += t * absorbed;
			distanceWeight += absorbed;
			transmittance *= stepTransmittance;
		}
		t += dt;
	}
	_distance = distanceWeight > 0.0 ? distanceSum / distanceWeight : end;
	return cloudFinish(_dir, luminance, transmittance, _distance);
}

// Optical depth of a tower between _pos and the light (samples spaced further apart as they go).
float cloudTowerLightDepth(vec3 _pos, vec3 _lightDir, vec4 _tower, vec3 _anvil)
{
	float stepLength = 0.15;
	float depth = 0.0;
	vec3 pos = _pos;
	for (int i = 0; i < 4; ++i)
	{
		pos += _lightDir * stepLength;
		depth += cloudTowerDensity(pos, length(pos) - SKY_BOTTOM_RADIUS, _tower, _anvil, false) * stepLength;
		stepLength *= 2.4;
	}
	return depth * u_cloudLayer.w;
}

// The cumulonimbus along a ray: the grid's cells are walked in order along it (a 2D DDA), and only
// the stretch inside each tower's bounds is marched, so empty sky costs a hash per cell.
vec4 cloudTowerMarch(vec3 _origin, vec3 _dir, float _jitter, vec3 _skyAbove, vec3 _groundBelow, out float _distance)
{
	_distance = 1e6;
	vec2 dir2 = _dir.xz;
	// Straight up or down there is no tower in reach: they stand well away.
	if (u_cloudConvection.x <= 0.0 || dot(dir2, dir2) < 1e-4)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	// Out above the tallest tops, or down into the ground under the layer, they are left behind.
	float limit = CLOUD_TOWER_REACH;
	float tCeiling = skyRaySphere(_origin, _dir, SKY_BOTTOM_RADIUS + 11.8);
	if (tCeiling > 0.0)
	{
		limit = min(limit, tCeiling);
	}
	float tFloor = skyRaySphere(_origin, _dir, SKY_BOTTOM_RADIUS + u_cloudLayer.x);
	if (tFloor > 0.0 && length(_origin) > SKY_BOTTOM_RADIUS + u_cloudLayer.x)
	{
		limit = min(limit, tFloor);
	}

	vec3 sunDir = u_skySun.xyz;
	vec3 moonDir = u_skyMoon.xyz;
	bool moonLit = dot(u_skyMoonColor.rgb, vec3_splat(1.0)) > 0.0;
	float cosSun = dot(_dir, sunDir);
	float cosMoon = dot(_dir, moonDir);

	vec2 stepSign = vec2(dir2.x >= 0.0 ? 1.0 : -1.0, dir2.y >= 0.0 ? 1.0 : -1.0);
	vec2 invDir = vec2_splat(1.0) / max(abs(dir2), vec2_splat(1e-6));
	vec2 cell = floor(_origin.xz / CLOUD_TOWER_CELL);
	vec2 tDelta = invDir * CLOUD_TOWER_CELL;
	vec2 tNext = ((cell + max(stepSign, vec2_splat(0.0))) * CLOUD_TOWER_CELL - _origin.xz) * stepSign * invDir;
	float tCell = 0.0;
	float a = dot(dir2, dir2);

	vec3 luminance = vec3_splat(0.0);
	float transmittance = 1.0;
	float distanceSum = 0.0;
	float distanceWeight = 0.0;
	for (int c = 0; c < CLOUD_TOWER_CELLS; ++c)
	{
		if (tCell >= limit || transmittance < 0.01)
		{
			break;
		}
		vec3 anvil;
		vec4 tower = cloudTower(cell, anvil);
		if (tower.z > 0.0)
		{
			// Its bounds: the anvil's reach (lumps included) and its lean, round the centre.
			float reach = max(max(anvil.x * 1.45, tower.z * 1.1) * 1.25 + max(anvil.x - tower.z, 0.0) * anvil.z, tower.z * 2.4) + 0.2;
			vec2 oc = _origin.xz - tower.xy;
			float b = dot(oc, dir2);
			float discriminant = b * b - a * (dot(oc, oc) - reach * reach);
			if (discriminant > 0.0)
			{
				float s = sqrt(discriminant);
				float t0 = max((-b - s) / a, 0.0);
				float t1 = min((-b + s) / a, limit);
				float dt = (t1 - t0) / float(CLOUD_TOWER_STEPS);
				float t = t0 + dt * _jitter;
				for (int i = 0; i < CLOUD_TOWER_STEPS; ++i)
				{
					if (t1 <= t0 || transmittance < 0.01)
					{
						break;
					}
					vec3 pos = _origin + _dir * t;
					float altitude = length(pos) - SKY_BOTTOM_RADIUS;
					float density = cloudTowerDensity(pos, altitude, tower, anvil, true);
					if (density > 0.0)
					{
						float extinction = density * u_cloudLayer.w;
						vec3 light = cloudDirectLight(pos, cloudTowerLightDepth(pos, sunDir, tower, anvil), moonLit ? cloudTowerLightDepth(pos, moonDir, tower, anvil) : 0.0, cosSun, cosMoon, moonLit);
						float height = saturate((altitude - u_cloudLayer.x) / (tower.w - u_cloudLayer.x));
						// The sky lights its upper reaches, the cloud sea it stands in (brighter than
						// the ground) its foot.
						vec3 source = light + _skyAbove * mix(0.35, 1.0, height) + _groundBelow * (1.6 * saturate(1.0 - height * 1.5));
						float stepTransmittance = exp(-extinction * dt);
						float absorbed = transmittance * (1.0 - stepTransmittance);
						luminance += source * absorbed;
						distanceSum += t * absorbed;
						distanceWeight += absorbed;
						transmittance *= stepTransmittance;
					}
					t += dt;
				}
			}
		}
		if (tNext.x < tNext.y)
		{
			tCell = tNext.x;
			tNext.x += tDelta.x;
			cell.x += stepSign.x;
		}
		else
		{
			tCell = tNext.y;
			tNext.y += tDelta.y;
			cell.y += stepSign.y;
		}
	}
	if (distanceWeight <= 0.0)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	_distance = distanceSum / distanceWeight;
	return cloudFinish(_dir, luminance, transmittance, _distance);
}

// Clouds along a ray: rgb light scattered towards the camera (and the haze in front of far clouds),
// a transmittance, so the sky composites as sky * a + rgb. _jitter (0..1) offsets the samples so
// successive frames integrate different points. The layer and the towers are marched apart and
// laid one over the other, the nearer in front.
vec4 cloudMarch(vec3 _origin, vec3 _dir, float _jitter)
{
	vec3 sunDir = u_skySun.xyz;
	vec3 moonDir = u_skyMoon.xyz;
	bool moonLit = dot(u_skyMoonColor.rgb, vec3_splat(1.0)) > 0.0;

	// Ambient: the sky above (zenith and around the horizon) and light bounced off the ground below.
	vec3 skyAbove = skyViewLuminance(vec3(0.0, 1.0, 0.0)) * 0.5
		+ (skyViewLuminance(normalize(vec3(sunDir.x, 0.3, sunDir.z))) + skyViewLuminance(normalize(vec3(-sunDir.x, 0.3, -sunDir.z)))) * 0.25;
	vec3 groundIrradiance = u_skySunColor.rgb * skySolarVisibility(_origin) * skyTransmittance(SKY_BOTTOM_RADIUS + 0.01, sunDir.y) * saturate(sunDir.y) + skyAbove * SKY_PI;
	if (moonLit)
	{
		groundIrradiance += u_skyMoonColor.rgb * skyTransmittance(SKY_BOTTOM_RADIUS + 0.01, moonDir.y) * saturate(moonDir.y);
	}
	vec3 groundBelow = groundIrradiance * u_skyAtmosphere.z / SKY_PI;
	skyAbove *= u_cloudWind.w;
	groundBelow *= u_cloudWind.w;

	float layerDistance;
	vec4 layer = cloudLayerMarch(_origin, _dir, _jitter, skyAbove, groundBelow, layerDistance);
	if (u_cloudConvection.x <= 0.0)
	{
		return layer;
	}
	float towerDistance;
	vec4 towers = cloudTowerMarch(_origin, _dir, _jitter, skyAbove, groundBelow, towerDistance);
	vec4 front = towerDistance < layerDistance ? towers : layer;
	vec4 back = towerDistance < layerDistance ? layer : towers;
	return vec4(front.rgb + back.rgb * front.a, front.a * back.a);
}

#endif // __CLOUDS_SH__
