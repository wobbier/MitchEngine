$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Depth prepass of a Standard surface that drops nothing (no alpha cutoff, fade or clip plane):
// depth only, with no discard so the GPU can skip the fragment stage. Pairs with Standard.vert
// and StandardSkinned.vert; StandardDepth.frag is the variant that cuts holes.
#include "Common.sh"

void main()
{
	gl_FragColor = vec4_splat(0.0);
}
