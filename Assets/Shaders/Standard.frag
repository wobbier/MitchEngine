$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Metallic-roughness PBR (glTF conventions): base colour, normal, metallic-roughness (G rough, B metal),
// occlusion, emissive, opacity; alpha cutout via u_emissive.w.
#include "Common.sh"
#include "Era.sh"
#include "StandardSurface.sh"
#include "Dither.sh"

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
uniform vec4 u_drawParams;  // x screen-door fade (Dither.sh), per draw

void main()
{
	if (ditherDrops(u_drawParams.x, gl_FragCoord.xy))
	{
		discard;
	}
	vec2 meshUv = u_eraParams.y > 0.5 ? v_affine.xy / max(v_affine.z, 0.000001) : v_texcoord0;
	vec2 uv = meshUv * u_uvTransform.xy + u_uvTransform.zw;
	vec4 base = toLinear(u_eraParams.z > 0.5 ? eraThreePoint(s_texDiffuse, uv) : texture2D(s_texDiffuse, uv)) * u_baseColor;
	float alpha = base.a * texture2D(s_texAlpha, uv).r;
	if (u_emissive.w > 0.0 && alpha < u_emissive.w)
	{
		discard;
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

	vec3 mr = texture2D(s_texMetallicRoughness, uv).rgb;
	float metalMap = mix(mr.b, texture2D(s_texMetallic, uv).r, u_pbrTextureParams.x);
	float roughMap = mix(mr.g, texture2D(s_texRoughness, uv).r, u_pbrTextureParams.y);
	roughMap = mix(roughMap, 1.0 - roughMap, u_pbrTextureParams.z);

	SurfaceInput surface;
	surface.baseColor = base.rgb;
	surface.alpha = alpha;
	surface.normal = N;
	surface.metallic = u_pbrParams.x * metalMap;
	surface.roughness = u_pbrParams.y * roughMap;
	surface.occlusion = mix(1.0, texture2D(s_texOcclusion, uv).r, u_pbrParams.w);
	surface.emissive = toLinear(texture2D(s_texEmissive, uv).rgb) * u_emissive.rgb;
	surface.uv = uv;
	gl_FragColor = shadeStandardSurface(surface, v_worldPos, v_normal, v_viewPos, gl_FragCoord.xy, gl_FrontFacing);
}
