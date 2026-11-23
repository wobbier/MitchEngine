$input v_texcoord0

// Render texture mirror (ME-414): a render texture camera with MirrorMainCamera takes the main
// camera's final image (after post, before overlays and UI) instead of rendering the scene. The
// centre of that image, cropped to the texture's shape, is box-filtered down in linear light: up
// to 4 x 4 bilinear taps per output texel (8 x 8 source texels), then display-encoded again like
// any render texture.
#include "../Common.sh"
SAMPLER2D(s_input0, 0);
uniform vec4 u_mirrorRect;   // xy source uv of the crop's top-left, zw its uv size
uniform vec4 u_mirrorTaps;   // xy one output texel's footprint in source uv, z taps per axis (1-4)

void main()
{
	vec2 centre = u_mirrorRect.xy + v_texcoord0 * u_mirrorRect.zw;
	float taps = clamp(u_mirrorTaps.z, 1.0, 4.0);
	vec3 sum = vec3_splat(0.0);
	for (int y = 0; y < 4; ++y)
	{
		if (float(y) >= taps)
		{
			break;
		}
		for (int x = 0; x < 4; ++x)
		{
			if (float(x) >= taps)
			{
				break;
			}
			vec2 offset = (vec2(float(x), float(y)) + 0.5) / taps - 0.5;
			sum += toLinear(texture2DLod(s_input0, centre + offset * u_mirrorTaps.xy, 0.0).rgb);
		}
	}
	gl_FragColor = vec4(toGamma(sum / (taps * taps)), 1.0);
}
