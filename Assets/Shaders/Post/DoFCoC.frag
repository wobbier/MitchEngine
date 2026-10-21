$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0);

void main()
{
	float raw = texture2D(s_dof0, v_texcoord0 + u_dofJitter.xy).r;
	float ndc = u_dofProjection.z > 0.5 ? raw * 2.0 - 1.0 : raw;
	// MitchEngine's projection uses positive view-space Z on all backends.
	float depth = raw >= 0.99999 ? u_dofProjection.w : u_dofProjection.y / (ndc - u_dofProjection.x);
	float coc = clamp(u_dofLens.y * (1.0 - u_dofLens.x / max(depth, 1e-4)), -u_dofLens.z, u_dofLens.z);
	gl_FragColor = vec4(coc, 0.0, 0.0, 1.0);
}
