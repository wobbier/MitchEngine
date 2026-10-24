$input a_position, a_normal, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

// Skinned inverted-hull outline (OutlineHull.vert plus the bone palette).
#include "Common.sh"
#include "Skinning.sh"

uniform vec4 u_outlineHull; // xyz linear colour, w width in metres

void main()
{
	mat4 model = mul(mtxFromCols(i_data0, i_data1, i_data2, i_data3), skinMatrix(a_indices, a_weight));
	vec3 normal = normalize(mul(cofactor(model), a_normal));
	vec4 world = mul(model, vec4(a_position, 1.0));
	world.xyz += normal * u_outlineHull.w;
	gl_Position = mul(u_viewProj, world);
	v_texcoord0 = vec2_splat(0.0);
}
