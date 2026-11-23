$input v_texcoord0

// Volumetric fog at half resolution for a camera with VolumetricLightSamples (FogScatter.frag is
// the pass without it): the same march through the height fog for the ambient fog colour and the
// sun through its cascaded shadow, but local lights are not lit at every step from the step's
// cluster. Each one is integrated on its own along the part of the ray inside its range and cone
// (fogLightBeam, u_fogScatter.x samples), so a ray is tested against a light once instead of every
// step against every light of its cluster, and a thin beam is sampled where it is. Output as
// FogScatter: rgb in-scattered light, a transmittance.
#include "../Common.sh"
#include "PostCommon.sh"
#include "../Lighting.sh"
#include "../Fog.sh"

SAMPLER2D(s_input0, 0);   // camera depth

uniform vec4 u_fogScatter;   // x samples per local light along its own stretch of the ray, y lights the fog shows (first in the light data, ME-409)

#define FOG_MAX_LOCAL_LIGHTS 256   // LIGHT_DATA_WIDTH / LIGHT_DATA_TEXELS
#define FOG_MAX_LIGHT_SAMPLES 32

// The stretch of a ray (unit _dir, from the origin) inside a sphere; _toCentre is centre - origin.
// x start, y end; empty when y <= x.
vec2 fogRaySphere(vec3 _toCentre, vec3 _dir, float _radius)
{
	float along = dot(_toCentre, _dir);
	float h = along * along - dot(_toCentre, _toCentre) + _radius * _radius;
	if (h <= 0.0)
	{
		return vec2(1.0, 0.0);
	}
	h = sqrt(h);
	return vec2(along - h, along + h);
}

// The stretch of a ray inside a cone opening along _axis from its apex with half angle
// acos(_cosOuter) (under 90 degrees); _fromApex is origin - apex. The cone is convex, so it is one
// interval, open-ended when the ray runs steeper than the cone's side.
vec2 fogRayCone(vec3 _fromApex, vec3 _dir, vec3 _axis, float _cosOuter)
{
	float cos2 = _cosOuter * _cosOuter;
	float dirAxis = dot(_dir, _axis);
	float originAxis = dot(_fromApex, _axis);
	float a = dirAxis * dirAxis - cos2;
	float b = dirAxis * originAxis - cos2 * dot(_dir, _fromApex);
	float c = originAxis * originAxis - cos2 * dot(_fromApex, _fromApex);
	if (abs(a) < 1e-6)
	{
		a = a < 0.0 ? -1e-6 : 1e-6;
	}
	float discriminant = b * b - a * c;
	if (discriminant < 0.0)
	{
		if (a < 0.0)
		{
			return vec2(1.0, 0.0);
		}
		discriminant = 0.0;   // through the apex
	}
	float root = sqrt(discriminant);
	float t0 = (-b - root) / a;
	float t1 = (-b + root) / a;
	if (a > 0.0)
	{
		// Steeper than the side (t0 <= t1): the roots are one on each nappe, and the ray is in
		// the forward one after the later root, or before the earlier one when it runs backwards.
		return dirAxis > 0.0 ? vec2(t1, 1e9) : vec2(-1e9, t0);
	}
	// Shallower (t1 <= t0): both roots on one nappe; the mirrored one behind the apex lights nothing.
	float midAxis = originAxis + dirAxis * (t0 + t1) * 0.5;
	return midAxis > 0.0 ? vec2(t1, t0) : vec2(1.0, 0.0);
}

// One hardware-filtered tap of a spot or point light's shadow at a point in the air (surfaces
// take nine, localShadow); the march's jitter and the resolves soften it. _info is the light's
// shadow texel (slot, texel size per unit distance, depth bias, normal bias), _L points at the light.
float fogLocalShadow(vec3 _position, vec3 _L, float _distance, vec4 _info, float _type)
{
	float tile = _info.x;
	if (_type < 1.5)
	{
		vec3 v = -_L;
		vec3 a = abs(v);
		float face;
		if (a.x >= a.y && a.x >= a.z)
		{
			face = v.x > 0.0 ? 0.0 : 1.0;
		}
		else if (a.y >= a.z)
		{
			face = v.y > 0.0 ? 2.0 : 3.0;
		}
		else
		{
			face = v.z > 0.0 ? 4.0 : 5.0;
		}
		tile = 4.0 + _info.x * 6.0 + face;
	}
	vec3 position = _position + _L * (_info.z * _info.y * _distance);
	vec4 coord = mul(u_localShadowMatrix[int(tile)], vec4(position, 1.0));
	if (coord.w <= 0.0)
	{
		return 1.0;
	}
	vec3 projected = coord.xyz / coord.w;
	vec4 rect = localShadowRect(tile);
	vec2 texel = u_spotShadowParams.xy;
	vec2 uv = clamp(projected.xy, rect.xy + texel * 1.5, rect.zw - texel * 1.5);
	return shadowCompare(s_spotShadowMap, vec3(uv, projected.z));
}

