$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

// Depth-only shadow caster (instanced: the model matrix comes from the instance buffer).
#include "Common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
	v_texcoord0 = a_texcoord0;
}
