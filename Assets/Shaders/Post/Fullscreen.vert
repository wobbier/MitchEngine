$input a_position, a_texcoord0
$output v_texcoord0

// Fullscreen triangle for post-processing passes (orthographic 0..1 view, see Moonlight::screenSpaceQuad).
#include "../Common.sh"

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position.xyz, 1.0));
	v_texcoord0 = a_texcoord0;
}
