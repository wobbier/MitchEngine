$input v_texcoord0

// Accumulates the raymarched fog over frames (half resolution): this frame's jittered march is
// blended into last frame's result, reprojected through the opaque depth, so the march's noise
// converges instead of being blurred away. A 3x3 neighbourhood clamp bounds what the history may
// contribute, so moving lights and disocclusions trail for a few frames rather than ghosting.
#include "../Common.sh"
#include "../Fog.sh"

SAMPLER2D(s_input0, 0);       // this frame's raymarched fog
SAMPLER2D(s_input1, 1);       // camera depth
SAMPLER2D(s_fogHistory, 2);   // last frame's resolve (linear)
uniform mat4 u_fogReproject;  // unjittered clip space now -> last frame's

vec4 fogTexel(vec2 _texel, vec2 _halfSize)
{
	return texture2DLod(s_input0, (_texel + 0.5) / _halfSize, 0.0);
}

void main()
{
	vec2 halfSize = u_fogScreen.zw;
	vec2 centre = floor(v_texcoord0 * halfSize);
	vec4 current = fogTexel(centre, halfSize);
	if (u_fogVolume2.z < 0.5)
	{
		gl_FragColor = current;
		return;
	}

	// The neighbourhood box of the raw march, widened by half its extent: per-frame jitter makes
	// neighbours differ a lot, and a tight box would reject the history it is meant to converge.
	vec4 boxMin = current;
	vec4 boxMax = current;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec4 tap = fogTexel(clamp(centre + vec2(float(x), float(y)), vec2_splat(0.0), halfSize - 1.0), halfSize);
			boxMin = min(boxMin, tap);
			boxMax = max(boxMax, tap);
		}
	}
	vec4 extent = (boxMax - boxMin) * 0.5;
	boxMin -= extent;
	boxMax += extent;

	// Where this pixel's surface was last frame: the same full-resolution depth texel the march
	// snapped to. The sky reprojects at the far plane, which is right for camera rotation.
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	vec2 uv = (floor(((centre + 0.5) / halfSize) / fullTexel) + 0.5) * fullTexel;
	float raw = texture2D(s_input1, uv).r;
	float ndcZ = u_fogProjection.w > 0.5 ? raw * 2.0 - 1.0 : raw;
	vec4 previous = mul(u_fogReproject, vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, ndcZ, 1.0));
	if (previous.w <= 0.0)
	{
		gl_FragColor = current;
		return;
	}
	vec2 previousUv = vec2(previous.x / previous.w * 0.5 + 0.5, 0.5 - previous.y / previous.w * 0.5);
	if (any(lessThan(previousUv, vec2_splat(0.0))) || any(greaterThan(previousUv, vec2_splat(1.0))))
	{
		gl_FragColor = current;
		return;
	}
	vec4 history = clamp(texture2DLod(s_fogHistory, previousUv, 0.0), boxMin, boxMax);
	gl_FragColor = mix(current, history, u_fogVolume2.y);
}
