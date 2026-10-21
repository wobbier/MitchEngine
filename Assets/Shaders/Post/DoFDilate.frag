$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0);

void main()
{
	vec2 axis = u_dofPass.x < 0.5 ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
	vec2 pixel = floor(v_texcoord0 * u_dofScreen.zw);
	float radius = 0.0;
	// Every half-resolution texel in the bounded footprint is visited, including thin bars.
	// This is a conservative search radius; the gather applies the actual circular coverage.
	for (int i = -32; i <= 32; ++i)
	{
		float distance = abs(float(i)) * 2.0;
		if (distance > u_dofLens.z + 1.0) continue;
		vec2 uv = (clamp(pixel + axis * float(i), vec2_splat(0.0), u_dofScreen.zw - 1.0) + 0.5) / u_dofScreen.zw;
		vec4 tap = texture2D(s_dof0, uv);
		float r = u_dofPass.x < 0.5 ? max(-tap.a, 0.0) : tap.r;
		if (r + 1.0 >= distance) radius = max(radius, r);
	}
	gl_FragColor = vec4(radius, 0.0, 0.0, 1.0);
}
