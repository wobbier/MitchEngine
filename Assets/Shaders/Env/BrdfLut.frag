$input v_texcoord0

// Split-sum environment BRDF (Karis 2013): x = NoV, y = perceptual roughness -> (scale, bias) on F0.
// Uses the same height-correlated Smith visibility as the direct lighting.
#include "../Common.sh"
#include "EnvCommon.sh"

float smithVisibility(float _NoV, float _NoL, float _a)
{
	float a2 = _a * _a;
	float ggxL = _NoV * sqrt((-_NoL * a2 + _NoL) * _NoL + a2);
	float ggxV = _NoL * sqrt((-_NoV * a2 + _NoV) * _NoV + a2);
	return 0.5 / (ggxV + ggxL + 1e-7);
}

void main()
{
	vec2 st = gl_FragCoord.xy * u_viewTexel.xy;
	float NoV = max(st.x, 1e-3);
	float roughness = st.y;
	float a = max(roughness * roughness, 1e-3);
	vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
	vec3 N = vec3(0.0, 0.0, 1.0);
	float A = 0.0;
	float B = 0.0;
	const float count = 256.0;
	for (int i = 0; i < 256; ++i)
	{
		vec3 H = importanceSampleGGX(hammersley(float(i), count), N, a);
		vec3 L = 2.0 * dot(V, H) * H - V;
		float NoL = clamp(L.z, 0.0, 1.0);
		float NoH = clamp(H.z, 0.0, 1.0);
		float VoH = clamp(dot(V, H), 0.0, 1.0);
		if (NoL > 0.0)
		{
			// pdf = D * NoH / (4 VoH); BRDF * NoL / pdf = 4 * Vis * NoL * VoH / NoH (the F and D cancel).
			float Gv = 4.0 * smithVisibility(NoV, NoL, a) * NoL * VoH / max(NoH, 1e-4);
			float Fc = pow(1.0 - VoH, 5.0);
			A += (1.0 - Fc) * Gv;
			B += Fc * Gv;
		}
	}
	gl_FragColor = vec4(A / count, B / count, 0.0, 1.0);
}
