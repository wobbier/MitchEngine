#ifndef __LIGHTING_SH__
#define __LIGHTING_SH__
// Physically based shading: GGX / Smith-GGX correlated / Schlick, Lambert diffuse.
// Directional lights come from uniforms; point and spot lights from clustered light lists.

#define PI 3.14159265359
#define MAX_DIRECTIONAL_LIGHTS 4
#define LIGHT_DATA_WIDTH 1024.0      // 4 texels per light, 256 lights
#define CLUSTER_INDEX_WIDTH 1024.0
#define CLUSTER_INDEX_HEIGHT 128.0
#define MAX_LIGHTS_PER_CLUSTER 64

uniform vec4 u_lightParams;                              // x directional count, y local light count, z ambient intensity, w IBL enabled
uniform vec4 u_dirLightDirection[MAX_DIRECTIONAL_LIGHTS]; // xyz direction light travels, w shadowed (1)
uniform vec4 u_dirLightColor[MAX_DIRECTIONAL_LIGHTS];     // rgb linear colour * intensity
uniform vec4 u_clusterParams;                            // x tile width px, y tile height px, z slice scale, w slice bias
uniform vec4 u_clusterGrid;                              // x, y, z grid size, w 1 = gl_FragCoord origin bottom-left
uniform vec4 u_ambientSky;                               // rgb sky ambient (linear)
uniform vec4 u_ambientGround;                            // rgb ground ambient (linear)

SAMPLER2D(s_lightData, 8);
SAMPLER2D(s_clusterGrid, 9);
SAMPLER2D(s_clusterIndices, 10);

float D_GGX(float _NoH, float _a)
{
	float a2 = _a * _a;
	float f = (_NoH * a2 - _NoH) * _NoH + 1.0;
	return a2 / (PI * f * f + 1e-7);
}

float V_SmithGGXCorrelated(float _NoV, float _NoL, float _a)
{
	float a2 = _a * _a;
	float ggxL = _NoV * sqrt((-_NoL * a2 + _NoL) * _NoL + a2);
	float ggxV = _NoL * sqrt((-_NoV * a2 + _NoV) * _NoV + a2);
	return 0.5 / (ggxV + ggxL + 1e-7);
}

vec3 F_Schlick(float _VoH, vec3 _f0)
{
	float f = pow(1.0 - _VoH, 5.0);
	return f + _f0 * (1.0 - f);
}

struct Surface
{
	vec3 position;
	vec3 normal;
	vec3 view;
	vec3 diffuseColor;
	vec3 f0;
	float roughness;   // perceptual
	float NoV;
};

// Radiance arriving from direction _L (towards the light) with colour _radiance.
vec3 shadeLight(Surface _s, vec3 _L, vec3 _radiance)
{
	float NoL = clamp(dot(_s.normal, _L), 0.0, 1.0);
	if (NoL <= 0.0)
	{
		return vec3_splat(0.0);
	}
	vec3 H = normalize(_L + _s.view);
	float NoH = clamp(dot(_s.normal, H), 0.0, 1.0);
	float VoH = clamp(dot(_s.view, H), 0.0, 1.0);
	float a = _s.roughness * _s.roughness;
	float D = D_GGX(NoH, a);
	float V = V_SmithGGXCorrelated(_s.NoV, NoL, a);
	vec3 F = F_Schlick(VoH, _s.f0);
	vec3 specular = D * V * F;
	vec3 diffuse = _s.diffuseColor * (1.0 / PI);
	return (diffuse * (vec3_splat(1.0) - F) + specular) * _radiance * NoL;
}

// Windowed inverse-square falloff (smoothly reaches zero at the range).
float distanceAttenuation(float _distanceSq, float _range)
{
	float ratio = _distanceSq / (_range * _range);
	float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
	return window * window / max(_distanceSq, 0.0001);
}

