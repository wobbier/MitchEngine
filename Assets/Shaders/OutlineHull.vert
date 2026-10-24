$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0

// Inverted-hull outline: the mesh inflated along its normals (u_outlineHull.w metres), drawn with
// the opposite cull so only a silhouette ring survives the scene's depth. Instanced model matrix.
#include "Common.sh"

uniform vec4 u_outlineHull; // xyz linear colour, w width in metres

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec3 normal = normalize(mul(cofactor(model), a_normal));
	vec4 world = mul(model, vec4(a_position, 1.0));
	world.xyz += normal * u_outlineHull.w;
	gl_Position = mul(u_viewProj, world);
	v_texcoord0 = vec2_splat(0.0);
}
