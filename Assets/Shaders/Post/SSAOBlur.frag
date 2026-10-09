$input v_texcoord0

// Depth-aware 4x4 blur of the raw SSAO.
#include "../Common.sh"

SAMPLER2D(s_input0, 0); // raw AO
SAMPLER2D(s_input1, 1); // depth

void main()
{
	vec2 t = u_viewTexel.xy;
	float centerDepth = texture2D(s_input1, v_texcoord0).r;
	float sum = 0.0;
	float weights = 0.0;
	for (int y = -2; y < 2; ++y)
	{
		for (int x = -2; x < 2; ++x)
		{
			vec2 uv = v_texcoord0 + (vec2(float(x), float(y)) + 0.5) * t;
			float depth = texture2D(s_input1, uv).r;
			float w = 1.0 / (0.0001 + abs(depth - centerDepth) * 1000.0);
			sum += texture2D(s_input0, uv).r * w;
			weights += w;
		}
	}
	gl_FragColor = vec4_splat(sum / max(weights, 1e-5));
}
