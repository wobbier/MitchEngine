$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0); // half-res HDR + signed CoC
SAMPLER2D(s_dof1, 1); // conservative foreground search radius

vec4 diskTap(vec2 _uv, int _index, float _radius)
{
	float distance = sqrt((float(_index) + 0.5) / 64.0) * _radius;
	float angle = float(_index) * 2.39996323;
	return texture2D(s_dof0, _uv + vec2(cos(angle), sin(angle)) * distance / u_dofScreen.xy);
}

vec3 fillBackground(vec2 _uv, vec4 _centre, float _radius)
{
	// This is extrapolated colour behind an occluder, not a lens blur of those background
	// sources. Averaging sharp HDR points over the foreground disk creates displaced copies
	// and Vogel-pattern fireflies. Estimate the local background, then reject luminance outliers
	// only in this synthetic fill. Real visible highlights retain their original HDR radiance.
	const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
	float logLuminance = 0.0;
	float count = 0.0;
	for (int i = 0; i < 64; ++i)
	{
		vec4 tap = diskTap(_uv, i, _radius);
		// CoC increases with depth: include surfaces behind this occluder even when they
		// are themselves in front of focus. The old sign-only test discarded those layers.
		float w = step(_centre.a + 0.5, tap.a);
		// A linear mean lets the HDR points set their own rejection threshold. Log space
		// estimates the typical background instead, including partially covered point edges.
		logLuminance += log2(max(dot(tap.rgb, luma), 1e-4)) * w;
		count += w;
	}
	if (count < 0.5) return _centre.rgb;
	float average = exp2(logLuminance / count);
	vec3 sum = vec3_splat(0.0);
	float weights = 0.0;
	for (int i = 0; i < 64; ++i)
	{
		vec4 tap = diskTap(_uv, i, _radius);
		// A bounded, redescending weight rejects bright point footprints instead of merely
		// dimming their displaced copies. Normalise to this neighbourhood for exposure invariance;
		// a uniformly bright background keeps its radiance because all its weights are equal.
		float relativeLuma = dot(tap.rgb, luma) / (2.0 * average);
		float robust = saturate(1.0 - relativeLuma * relativeLuma);
		float w = step(_centre.a + 0.5, tap.a) * robust * robust;
		sum += tap.rgb * w;
		weights += w;
	}
	return sum / max(weights, 1e-5);
}

void main()
{
	vec4 centre = texture2D(s_dof0, v_texcoord0);
	bool nearField = u_dofPass.x > 0.5;
	float radius = nearField ? texture2D(s_dof1, v_texcoord0).r : abs(centre.a);
	if (radius < 0.5)
	{
		gl_FragColor = nearField ? vec4_splat(0.0) : vec4(centre.rgb, 1.0);
		return;
	}
	if (!nearField && centre.a < -0.5)
	{
		gl_FragColor = vec4(fillBackground(v_texcoord0, centre, radius), 1.0);
		return;
	}
	vec3 sum = vec3_splat(0.0);
	float weights = 0.0;
	// A fixed Vogel disk is stable without temporal AA and avoids separable square bokeh.
	// Keep this deterministic: changing the pattern every frame after TAA would shimmer.
	for (int i = 0; i < 64; ++i)
	{
		float distance = sqrt((float(i) + 0.5) / 64.0) * radius;
		vec4 tap = diskTap(v_texcoord0, i, radius);
		float w;
		if (nearField)
		{
			float r = max(-tap.a, 0.0);
			// Integrate source disks, including their area: a small foreground disk must not
			// become opaque just because one tap found it. Output premultiplied coverage.
			w = saturate(r - distance + 1.0) * smoothstep(0.5, 1.5, r) / max(r * r, 1.0);
		}
		else
		{
			// A background source may only contribute within its own disk, and never blur
			// across a sharp foreground target. Full-resolution CoC protects its final edge.
			w = saturate(min(tap.a, centre.a) - distance + 1.0) * step(0.5, tap.a);
		}
		sum += tap.rgb * w;
		weights += w;
	}
	vec3 colour = weights > 1e-5 ? sum / weights : centre.rgb;
	float coverage = nearField ? saturate(weights * radius * radius / 64.0) : 1.0;
	gl_FragColor = vec4(colour * coverage, coverage);
}
