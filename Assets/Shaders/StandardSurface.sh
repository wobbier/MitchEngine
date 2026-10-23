#ifndef STANDARD_SURFACE_SH
#define STANDARD_SURFACE_SH

// The forward shading of a metallic-roughness surface, shared by Standard.frag and shader graph
// Lit materials: view-shading debug modes, the sun (with cascaded shadows), clustered local
// lights, image-based ambient, emissive and forward fog. Callers sample their textures (or run
// their graph), fill a SurfaceInput and return shadeStandardSurface(...) as gl_FragColor.
#include "Lighting.sh"
#include "Fog.sh"

// x: ViewShadingMode (CameraData.h); y: depth display range in metres.
uniform vec4 u_viewShading;

struct SurfaceInput
{
	vec3 baseColor;   // linear
	float alpha;
	vec3 normal;      // world space, normalized, after normal mapping
	float metallic;
	float roughness;  // perceptual
	float occlusion;
	vec3 emissive;    // linear radiance
	vec2 uv;          // for the UV checker view
#ifdef SURFACE_TRANSLUCENCY
	vec3 translucency;     // linear: light passed through a thin surface lit from behind (leaves)
	float directOcclusion; // baked occlusion also applied to direct light
#endif
};

#ifdef SURFACE_TRANSLUCENCY
// A thin surface lit from behind: diffuse transmission plus a forward-scattering lobe toward the
// viewer (looking into the sun through leaves).
vec3 shadeTranslucency(Surface _s, vec3 _L, vec3 _radiance, vec3 _transmission)
{
	float back = clamp(-dot(_s.normal, _L), 0.0, 1.0);
	float forward = pow(clamp(dot(_s.view, -_L), 0.0, 1.0), 6.0);
	return _transmission * _radiance * ((back + forward * 2.0) * (1.0 / PI));
}
#endif

// _vertexNormal: the interpolated (unmapped) world normal. gl_FragCoord and gl_FrontFacing are only
// readable in main() on some backends, so they come in as parameters.
vec4 shadeStandardSurface(SurfaceInput _in, vec3 _worldPos, vec3 _vertexNormal, vec4 _viewPos, vec2 _fragCoord, bool _frontFacing)
{
	vec3 base = _in.baseColor;
	float alpha = _in.alpha;
	if (u_viewShading.x > 0.5 && u_viewShading.x < 1.5)
	{
		return vec4(base, alpha);
	}

	vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 viewDirection = normalize(cameraPos - _worldPos);
	if (u_viewShading.x > 3.5 && u_viewShading.x < 4.5)
	{
		return vec4(0.05, 0.7, 0.95, 1.0);
	}
	if (u_viewShading.x > 4.5 && u_viewShading.x < 5.5)
	{
		return vec4(toLinearAccurate(normalize(_vertexNormal) * 0.5 + 0.5), alpha);
	}
	if (u_viewShading.x > 9.5 && u_viewShading.x < 10.5)
	{
		vec2 checks = floor(_in.uv * 10.0);
		float checker = mod(checks.x + checks.y, 2.0);
		vec2 tile = fract(_in.uv);
		float border = min(min(tile.x, 1.0 - tile.x), min(tile.y, 1.0 - tile.y));
		vec3 color = border < 0.015 ? vec3(0.1, 0.7, 1.0) : mix(vec3_splat(0.2), vec3_splat(0.8), checker);
		return vec4(toLinearAccurate(color), alpha);
	}
	if (u_viewShading.x > 10.5 && u_viewShading.x < 11.5)
	{
		float depth = 1.0 - clamp(abs(_viewPos.z) / max(u_viewShading.y, 0.1), 0.0, 1.0);
		return vec4(toLinearAccurate(vec3_splat(depth)), alpha);
	}
	if (u_viewShading.x > 1.5 && u_viewShading.x < 2.5)
	{
		// A soft headlight makes the geometry readable even inside an unlit room.
		float light = 0.16 + 0.5 * abs(dot(normalize(_vertexNormal), viewDirection));
		return vec4(vec3_splat(light), alpha);
	}

	vec3 N = _in.normal;
	Surface s;
	s.position = _worldPos;
	s.view = viewDirection;
	if (dot(N, s.view) < 0.0 && !_frontFacing)
	{
		N = -N;   // double-sided materials
	}
	if (u_viewShading.x > 2.5 && u_viewShading.x < 3.5)
	{
		// The display pass gamma-encodes this; preserve the conventional normal colors.
		return vec4(toLinearAccurate(N * 0.5 + 0.5), alpha);
	}

	float metallic = clamp(_in.metallic, 0.0, 1.0);
	float roughness = clamp(_in.roughness, 0.045, 1.0);
	float occlusion = _in.occlusion;
	if (u_viewShading.x > 5.5 && u_viewShading.x < 8.5)
	{
		float value = u_viewShading.x < 6.5 ? metallic : (u_viewShading.x < 7.5 ? roughness : occlusion);
		return vec4(toLinearAccurate(vec3_splat(value)), alpha);
	}
	vec3 emissive = _in.emissive;
	if (u_viewShading.x > 8.5 && u_viewShading.x < 9.5)
	{
		return vec4(emissive, alpha);
	}
	bool lightingOnly = u_viewShading.x > 11.5;
	s.normal = N;
	s.diffuseColor = lightingOnly ? vec3_splat(0.5) : base * (1.0 - metallic);
	s.f0 = lightingOnly ? vec3_splat(0.04) : mix(vec3_splat(0.04), base, metallic);
	s.roughness = lightingOnly ? 0.7 : roughness;
	s.NoV = max(dot(N, s.view), 1e-4);

	vec3 color = vec3_splat(0.0);
	for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
	{
		if (float(i) >= u_lightParams.x)
		{
			break;
		}
		vec3 L = -u_dirLightDirection[i].xyz;
		float visibility = 1.0;
		if (i == 0 && u_dirLightDirection[0].w > 0.5)
		{
			visibility = sunShadow(s, L, _viewPos.z);
		}
		color += shadeLight(s, L, directionalRadiance(i, s.position)) * visibility;
#ifdef SURFACE_TRANSLUCENCY
		color += shadeTranslucency(s, L, directionalRadiance(i, s.position), _in.translucency) * visibility;
#endif
	}
	color += shadeClusteredLights(s, _fragCoord, _viewPos.z);
#ifdef SURFACE_TRANSLUCENCY
	color *= _in.directOcclusion;
#endif
	color += ambientLighting(s, occlusion);
	color += lightingOnly ? vec3_splat(0.0) : emissive;

	color *= cascadeDebugTint(_viewPos.z);

	// Transparent surfaces are drawn after the fog pass, so they fog themselves.
	vec3 sunColor = u_lightParams.x > 0.5 ? directionalRadiance(0, _worldPos) : vec3_splat(0.0);
	vec4 fog = fogForward(cameraPos, _worldPos, sunColor, u_dirLightDirection[0].xyz);
	color = color * fog.a + fog.rgb;

	return vec4(color, alpha);
}

#endif // STANDARD_SURFACE_SH
