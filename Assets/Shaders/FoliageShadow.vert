$input a_position, a_normal, a_texcoord0, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

// MitchTree shadow caster (instanced): the same wind as Foliage.vert, so shadows move with the tree.
#include "Common.sh"
#include "Wind.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec3 worldNormal = normalize(mul(cofactor(model), a_normal));
	vec3 worldPos = applyWind(mul(model, vec4(a_position, 1.0)).xyz, a_position, model, a_color0, worldNormal);
	gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));
	v_texcoord0 = a_texcoord0;
}
