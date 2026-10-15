$input v_texcoord0

// Renders the environment seen from the camera's background into one cube face: a flat colour or an
// equirectangular panorama. The procedural sky captures itself (Sky/SkyCapture.frag).
#include "../Common.sh"
#include "EnvCommon.sh"

SAMPLER2D(s_envPanorama, 0);

uniform vec4 u_envSource;       // x mode (0 colour, 1 panorama)
uniform vec4 u_envColor;        // rgb linear background colour

void main()
{
	vec3 dir = currentFaceDirection(gl_FragCoord.xy);
	vec3 color;
	if (u_envSource.x > 0.5)
	{
		vec2 uv = vec2(atan2(dir.z, dir.x) / (2.0 * ENV_PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / ENV_PI);
		color = toLinear(texture2DLod(s_envPanorama, uv, 0.0).rgb);
	}
	else
	{
		// A flat background colour, slightly darker below the horizon.
		color = u_envColor.rgb * mix(0.5, 1.0, smoothstep(-0.3, 0.3, dir.y));
	}
	gl_FragColor = vec4(color, 1.0);
}
