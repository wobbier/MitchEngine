$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Depth prepass of a Standard surface that drops fragments (alpha cutoff, screen-door fade, portal
// clip plane): Standard.frag's discards and nothing else. Pairs with Standard.vert and
// StandardSkinned.vert, the colour pass's own vertex shaders, so both passes rasterize the same
// depth. Same inputs as Standard.frag, in the same order (the stages link by position on D3D).
#include "Common.sh"
#include "StandardCutout.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texAlpha, 2);

void main()
{
	if (standardClipped(v_worldPos, gl_FragCoord.xy))
	{
		discard;
	}
	if (u_emissive.w > 0.0)
	{
		vec2 uv = standardUv(v_texcoord0, v_affine);
		float alpha = standardBase(s_texDiffuse, uv).a * texture2D(s_texAlpha, uv).r;
		if (standardCutout(alpha))
		{
			discard;
		}
	}
	gl_FragColor = vec4_splat(0.0);
}
