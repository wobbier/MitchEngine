$input v_texcoord0

// An overlay camera's final image over the main camera's (Camera::Overlay). Where the overlay drew
// opaque geometry (its depth) it replaces the scene; elsewhere its image adds on top, so additive
// and transparent effects over its black background glow over the scene. The coverage is averaged
// over a 3x3 neighbourhood to soften the overlay's silhouette, and fades out with distance between
// the camera's fade start and end.
#include "../Common.sh"
SAMPLER2D(s_input0, 0); // overlay colour (display-encoded)
SAMPLER2D(s_input1, 1); // overlay depth
uniform vec4 u_overlayParams; // xy one depth texel in uv, z OpenGL-style depth, w orthographic
uniform vec4 u_overlayFade;   // near, far, fade start, fade end
uniform vec4 u_overlayLook;   // x the darkest pixels' opacity

// How much of the overlay to keep at a depth sample: 0 where nothing drew, else its distance fade.
float keep(vec2 uv)
{
	float d = texture2DLod(s_input1, uv, 0.0).r;
	if (d >= 0.999999)
	{
		return 0.0;
	}
	float fadeStart = u_overlayFade.z;
	float fadeEnd = u_overlayFade.w;
	if (fadeEnd <= fadeStart)
	{
		return 1.0;
	}
	float n = u_overlayFade.x;
	float f = u_overlayFade.y;
	float z;
	if (u_overlayParams.w > 0.5)
	{
		z = n + d * (f - n);
	}
	else if (u_overlayParams.z > 0.5)
	{
		z = 2.0 * n * f / ((f + n) - (2.0 * d - 1.0) * (f - n));
	}
	else
	{
		z = n * f / (f - d * (f - n));
	}
	return 1.0 - smoothstep(fadeStart, fadeEnd, z);
}

void main()
{
	vec3 color = texture2DLod(s_input0, v_texcoord0, 0.0).rgb;
	vec2 t = u_overlayParams.xy;
	float centre = keep(v_texcoord0);
	float coverage = centre * 4.0
		+ (keep(v_texcoord0 + vec2(t.x, 0.0)) + keep(v_texcoord0 - vec2(t.x, 0.0))
		+ keep(v_texcoord0 + vec2(0.0, t.y)) + keep(v_texcoord0 - vec2(0.0, t.y))) * 2.0
		+ keep(v_texcoord0 + t) + keep(v_texcoord0 - t)
		+ keep(v_texcoord0 + vec2(t.x, -t.y)) + keep(v_texcoord0 + vec2(-t.x, t.y));
	coverage /= 16.0;
	// Dark pixels can let the scene through; bright ones (luma 0.3 and up) cover it.
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float opacity = mix(u_overlayLook.x, 1.0, smoothstep(0.02, 0.3, luma));
	coverage *= opacity;
	// Premultiplied: a faded pixel's colour fades with it. Pixels nothing drew keep their colour
	// (bloom, additive effects) and add over the scene.
	float drawn = texture2DLod(s_input1, v_texcoord0, 0.0).r < 0.999999 ? 1.0 : 0.0;
	float fade = mix(1.0, centre * opacity, drawn);
	gl_FragColor = vec4(color * fade, coverage);
}
