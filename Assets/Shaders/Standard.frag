$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

// Metallic-roughness PBR (glTF conventions): base colour, normal, metallic-roughness (G rough, B metal),
// occlusion, emissive, opacity; alpha cutout via u_emissive.w.
#include "Common.sh"
#include "Lighting.sh"
#include "Fog.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texAlpha, 2);
SAMPLER2D(s_texMetallicRoughness, 3);
SAMPLER2D(s_texEmissive, 4);
SAMPLER2D(s_texOcclusion, 5);
SAMPLER2D(s_texMetallic, 6);
SAMPLER2D(s_texRoughness, 7);

uniform vec4 u_baseColor;   // linear rgb, opacity
uniform vec4 u_pbrParams;   // x metallic, y roughness, z normal strength, w occlusion strength
uniform vec4 u_emissive;    // rgb linear * intensity, w alpha cutoff (0 = off)
uniform vec4 u_uvTransform; // xy tiling, zw offset
uniform vec4 u_pbrTextureParams; // xy separate metallic/roughness maps, z invert roughness
// x: ViewShadingMode (CameraData.h); y: depth display range in metres.
uniform vec4 u_viewShading;

void main()
{
	vec2 uv = v_texcoord0 * u_uvTransform.xy + u_uvTransform.zw;
	vec4 base = toLinear(texture2D(s_texDiffuse, uv)) * u_baseColor;
	float alpha = base.a * texture2D(s_texAlpha, uv).r;
	if (u_emissive.w > 0.0 && alpha < u_emissive.w)
	{
		discard;
	}

	if (u_viewShading.x > 0.5 && u_viewShading.x < 1.5)
	{
		gl_FragColor = vec4(base.rgb, alpha);
		return;
	}

	vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 viewDirection = normalize(cameraPos - v_worldPos);
	if (u_viewShading.x > 3.5 && u_viewShading.x < 4.5)
	{
		gl_FragColor = vec4(0.05, 0.7, 0.95, 1.0);
		return;
	}
	if (u_viewShading.x > 4.5 && u_viewShading.x < 5.5)
	{
		gl_FragColor = vec4(toLinearAccurate(normalize(v_normal) * 0.5 + 0.5), alpha);
		return;
	}
	if (u_viewShading.x > 9.5 && u_viewShading.x < 10.5)
	{
		vec2 checks = floor(uv * 10.0);
		float checker = mod(checks.x + checks.y, 2.0);
		vec2 tile = fract(uv);
		float border = min(min(tile.x, 1.0 - tile.x), min(tile.y, 1.0 - tile.y));
		vec3 color = border < 0.015 ? vec3(0.1, 0.7, 1.0) : mix(vec3_splat(0.2), vec3_splat(0.8), checker);
		gl_FragColor = vec4(toLinearAccurate(color), alpha);
		return;
	}
	if (u_viewShading.x > 10.5 && u_viewShading.x < 11.5)
	{
		float depth = 1.0 - clamp(abs(v_viewPos.z) / max(u_viewShading.y, 0.1), 0.0, 1.0);
		gl_FragColor = vec4(toLinearAccurate(vec3_splat(depth)), alpha);
		return;
	}
	if (u_viewShading.x > 1.5 && u_viewShading.x < 2.5)
	{
		// A soft headlight makes the geometry readable even inside an unlit room.
		float light = 0.16 + 0.5 * abs(dot(normalize(v_normal), viewDirection));
		gl_FragColor = vec4(vec3_splat(light), alpha);
		return;
	}

	// Tangent-space normal (z reconstructed, so two-channel BC5 maps work too).
	vec3 N = normalize(v_normal);
	vec3 tangentNormal;
	tangentNormal.xy = (texture2D(s_texNormal, uv).xy * 2.0 - 1.0) * u_pbrParams.z;
	tangentNormal.z = sqrt(max(1.0 - dot(tangentNormal.xy, tangentNormal.xy), 0.0));
	if (dot(v_tangent, v_tangent) > 1e-6 && dot(v_bitangent, v_bitangent) > 1e-6)
	{
		mat3 TBN = mtxFromCols(normalize(v_tangent), normalize(v_bitangent), N);
		N = normalize(mul(TBN, tangentNormal));
	}

	Surface s;
	s.position = v_worldPos;
	s.view = viewDirection;
	if (dot(N, s.view) < 0.0 && !gl_FrontFacing)
	{
		N = -N;   // double-sided materials
	}
	if (u_viewShading.x > 2.5 && u_viewShading.x < 3.5)
	{
		// The display pass gamma-encodes this; preserve the conventional normal colors.
		gl_FragColor = vec4(toLinearAccurate(N * 0.5 + 0.5), alpha);
		return;
	}

	vec3 mr = texture2D(s_texMetallicRoughness, uv).rgb;
	float metalMap = mix(mr.b, texture2D(s_texMetallic, uv).r, u_pbrTextureParams.x);
	float roughMap = mix(mr.g, texture2D(s_texRoughness, uv).r, u_pbrTextureParams.y);
	roughMap = mix(roughMap, 1.0 - roughMap, u_pbrTextureParams.z);
	float metallic = clamp(u_pbrParams.x * metalMap, 0.0, 1.0);
	float roughness = clamp(u_pbrParams.y * roughMap, 0.045, 1.0);
	float occlusion = mix(1.0, texture2D(s_texOcclusion, uv).r, u_pbrParams.w);
	if (u_viewShading.x > 5.5 && u_viewShading.x < 8.5)
	{
		float value = u_viewShading.x < 6.5 ? metallic : (u_viewShading.x < 7.5 ? roughness : occlusion);
		gl_FragColor = vec4(toLinearAccurate(vec3_splat(value)), alpha);
		return;
	}
	vec3 emissive = toLinear(texture2D(s_texEmissive, uv).rgb) * u_emissive.rgb;
	if (u_viewShading.x > 8.5 && u_viewShading.x < 9.5)
	{
		gl_FragColor = vec4(emissive, alpha);
		return;
	}
	bool lightingOnly = u_viewShading.x > 11.5;
	s.normal = N;
	s.diffuseColor = lightingOnly ? vec3_splat(0.5) : base.rgb * (1.0 - metallic);
	s.f0 = lightingOnly ? vec3_splat(0.04) : mix(vec3_splat(0.04), base.rgb, metallic);
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
			visibility = sunShadow(s, L, v_viewPos.z);
		}
		color += shadeLight(s, L, u_dirLightColor[i].rgb) * visibility;
	}
	color += shadeClusteredLights(s, gl_FragCoord.xy, v_viewPos.z);
	color += ambientLighting(s, occlusion);
	color += lightingOnly ? vec3_splat(0.0) : emissive;

	color *= cascadeDebugTint(v_viewPos.z);

	// Transparent surfaces are drawn after the fog pass, so they fog themselves.
	vec3 sunColor = u_lightParams.x > 0.5 ? u_dirLightColor[0].rgb : vec3_splat(0.0);
	vec4 fog = fogForward(cameraPos, v_worldPos, sunColor, u_dirLightDirection[0].xyz);
	color = color * fog.a + fog.rgb;

	gl_FragColor = vec4(color, alpha);
}
