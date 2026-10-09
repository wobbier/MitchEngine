$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

// Skinned PBR vertex shader (Standard.vert plus the bone palette); pairs with Standard.frag.
#include "Common.sh"
#include "Skinning.sh"

void main()
{
	mat4 model = mul(mtxFromCols(i_data0, i_data1, i_data2, i_data3), skinMatrix(a_indices, a_weight));
	vec4 worldPos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, worldPos);

	mat3 normalMatrix = cofactor(model);
	v_normal = mul(normalMatrix, a_normal);
	v_tangent = mul(model, vec4(a_tangent, 0.0)).xyz;
	v_bitangent = mul(model, vec4(a_bitangent, 0.0)).xyz;
	v_worldPos = worldPos.xyz;
	v_viewPos = mul(u_view, worldPos);
	v_texcoord0 = a_texcoord0;
}
