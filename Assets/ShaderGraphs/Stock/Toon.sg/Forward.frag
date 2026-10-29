$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos, v_objectPos, v_objectNormal

// Generated from Assets/ShaderGraphs/Stock/Toon.shadergraph by the shader graph compiler: edit the graph, not this file.
#define SG_FOG 1

#include "Common.sh"
#include "Lighting.sh"
#include "Fog.sh"
// Custom Lit (LOOK-S7): the lighting helpers in SGLib use the real lights here.
#define SG_LIGHTING 1
#include "ShaderGraph/SGLib.sh"
#include "Dither.sh"

uniform vec4 u_drawParams; // x screen-door fade (Dither.sh), per draw

uniform vec4 u_sgParams[16];
// u_sgParams[0]: Base Color (Vector 3)
// u_sgParams[1]: Shade Color (Vector 3)
// u_sgParams[2]: Threshold (Float)
// u_sgParams[3]: Smoothness (Float)
// u_sgParams[4]: Rim Color (Vector 3)
// u_sgParams[5]: Rim Power (Float)
// u_sgParams[6]: Rim Strength (Float)


void main()
{
	if (ditherDrops(u_drawParams.x, gl_FragCoord.xy))
	{
		discard;
	}
	vec4 sg_fragCoord = gl_FragCoord;
	bool sg_frontFacing = gl_FrontFacing;
	vec3 sg_PositionWorld = v_worldPos;
	vec3 sg_PositionView = v_viewPos.xyz;
	vec3 sg_NormalWorld = sgSafeNormalize(v_normal);
	vec3 sg_CameraPosition = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 sg_ViewDirectionWorld = sgSafeNormalize(sg_CameraPosition - v_worldPos);
	vec2 sg_ScreenPixel = sg_fragCoord.xy;

	// Add #11
	float n11_Out = u_sgParams[2].x + u_sgParams[3].x;
	// Main Light #10
	vec3 n10_Direction = sgMainLightDirection();
	vec3 n10_Color = sgMainLightColor(sg_PositionWorld);
	float n10_Shadow = sgMainLightShadow(sg_PositionWorld, sg_NormalWorld, sg_PositionView.z);
	float n10_NdotL = dot(sgSafeNormalize(sg_NormalWorld), sgMainLightDirection());
	// Smoothstep #12
	float n12_Out = smoothstep(u_sgParams[2].x, n11_Out, n10_NdotL);
	// Multiply #13
	float n13_Out = n12_Out * n10_Shadow;
	// Lerp #14
	vec3 n14_Out = mix(u_sgParams[1].xyz, vec3(1.0, 1.0, 1.0), vec3_splat(n13_Out));
	// Multiply #15
	vec3 n15_Out = n14_Out * n10_Color;
	// Ambient Light #16
	vec3 n16_Color = sgAmbientLight(sg_NormalWorld);
	// Add #18
	vec3 n18_Out = n15_Out + n16_Color;
	// Additional Lights #17
	vec3 n17_Diffuse = sgAdditionalLights(sg_PositionWorld, sg_NormalWorld, sg_ScreenPixel, sg_PositionView.z, 0.0, 0.0, 1.0);
	vec3 n17_Banded = sgAdditionalLights(sg_PositionWorld, sg_NormalWorld, sg_ScreenPixel, sg_PositionView.z, 1.0, u_sgParams[2].x, u_sgParams[3].x);
	// Add #19
	vec3 n19_Out = n18_Out + n17_Banded;
	// Multiply #20
	vec3 n20_Out = u_sgParams[0].xyz * n19_Out;
	// Fresnel Effect #21
	float n21_Out = pow(1.0 - clamp(dot(sgSafeNormalize(sg_NormalWorld), sgSafeNormalize(sg_ViewDirectionWorld)), 0.0, 1.0), u_sgParams[5].x);
	// Multiply #22
	float n22_Out = n21_Out * u_sgParams[6].x;
	// Multiply #23
	float n23_Out = n22_Out * n13_Out;
	// Multiply #24
	vec3 n24_Out = u_sgParams[4].xyz * vec3_splat(n23_Out);
	// Add #25
	vec3 n25_Out = n20_Out + n24_Out;
	// Outputs
	vec3 sgOut_Color = n25_Out;
	float sgOut_Alpha = 1.0;
	float sgOut_AlphaClipThreshold = 0.5;

#if SG_ALPHA_CLIP
	if (sgOut_Alpha < sgOut_AlphaClipThreshold)
	{
		discard;
	}
#endif

	// The graph's own lighting result (Main Light, Additional Lights and Ambient Light nodes).
	vec3 color = sgOut_Color;
#if SG_FOG
	// Opaque surfaces are fogged by the fog pass; transparent ones (drawn after it) fog themselves.
	vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 sunColor = u_lightParams.x > 0.5 ? directionalRadiance(0, v_worldPos) : vec3_splat(0.0);
	vec4 fog = fogForward(cameraPos, v_worldPos, sunColor, u_dirLightDirection[0].xyz);
	color = color * fog.a + fog.rgb;
#endif
	gl_FragColor = vec4(color, sgOut_Alpha);
}
