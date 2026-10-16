$input v_texcoord0

// Horizon-based ambient occlusion from full-resolution depth, evaluated at half resolution.
#include "../Common.sh"
#include "PostCommon.sh"
#include "SSAOCommon.sh"

SAMPLER2D(s_input0, 0); // camera depth (point sampled)

vec3 viewPosition(vec2 _uv)
{
	_uv = aoDepthUv(_uv);
	return aoViewPosition(_uv, texture2DLod(s_input0, _uv, 0.0).r);
}

void main()
{
	vec2 halfUv = (floor(v_texcoord0 * u_ssaoScreen.zw) + 0.5) / u_ssaoScreen.zw;
	vec2 uv = aoDepthUv(halfUv);
	float rawDepth = texture2DLod(s_input0, uv, 0.0).r;
	if (rawDepth >= 1.0)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}
	vec3 p = aoViewPosition(uv, rawDepth);
	// Reconstruct at full-resolution spacing to retain small contacts and thin geometry. Select
	// the flatter side independently in each axis, without choosing a zero step at screen borders.
	vec2 dx = vec2(u_ssaoScreen.x, 0.0);
	vec2 dy = vec2(0.0, u_ssaoScreen.y);
	vec3 left = p - viewPosition(uv - dx);
	vec3 right = viewPosition(uv + dx) - p;
	vec3 below = p - viewPosition(uv - dy);
	vec3 above = viewPosition(uv + dy) - p;
	vec3 stepX = abs(left.z) < abs(right.z) ? left : right;
	vec3 stepY = abs(below.z) < abs(above.z) ? below : above;
	if (dot(stepX, stepX) < 1e-16) stepX = left + right;
	if (dot(stepY, stepY) < 1e-16) stepY = below + above;
	vec3 n = cross(stepY, stepX);
	n *= inversesqrt(max(dot(n, n), 1e-20));
	vec3 toCamera = u_ssaoProjection.z > 0.5 ? vec3(0.0, 0.0, -1.0) : -p;
	if (dot(n, toCamera) < 0.0) n = -n;

	float viewScale = u_ssaoProjection.z > 0.5 ? 1.0 : max(abs(p.z), 0.01);
	// Project the radius in both axes: a world-space circle stays circular on wide viewports.
	vec2 radiusUv = 0.5 * u_ssao.x * abs(u_ssaoProjection.xy) / viewScale;
	vec2 radiusPixels = radiusUv * u_ssaoScreen.zw;
	float maxRadius = max(radiusPixels.x, radiusPixels.y);
	// Limit the near-camera sampling footprint without changing the projected circle's aspect ratio.
	radiusUv *= min(1.0, 64.0 / max(maxRadius, 1e-5));

	const int kDirections = 8;
	const int kSteps = 4;
	float noise = interleavedGradientNoise(gl_FragCoord.xy);
	float angle = noise * 6.2831853;
	float jitter = fract(noise * 17.0 + 0.5);
	float radiusSquared = u_ssao.x * u_ssao.x;
	float occlusion = 0.0;
	for (int direction = 0; direction < kDirections; ++direction)
	{
		float a = angle + float(direction) * (6.2831853 / float(kDirections));
		vec2 ray = vec2(cos(a), sin(a));
		float horizon = u_ssao.z;
		for (int step = 0; step < kSteps; ++step)
		{
			// Quadratic spacing spends more samples near the contact. Start one full-resolution
			// pixel away to avoid repeatedly sampling the centre for small projected radii.
			float t = (float(step) + 0.5 + 0.5 * jitter) / float(kSteps);
			vec2 sampleUv = uv + ray * (u_ssaoScreen.xy + radiusUv * t * t);
			if (any(lessThan(sampleUv, vec2_splat(0.0))) || any(greaterThanEqual(sampleUv, vec2_splat(1.0)))) continue;
			sampleUv = aoDepthUv(sampleUv);
			float sampleDepth = texture2DLod(s_input0, sampleUv, 0.0).r;
			if (sampleDepth >= 1.0) continue;
			vec3 v = aoViewPosition(sampleUv, sampleDepth) - p;
			float distanceSquared = dot(v, v);
			if (distanceSquared < 1e-10 || distanceSquared >= radiusSquared) continue;
			float elevation = dot(n, v) * inversesqrt(distanceSquared);
			float falloff = 1.0 - distanceSquared / radiusSquared;
			// Only newly hidden angles contribute: several samples on the same occluder must
			// not count it repeatedly. Normalised angles also make AO independent of scene scale.
			occlusion += max(elevation - horizon, 0.0) * falloff;
			horizon = max(horizon, elevation);
		}
	}
	occlusion /= float(kDirections) * (1.0 - u_ssao.z);
	// Fade out sub-pixel radii, where the depth buffer cannot resolve the requested geometry.
	float visibility = 1.0 - clamp(occlusion, 0.0, 1.0) * clamp(maxRadius, 0.0, 1.0);
	gl_FragColor = vec4_splat(visibility);
}
