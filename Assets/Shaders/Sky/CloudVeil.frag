// The sky's clouds (Clouds.frag, accumulated at half resolution) laid over the scene where it lies
// under the cloud layer (ProceduralSkySettings::SceneUnderClouds): blended as scene * a + rgb, the
// way the sky composites them over itself, so ground seen through a gap in the cloud reads as one
// with the sky round it. Toward the horizon the scene gives way to the sky's own ground: the set's
// ground is a few kilometres across, where the sky's is a planet seen through tens of kilometres of
// air, so far off the set's would stand dark and sharp through the haze over the cloud sea.
#include "../Common.sh"
#include "Atmosphere.sh"

uniform vec4 u_skyBasisRight;   // xyz camera right * tan(half horizontal fov)
uniform vec4 u_skyBasisUp;      // xyz camera up * tan(half vertical fov)
uniform vec4 u_skyBasisForward; // xyz camera forward, w 1 = render target origin bottom-left
uniform vec4 u_cloudLayer;      // x base altitude (km)
SAMPLER2D(s_skyClouds, 3);

// Kilometres to the sky's ground over which the set's gives way to it.
#define VEIL_GROUND_REACH 6.0

void main()
{
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec4 clouds = texture2DLod(s_skyClouds, uv, 0.0);
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, u_skyBasisForward.w > 0.5 ? uv.y * 2.0 - 1.0 : 1.0 - uv.y * 2.0);
	vec3 dir = normalize(u_skyBasisRight.xyz * ndc.x + u_skyBasisUp.xyz * ndc.y + u_skyBasisForward.xyz);
	float tGround = skyRaySphere(vec3(0.0, skyCameraRadius(), 0.0), dir, SKY_BOTTOM_RADIUS);
	float reach = tGround > 0.0 ? tGround / VEIL_GROUND_REACH : 100.0;
	// Only once the camera is up at the layer: below it the set's ground is the ground.
	float sky = (1.0 - exp(-reach * reach)) * saturate((u_skyCamera.y - u_cloudLayer.x) / 0.2);
	vec3 skyGround = skyViewLuminance(dir) + skyNightGlow(dir);
	gl_FragColor = vec4(clouds.rgb + skyGround * (sky * clouds.a), clouds.a * (1.0 - sky));
}
