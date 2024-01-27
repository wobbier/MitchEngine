$input a_position, a_color0, a_texcoord0
$output gl_Position, v_color0, v_texcoord0

#include "Common.sh"

uniform vec4 State;
uniform vec4 u_testUniform;
uniform mat4 Transform;
uniform vec4 Scalar4[2];
uniform vec4 Vector[8];
uniform vec4 ClipSize;
uniform mat4 Clip[8];
// mains
void main()
{
	gl_Position = mul(Transform, vec4(a_position.x, a_position.y, 0.0, 1.0));
	v_color0 = a_color0;
	v_texcoord0 = a_texcoord0;
}
