$input v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texUI, 0);

void main()
{
	vec4 src = toLinear(texture2D(s_texUI, v_texcoord0));
	
	float gamma = 1.0 / 2.2;
	vec3 finalColor = pow(src.rgb, vec3(gamma, gamma, gamma));
	gl_FragColor = vec4(finalColor.x, finalColor.y, finalColor.z, src.a);
}