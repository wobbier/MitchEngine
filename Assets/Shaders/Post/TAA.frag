$input v_texcoord0

// Temporal anti-aliasing resolve (HDR, before bloom and tonemapping). Each frame renders with a
// sub-pixel jitter; this pass unjitters the current frame, finds where the pixel was last frame
// (camera motion reprojected through the depth), fetches the history there (Catmull-Rom), clips it
// to the current neighbourhood's colour box (variance clipping in YCoCg) so stale or disoccluded
// history can't ghost, and blends with luma weights so bright samples don't flicker.
#include "../Common.sh"
#include "PostCommon.sh"

SAMPLER2D(s_taaCurrent, 0);
SAMPLER2D(s_taaHistory, 1);
SAMPLER2D(s_taaDepth, 2);

uniform vec4 u_taaParams;      // x history weight, y 1 = history valid, z sharpness, w 1 = depth range -1..1
uniform vec4 u_taaJitter;      // xy this frame's jitter in uv (the current frame is sampled there)
uniform mat4 u_taaReproject;   // unjittered clip space -> last frame's clip space

vec3 rgbToYCoCg(vec3 _rgb)
{
	return vec3(
		 0.25 * _rgb.r + 0.5 * _rgb.g + 0.25 * _rgb.b,
		 0.5  * _rgb.r                - 0.5  * _rgb.b,
		-0.25 * _rgb.r + 0.5 * _rgb.g - 0.25 * _rgb.b);
}

vec3 yCoCgToRgb(vec3 _ycocg)
{
	return vec3(_ycocg.x + _ycocg.y - _ycocg.z, _ycocg.x + _ycocg.z, _ycocg.x - _ycocg.y - _ycocg.z);
}

// Bicubic Catmull-Rom in 5 bilinear taps (Jimenez): sharper history than bilinear.
vec3 sampleHistory(vec2 _uv, vec2 _size)
{
	vec2 samplePos = _uv * _size;
	vec2 texPos1 = floor(samplePos - 0.5) + 0.5;
	vec2 f = samplePos - texPos1;
	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);
	vec2 w12 = w1 + w2;
	vec2 texPos0 = (texPos1 - 1.0) / _size;
	vec2 texPos3 = (texPos1 + 2.0) / _size;
	vec2 texPos12 = (texPos1 + w2 / w12) / _size;

	vec3 result = texture2D(s_taaHistory, vec2(texPos12.x, texPos0.y)).rgb * (w12.x * w0.y);
	result += texture2D(s_taaHistory, vec2(texPos0.x, texPos12.y)).rgb * (w0.x * w12.y);
	result += texture2D(s_taaHistory, vec2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y);
	result += texture2D(s_taaHistory, vec2(texPos3.x, texPos12.y)).rgb * (w3.x * w12.y);
	result += texture2D(s_taaHistory, vec2(texPos12.x, texPos3.y)).rgb * (w12.x * w3.y);
	float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return max(result / weight, vec3_splat(0.0));
}

// Pulls _point toward the box centre until it's inside (clipping, not per-channel clamping, so
// the colour's hue is kept).
vec3 clipToBox(vec3 _point, vec3 _boxMin, vec3 _boxMax)
{
	vec3 centre = 0.5 * (_boxMax + _boxMin);
	vec3 extents = 0.5 * (_boxMax - _boxMin) + 1e-4;
	vec3 offset = _point - centre;
	vec3 units = abs(offset / extents);
	float furthest = max(units.x, max(units.y, units.z));
	return furthest > 1.0 ? centre + offset / furthest : _point;
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 texel = u_viewTexel.xy;
	vec2 currentUv = uv + u_taaJitter.xy;

	// The current frame's 3x3 neighbourhood: its mean and spread bound what the history may be.
	vec3 centre = rgbToYCoCg(texture2D(s_taaCurrent, currentUv).rgb);
	vec3 sum = centre;
	vec3 sumSquares = centre * centre;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			if (x == 0 && y == 0)
			{
				continue;
			}
			vec3 neighbour = rgbToYCoCg(texture2D(s_taaCurrent, currentUv + vec2(float(x), float(y)) * texel).rgb);
			sum += neighbour;
			sumSquares += neighbour * neighbour;
		}
	}
	vec3 mean = sum / 9.0;
	vec3 deviation = sqrt(max(sumSquares / 9.0 - mean * mean, vec3_splat(0.0)));

	// Sharpen the current sample a little: the resolve averages sub-pixel positions.
	vec3 current = centre + (centre - mean) * u_taaParams.z;
	current.x = max(current.x, 0.0);

	// Where this pixel was last frame.
	float depth = texture2D(s_taaDepth, currentUv).r;
	float ndcZ = u_taaParams.w > 0.5 ? depth * 2.0 - 1.0 : depth;
	vec4 previous = mul(u_taaReproject, vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, ndcZ, 1.0));
	vec2 previousUv = vec2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
	bool offScreen = any(lessThan(previousUv, vec2_splat(0.0))) || any(greaterThan(previousUv, vec2_splat(1.0)));

	if (u_taaParams.y < 0.5 || offScreen || previous.w <= 0.0)
	{
		gl_FragColor = vec4(yCoCgToRgb(current), 1.0);
		return;
	}

	vec3 history = rgbToYCoCg(sampleHistory(previousUv, 1.0 / texel));
	history = clipToBox(history, mean - deviation, mean + deviation);

	// Luma-weighted blend (Karis): fireflies don't dominate the accumulation.
	float currentWeight = (1.0 - u_taaParams.x) / (1.0 + current.x);
	float historyWeight = u_taaParams.x / (1.0 + history.x);
	vec3 resolved = (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
	gl_FragColor = vec4(max(yCoCgToRgb(resolved), vec3_splat(0.0)), 1.0);
}
