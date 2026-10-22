$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0); // full-res HDR
SAMPLER2D(s_dof1, 1); // full-res CoC
SAMPLER2D(s_dof2, 2); // far gather (including background reconstruction at near silhouettes)
SAMPLER2D(s_dof3, 3); // premultiplied near gather
SAMPLER2D(s_dof4, 4); // completed first lens (split diopter only)

void main()
{
	vec4 sharp = texture2D(s_dof0, v_texcoord0);
	float coc = texture2D(s_dof1, v_texcoord0).r;
	if (u_dofLens.w > 0.5)
	{
		sharp = vec4(vec3(max(-coc, 0.0), max(coc, 0.0), 0.0) / max(u_dofLens.z, 1e-4), 1.0);
	}
	else
	{
		vec3 farColour = texture2D(s_dof2, v_texcoord0).rgb;
		vec3 colour = mix(sharp.rgb, farColour, smoothstep(0.5, 1.5, abs(coc)));
		vec4 nearColour = texture2D(s_dof3, v_texcoord0);
		sharp.rgb = colour * (1.0 - nearColour.a) + nearColour.rgb;
	}
	if (u_dofSplit.z > 0.5)
	{
		// Blend completed lens images: signed CoCs can cancel at the seam, and comparing
		// CoCs from different lenses during gathering breaks foreground depth ordering.
		float halfWidth = u_dofSplit.y * 0.5;
		float weight = halfWidth > 0.0
			? smoothstep(u_dofSplit.x - halfWidth, u_dofSplit.x + halfWidth, v_texcoord0.x)
			: step(u_dofSplit.x, v_texcoord0.x);
		if (u_dofSplit.x <= 0.0) weight = 1.0;
		if (u_dofSplit.x >= 1.0) weight = 0.0;
		sharp = mix(texture2D(s_dof4, v_texcoord0), sharp, weight);
	}
	gl_FragColor = sharp;
}
