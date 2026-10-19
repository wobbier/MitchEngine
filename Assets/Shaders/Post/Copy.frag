$input v_texcoord0

// Copy the current post-process graph input. Resolving the target generates its filtered mips.
#include "../Common.sh"
SAMPLER2D(s_input0, 0);
uniform vec4 u_graphSceneFilter; // x optional bright-pass threshold (negative: unfiltered colour)

void main()
{
	vec3 color = texture2DLod(s_input0, v_texcoord0, 0.0).rgb;
	if (u_graphSceneFilter.x >= 0.0)
	{
		float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
		float threshold = u_graphSceneFilter.x;
		float knee = max(threshold * 0.5, 0.0001);
		float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
		soft = soft * soft / (4.0 * knee);
		color *= max(brightness - threshold, soft) / max(brightness, 0.0001);
	}
	gl_FragColor = vec4(color, 1.0);
}
