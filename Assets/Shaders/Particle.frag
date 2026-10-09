$input v_texcoord0, v_color0, v_viewPos

// Particles in linear HDR. No depth buffer is bound (so the scene depth can be read as a texture):
// the depth test happens here, and particles fade out where they meet geometry (soft particles).
#include "Common.sh"

SAMPLER2D(s_texParticle, 0);
SAMPLER2D(s_sceneDepth, 1);

uniform vec4 u_particleParams;   // z softness (view-space distance)
uniform vec4 u_particleParams2;  // y 1 = additive (premultiply)
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
	gl_FragColor = u_particleParams2.y > 0.5 ? vec4(color.rgb * color.a, color.a) : color;
}
