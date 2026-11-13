$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_affine

// Instanced PBR vertex shader: the model matrix comes from the instance buffer.
#include "Common.sh"
#include "Era.sh"

void main()
{
	// The matrix is affine, so its bottom row is free: the renderer puts the mesh's per-instance
	// values there (BGFXRenderer::InstanceMatrix, ME-365; zero unless a mesh sets them) and the row
	// goes back to (0, 0, 0, 1) here. xy offsets the uv before the material's tiling (an atlas tile).
	vec2 instanceUv = vec2(i_data0.w, i_data1.w);
	mat4 model = mtxFromCols(vec4(i_data0.xyz, 0.0), vec4(i_data1.xyz, 0.0), vec4(i_data2.xyz, 0.0), i_data3);
	vec4 worldPos = mul(model, vec4(a_position, 1.0));
	gl_Position = eraSnapVertex(mul(u_viewProj, worldPos));

	// Inverse-transpose via the cofactor matrix (correct under non-uniform scale).
	mat3 normalMatrix = cofactor(model);
	v_normal = mul(normalMatrix, a_normal);
	v_tangent = mul(model, vec4(a_tangent, 0.0)).xyz;
	v_bitangent = mul(model, vec4(a_bitangent, 0.0)).xyz;
	v_worldPos = worldPos.xyz;
	v_viewPos = mul(u_view, worldPos);
	vec2 uv = a_texcoord0 + instanceUv;
	v_texcoord0 = uv;
	// Affine mapping (u_eraParams.y): uv * w and w, so uv / w after interpolation is screen-linear.
	v_affine = vec3(uv * gl_Position.w, gl_Position.w);
}
