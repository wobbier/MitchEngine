$input v_texcoord0

// A sprite's id only where its texture isn't (nearly) clear, so clicks go through a coin's
// transparent corners to whatever is behind (ME-278).
#include "../Common.sh"

SAMPLER2D(s_texSprite, 0);
uniform vec4 u_id;

void main()
{
	if (texture2D(s_texSprite, v_texcoord0).a < 0.5)
	{
		discard;
	}
	gl_FragColor = vec4(u_id.xyz, 1.0);
}
