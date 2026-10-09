$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

// Metallic-roughness PBR (glTF conventions): base colour, normal, metallic-roughness (G rough, B metal),
// occlusion, emissive, opacity; alpha cutout via u_emissive.w.
#include "Common.sh"
#include "Lighting.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texAlpha, 2);
SAMPLER2D(s_texMetallicRoughness, 3);
SAMPLER2D(s_texEmissive, 4);
SAMPLER2D(s_texOcclusion, 5);

uniform vec4 u_baseColor;   // linear rgb, opacity
uniform vec4 u_pbrParams;   // x metallic, y roughness, z normal strength, w occlusion strength
uniform vec4 u_emissive;    // rgb linear * intensity, w alpha cutoff (0 = off)
uniform vec4 u_uvTransform; // xy tiling, zw offset

void main()
{
	vec2 uv = v_texcoord0 * u_uvTransform.xy + u_uvTransform.zw;
	vec4 base = toLinear(texture2D(s_texDiffuse, uv)) * u_baseColor;
	float alpha = base.a * texture2D(s_texAlpha, uv).r;
	if (u_emissive.w > 0.0 && alpha < u_emissive.w)
	{
		discard;
	}

	vec3 mr = texture2D(s_texMetallicRoughness, uv).rgb;
	float metallic = clamp(u_pbrParams.x * mr.b, 0.0, 1.0);
	float roughness = clamp(u_pbrParams.y * mr.g, 0.045, 1.0);
	float occlusion = mix(1.0, texture2D(s_texOcclusion, uv).r, u_pbrParams.w);

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

	vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	Surface s;
	s.position = v_worldPos;
	s.view = normalize(cameraPos - v_worldPos);
	if (dot(N, s.view) < 0.0 && !gl_FrontFacing)
	{
		N = -N;   // double-sided materials
	}
	s.normal = N;
	s.diffuseColor = base.rgb * (1.0 - metallic);
	s.f0 = mix(vec3_splat(0.04), base.rgb, metallic);
	s.roughness = roughness;
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
	color += toLinear(texture2D(s_texEmissive, uv).rgb) * u_emissive.rgb;

	color *= cascadeDebugTint(v_viewPos.z);

	gl_FragColor = vec4(color, alpha);
}
