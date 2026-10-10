#ifndef __LIGHTING_SH__
#define __LIGHTING_SH__
// Physically based shading: GGX / Smith-GGX correlated / Schlick, Lambert diffuse.
// Directional lights come from uniforms; point and spot lights from clustered light lists.

#define PI 3.14159265359
#define MAX_DIRECTIONAL_LIGHTS 4
#define LIGHT_DATA_WIDTH 2048.0      // 5 texels per light, 256 lights
#define LIGHT_DATA_TEXELS 5.0
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
SAMPLER2DSHADOW(s_shadowMap, 11);
SAMPLER2DSHADOW(s_spotShadowMap, 12);
SAMPLERCUBE(s_envSpecular, 13);
SAMPLERCUBE(s_envIrradiance, 14);
SAMPLER2D(s_brdfLut, 15);

uniform vec4 u_envParams;            // x intensity, y specular mip count - 1, z 1 = environment ready

// Shadows. Both maps are 2x2 atlases; the matrices map world space straight to atlas uv + depth.
uniform mat4 u_shadowMatrix[4];      // sun cascades
uniform vec4 u_cascadeSplits;        // view depth where each cascade ends
uniform vec4 u_cascadeTexel;         // world size of one shadow texel, per cascade
uniform vec4 u_shadowParams;         // x depth bias, y normal bias (texels), z 1 / atlas size, w 0 off, 1 on, 2 tint cascades
uniform mat4 u_localShadowMatrix[16];   // 0-3 spot lights, 4-15 point light cube faces (light * 6 + face)
uniform vec4 u_spotShadowParams;        // local shadow atlas: xy 1 / size, zw spot tile size (uv)
uniform vec4 u_pointShadowParams;       // x u where point tiles start, yz point tile size (uv)

#if BGFX_SHADER_LANGUAGE_GLSL >= 130
#	define shadowCompare(_sampler, _coord) texture(_sampler, _coord)
#	define cubeLod(_sampler, _dir, _lod) textureLod(_sampler, _dir, _lod)
#else
#	define shadowCompare(_sampler, _coord) shadow2D(_sampler, _coord)
#	define cubeLod(_sampler, _dir, _lod) textureCubeLod(_sampler, _dir, _lod)
#endif

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
	return texture2DLod(s_lightData, vec2((_light * LIGHT_DATA_TEXELS + _texel + 0.5) / LIGHT_DATA_WIDTH, 0.5), 0.0);
}

// One-hot select of a vec4 component by a float index (0..3).
float vec4Component(vec4 _v, float _index)
{
	vec4 lower = step(vec4(0.0, 1.0, 2.0, 3.0) - 0.5, vec4_splat(_index));
	vec4 upper = step(vec4(1.0, 2.0, 3.0, 4.0) - 0.5, vec4_splat(_index));
	return dot(_v, lower - upper);
}

// Atlas tile (2x2) as minU, minV, maxU, maxV; tile 0 is the top-left of the render target.
vec4 shadowTileRect(float _tile)
{
	float column = mod(_tile, 2.0);
	float row = floor(_tile / 2.0);
	float minV = (u_clusterGrid.w > 0.5) ? (0.5 - row * 0.5) : (row * 0.5);
	return vec4(column * 0.5, minV, column * 0.5 + 0.5, minV + 0.5);
}

// 3x3 hardware-compared taps (each bilinear), kept inside the tile.
float sunShadowPcf(vec3 _coord, vec4 _rect)
{
	float texel = u_shadowParams.z;
	vec2 lo = _rect.xy + texel * 1.5;
	vec2 hi = _rect.zw - texel * 1.5;
	if (any(lessThan(_coord.xy, lo)) || any(greaterThan(_coord.xy, hi)) || _coord.z >= 1.0)
	{
		return 1.0;
	}
	float sum = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			sum += shadowCompare(s_shadowMap, vec3(_coord.xy + vec2(float(x), float(y)) * texel, _coord.z));
		}
	}
	return sum / 9.0;
}

