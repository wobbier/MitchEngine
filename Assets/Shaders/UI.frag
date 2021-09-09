$input v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texUI, 0);

void main()
{
	vec4 src = toLinear(texture2D(s_texUI, v_texcoord0));
	
	vec3 finalColor = pow(src, 1.0 / 2.2);
	gl_FragColor = vec4(finalColor.x, finalColor.y, finalColor.z, src.a);
}