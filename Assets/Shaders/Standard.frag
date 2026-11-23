$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Metallic-roughness PBR (glTF conventions): base colour, normal, metallic-roughness (G rough, B metal),
// occlusion, emissive, opacity; alpha cutout via u_emissive.w.
#include "Common.sh"
#include "StandardCutout.sh"
#include "StandardSurface.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texAlpha, 2);
SAMPLER2D(s_texMetallicRoughness, 3);
SAMPLER2D(s_texEmissive, 4);
SAMPLER2D(s_texOcclusion, 5);
SAMPLER2D(s_texMetallic, 6);
SAMPLER2D(s_texRoughness, 7);

// u_baseColor, u_emissive, u_uvTransform, u_drawParams and u_portalClipPlane: StandardCutout.sh.
uniform vec4 u_pbrParams;   // x metallic, y roughness, z normal strength, w occlusion strength
uniform vec4 u_pbrTextureParams; // xy separate metallic/roughness maps, z invert roughness, w spectral rim

void main()
{
	// The discards, in the order StandardDepth.frag (the depth prepass) makes them.
	if (standardClipped(v_worldPos, gl_FragCoord.xy))
	{
		discard;
	}
	vec2 uv = standardUv(v_texcoord0, v_affine);
	vec4 base = standardBase(s_texDiffuse, uv);
	float alpha = base.a * texture2D(s_texAlpha, uv).r;
	if (standardCutout(alpha))
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
	// The spectral rim (StandardMaterial::Rim): thin where the surface faces the eye, solid and
	// glowing with the emissive colour where it turns away. A spectre is of one colour, its
	// glow's: what it wore shows only as light and dark in it. It takes a share of the room's
	// light in that colour (a wash that burns a living figure white, or daylight, would hide
	// that it is see-through), and nothing glints off it.
	if (u_pbrTextureParams.w > 0.0)
	{
		float spectral = u_pbrTextureParams.w;
		vec3 eye = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
		float rim = 1.0 - abs(dot(N, normalize(eye - v_worldPos)));
		rim = rim * rim;
		float tone = dot(base.rgb, vec3(0.299, 0.587, 0.114));
		vec3 hue = mix(vec3_splat(1.0), u_emissive.rgb / max(max(u_emissive.r, max(u_emissive.g, u_emissive.b)), 0.0001), 0.45);
		surface.baseColor = mix(base.rgb, mix(vec3_splat(tone), base.rgb, 0.15) * hue * 0.4, spectral);
		surface.metallic *= 1.0 - spectral;
		surface.roughness = mix(surface.roughness, 1.0, spectral);
		surface.emissive = surface.emissive * mix(1.0, 0.3 + 1.4 * tone, spectral) + u_emissive.rgb * (rim * 0.9 * spectral);
		surface.alpha = alpha * mix(1.0, min(0.45 + 1.2 * rim, 1.0), spectral);
	}
	surface.uv = uv;
	gl_FragColor = shadeStandardSurface(surface, v_worldPos, v_normal, v_viewPos, gl_FragCoord.xy, gl_FrontFacing);
}
