$input v_texcoord0

// Screen-space ambient occlusion (Alchemy / McGuire 2011) from the camera depth at half resolution.
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_input0, 0);         // camera depth
uniform vec4 u_ssao;            // x world radius, y intensity, z bias, w projection scale (P[1][1])
uniform vec4 u_ssaoProjection;  // x P[2][2], y P[3][2], z 1 = orthographic, w 1 = depth range -1..1
uniform vec4 u_ssaoProjection2; // x P[0][0], y P[1][1]

float viewDepth(vec2 _uv)
{
	float d = texture2D(s_input0, _uv).r;
	float ndc = u_ssaoProjection.w > 0.5 ? d * 2.0 - 1.0 : d;
	if (u_ssaoProjection.z > 0.5)
	{
		return (ndc - u_ssaoProjection.y) / u_ssaoProjection.x;
	}
	return u_ssaoProjection.y / (ndc - u_ssaoProjection.x);
}

vec3 viewPosition(vec2 _uv)
{
	float z = viewDepth(_uv);
	vec2 ndc = vec2(_uv.x * 2.0 - 1.0, 1.0 - _uv.y * 2.0);
	return vec3(ndc.x * z / u_ssaoProjection2.x, ndc.y * z / u_ssaoProjection2.y, z);
}

void main()
{
	vec2 uv = v_texcoord0;
	float rawDepth = texture2D(s_input0, uv).r;
	if (rawDepth >= 0.99999)
	{
		gl_FragColor = vec4_splat(1.0);   // sky
		return;
	}
	vec3 p = viewPosition(uv);
	// Normal from the neighbour on the flatter side in each axis (avoids smearing across edges).
	vec2 dx = vec2(u_viewTexel.x, 0.0);
	vec2 dy = vec2(0.0, u_viewTexel.y);
	vec3 pl = viewPosition(uv - dx);
	vec3 pr = viewPosition(uv + dx);
	vec3 pd = viewPosition(uv - dy);
	vec3 pu = viewPosition(uv + dy);
	vec3 ddx = abs(pr.z - p.z) < abs(p.z - pl.z) ? pr - p : p - pl;
	vec3 ddy = abs(pu.z - p.z) < abs(p.z - pd.z) ? pu - p : p - pd;
	vec3 n = normalize(cross(ddy, ddx));
	if (dot(n, p) > 0.0)
	{
		n = -n;
	}

	const int kSamples = 12;
	float screenRadius = u_ssao.x * u_ssao.w / max(p.z, 0.01) * 0.5;
	screenRadius = min(screenRadius, 0.15);
	float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
	float occlusion = 0.0;
	for (int i = 0; i < kSamples; ++i)
	{
		float t = (float(i) + 0.5) / float(kSamples);
		float a = angle + t * 6.2831853 * 3.0;
		vec2 offset = vec2(cos(a), sin(a)) * (t * screenRadius);
		vec3 q = viewPosition(uv + offset);
		vec3 v = q - p;
		float vv = dot(v, v);
		float vn = dot(v, n);
		float falloff = max(0.0, 1.0 - vv / (u_ssao.x * u_ssao.x));
		// The bias scales with the radius and distance so flat surfaces don't self-occlude.
		occlusion += max(0.0, vn - (0.03 * u_ssao.x + u_ssao.z * p.z)) / (vv + 0.0001) * falloff;
	}
	occlusion = max(0.0, 1.0 - 2.0 * u_ssao.y * occlusion / float(kSamples));
	gl_FragColor = vec4_splat(occlusion);
}