// Receiver offset that keeps a sloped surface out of its own shadow: a PCF tap t texels away sees
// the surface t*tan(theta) deeper, while lifting the point n along its normal brings it n/cos(theta)
// closer to the light, so the normal offset needs to grow with sin(theta).
vec3 shadowReceiverOffset(Surface _s, vec3 _L, float _texelWorld, float _depthBias, float _normalBias)
{
	float NoL = clamp(dot(_s.normal, _L), 0.0, 1.0);
	float sinTheta = sqrt(1.0 - NoL * NoL);
	return _s.normal * (_normalBias * _texelWorld * sinTheta) + _L * (_depthBias * _texelWorld);
}

float sunShadowCascade(Surface _s, vec3 _L, float _cascade)
{
	float texelWorld = vec4Component(u_cascadeTexel, _cascade);
	vec3 position = _s.position + shadowReceiverOffset(_s, _L, texelWorld, u_shadowParams.x, u_shadowParams.y);
	vec4 coord = mul(u_shadowMatrix[int(_cascade)], vec4(position, 1.0));
	return sunShadowPcf(coord.xyz, shadowTileRect(_cascade));
}

float sunCascadeIndex(float _viewZ)
{
	return dot(step(u_cascadeSplits, vec4_splat(_viewZ)), vec4_splat(1.0));
}

// Visibility of the shadowed directional light (index 0); _L points towards the light.
float sunShadow(Surface _s, vec3 _L, float _viewZ)
{
	float cascade = sunCascadeIndex(_viewZ);
	if (u_shadowParams.w < 0.5 || cascade > 3.5)
	{
		return 1.0;
	}
	float shadow = sunShadowCascade(_s, _L, cascade);

	// Blend into the next cascade over the last 10% of this one (and fade out after the last).
	float splitEnd = vec4Component(u_cascadeSplits, cascade);
	float splitStart = cascade > 0.5 ? vec4Component(u_cascadeSplits, cascade - 1.0) : 0.0;
	float band = (splitEnd - splitStart) * 0.1;
	float t = clamp((_viewZ - (splitEnd - band)) / max(band, 1e-4), 0.0, 1.0);
	if (t > 0.0)
	{
		float next = cascade < 2.5 ? sunShadowCascade(_s, _L, cascade + 1.0) : 1.0;
		shadow = mix(shadow, next, t);
	}
	return shadow;
}

vec3 cascadeDebugTint(float _viewZ)
{
	float cascade = sunCascadeIndex(_viewZ);
	if (u_shadowParams.w < 1.5 || cascade > 3.5)
	{
		return vec3_splat(1.0);
	}
	vec3 tints[4];
	tints[0] = vec3(1.0, 0.45, 0.45);
	tints[1] = vec3(0.45, 1.0, 0.45);
	tints[2] = vec3(0.45, 0.6, 1.0);
	tints[3] = vec3(1.0, 1.0, 0.4);
	return tints[int(cascade)];
}

// Local shadow atlas: spot tiles (0-3) in a 2x2 block on the left, point light faces (4-19) in a
// 4x4 block of smaller tiles to its right.
vec4 localShadowRect(float _tile)
{
	vec2 origin;
	vec2 size;
	if (_tile < 3.5)
	{
		size = u_spotShadowParams.zw;
		origin = vec2(mod(_tile, 2.0), floor(_tile / 2.0)) * size;
	}
	else
	{
		float t = _tile - 4.0;
		size = u_pointShadowParams.yz;
		origin = vec2(u_pointShadowParams.x, 0.0) + vec2(mod(t, 4.0), floor(t / 4.0)) * size;
	}
	float minV = (u_clusterGrid.w > 0.5) ? (1.0 - origin.y - size.y) : origin.y;
	return vec4(origin.x, minV, origin.x + size.x, minV + size.y);
}

