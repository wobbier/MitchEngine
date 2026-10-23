#ifndef WIND_SH
#define WIND_SH

// Hierarchical vegetation wind (after GPU Gems 3 ch. 16 and Crytek's vegetation bending), all on the
// vertex: branches sway on their own phase, leaves flutter along their normal, then the whole tree
// leans downwind from its base. Shared by the foliage colour and shadow programs so shadows move
// with the leaves. Needs Common.sh (u_time).
//
// The mesh's wind stream (a_color0, see MitchTree): R leaf flutter weight (0 at the stalk), G branch
// phase (shared by a first-level branch and everything on it), B branch sway weight, A baked crown
// occlusion.

uniform vec4 u_wind;          // xyz world direction (unit, horizontal), w strength (0 calm, 1 strong breeze)
uniform vec4 u_windParams;    // x gusts, y gust fronts per second, z turbulence
uniform vec4 u_foliageWind;   // per tree: x trunk lean (fraction of the height at the top), y branch sway (m), z leaf flutter (m), w frequency
uniform vec4 u_foliage;       // per tree: x height (m), y translucency, z tint variation, w 1 = leaves

float windHash(float _n)
{
	return fract(sin(_n) * 43758.5453);
}

float windNoise(float _x)
{
	float i = floor(_x);
	float f = fract(_x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(windHash(i), windHash(i + 1.0), f);
}

// The wind strength at a tree, with gust fronts that roll downwind across a forest.
float windStrengthAt(vec3 _origin)
{
	float front = u_time.y * u_windParams.y - dot(_origin.xz, u_wind.xz) * 0.035;
	float gust = windNoise(front) * 0.7 + windNoise(front * 2.7 + 17.0) * 0.3;
	return u_wind.w * max(0.0, 1.0 + u_windParams.x * (gust * 2.0 - 0.7));
}

// _worldPos: the vertex after the instance transform (_model); _objectPos: before it.
vec3 applyWind(vec3 _worldPos, vec3 _objectPos, mat4 _model, vec4 _windData, vec3 _worldNormal)
{
	if (u_wind.w <= 0.0)
	{
		return _worldPos;
	}
	vec3 origin = mul(_model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float scale = length(mul(_model, vec4(0.0, 1.0, 0.0, 0.0)).xyz);
	float time = u_time.y * u_foliageWind.w;
	float strength = windStrengthAt(origin);
	float turbulence = u_windParams.z;
	vec3 downwind = u_wind.xyz;
	vec3 across = vec3(-downwind.z, 0.0, downwind.x);
	float treePhase = dot(origin, vec3(0.173, 0.0, 0.311));

	// Branches: pushed downwind and bobbing, each limb on its own phase.
	float branchPhase = _windData.g * 6.2831853 + treePhase;
	float slow = sin(time * 1.9 + branchPhase);
	float fast = sin(time * 3.7 + branchPhase * 1.7);
	vec3 limb = downwind * (0.6 + 0.4 * slow) * strength
		+ vec3(0.0, 0.5, 0.0) * (0.6 * slow + 0.4 * fast) * (0.25 + strength)
		+ across * fast * turbulence * strength;
	vec3 position = _worldPos + limb * (_windData.b * u_foliageWind.y * scale);

	// Leaves flutter along their normal, most at the tip.
	float leafPhase = dot(_objectPos, vec3(1.7, 2.3, 1.1)) + branchPhase;
	float flutter = 0.7 * sin(time * 14.0 + leafPhase) + 0.3 * sin(time * 23.0 + leafPhase * 1.3);
	position += _worldNormal * (flutter * _windData.r * u_foliageWind.z * scale * (0.2 + strength));

	// The whole tree leans from its base (keeping each vertex's distance to the base, so it bends
	// rather than shears).
	float height = clamp(_objectPos.y / max(u_foliage.x, 0.1), 0.0, 1.5);
	float lean = strength * u_foliageWind.x * (1.0 + 0.3 * sin(time * 0.8 + treePhase) + 0.1 * sin(time * 2.3 + treePhase * 1.3));
	vec3 offset = (downwind + across * (turbulence * 0.3 * sin(time * 1.1 + treePhase))) * (lean * height * height * u_foliage.x * scale);
	vec3 relative = position - origin;
	float reach = length(relative);
	vec3 bent = relative + offset;
	if (reach > 1e-4)
	{
		bent = normalize(bent) * reach;
	}
	return origin + bent;
}

#endif // WIND_SH
