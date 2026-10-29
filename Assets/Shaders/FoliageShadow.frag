$input v_texcoord0

// MitchTree shadow caster: leaves cut their holes with the diffuse alpha (FoliageMaterial binds it).
#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);

uniform vec4 u_shadowAlpha; // x alpha cutoff (0 = opaque), yz uv tiling
uniform vec4 u_shadowUvOffset; // xy uv offset, added after the tiling (the material's Offset)

void main()
{
	if (u_shadowAlpha.x > 0.0)
	{
		if (texture2D(s_texDiffuse, v_texcoord0 * u_shadowAlpha.yz + u_shadowUvOffset.xy).a < u_shadowAlpha.x)
		{
			discard;
		}
	}
	gl_FragColor = vec4_splat(0.0);
}