// One local shadow tile: _info = slot, texel size per unit distance, depth bias, normal bias.
float localShadow(Surface _s, vec3 _L, float _distance, vec4 _info, float _tile)
{
	float texelWorld = _info.y * _distance;
	vec3 position = _s.position + shadowReceiverOffset(_s, _L, texelWorld, _info.z, _info.w);
	vec4 coord = mul(u_localShadowMatrix[int(_tile)], vec4(position, 1.0));
	if (coord.w <= 0.0)
	{
		return 1.0;
	}
	vec3 projected = coord.xyz / coord.w;
	vec4 rect = localShadowRect(_tile);
	vec2 texel = u_spotShadowParams.xy;
	vec2 lo = rect.xy + texel * 1.5;
	vec2 hi = rect.zw - texel * 1.5;
	float sum = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 uv = clamp(projected.xy + vec2(float(x), float(y)) * texel, lo, hi);
			sum += shadowCompare(s_spotShadowMap, vec3(uv, projected.z));
		}
	}
	return sum / 9.0;
}

float spotShadow(Surface _s, vec3 _L, float _distance, vec4 _info)
{
	return localShadow(_s, _L, _distance, _info, _info.x);
}

// Point lights: the cube face the fragment falls in (by the light->fragment direction's major axis).
float pointShadow(Surface _s, vec3 _L, float _distance, vec4 _info)
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
	return localShadow(_s, _L, _distance, _info, 4.0 + _info.x * 6.0 + face);
}

// Light data texels: 0 position.xyz range | 1 colour.rgb type (1 point, 2 spot) | 2 direction.xyz cosOuter
// | 3 cosInner | 4 shadow slot (-1 none), texel size per unit distance, depth bias, normal bias
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
		vec4 t4 = fetchLightTexel(_light, 4.0);
		if (t4.x > -0.5 && attenuation > 0.0)
		{
			attenuation *= spotShadow(_s, L, sqrt(distanceSq), t4);
		}
	}
	else if (attenuation > 0.0)
	{
		vec4 t4 = fetchLightTexel(_light, 4.0);
		if (t4.x > -0.5)
		{
			attenuation *= pointShadow(_s, L, sqrt(distanceSq), t4);
		}
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

// Image-based ambient: prefiltered radiance + irradiance with the split-sum BRDF, including the
// multiple-scattering energy compensation (Fdez-Aguera 2019) so rough metals don't darken, and
// specular occlusion from the material / SSAO occlusion (Lagarde 2014).
vec3 ambientLighting(Surface _s, float _occlusion)
{
	if (u_envParams.z < 0.5)
	{
		return hemisphereAmbient(_s) * _occlusion;
	}
	vec2 brdf = texture2DLod(s_brdfLut, vec2(_s.NoV, _s.roughness), 0.0).rg;
	float a = _s.roughness * _s.roughness;
	vec3 R = reflect(-_s.view, _s.normal);
	// Rough lobes lean towards the normal (Frostbite's dominant direction).
	R = normalize(mix(_s.normal, R, (1.0 - a) * (sqrt(1.0 - a) + a)));
	vec3 radiance = cubeLod(s_envSpecular, R, _s.roughness * u_envParams.y).rgb;
	vec3 irradiance = cubeLod(s_envIrradiance, _s.normal, 0.0).rgb;

	vec3 FssEss = _s.f0 * brdf.x + brdf.y;
	float Ems = 1.0 - (brdf.x + brdf.y);
	vec3 Favg = _s.f0 + (vec3_splat(1.0) - _s.f0) / 21.0;
	vec3 FmsEms = Ems * FssEss * Favg / (vec3_splat(1.0) - Favg * Ems);
	vec3 kD = _s.diffuseColor * (vec3_splat(1.0) - FssEss - FmsEms);

	float specularOcclusion = clamp(pow(_s.NoV + _occlusion, exp2(-16.0 * _s.roughness - 1.0)) - 1.0 + _occlusion, 0.0, 1.0);
	return (FssEss * radiance * specularOcclusion + (FmsEms + kD) * irradiance * _occlusion) * u_envParams.x;
}

#endif // __LIGHTING_SH__
