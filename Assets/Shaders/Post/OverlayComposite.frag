$input v_texcoord0

// An overlay camera's final image over the main camera's (Camera::Overlay). Where the overlay drew
// opaque geometry (its depth) it replaces the scene; elsewhere its image adds on top, so additive
// and transparent effects over its black background glow over the scene. The coverage is averaged
// over a 3x3 neighbourhood to soften the overlay's silhouette, and fades out with distance between
// the camera's fade start and end, toward the top of the screen (Camera::OverlayTopFade) and as a
// whole (Camera::OverlayOpacity).
#include "../Common.sh"
SAMPLER2D(s_input0, 0); // overlay colour (display-encoded)
SAMPLER2D(s_input1, 1); // overlay depth
uniform vec4 u_overlayParams; // xy one depth texel in uv, z OpenGL-style depth, w orthographic
uniform vec4 u_overlayFade;   // near, far, fade start, fade end
uniform vec4 u_overlayLook;   // x the darkest pixels' opacity, y the whole overlay's, z top fade (share of the height), w 1 when v runs up the screen

// x: how much of the overlay to keep at a depth sample (0 where nothing drew, else its distance
// fade). y: 1 where something drew.
vec2 keep(vec2 uv)
{
	float d = texture2DLod(s_input1, uv, 0.0).r;
	if (d >= 0.999999)
	{
		return vec2(0.0, 0.0);
	}
	float fadeStart = u_overlayFade.z;
	float fadeEnd = u_overlayFade.w;
	if (fadeEnd <= fadeStart)
	{
		return vec2(1.0, 1.0);
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
	return vec2(1.0 - smoothstep(fadeStart, fadeEnd, z), 1.0);
}

void main()
{
	vec3 color = texture2DLod(s_input0, v_texcoord0, 0.0).rgb;
	vec2 t = u_overlayParams.xy;
	vec2 centre = keep(v_texcoord0);
	vec2 e0 = keep(v_texcoord0 + vec2(t.x, 0.0));
	vec2 e1 = keep(v_texcoord0 - vec2(t.x, 0.0));
	vec2 e2 = keep(v_texcoord0 + vec2(0.0, t.y));
	vec2 e3 = keep(v_texcoord0 - vec2(0.0, t.y));
	vec2 c0 = keep(v_texcoord0 + t);
	vec2 c1 = keep(v_texcoord0 - t);
	vec2 c2 = keep(v_texcoord0 + vec2(t.x, -t.y));
	vec2 c3 = keep(v_texcoord0 + vec2(-t.x, t.y));
	float coverage = (centre.x * 4.0 + (e0.x + e1.x + e2.x + e3.x) * 2.0 + c0.x + c1.x + c2.x + c3.x) / 16.0;
	// Dark pixels can let the scene through; bright ones (luma 0.3 and up) cover it.
	float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
	float opacity = mix(u_overlayLook.x, 1.0, smoothstep(0.02, 0.3, luma));
	coverage *= opacity;
	// Premultiplied: a faded pixel's colour fades with it. Pixels nothing drew keep their colour
	// (bloom, additive effects) and add over the scene, except beside the silhouette: there the
	// colour is the geometry's own edge (antialiasing, upscaling: the image is filtered, the depth
	// isn't), and fades with the nearest geometry instead of staying behind as a thin line.
	float nearDrawn = max(max(max(e0.y, e1.y), max(e2.y, e3.y)), max(max(c0.y, c1.y), max(c2.y, c3.y)));
	float nearKeep = max(max(max(e0.x, e1.x), max(e2.x, e3.x)), max(max(c0.x, c1.x), max(c2.x, c3.x)));
	float undrawn = mix(1.0, nearKeep, nearDrawn);
	float fade = mix(undrawn, centre.x * opacity, centre.y);
	// The whole layer: its opacity, and the fade toward the top of the screen.
	float top = u_overlayLook.w > 0.5 ? 1.0 - v_texcoord0.y : v_texcoord0.y;
	float layer = u_overlayLook.y * (u_overlayLook.z > 0.0 ? smoothstep(0.0, u_overlayLook.z, top) : 1.0);
	gl_FragColor = vec4(color * fade * layer, coverage * layer);
}
