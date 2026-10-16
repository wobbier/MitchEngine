$input v_texcoord0

// Symmetric bilateral filter of half-resolution AO, using view-space depth in metres.
#include "../Common.sh"
#include "SSAOCommon.sh"

SAMPLER2D(s_input0, 0); // raw AO (point sampled)
SAMPLER2D(s_input1, 1); // camera depth (point sampled)

void main()
{
	vec2 size = u_ssaoScreen.zw;
	vec2 center = floor(v_texcoord0 * size);
	vec2 centerUv = aoDepthUv((center + 0.5) / size);
	float rawDepth = texture2DLod(s_input1, centerUv, 0.0).r;
	if (rawDepth >= 1.0)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}
	float centerZ = aoViewPosition(centerUv, rawDepth).z;
	float sum = 0.0;
	float weights = 0.0;
	for (int y = -2; y <= 2; ++y)
	{
		for (int x = -2; x <= 2; ++x)
		{
			vec2 offset = vec2(float(x), float(y));
			vec2 tap = clamp(center + offset, vec2_splat(0.0), size - 1.0);
			vec2 uv = (tap + 0.5) / size;
			vec2 depthUv = aoDepthUv(uv);
			float depth = texture2DLod(s_input1, depthUv, 0.0).r;
			if (depth >= 1.0) continue;
			float z = aoViewPosition(depthUv, depth).z;
			float w = exp2(-dot(offset, offset) * 0.5) * aoDepthWeight(centerZ, z);
			sum += texture2DLod(s_input0, uv, 0.0).r * w;
			weights += w;
		}
	}
	gl_FragColor = vec4_splat(sum / max(weights, 1e-5));
}
