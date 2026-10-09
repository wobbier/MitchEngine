$input v_texcoord0

// GGX-prefiltered radiance for one roughness (split-sum, N = V = R). Samples come from a source cube
// already downsampled to suit the roughness, which keeps the result smooth with few samples.
#include "../Common.sh"
#include "EnvCommon.sh"

SAMPLERCUBE(s_envSource, 0);

void main()
{
	vec3 N = currentFaceDirection(gl_FragCoord.xy);
	float roughness = u_envFace.y;
	if (roughness < 0.01)
	{
		gl_FragColor = vec4(envCubeLod(s_envSource, N, 0.0).rgb, 1.0);
		return;
	}
	float a = roughness * roughness;
	float count = u_envFace.w;
	vec3 sum = vec3_splat(0.0);
	float weight = 0.0;
	for (int i = 0; i < 256; ++i)
	{
		if (float(i) >= count)
		{
			break;
		}
		vec3 H = importanceSampleGGX(hammersley(float(i), count), N, a);
		vec3 L = 2.0 * dot(N, H) * H - N;
		float NoL = dot(N, L);
		if (NoL > 0.0)
		{
			sum += envCubeLod(s_envSource, L, 0.0).rgb * NoL;
			weight += NoL;
		}
	}
	gl_FragColor = vec4(sum / max(weight, 1e-4), 1.0);
}
