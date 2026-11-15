#ifndef __FOG_FROXEL_SH__
#define __FOG_FROXEL_SH__
// Froxel volumetric fog (the compute path of PostProcess::RenderFog): a camera-aligned grid,
// screen-tiled in x/y and exponential in view depth like the light clusters. Shared by
// FogFroxelInject/Blend/Integrate.comp and FogFroxel.frag. Include after Lighting.sh and Fog.sh.
//
// Slice boundary k (0..Z) sits at view depth near * (far / near)^(k / Z). The fog in front of the
// grid (from u_froxelParams.y, the fog's start, up to near) counts with slice 0.

uniform vec4 u_froxelGrid;    // xyz grid size, w 1 = history valid
uniform vec4 u_froxelDepth;   // x grid near, y grid far (view depth), z 1 / log(far / near), w jitter phase (-1: none, cell centres)
uniform vec4 u_froxelParams;  // x history weight, y fog start (view depth, <= near), zw last frame's P[0][0], P[1][1] (FogFroxelBlend)

// View depth of slice coordinate _s (0 at the near boundary, Z at the far one).
float froxelViewDepth(float _s)
{
	return u_froxelDepth.x * exp(_s / (u_froxelGrid.z * u_froxelDepth.z));
}

// Slice coordinate of view depth _viewZ (negative in front of the grid).
float froxelSliceCoord(float _viewZ)
{
	return u_froxelGrid.z * log(max(_viewZ, 1e-4) / u_froxelDepth.x) * u_froxelDepth.z;
}

// The view depth a cell's sample sits at, _f (0..1) through the cell: slice 0 reaches back to the
// fog's start.
float froxelCellDepth(float _slice, float _f)
{
	if (_slice < 0.5)
	{
		return mix(min(u_froxelParams.y, u_froxelDepth.x), froxelViewDepth(1.0), _f);
	}
	return froxelViewDepth(_slice + _f);
}

// The inverse of froxelCellDepth: the slice coordinate of view depth _viewZ, slice 0 running
// linearly from the fog's start (negative in front of it).
float froxelCellCoord(float _viewZ)
{
	float second = froxelViewDepth(1.0);
	if (_viewZ < second)
	{
		float start = min(u_froxelParams.y, u_froxelDepth.x);
		return (_viewZ - start) / max(second - start, 1e-4);
	}
	return froxelSliceCoord(_viewZ);
}

#ifdef __LIGHTING_SH__
// One hardware-filtered tap of a spot or point light's shadow at a point in the air, as
// FogScatterLights.frag's fogLocalShadow (surfaces take nine): _info is the light's shadow texel
// (slot, texel size per unit distance, depth bias, normal bias), _L points at the light.
float froxelLocalShadow(vec3 _position, vec3 _L, float _distance, vec4 _info, float _type)
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

// Radiance reaching a point in the air from one local light (volumeLocalLightDir's shape, range
// and cone, with the one-tap shadow): the range sphere and, for a spot, the cone reject the point
// before anything else is fetched. _toLight is the unit direction towards the source. _nearField
// (metres squared) softens the 1 / d^2 peak to about the cell's size: one point stands for the
// whole cell, and next to a bright fixture an unsoftened sample overflows the half-float volume.
vec3 froxelLocalLight(vec3 _position, float _light, float _nearField, out vec3 _toLight)
{
	_toLight = vec3(0.0, 1.0, 0.0);
	vec4 t0 = fetchLightTexel(_light, 0.0);   // position, range
	vec4 t1 = fetchLightTexel(_light, 1.0);   // colour, type
	float lightType = t1.w;
	if (t1.r + t1.g + t1.b <= 0.0)
	{
		return vec3_splat(0.0);   // a fixture that is off
	}
	bool spot = lightType > 1.5 && lightType < 2.5;
	vec4 t2 = vec4_splat(0.0);
	if (lightType < 2.5)
	{
		// Points and spots: the centre is the source, so the sphere and cone tests are exact.
		vec3 toCentre = t0.xyz - _position;
		float distanceSq = dot(toCentre, toCentre);
		if (distanceSq > t0.w * t0.w)
		{
			return vec3_splat(0.0);
		}
		if (spot)
		{
			t2 = fetchLightTexel(_light, 2.0);   // direction, cosOuter
			if (dot(-toCentre, t2.xyz) <= t2.w * sqrt(distanceSq))
			{
				return vec3_splat(0.0);
			}
		}
	}
	vec4 geometry = localLightGeometry(_position, _light, t0, lightType);
	if (geometry.w > t0.w * t0.w)
	{
		return vec3_splat(0.0);
	}
	vec3 L = geometry.xyz;
	_toLight = L;
	// localLightAttenuation with the cell's near field.
	float nearField = lightType > 2.5 ? fetchLightTexel(_light, 3.0).z : 0.0;
	float attenuation = distanceAttenuation(geometry.w, t0.w, max(nearField, _nearField));
	if (lightType > 4.5)
	{
		t2 = fetchLightTexel(_light, 2.0);   // facing, cosOuter
		if (dot(_position - t0.xyz, t2.xyz) <= 0.0)
		{
			return vec3_splat(0.0);
		}
		attenuation *= localLightCone(L, t2, fetchLightTexel(_light, 3.0));
	}
	if (spot)
	{
		attenuation *= localLightCone(L, t2, fetchLightTexel(_light, 3.0));
	}
	if (attenuation <= 0.0)
	{
		return vec3_splat(0.0);
	}
	if (lightType < 2.5)
	{
		vec4 shadowInfo = fetchLightTexel(_light, 4.0);
		if (shadowInfo.x > -0.5)
		{
			attenuation *= froxelLocalShadow(_position, L, sqrt(geometry.w), shadowInfo, lightType);
		}
	}
	return t1.rgb * attenuation;
}
#endif

#endif // __FOG_FROXEL_SH__
