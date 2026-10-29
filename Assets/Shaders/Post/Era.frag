$input v_texcoord0

// Era image effects (LOOK-S11), on the display image at the render size (before any upscale, so
// their pixels are the art's): a Kuwahara painterly filter, quantizing to a palette, then colour
// depth with ordered (Bayer 4 x 4) dithering.
#include "../Common.sh"
SAMPLER2D(s_input0, 0);   // the display image
SAMPLER2D(s_input1, 1);   // the palette: one row of colours
uniform vec4 u_eraPost;   // x Kuwahara radius (0 = off), y colour bits (0 = full), z dither 0..1, w palette size (0 = off)

#define ERA_MAX_RADIUS 6

float bayer4(vec2 _pixel)
{
	vec2 p = mod(floor(_pixel), 4.0);
	float index = p.x + p.y * 4.0;
	// The 4 x 4 Bayer matrix, row by row.
	float m[16];
	m[0] = 0.0;  m[1] = 8.0;  m[2] = 2.0;  m[3] = 10.0;
	m[4] = 12.0; m[5] = 4.0;  m[6] = 14.0; m[7] = 6.0;
	m[8] = 3.0;  m[9] = 11.0; m[10] = 1.0; m[11] = 9.0;
	m[12] = 15.0; m[13] = 7.0; m[14] = 13.0; m[15] = 5.0;
	float value = 0.0;
	for (int i = 0; i < 16; ++i)
	{
		value = float(i) == index ? m[i] : value;
	}
	return (value + 0.5) / 16.0;
}

vec3 kuwahara(vec2 _uv, int _radius)
{
	vec2 texel = u_viewTexel.xy;
	vec3 bestMean = texture2DLod(s_input0, _uv, 0.0).rgb;
	float bestVariance = 1e9;
	for (int q = 0; q < 4; ++q)
	{
		vec2 dir = vec2(q == 1 || q == 3 ? 1.0 : -1.0, q >= 2 ? 1.0 : -1.0);
		vec3 sum = vec3_splat(0.0);
		vec3 sumSq = vec3_splat(0.0);
		float count = 0.0;
		for (int y = 0; y <= ERA_MAX_RADIUS; ++y)
		{
			for (int x = 0; x <= ERA_MAX_RADIUS; ++x)
			{
				if (x > _radius || y > _radius)
				{
					continue;
				}
				vec3 c = texture2DLod(s_input0, _uv + vec2(float(x), float(y)) * dir * texel, 0.0).rgb;
				sum += c;
				sumSq += c * c;
				count += 1.0;
			}
		}
		vec3 mean = sum / count;
		vec3 variance = sumSq / count - mean * mean;
		float v = variance.r + variance.g + variance.b;
		if (v < bestVariance)
		{
			bestVariance = v;
			bestMean = mean;
		}
	}
	return bestMean;
}

void main()
{
	vec3 color = texture2DLod(s_input0, v_texcoord0, 0.0).rgb;
	int radius = int(u_eraPost.x + 0.5);
	if (radius > 0)
	{
		color = kuwahara(v_texcoord0, min(radius, ERA_MAX_RADIUS));
	}
	float threshold = bayer4(gl_FragCoord.xy) - 0.5;
	if (u_eraPost.w > 0.5)
	{
		// The nearest palette colour, after a dithered nudge.
		vec3 nudged = clamp(color + threshold * u_eraPost.z * 0.25, 0.0, 1.0);
		vec3 best = color;
		float bestDistance = 1e9;
		for (int i = 0; i < 64; ++i)
		{
			if (float(i) >= u_eraPost.w)
			{
				break;
			}
			vec3 entry = texture2DLod(s_input1, vec2((float(i) + 0.5) / u_eraPost.w, 0.5), 0.0).rgb;
			vec3 delta = nudged - entry;
			float distance = dot(delta, delta * vec3(0.3, 0.59, 0.11));
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = entry;
			}
		}
		color = best;
	}
	else if (u_eraPost.y > 0.5)
	{
		float levels = exp2(u_eraPost.y) - 1.0;
		color = floor(clamp(color, 0.0, 1.0) * levels + 0.5 + threshold * u_eraPost.z) / levels;
	}
	gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