vec4 fetchLightTexel(float _light, float _texel)
{
	return texture2DLod(s_lightData, vec2((_light * 4.0 + _texel + 0.5) / LIGHT_DATA_WIDTH, 0.5), 0.0);
}

// Light data texels: 0 position.xyz range | 1 colour.rgb type (1 point, 2 spot) | 2 direction.xyz cosOuter | 3 cosInner
vec3 shadeLocalLight(Surface _s, float _light)
{
	vec4 t0 = fetchLightTexel(_light, 0.0);
	vec4 t1 = fetchLightTexel(_light, 1.0);
	vec3 toLight = t0.xyz - _s.position;
	float distanceSq = dot(toLight, toLight);
	if (distanceSq > t0.w * t0.w)
	{
		return vec3_splat(0.0);
	}
	vec3 L = toLight * inversesqrt(max(distanceSq, 1e-8));
	float attenuation = distanceAttenuation(distanceSq, t0.w);
	if (t1.w > 1.5)
	{
		vec4 t2 = fetchLightTexel(_light, 2.0);
		vec4 t3 = fetchLightTexel(_light, 3.0);
		float cd = dot(-L, t2.xyz);
		float spot = clamp((cd - t2.w) / max(t3.x - t2.w, 1e-4), 0.0, 1.0);
		attenuation *= spot * spot;
	}
	return shadeLight(_s, L, t1.rgb * attenuation);
}

// Point and spot lights from this fragment's cluster.
vec3 shadeClusteredLights(Surface _s, vec2 _fragCoord, float _viewZ)
{
	if (u_lightParams.y < 0.5)
	{
		return vec3_splat(0.0);
	}
	vec2 pixel = _fragCoord;
	if (u_clusterGrid.w > 0.5)
	{
		pixel.y = u_viewRect.w - pixel.y;
	}
	float tileX = clamp(floor(pixel.x / u_clusterParams.x), 0.0, u_clusterGrid.x - 1.0);
	float tileY = clamp(floor(pixel.y / u_clusterParams.y), 0.0, u_clusterGrid.y - 1.0);
	float slice = clamp(floor(log(max(_viewZ, 1e-4)) * u_clusterParams.z + u_clusterParams.w), 0.0, u_clusterGrid.z - 1.0);
	vec2 gridUv = vec2((tileX + tileY * u_clusterGrid.x + 0.5) / (u_clusterGrid.x * u_clusterGrid.y), (slice + 0.5) / u_clusterGrid.z);
	vec4 cluster = texture2DLod(s_clusterGrid, gridUv, 0.0);
	float offset = cluster.x;
	int count = int(min(cluster.y, float(MAX_LIGHTS_PER_CLUSTER)));

	vec3 color = vec3_splat(0.0);
	for (int i = 0; i < MAX_LIGHTS_PER_CLUSTER; ++i)
	{
		if (i >= count)
		{
			break;
		}
		float index = offset + float(i);
		vec2 indexUv = vec2((mod(index, CLUSTER_INDEX_WIDTH) + 0.5) / CLUSTER_INDEX_WIDTH, (floor(index / CLUSTER_INDEX_WIDTH) + 0.5) / CLUSTER_INDEX_HEIGHT);
		float light = texture2DLod(s_clusterIndices, indexUv, 0.0).r;
		color += shadeLocalLight(_s, light);
	}
	return color;
}

// Hemisphere ambient (replaced by image-based lighting when an environment is available).
vec3 hemisphereAmbient(Surface _s)
{
	float up = _s.normal.y * 0.5 + 0.5;
	vec3 irradiance = mix(u_ambientGround.rgb, u_ambientSky.rgb, up);
	vec3 F = _s.f0 + (max(vec3_splat(1.0 - _s.roughness), _s.f0) - _s.f0) * pow(1.0 - _s.NoV, 5.0);
	return (_s.diffuseColor * (vec3_splat(1.0) - F) + F * 0.5) * irradiance * u_lightParams.z;
}

#endif // __LIGHTING_SH__
