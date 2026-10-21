$input v_texcoord0
#include "../Common.sh"
#include "DoFCommon.sh"
SAMPLER2D(s_dof0, 0); // HDR
SAMPLER2D(s_dof1, 1); // signed CoC

void main()
{
	vec2 first = floor(v_texcoord0 * u_dofScreen.zw) * 2.0;
	float nearCoC = 0.0;
	float farCoC = 0.0;
	for (int y = 0; y < 2; ++y)
	{
		for (int x = 0; x < 2; ++x)
		{
			float coc = texture2D(s_dof1, dofFullUV(first + vec2(float(x), float(y)))).r;
			nearCoC = min(nearCoC, coc);
			farCoC = max(farCoC, coc);
		}
	}
	// Keep foreground silhouettes in mixed blocks. Colour filtering must use the same layer
	// as CoC, otherwise background highlights contaminate the foreground before gathering.
	float coc = nearCoC < -0.5 ? nearCoC : farCoC;
	vec3 colour = vec3_splat(0.0);
	float weights = 0.0;
	for (int y = 0; y < 2; ++y)
	{
		for (int x = 0; x < 2; ++x)
		{
			vec2 uv = dofFullUV(first + vec2(float(x), float(y)));
			float tapCoC = texture2D(s_dof1, uv).r;
			float w = coc < -0.5 ? step(tapCoC, -0.5) : step(-0.5, tapCoC);
			colour += texture2D(s_dof0, uv).rgb * w;
			weights += w;
		}
	}
	gl_FragColor = vec4(colour / max(weights, 1.0), coc);
}
