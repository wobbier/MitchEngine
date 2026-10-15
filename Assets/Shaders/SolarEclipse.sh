#ifndef __SOLAR_ECLIPSE_SH__
#define __SOLAR_ECLIPSE_SH__
// The same angular-disc overlap as ComputeDiscVisibility on the CPU. World-space parallax
// gives the lunar shadow a real umbra and penumbra rather than darkening all receivers equally.
uniform vec4 u_eclipseSun;    // xyz towards the sun at world origin, w angular radius
uniform vec4 u_eclipseMoon;   // xyz towards the moon at world origin, w angular radius
uniform vec4 u_eclipseParams; // x inverse lunar distance (metres), y enabled

float solarVisibility(vec3 _worldPosition)
{
	if (u_eclipseParams.y < 0.5) return 1.0;
	vec3 moonPosition = u_eclipseMoon.xyz - _worldPosition * u_eclipseParams.x;
	float distance = max(length(moonPosition), 1e-5);
	vec3 moonDir = moonPosition / distance;
	float s = max(u_eclipseSun.w, 1e-6);
	float m = u_eclipseMoon.w / distance;
	float d = atan2(length(cross(u_eclipseSun.xyz, moonDir)), dot(u_eclipseSun.xyz, moonDir));
	if (d >= s + m) return 1.0;
	if (d <= abs(s - m)) return saturate(1.0 - m * m / (s * s));
	// Work in solar radii to keep the lens area well-conditioned at small angular sizes.
	m /= s;
	d /= s;
	float a = acos(clamp((d * d + 1.0 - m * m) / (2.0 * d), -1.0, 1.0));
	float b = acos(clamp((d * d + m * m - 1.0) / (2.0 * d * m), -1.0, 1.0));
	float lens = sqrt(max((-d + 1.0 + m) * (d + 1.0 - m) * (d - 1.0 + m) * (d + 1.0 + m), 0.0));
	return saturate(1.0 - (a + m * m * b - 0.5 * lens) / 3.14159265359);
}
#endif
