$input a_position, a_color0, a_color1, a_color2, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6
$output v_color0, v_color1, v_color2, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_texcoord5, v_texcoord6

#include "Common.sh"

uniform vec4 State;
uniform mat4 Transform;
uniform vec4 Scalar4[2];
uniform vec4 Vector[8];
uniform vec4 ClipSize;
uniform mat4 Clip[8];
//main
void main()
{
	gl_Position = mul(Transform, vec4(a_position.x, a_position.y, 0.0, 1.0));
	v_color0 = vec4(a_color0) / 255.0;
	v_color1 = a_color1;
	v_color2 = a_color2;
	v_texcoord0 = a_texcoord0;
	v_texcoord1 = a_texcoord1;
	v_texcoord2 = a_texcoord2;
	v_texcoord3 = a_texcoord3;
	v_texcoord4 = a_texcoord4;
	v_texcoord5 = a_texcoord5;
	v_texcoord6 = a_texcoord6;
}