// One local light's in-scattering along the ray, integrated on its own: the ray is clipped to the
// light's range sphere and, for a spot, its cone, and _samples jittered samples go into what is
// left, spaced evenly in the angle seen from the light (equi-angular, so they follow the 1 / d^2
// falloff). A beam is therefore sampled where it is, however thin, and a light the ray misses costs
// its two or three data texels. The fog between the camera and each sample is the closed form.
// Returns radiance * phase, already weighted by the fog's scattering and transmittance.
vec3 fogLightBeam(float _light, vec3 _origin, vec3 _dir, float _marchStart, float _marchEnd, float _jitter, float _samples)
{
	vec4 t0 = fetchLightTexel(_light, 0.0);   // position, range
	vec4 t1 = fetchLightTexel(_light, 1.0);   // colour, type
	float lightType = t1.w;
	vec3 toLight = t0.xyz - _origin;
	if (t1.r + t1.g + t1.b <= 0.0)
	{
		return vec3_splat(0.0);   // a fixture that is off
	}

	// Extended sources reach their range from the emitter's surface: bound them by its half diagonal.
	float extent = 0.0;
	if (lightType > 2.5)
	{
		float halfX = fetchLightTexel(_light, 5.0).w;
		if (lightType < 3.5)
		{
			extent = halfX + fetchLightTexel(_light, 3.0).y;
		}
		else
		{
			float halfY = fetchLightTexel(_light, 6.0).w;
			float halfZ = lightType < 4.5 ? fetchLightTexel(_light, 7.0).w : 0.0;
			extent = sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
		}
	}
	vec2 span = fogRaySphere(toLight, _dir, t0.w + extent);
	span = vec2(max(span.x, _marchStart), min(span.y, _marchEnd));
	if (span.y <= span.x)
	{
		return vec3_splat(0.0);
	}

	bool spot = lightType > 1.5 && lightType < 2.5;
	vec4 t2 = vec4_splat(0.0);
	// cosInner, and w the light's fog scale (ME-409).
	vec4 t3 = fetchLightTexel(_light, 3.0);
	if (t3.w <= 0.0)
	{
		return vec3_splat(0.0);
	}
	if (spot)
	{
		t2 = fetchLightTexel(_light, 2.0);   // direction, cosOuter
		if (t2.w > 0.02)
		{
			vec2 cone = fogRayCone(-toLight, _dir, t2.xyz, t2.w);
			span = vec2(max(span.x, cone.x), min(span.y, cone.y));
			if (span.y <= span.x)
			{
				return vec3_splat(0.0);
			}
		}
	}
	vec4 shadowInfo = vec4(-1.0, 0.0, 0.0, 0.0);
	if (lightType < 2.5)
	{
		shadowInfo = fetchLightTexel(_light, 4.0);
	}

	// Equi-angular: the angle from the light's closest approach to the ray, kept off zero (and
	// outside an extended emitter) so the weights stay bounded looking straight down a beam.
	float along = dot(toLight, _dir);
	float approach = max(sqrt(max(dot(toLight, toLight) - along * along, 0.0)), max(extent, 0.01));
	float thetaStart = atan((span.x - along) / approach);
	float thetaEnd = atan((span.y - along) / approach);
	vec3 fogOrigin = _origin + _dir * _marchStart;

	float sum = 0.0;
	for (int i = 0; i < FOG_MAX_LIGHT_SAMPLES; ++i)
	{
		if (float(i) >= _samples)
		{
			break;
		}
		float offset = approach * tan(mix(thetaStart, thetaEnd, (float(i) + _jitter) / _samples));
		float t = clamp(along + offset, span.x, span.y);
		vec3 p = _origin + _dir * t;
		vec4 geometry = localLightGeometry(p, _light, t0, lightType);
		if (geometry.w > t0.w * t0.w)
		{
			continue;
		}
		float attenuation = localLightAttenuation(p, geometry, _light, t0, lightType);
		if (spot)
		{
			attenuation *= localLightCone(geometry.xyz, t2, t3);
		}
		if (attenuation <= 0.0)
		{
			continue;
		}
		if (shadowInfo.x > -0.5)
		{
			attenuation *= fogLocalShadow(p, geometry.xyz, sqrt(geometry.w), shadowInfo, lightType);
		}
		// Phase against the direction towards the source (the sun's convention in the march); the
		// fog from where it starts to the sample in closed form.
		float scatter = fogDensity(p.y) * exp(-fogOpticalDepth(fogOrigin, _dir, t - _marchStart));
		float weight = (approach * approach + offset * offset) / approach;   // 1 / pdf, less the angle and count
		sum += attenuation * henyeyGreenstein(dot(_dir, geometry.xyz), u_fogVolume2.x) * scatter * weight;
	}
	return t1.rgb * (sum * (thetaEnd - thetaStart) / _samples * t3.w);
}

