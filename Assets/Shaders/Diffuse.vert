$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_normal, v_texcoord0

#include "Common.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);

	vec4 worldPos = mul(model, vec4(a_position, 1.0) );
	gl_Position = mul(u_viewProj, worldPos);

	v_color0 = vec4(a_normal.x,a_normal.y,a_normal.z,1.0);
	v_texcoord0 = a_texcoord0;

	vec3 normal = a_normal.xyz;
	v_normal = mul(model, vec4(normalize(normal), 0.0) ).xyz;
}
