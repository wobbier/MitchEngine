#ifndef SSAO_COMMON_SH
#define SSAO_COMMON_SH

uniform vec4 u_ssao;           // x world radius, y intensity, z horizon bias, w UV -> NDC Y sign
uniform vec4 u_ssaoProjection; // xy projection scale, z 1 = orthographic, w 1 = depth range -1..1
uniform vec4 u_ssaoScreen;     // xy full-resolution depth texel size, zw AO target size
uniform mat4 u_ssaoInvProjection;

// All three passes must associate an AO texel with exactly the same full-resolution depth texel,
// including odd viewport sizes. Never interpolate the depth or the AO before rejecting edges.
vec2 aoDepthUv(vec2 _uv)
{
	vec2 texel = u_ssaoScreen.xy;
	return clamp((floor(_uv / texel) + 0.5) * texel, texel * 0.5, 1.0 - texel * 0.5);
}

vec3 aoViewPosition(vec2 _uv, float _rawDepth)
{
	vec2 ndc = vec2(_uv.x * 2.0 - 1.0, (_uv.y * 2.0 - 1.0) * u_ssao.w);
	float z = u_ssaoProjection.w > 0.5 ? _rawDepth * 2.0 - 1.0 : _rawDepth;
	vec4 p = mul(u_ssaoInvProjection, vec4(ndc, z, 1.0));
	return p.xyz / p.w;
}

// A world-space tolerance with a pixel-footprint floor, so filtering behaves consistently as
// the camera's near/far planes, projection and distance change.
float aoDepthWeight(float _centerZ, float _sampleZ)
{
	float viewScale = u_ssaoProjection.z > 0.5 ? 1.0 : abs(_centerZ);
	vec2 footprint = 2.0 * u_ssaoScreen.xy * viewScale / abs(u_ssaoProjection.xy);
	float sigma = max(u_ssao.x * 0.1, max(footprint.x, footprint.y) * 2.0);
	float delta = (_sampleZ - _centerZ) / max(sigma, 1e-5);
	return exp2(-delta * delta);
}

#endif
