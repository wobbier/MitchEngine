$input v_texcoord0

// Overdraw view: every shaded fragment adds one layer of heat (additively blended, no depth test).
// Paired with ShadowDepth.vert / ShadowDepthSkinned.vert; alpha-tested holes don't count.
#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texAlpha, 2);

uniform vec4 u_shadowAlpha; // x alpha cutoff (0 = opaque), yz uv tiling
uniform vec4 u_shadowUvOffset; // xy uv offset, added after the tiling (the material's Offset)

void main()
{
	if (u_shadowAlpha.x > 0.0)
	{
		vec2 uv = v_texcoord0 * u_shadowAlpha.yz + u_shadowUvOffset.xy;
		float alpha = texture2D(s_texDiffuse, uv).a * texture2D(s_texAlpha, uv).r;
		if (alpha < u_shadowAlpha.x)
		{
			discard;
		}
	}
	// Channels saturate at different layer counts (4 / 10 / 25), so heat runs brown, orange, yellow, white.
	gl_FragColor = vec4(0.25, 0.1, 0.04, 0.0);
}
