$input a_position, a_normal, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_normal, v_viewPos, v_worldPos

// Sprites (LOOK-S2): SpriteCore builds the quads in world space on the CPU, batched by texture.
#include "Common.sh"

void main()
{
	gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
	v_viewPos = mul(u_view, vec4(a_position, 1.0));
	v_worldPos = a_position;
	v_normal = a_normal;
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
