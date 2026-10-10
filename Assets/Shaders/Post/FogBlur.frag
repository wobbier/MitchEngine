$input v_texcoord0

// Depth-aware 4x4 blur of the raymarched fog (half resolution): hides the per-pixel step jitter.
#include "../Common.sh"
#include "../Fog.sh"

SAMPLER2D(s_input0, 0);   // raymarched fog
SAMPLER2D(s_input1, 1);   // camera depth

float depthAt(vec2 _uv)
{
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	return fogViewDepth(texture2D(s_input1, (floor(_uv / fullTexel) + 0.5) * fullTexel).r);
}

void main()
{
	vec2 t = u_viewTexel.xy;
	float centerDepth = depthAt(v_texcoord0);
	vec4 sum = vec4_splat(0.0);
	float weights = 0.0;
	for (int y = -2; y < 2; ++y)
	{
		for (int x = -2; x < 2; ++x)
		{
			vec2 uv = v_texcoord0 + (vec2(float(x), float(y)) + 0.5) * t;
			float w = 1.0 / (0.02 + abs(depthAt(uv) - centerDepth) / max(centerDepth, 1e-3) * 20.0);
			sum += texture2DLod(s_input0, uv, 0.0) * w;
			weights += w;
		}
	}
	gl_FragColor = sum / max(weights, 1e-5);
}
