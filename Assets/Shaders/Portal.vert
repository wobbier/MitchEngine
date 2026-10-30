$input a_position
$output v_worldPos, v_portalOrigin, v_portalRight, v_portalUp

#include "Common.sh"

void main()
{
    v_worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    // Camera-view surfaces are boxes whose local +Z face is the doorway. Rasterize
    // the volume for near-plane coverage, but depth-test at the doorway itself.
    v_portalOrigin = mul(u_model[0], vec4(0.0, 0.0, 1.0, 1.0)).xyz;
    v_portalRight = u_model[0][0].xyz;
    v_portalUp = u_model[0][1].xyz;
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0) );
}
