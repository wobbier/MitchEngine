$input v_color0, v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texAlpha, 1);
uniform vec4 s_diffuse;
uniform vec4 s_tiling;

void main()
{
	vec2 uvs = v_texcoord0 * s_tiling.xy;
	vec4 color = toLinear(texture2D(s_texDiffuse, uvs) * s_diffuse);
	color.a = toLinear(texture2D(s_texAlpha, uvs)).r;
	gl_FragColor = color;
}