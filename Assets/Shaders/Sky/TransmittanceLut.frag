$input v_texcoord0

// Atmosphere transmittance from any altitude towards the top of the atmosphere (see Atmosphere.sh).
#include "../Common.sh"
#include "Atmosphere.sh"

void main()
{
	float r;
	float mu;
	skyTransmittanceRMu(v_texcoord0, r, mu);
	vec3 origin = vec3(0.0, r, 0.0);
	vec3 dir = vec3(sqrt(max(1.0 - mu * mu, 0.0)), mu, 0.0);
	float distance = skyRaySphere(origin, dir, SKY_TOP_RADIUS);
	vec3 opticalDepth = vec3_splat(0.0);
	const int steps = 40;
	float dt = max(distance, 0.0) / float(steps);
	for (int i = 0; i < steps; ++i)
	{
		vec3 rayleigh;
		vec3 mie;
		vec3 extinction;
		skyMedium(length(origin + dir * ((float(i) + 0.5) * dt)) - SKY_BOTTOM_RADIUS, rayleigh, mie, extinction);
		opticalDepth += extinction * dt;
	}
	gl_FragColor = vec4(exp(-opticalDepth), 1.0);
}
