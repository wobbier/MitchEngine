$input v_texcoord0

// Diffuse irradiance: cosine-weighted average of the environment around the normal (E / pi), so
// Lambert shading is just albedo * this.
#include "../Common.sh"
#include "EnvCommon.sh"

SAMPLERCUBE(s_envSource, 0);

void main()
{
	vec3 N = currentFaceDirection(gl_FragCoord.xy);
	vec3 t;
	vec3 b;
	tangentFrame(N, t, b);
	float count = u_envFace.w;
	vec3 sum = vec3_splat(0.0);
	for (int i = 0; i < 512; ++i)
	{
		if (float(i) >= count)
		{
			break;
		}
		vec2 xi = hammersley(float(i), count);
		float phi = 2.0 * ENV_PI * xi.x;
		float cosTheta = sqrt(1.0 - xi.y);
		float sinTheta = sqrt(xi.y);
		vec3 L = t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + N * cosTheta;
		sum += envCubeLod(s_envSource, L, 0.0).rgb;
	}
	gl_FragColor = vec4(sum / max(count, 1.0), 1.0);
}
