$input v_texcoord0

// Eye adaptation: blends last frame's adapted luminance toward this frame's average.
#include "../Common.sh"

SAMPLER2D(s_input0, 0); // current average luminance (1x1)
SAMPLER2D(s_input1, 1); // previous adapted luminance (1x1)
uniform vec4 u_adapt;   // x blend factor (1 - exp(-dt * speed)), y min luminance, z max luminance

void main()
{
	float current = clamp(texture2D(s_input0, vec2_splat(0.5)).r, u_adapt.y, u_adapt.z);
	float previous = texture2D(s_input1, vec2_splat(0.5)).r;
	float adapted = previous + (current - previous) * u_adapt.x;
	gl_FragColor = vec4_splat(adapted);
}
