$input a_position, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

// Skinned shadow caster (ShadowDepth.vert plus the bone palette); pairs with ShadowDepth.frag.
#include "Common.sh"
#include "Skinning.sh"

void main()
{
	mat4 model = mul(mtxFromCols(i_data0, i_data1, i_data2, i_data3), skinMatrix(a_indices, a_weight));
	gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
	v_texcoord0 = a_texcoord0;
}
