$input a_position, a_normal, a_texcoord0, a_tangent, a_bitangent, a_color0, i_data0, i_data1, i_data2, i_data3
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_foliage

// MitchTree bark and leaves (instanced): Standard.vert plus wind from the mesh's wind stream.
#include "Common.sh"
#include "Wind.sh"

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	// Inverse-transpose via the cofactor matrix (correct under non-uniform scale).
	mat3 normalMatrix = cofactor(model);
	v_normal = mul(normalMatrix, a_normal);
	vec3 worldPos = mul(model, vec4(a_position, 1.0)).xyz;
	worldPos = applyWind(worldPos, a_position, model, a_color0, normalize(v_normal));
	gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));

	v_tangent = mul(model, vec4(a_tangent, 0.0)).xyz;
	v_bitangent = mul(model, vec4(a_bitangent, 0.0)).xyz;
	v_worldPos = worldPos;
	v_viewPos = mul(u_view, vec4(worldPos, 1.0));
	v_texcoord0 = a_texcoord0;
	// x: baked crown occlusion; y: this branch's tint variation (-1..1), different per tree.
	vec3 origin = mul(model, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float variation = fract(sin(a_color0.g * 91.7 + dot(origin, vec3(12.9898, 0.0, 78.233))) * 43758.5453);
	v_foliage = vec2(a_color0.a, variation * 2.0 - 1.0);
}
