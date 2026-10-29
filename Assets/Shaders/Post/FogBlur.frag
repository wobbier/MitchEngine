$input v_texcoord0

// Depth-aware blur of the raymarched fog (half resolution): hides the per-pixel step jitter. 5x5
// for a single frame's march, 3x3 (u_fogVolume2.w) once the temporal history has converged.
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
	vec2 halfSize = u_fogScreen.zw;
	vec2 center = floor(v_texcoord0 * halfSize);
	float centerDepth = depthAt((center + 0.5) / halfSize);
	vec4 sum = vec4_splat(0.0);
	float weights = 0.0;
	for (int y = -2; y <= 2; ++y)
	{
		for (int x = -2; x <= 2; ++x)
		{
			if (abs(float(x)) > u_fogVolume2.w || abs(float(y)) > u_fogVolume2.w)
			{
				continue;
			}
			// Each fog value must use the depth of the ray that produced it. Sampling between
			// texels blends across silhouettes before the depth weight can reject the other side.
			vec2 tap = clamp(center + vec2(float(x), float(y)), vec2_splat(0.0), halfSize - 1.0);
			vec2 uv = (tap + 0.5) / halfSize;
			float spatial = (3.0 - abs(float(x))) * (3.0 - abs(float(y)));
			float w = spatial / (0.02 + abs(depthAt(uv) - centerDepth) / max(centerDepth, 1e-3) * 20.0);
			sum += texture2DLod(s_input0, uv, 0.0) * w;
			weights += w;
		}
	}
	gl_FragColor = sum / max(weights, 1e-5);
}
