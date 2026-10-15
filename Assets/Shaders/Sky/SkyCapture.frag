$input v_texcoord0

// The procedural sky for an environment probe face (EnvironmentLighting): the atmosphere and the
// clouds, without the sun and moon discs (they light the scene analytically) or the stars.
#include "../Common.sh"
#include "../Env/EnvCommon.sh"
#include "Atmosphere.sh"
#include "Clouds.sh"

void main()
{
	vec3 dir = currentFaceDirection(gl_FragCoord.xy);
	vec3 color = skyViewLuminance(dir) + skyNightGlow(dir);
	if (u_skyCamera.w > 0.5)
	{
		vec3 origin = vec3(u_skyCamera.x, SKY_BOTTOM_RADIUS + u_skyCamera.y, u_skyCamera.z);
		vec4 clouds = cloudMarch(origin, dir, 0.5);
		color = color * clouds.a + clouds.rgb;
	}
	gl_FragColor = vec4(color, 1.0);
}
