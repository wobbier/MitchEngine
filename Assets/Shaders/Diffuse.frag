$input v_color0, v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texAlpha, 1);
uniform vec4 s_diffuse;

void main()
{
	vec4 color = toLinear(texture2D(s_texDiffuse, v_texcoord0) * s_diffuse);
	color.a = toLinear(texture2D(s_texAlpha, v_texcoord0)).r;
	gl_FragColor = color;
}