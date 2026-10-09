$input v_texcoord0

// Shadow caster: only depth matters; alpha-tested materials cut their holes here as well.
#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texAlpha, 2);

uniform vec4 u_shadowAlpha; // x alpha cutoff (0 = opaque), yz uv tiling

void main()
{
	if (u_shadowAlpha.x > 0.0)
	{
		vec2 uv = v_texcoord0 * u_shadowAlpha.yz;
		float alpha = texture2D(s_texDiffuse, uv).a * texture2D(s_texAlpha, uv).r;
		if (alpha < u_shadowAlpha.x)
		{
			discard;
		}
	}
	gl_FragColor = vec4_splat(0.0);
}
