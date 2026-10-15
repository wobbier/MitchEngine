$input a_position
$output v_skyDir

// Fullscreen triangle at the far plane (see Moonlight::screenSpaceQuad), carrying each corner's view
// direction: linear across the screen, so it interpolates exactly.
#include "../Common.sh"

uniform vec4 u_skyBasisRight;   // xyz camera right * tan(half horizontal fov)
uniform vec4 u_skyBasisUp;      // xyz camera up * tan(half vertical fov)
uniform vec4 u_skyBasisForward; // xyz camera forward

void main()
{
	vec2 ndc = vec2(a_position.x * 2.0 - 1.0, 1.0 - a_position.y * 2.0);
	gl_Position = vec4(ndc, 1.0, 1.0);
	v_skyDir = u_skyBasisRight.xyz * ndc.x + u_skyBasisUp.xyz * ndc.y + u_skyBasisForward.xyz;
}
