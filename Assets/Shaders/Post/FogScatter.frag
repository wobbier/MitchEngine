$input v_texcoord0

// Volumetric fog at half resolution: marches each view ray through the height fog and lights every
// step with the ambient fog colour, the sun through its cascaded shadow (Henyey-Greenstein phase)
// and the cluster's point and spot lights with their shadows. Fog beyond the march distance is added
// analytically. Output: rgb in-scattered light, a transmittance.
#include "../Common.sh"
#include "PostCommon.sh"
#include "../Lighting.sh"
#include "../Fog.sh"

SAMPLER2D(s_input0, 0);   // camera depth

void main()
{
	// Snap to a full-resolution depth texel (half-resolution centres fall between them, see SSAO);
	// the composite looks the depth up the same way.
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	vec2 uv = (floor(v_texcoord0 / fullTexel) + 0.5) * fullTexel;
	float viewZ = fogViewDepth(texture2D(s_input0, uv).r);
	vec3 dir;
	float distance;
	float viewZPerMetre;
	float originViewZ;
	vec3 origin = fogRay(uv, viewZ, dir, distance, viewZPerMetre, originViewZ);

	vec3 sunRadiance = vec3_splat(0.0);
	if (u_lightParams.x > 0.5)
	{
		sunRadiance = u_dirLightColor[0].rgb * henyeyGreenstein(dot(dir, -u_dirLightDirection[0].xyz), u_fogVolume.y) * u_fogVolume.x;
	}
	float isotropic = u_fogVolume.x / (4.0 * PI);

	float marchEnd = min(distance, u_fogVolume.z);
	int steps = int(u_fogVolume.w);
	float stepLength = marchEnd / max(u_fogVolume.w, 1.0);
	float jitter = interleavedGradientNoise(gl_FragCoord.xy);
	vec2 pixel = uv * u_fogScreen.xy;   // full-resolution pixel from the top-left, for the clusters

	vec3 scattered = vec3_splat(0.0);
	float transmittance = 1.0;
	for (int i = 0; i < 64; ++i)
	{
		if (i >= steps)
		{
			break;
		}
		float t = (float(i) + jitter) * stepLength;
		vec3 p = origin + dir * t;
		float sigma = t >= u_fogParams.w ? fogDensity(p.y) : 0.0;
		if (sigma > 0.0)
		{
			float stepViewZ = originViewZ + t * viewZPerMetre;
			vec3 light = u_fogColor.rgb + sunRadiance * sunShadowVolume(p, stepViewZ);
			if (u_lightParams.y > 0.5)
			{
				vec2 range = clusterRangeAt(pixel, stepViewZ);
				int count = int(range.y);
				for (int j = 0; j < MAX_LIGHTS_PER_CLUSTER; ++j)
				{
					if (j >= count)
					{
						break;
					}
					light += volumeLocalLight(p, clusterLight(range, j)) * isotropic;
				}
			}
			float stepTransmittance = exp(-sigma * stepLength);
			scattered += transmittance * light * (1.0 - stepTransmittance);
			transmittance *= stepTransmittance;
		}
	}
	vec4 rest = fogAnalytic(origin, dir, marchEnd, distance, sunRadiance);
	gl_FragColor = fogClampOpacity(vec4(scattered + transmittance * rest.rgb, transmittance * rest.a));
}
