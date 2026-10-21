$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0); // full-res HDR
SAMPLER2D(s_dof1, 1); // full-res CoC
SAMPLER2D(s_dof2, 2); // far gather (including background reconstruction at near silhouettes)
SAMPLER2D(s_dof3, 3); // premultiplied near gather

void main()
{
	vec4 sharp = texture2D(s_dof0, v_texcoord0);
	float coc = texture2D(s_dof1, v_texcoord0).r;
	if (u_dofLens.w > 0.5)
	{
		gl_FragColor = vec4(max(-coc, 0.0), max(coc, 0.0), 0.0, 1.0) / max(u_dofLens.z, 1e-4);
		gl_FragColor.a = 1.0;
		return;
	}
	vec3 farColour = texture2D(s_dof2, v_texcoord0).rgb;
	vec3 colour = mix(sharp.rgb, farColour, smoothstep(0.5, 1.5, abs(coc)));
	vec4 nearColour = texture2D(s_dof3, v_texcoord0);
	colour = colour * (1.0 - nearColour.a) + nearColour.rgb;
	gl_FragColor = vec4(colour, sharp.a);
}
