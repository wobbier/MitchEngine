$input v_texcoord0, v_color0, v_viewPos, v_worldPos

// Particles in linear HDR. No depth buffer is bound (so the scene depth can be read as a texture):
// the depth test happens here, and particles fade out where they meet geometry (soft particles).
#include "Common.sh"
#include "Lighting.sh"
#include "Fog.sh"

SAMPLER2D(s_texParticle, 0);
SAMPLER2D(s_sceneDepth, 1);

uniform vec4 u_particleParams;   // z softness (view-space distance)
uniform vec4 u_particleParams2;  // y 1 = additive (premultiply), z 1 = lit by the scene's lights
uniform vec4 u_particleDepth;    // x P[2][2], y P[3][2], z orthographic, w depth range -1..1

float sceneViewDepth(vec2 _uv)
{
	float d = texture2DLod(s_sceneDepth, _uv, 0.0).r;
	float ndc = u_particleDepth.w > 0.5 ? d * 2.0 - 1.0 : d;
	if (u_particleDepth.z > 0.5)
	{
		return (ndc - u_particleDepth.y) / u_particleDepth.x;
	}
	return u_particleDepth.y / (ndc - u_particleDepth.x);
}

void main()
{
	vec4 tex = texture2D(s_texParticle, v_texcoord0);
	vec4 color = vec4(toLinear(tex.rgb), tex.a) * v_color0;

	float particleZ = v_viewPos.z;
	float sceneZ = sceneViewDepth(gl_FragCoord.xy * u_viewTexel.xy);
	if (sceneZ <= particleZ)
	{
		discard;
	}
	if (u_particleParams.z > 0.0)
	{
		color.a *= clamp((sceneZ - particleZ) / u_particleParams.z, 0.0, 1.0);
	}
	if (color.a < 0.002)
	{
		discard;
	}
	if (u_particleParams2.z > 0.5)
	{
		// Smoke, dust, steam: the colour is an albedo lit like a volume.
		color.rgb *= volumeLighting(v_worldPos, gl_FragCoord.xy, particleZ);
	}
	// Fog in front of the particle: additive light is dimmed by it, blended particles take its colour.
	vec3 sunColor = u_lightParams.x > 0.5 ? u_dirLightColor[0].rgb : vec3_splat(0.0);
	vec4 fog = fogForward(mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz, v_worldPos, sunColor, u_dirLightDirection[0].xyz);
	color.rgb = u_particleParams2.y > 0.5 ? color.rgb * fog.a : color.rgb * fog.a + fog.rgb;
	gl_FragColor = u_particleParams2.y > 0.5 ? vec4(color.rgb * color.a, color.a) : color;
}
