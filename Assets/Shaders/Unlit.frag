$input v_color0, v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);

void main()
{
	vec4 color = texture2D(s_texDiffuse, v_texcoord0);
	color.a = 1.0;
	gl_FragColor = color;
}