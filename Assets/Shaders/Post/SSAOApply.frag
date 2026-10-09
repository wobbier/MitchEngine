$input v_texcoord0

// Multiplies the HDR scene by the blurred AO (multiply blend state does the multiplication).
#include "../Common.sh"

SAMPLER2D(s_input0, 0);

void main()
{
	float ao = texture2D(s_input0, v_texcoord0).r;
	gl_FragColor = vec4(ao, ao, ao, 1.0);
}
