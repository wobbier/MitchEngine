$input v_texcoord0

// Bloom upsample: 3x3 tent filter, blended additively into the next larger mip.
#include "../Common.sh"

SAMPLER2D(s_input0, 0);
uniform vec4 u_bloom; // z filter radius (destination texels)

void main()
{
	vec2 t = u_viewTexel.xy * u_bloom.z;
	vec2 uv = v_texcoord0;
	vec3 color = texture2D(s_input0, uv + vec2(-t.x, -t.y)).rgb;
	color += texture2D(s_input0, uv + vec2(0.0, -t.y)).rgb * 2.0;
	color += texture2D(s_input0, uv + vec2(t.x, -t.y)).rgb;
	color += texture2D(s_input0, uv + vec2(-t.x, 0.0)).rgb * 2.0;
	color += texture2D(s_input0, uv).rgb * 4.0;
	color += texture2D(s_input0, uv + vec2(t.x, 0.0)).rgb * 2.0;
	color += texture2D(s_input0, uv + vec2(-t.x, t.y)).rgb;
	color += texture2D(s_input0, uv + vec2(0.0, t.y)).rgb * 2.0;
	color += texture2D(s_input0, uv + vec2(t.x, t.y)).rgb;
	gl_FragColor = vec4(color / 16.0, 1.0);
}
