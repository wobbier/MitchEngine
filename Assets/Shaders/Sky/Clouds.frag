$input v_texcoord0

// Volumetric clouds for the camera at half resolution (see Clouds.sh), accumulated over frames:
// each frame marches a sub-pixel jittered ray with jittered samples and blends it into last frame's
// result, reprojected as if the clouds were infinitely far (they are kilometres away).
#include "../Common.sh"
#include "Atmosphere.sh"
#include "Clouds.sh"
#include "../Post/PostCommon.sh"

uniform vec4 u_skyBasisRight;       // xyz camera right * tan(half horizontal fov)
uniform vec4 u_skyBasisUp;          // xyz camera up * tan(half vertical fov)
uniform vec4 u_skyBasisForward;     // xyz camera forward, w 1 = render target origin bottom-left
uniform vec4 u_cloudHistoryRight;   // xyz last frame's right / tan(half horizontal fov), w 1 = history valid
uniform vec4 u_cloudHistoryUp;      // xyz last frame's up / tan(half vertical fov), w weight of the new frame
uniform vec4 u_cloudHistoryForward; // xyz last frame's forward
uniform vec4 u_cloudFrame;          // x frame index, yz sub-pixel offset (texels)
SAMPLER2D(s_cloudHistory, 3);

vec2 cloudUvToNdc(vec2 _uv)
{
	return vec2(_uv.x * 2.0 - 1.0, u_skyBasisForward.w > 0.5 ? _uv.y * 2.0 - 1.0 : 1.0 - _uv.y * 2.0);
}

vec2 cloudNdcToUv(vec2 _ndc)
{
	return vec2(_ndc.x * 0.5 + 0.5, u_skyBasisForward.w > 0.5 ? _ndc.y * 0.5 + 0.5 : 0.5 - _ndc.y * 0.5);
}

vec3 cloudViewDirection(vec2 _uv)
{
	vec2 ndc = cloudUvToNdc(_uv);
	return normalize(u_skyBasisRight.xyz * ndc.x + u_skyBasisUp.xyz * ndc.y + u_skyBasisForward.xyz);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec3 origin = vec3(u_skyCamera.x, SKY_BOTTOM_RADIUS + u_skyCamera.y, u_skyCamera.z);
	vec3 dir = cloudViewDirection(uv + u_cloudFrame.yz * u_viewTexel.xy);
	float jitter = fract(interleavedGradientNoise(gl_FragCoord.xy) + u_cloudFrame.x * 0.618034);
	vec4 clouds = cloudMarch(origin, dir, jitter);

	if (u_cloudHistoryRight.w > 0.5)
	{
		vec3 centre = cloudViewDirection(uv);
		vec3 previous = vec3(dot(centre, u_cloudHistoryRight.xyz), dot(centre, u_cloudHistoryUp.xyz), dot(centre, u_cloudHistoryForward.xyz));
		if (previous.z > 1e-4)
		{
			vec2 historyUv = cloudNdcToUv(previous.xy / previous.z);
			if (historyUv.x >= 0.0 && historyUv.y >= 0.0 && historyUv.x <= 1.0 && historyUv.y <= 1.0)
			{
				clouds = mix(texture2DLod(s_cloudHistory, historyUv, 0.0), clouds, u_cloudHistoryUp.w);
			}
		}
	}
	gl_FragColor = clouds;
}
