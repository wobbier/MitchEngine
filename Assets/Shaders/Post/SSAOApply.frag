$input v_texcoord0

// Joint bilateral upsample; multiply blending applies AO to the opaque HDR scene.
#include "../Common.sh"
#include "SSAOCommon.sh"

SAMPLER2D(s_input0, 0); // filtered half-resolution AO (point sampled)
SAMPLER2D(s_input1, 1); // full-resolution camera depth (point sampled)

void main()
{
	vec2 uv = aoDepthUv(v_texcoord0);
	float rawDepth = texture2DLod(s_input1, uv, 0.0).r;
	if (rawDepth >= 1.0)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}
	float centerZ = aoViewPosition(uv, rawDepth).z;
	vec2 size = u_ssaoScreen.zw;
	vec2 coord = v_texcoord0 * size - 0.5;
	vec2 base = floor(coord);
	vec2 f = coord - base;
	float sum = 0.0;
	float weights = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		vec2 offset = vec2(mod(float(i), 2.0), floor(float(i) * 0.5));
		vec2 tap = clamp(base + offset, vec2_splat(0.0), size - 1.0);
		vec2 tapUv = (tap + 0.5) / size;
		vec2 depthUv = aoDepthUv(tapUv);
		float depth = texture2DLod(s_input1, depthUv, 0.0).r;
		if (depth >= 1.0) continue;
		float z = aoViewPosition(depthUv, depth).z;
		float bilinear = mix(1.0 - f.x, f.x, offset.x) * mix(1.0 - f.y, f.y, offset.y);
		float w = bilinear * aoDepthWeight(centerZ, z);
		sum += texture2DLod(s_input0, tapUv, 0.0).r * w;
		weights += w;
	}
	// If a thin surface has no matching half-resolution sample, leave it unoccluded instead of
	// borrowing AO from the background. Apply artistic intensity after filtering.
	float ao = (sum + 1e-4) / (weights + 1e-4);
	ao = pow(clamp(ao, 1e-4, 1.0), u_ssao.y);
	gl_FragColor = vec4(ao, ao, ao, 1.0);
}
