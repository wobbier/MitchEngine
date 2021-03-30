$input v_color0, v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texCube, 0);
uniform vec4 s_diffuse;

void main()
{
	vec4 color = toLinear(texture2D(s_texCube, v_texcoord0) * s_diffuse);
	
	gl_FragColor = color;
}