void main()
{
	// Snap to a full-resolution depth texel (half-resolution centres fall between them, see SSAO);
	// the composite looks the depth up the same way.
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	vec2 halfUv = (floor(v_texcoord0 * u_fogScreen.zw) + 0.5) / u_fogScreen.zw;
	vec2 uv = (floor(halfUv / fullTexel) + 0.5) * fullTexel;
	float viewZ = fogViewDepth(texture2D(s_input0, uv).r);
	vec3 dir;
	float distance;
	float viewZPerMetre;
	float originViewZ;
	vec3 origin = fogRay(uv, viewZ, dir, distance, viewZPerMetre, originViewZ);

	vec3 directionalScatter[MAX_DIRECTIONAL_LIGHTS];
	for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
	{
		directionalScatter[i] = u_dirLightColor[i].rgb * henyeyGreenstein(dot(dir, -u_dirLightDirection[i].xyz), u_fogVolume.y) * u_fogVolume.x;
	}

	float marchEnd = min(distance, u_fogVolume.z);
	float marchStart = min(u_fogParams.w, marchEnd);
	int steps = int(u_fogVolume.w);
	float jitter = fract(interleavedGradientNoise(gl_FragCoord.xy) + u_fogProjection2.w);
	float lightSamples = clamp(u_fogScatter.x, 1.0, float(FOG_MAX_LIGHT_SAMPLES));

	vec3 scattered = vec3_splat(0.0);
	float transmittance = 1.0;
	for (int i = 0; i < 256; ++i)
	{
		if (i >= steps)
		{
			break;
		}
		// The same quadratic spacing as FogScatter, so the sun's shafts and the ambient fog match it.
		float startFraction = float(i) / max(u_fogVolume.w, 1.0);
		float endFraction = float(i + 1) / max(u_fogVolume.w, 1.0);
		float stepStart = mix(marchStart, marchEnd, startFraction * startFraction);
		float stepEnd = mix(marchStart, marchEnd, endFraction * endFraction);
		float stepLength = stepEnd - stepStart;
		float t = mix(stepStart, stepEnd, jitter);
		vec3 p = origin + dir * t;
		float sigma = fogDensity(p.y);
		if (sigma > 0.0)
		{
			float stepViewZ = originViewZ + t * viewZPerMetre;
			vec3 light = u_fogColor.rgb;
			for (int j = 0; j < MAX_DIRECTIONAL_LIGHTS; ++j)
			{
				if (float(j) >= u_lightParams.x) break;
				float visibility = j == 0 ? sunShadowVolume(p, stepViewZ) : 1.0;
				light += directionalScatter[j] * directionalEclipse(j, p) * visibility;
			}
			float stepTransmittance = exp(-sigma * stepLength);
			scattered += transmittance * light * (1.0 - stepTransmittance);
			transmittance *= stepTransmittance;
		}
	}
	if (marchEnd > marchStart)
	{
		// Every local light of the frame the fog shows, not a cluster's list: a ray crosses many
		// clusters. The renderer puts the lights with a zero VolumetricMultiplier last.
		vec3 beams = vec3_splat(0.0);
		float fogLights = min(u_lightParams.y, u_fogScatter.y);
		for (int i = 0; i < FOG_MAX_LOCAL_LIGHTS; ++i)
		{
			if (float(i) >= fogLights)
			{
				break;
			}
			// Golden-ratio offsets decorrelate the lights' sample positions within a pixel.
			beams += fogLightBeam(float(i), origin, dir, marchStart, marchEnd, fract(jitter + float(i) * 0.61803398875), lightSamples);
		}
		scattered += beams * u_fogVolume.x;
	}
	vec4 rest = fogAnalytic(origin, dir, marchEnd, distance, fogDirectionalRadiance(dir, origin + dir * ((marchEnd + distance) * 0.5)));
	gl_FragColor = fogClampOpacity(vec4(scattered + transmittance * rest.rgb, transmittance * rest.a));
}
