$input v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texUI, 0);

void main()
{
	vec4 color = toLinear(texture2D(s_texUI, v_texcoord0));
	
	gl_FragColor = color;
}