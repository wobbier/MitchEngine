$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Instanced PBR vertex shader: the model matrix comes from the instance buffer.
#include "Common.sh"
#include "Era.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 worldPos = mul(model, vec4(a_position, 1.0));
	gl_Position = eraSnapVertex(mul(u_viewProj, worldPos));

	// Inverse-transpose via the cofactor matrix (correct under non-uniform scale).
	mat3 normalMatrix = cofactor(model);
	v_normal = mul(normalMatrix, a_normal);
	v_tangent = mul(model, vec4(a_tangent, 0.0)).xyz;
	v_bitangent = mul(model, vec4(a_bitangent, 0.0)).xyz;
	v_worldPos = worldPos.xyz;
	v_viewPos = mul(u_view, worldPos);
	v_texcoord0 = a_texcoord0;
	// Affine mapping (u_eraParams.y): uv * w and w, so uv / w after interpolation is screen-linear.
	v_affine = vec3(a_texcoord0 * gl_Position.w, gl_Position.w);
}
