$input v_texcoord0

// Average log luminance of the HDR scene into a 1x1 target (16x16 grid of taps).
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_input0, 0);

void main()
{
	float sum = 0.0;
	for (int y = 0; y < 16; ++y)
	{
		for (int x = 0; x < 16; ++x)
		{
			vec2 uv = (vec2(float(x), float(y)) + 0.5) / 16.0;
			sum += log2(max(lumaRec709(texture2D(s_input0, uv).rgb), 0.0001));
		}
	}
	gl_FragColor = vec4_splat(exp2(sum / 256.0));
}
