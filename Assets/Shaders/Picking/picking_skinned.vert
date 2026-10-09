$input a_position, a_normal, a_indices, a_weight
$output v_pos, v_view, v_normal, v_color0

// Skinned variant of picking_shaded.vert: animated meshes pick where they are drawn.
#include "../Common.sh"
#include "../Skinning.sh"

uniform vec4 u_tint;

void main()
{
	mat4 skin = skinMatrix(a_indices, a_weight);
	vec3 pos = mul(skin, vec4(a_position, 1.0) ).xyz;
	vec3 normal = mul(skin, vec4(a_normal.xyz*2.0 - 1.0, 0.0) ).xyz;

	gl_Position = mul(u_modelViewProj, vec4(pos, 1.0) );
	v_pos = gl_Position.xyz;
	v_view = mul(u_modelView, vec4(pos, 1.0) ).xyz;

	v_normal = mul(u_modelView, vec4(normal, 0.0) ).xyz;

	v_color0 = u_tint*vec4(0.8, 0.8, 0.8, 1.0);
}
