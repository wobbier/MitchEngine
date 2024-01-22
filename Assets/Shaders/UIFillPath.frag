$input v_position, v_color0, v_texcoord0

#include "Common.sh"

uniform vec4 s_diffuse;

void main()
{
	vec3 finalColor = vec3(1.0, 0.0, 0.0);
	gl_FragColor = vec4(finalColor.x, finalColor.y, finalColor.z, 1.0);
}