$input v_texcoord0

// Renders the environment seen from the camera's background into one cube face: a flat colour, an
// equirectangular panorama, or the procedural (Preetham/Perez) sky without the sun disc (the sun is
// lit analytically by the directional light, so it must not be counted twice).
#include "../Common.sh"
#include "EnvCommon.sh"

SAMPLER2D(s_envPanorama, 0);

uniform vec4 u_envSource;       // x mode (0 colour, 1 panorama, 2 procedural sky)
uniform vec4 u_envColor;        // rgb linear background colour
uniform vec4 u_sunDirection;
uniform vec4 u_skyLuminanceXYZ;
uniform vec4 u_parameters;      // z exposition
uniform vec4 u_perezCoeff[5];

vec3 Perez(vec3 A, vec3 B, vec3 C, vec3 D, vec3 E, float costeta, float cosgamma)
{
	float gamma = acos(cosgamma);
	return (vec3_splat(1.0) + A * exp(B / costeta)) * (vec3_splat(1.0) + C * exp(D * gamma) + E * cosgamma * cosgamma);
}

vec3 proceduralSky(vec3 _dir)
{
	vec3 dir = vec3(_dir.x, max(_dir.y, 0.0), _dir.z);
	vec3 lightDir = normalize(u_sunDirection.xyz);
	float costeta = max(dir.y, 0.001);
	float cosgamma = clamp(dot(normalize(dir + vec3(0.0, 1e-4, 0.0)), lightDir), -0.9999, 0.9999);
	float cosgammas = dot(vec3(0.0, 1.0, 0.0), lightDir);
	vec3 P = Perez(u_perezCoeff[0].xyz, u_perezCoeff[1].xyz, u_perezCoeff[2].xyz, u_perezCoeff[3].xyz, u_perezCoeff[4].xyz, costeta, cosgamma);
	vec3 P0 = Perez(u_perezCoeff[0].xyz, u_perezCoeff[1].xyz, u_perezCoeff[2].xyz, u_perezCoeff[3].xyz, u_perezCoeff[4].xyz, 1.0, cosgammas);
	float sum = u_skyLuminanceXYZ.x + u_skyLuminanceXYZ.y + u_skyLuminanceXYZ.z;
	vec3 xyY = vec3(u_skyLuminanceXYZ.x / sum, u_skyLuminanceXYZ.y / sum, u_skyLuminanceXYZ.y);
	vec3 Yp = xyY * P / P0;
	vec3 XYZ = vec3(Yp.x * Yp.z / Yp.y, Yp.z, (1.0 - Yp.x - Yp.y) * Yp.z / Yp.y);
	return max(convertXYZ2RGB(XYZ * u_parameters.z), vec3_splat(0.0));
}

void main()
{
	vec3 dir = currentFaceDirection(gl_FragCoord.xy);
	vec3 color;
	if (u_envSource.x > 1.5)
	{
		vec3 sky = proceduralSky(dir);
		// Below the horizon: a dim ground lit by the sky instead of the mirrored sky the backdrop shows.
		vec3 ground = proceduralSky(normalize(vec3(dir.x, 0.05, dir.z))) * 0.3;
		color = mix(ground, sky, smoothstep(-0.08, 0.02, dir.y));
	}
	else if (u_envSource.x > 0.5)
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
