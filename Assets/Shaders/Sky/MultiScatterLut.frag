$input v_texcoord0

// Multiple scattering (Hillaire 2020, section 5.5): second-order light gathered from 64 directions
// with an isotropic phase, extended to infinitely many orders by the geometric series 1 / (1 - f).
// u the sun's zenith cosine, v the altitude; the result is per unit scattering and illuminance.
#include "../Common.sh"
#include "Atmosphere.sh"

void main()
{
	vec2 unit = saturate((v_texcoord0 - 0.5 / SKY_MULTISCATTER_SIZE) / (1.0 - 1.0 / SKY_MULTISCATTER_SIZE));
	float cosSunZenith = unit.x * 2.0 - 1.0;
	vec3 sunDir = vec3(sqrt(max(1.0 - cosSunZenith * cosSunZenith, 0.0)), cosSunZenith, 0.0);
	vec3 origin = vec3(0.0, SKY_BOTTOM_RADIUS + max(unit.y * (SKY_TOP_RADIUS - SKY_BOTTOM_RADIUS), 0.01), 0.0);
	const float isotropicPhase = 1.0 / (4.0 * SKY_PI);
	const int steps = 20;

	vec3 secondOrder = vec3_splat(0.0);
	vec3 transfer = vec3_splat(0.0);
	for (int i = 0; i < 64; ++i)
	{
		// Uniform directions over the sphere (8 x 8 stratified).
		float theta = 2.0 * SKY_PI * (floor(float(i) / 8.0) + 0.5) / 8.0;
		float cosPhi = 1.0 - 2.0 * (mod(float(i), 8.0) + 0.5) / 8.0;
		float sinPhi = sqrt(max(1.0 - cosPhi * cosPhi, 0.0));
		vec3 dir = vec3(cos(theta) * sinPhi, cosPhi, sin(theta) * sinPhi);

		float tGround = skyRaySphere(origin, dir, SKY_BOTTOM_RADIUS);
		float tMax = tGround > 0.0 ? tGround : skyRaySphere(origin, dir, SKY_TOP_RADIUS);
		float dt = max(tMax, 0.0) / float(steps);
		vec3 throughput = vec3_splat(1.0);
		vec3 luminance = vec3_splat(0.0);
		vec3 fraction = vec3_splat(0.0);
		for (int s = 0; s < steps; ++s)
		{
			vec3 pos = origin + dir * ((float(s) + 0.3) * dt);
			vec3 rayleigh;
			vec3 mie;
			vec3 extinction;
			skyMedium(length(pos) - SKY_BOTTOM_RADIUS, rayleigh, mie, extinction);
			extinction = max(extinction, vec3_splat(1e-7));
			vec3 scattering = rayleigh + mie;
			vec3 stepTransmittance = exp(-extinction * dt);
			vec3 source = skyLightTransmittance(pos, sunDir, 0.0) * scattering * isotropicPhase;
			luminance += throughput * (source - source * stepTransmittance) / extinction;
			fraction += throughput * (scattering - scattering * stepTransmittance) / extinction;
			throughput *= stepTransmittance;
		}
		if (tGround > 0.0)
		{
			// Sunlight bounced off the ground.
			vec3 pos = origin + dir * tGround;
			float ndotl = saturate(dot(normalize(pos), sunDir));
			luminance += throughput * skyLightTransmittance(pos, sunDir, 0.0) * ndotl * u_skyAtmosphere.z / SKY_PI;
		}
		secondOrder += luminance / 64.0;
		transfer += fraction / 64.0;
	}
	gl_FragColor = vec4(secondOrder / max(vec3_splat(1.0) - transfer, vec3_splat(1e-3)), 1.0);
}
