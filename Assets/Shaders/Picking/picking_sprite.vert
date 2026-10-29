$input a_position, a_texcoord0
$output v_texcoord0

// Sprites in the picking buffer (ME-278): world-space quads, carrying their texture coordinates so
// the fragment stage can skip clear texels.
#include "../Common.sh"

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
}
