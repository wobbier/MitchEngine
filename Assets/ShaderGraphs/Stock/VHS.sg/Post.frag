$input v_texcoord0

// Generated from Assets/ShaderGraphs/Stock/VHS.shadergraph by the shader graph compiler: edit the graph, not this file.
// A full-screen pass (with the engine's Post/Fullscreen.vert) on the HDR scene before tonemapping,
// or on the display image after it (the graph's Injection setting).
#define SG_TARGET_POST 1

#include "Common.sh"
#include "ShaderGraph/SGLib.sh"

SAMPLER2D(s_sgSceneColor, 6);
SAMPLER2D(s_sgSceneDepth, 7);
uniform vec4 u_sgPostProjection;   // x P[2][2], y P[3][2], z 1 = orthographic, w 1 = depth range -1..1
uniform vec4 u_sgPostProjection2;  // x P[0][0], y P[1][1], z far distance (sky)
uniform mat4 u_sgPostInvView;      // the camera's view -> world (full-screen views are orthographic)

vec3 sgSceneColor(vec2 _uv)
{
	return texture2DLod(s_sgSceneColor, _uv, 0.0).rgb;
}

// Linear view depth (distance along the view) of the opaque scene under a screen UV.
float sgSceneDepth(vec2 _uv)
{
	float raw = texture2DLod(s_sgSceneDepth, _uv, 0.0).r;
	if (raw >= 0.99999)
	{
		return u_sgPostProjection2.z;
	}
	float ndc = u_sgPostProjection.w > 0.5 ? raw * 2.0 - 1.0 : raw;
	if (u_sgPostProjection.z > 0.5)
	{
		return (ndc - u_sgPostProjection.y) / u_sgPostProjection.x;
	}
	return u_sgPostProjection.y / (ndc - u_sgPostProjection.x);
}

// The view-space position of the scene under a screen UV (top-left origin).
vec3 sgViewPosition(vec2 _uv)
{
	vec2 ndc = vec2(_uv.x * 2.0 - 1.0, 1.0 - _uv.y * 2.0);
	vec2 scale = vec2(1.0 / u_sgPostProjection2.x, 1.0 / u_sgPostProjection2.y);
	float viewZ = sgSceneDepth(_uv);
	return u_sgPostProjection.z > 0.5 ? vec3(ndc * scale, viewZ) : vec3(ndc * scale * viewZ, viewZ);
}

// The scene's view-space normal, from one-pixel depth taps on the flatter side of each axis.
vec3 sgViewNormal(vec2 _uv)
{
	vec3 center = sgViewPosition(_uv);
	vec2 dx = vec2(u_viewTexel.x, 0.0);
	vec2 dy = vec2(0.0, u_viewTexel.y);
	vec3 left = sgViewPosition(_uv - dx);
	vec3 right = sgViewPosition(_uv + dx);
	vec3 up = sgViewPosition(_uv - dy);
	vec3 down = sgViewPosition(_uv + dy);
	vec3 stepX = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
	vec3 stepY = abs(down.z - center.z) < abs(center.z - up.z) ? down - center : center - up;
	vec3 normal = sgSafeNormalize(cross(stepX, stepY));
	return dot(normal, center) > 0.0 ? -normal : normal;
}

uniform vec4 u_sgParams[16];
// u_sgParams[0]: Burst Rate (Float)
// u_sgParams[1]: Burst Chance (Float)
// u_sgParams[2]: Tear (Float)
// u_sgParams[3]: Rows (Float)
// u_sgParams[4]: Color Split (Float)
// u_sgParams[5]: Scanlines (Float)
// u_sgParams[6]: Grain (Float)


void main()
{
	vec4 sg_fragCoord = gl_FragCoord;
	vec2 sg_UV0 = v_texcoord0;
	vec2 sg_ScreenUV = v_texcoord0;
	vec2 sg_ScreenPixel = sg_fragCoord.xy;
	float sg_Time = u_time.y;
	float sg_DeltaTime = u_time.x;

	// Split #10
	vec2 n10_In = sg_UV0;
	// Multiply #22
	float n22_Out = n10_In.y * u_sgParams[3].x;
	// Floor #23
	float n23_Out = floor(n22_Out);
	// Multiply #20
	float n20_Out = sg_Time * 20.0;
	// Floor #21
	float n21_Out = floor(n20_Out);
	// Combine #28
	vec4 n28_RGBA = vec4(n23_Out, n21_Out, 0.0, 1.0);
	// Value Noise #29
	float n29_Out = sgValueNoise(n28_RGBA.xy, 1.0);
	// Subtract #30
	float n30_Out = n29_Out - 0.5;
	// Add #24
	float n24_Out = n23_Out + 31.0;
	// Combine #25
	vec4 n25_RGBA = vec4(n24_Out, n21_Out, 0.0, 1.0);
	// Value Noise #26
	float n26_Out = sgValueNoise(n25_RGBA.xy, 1.0);
	// Step #27
	float n27_Out = step(0.649999976, n26_Out);
	// Multiply #31
	float n31_Out = n30_Out * n27_Out;
	// Multiply #32
	float n32_Out = n31_Out * u_sgParams[2].x;
	// One Minus #16
	float n16_Out = 1.0 - u_sgParams[1].x;
	// Multiply #14
	float n14_Out = sg_Time * u_sgParams[0].x;
	// Floor #15
	float n15_Out = floor(n14_Out);
	// Combine #17
	vec4 n17_RGBA = vec4(n15_Out, 3.0, 0.0, 1.0);
	// Value Noise #18
	float n18_Out = sgValueNoise(n17_RGBA.xy, 1.0);
	// Step #19
	float n19_Out = step(n16_Out, n18_Out);
	// Multiply #33
	float n33_Out = n32_Out * n19_Out;
	// Multiply #34
	float n34_Out = n10_In.y * 90.0;
	// Multiply #35
	float n35_Out = sg_Time * 6.0;
	// Combine #36
	vec4 n36_RGBA = vec4(n34_Out, n35_Out, 0.0, 1.0);
	// Gradient Noise #37
	float n37_Out = sgGradientNoise(n36_RGBA.xy, 1.0);
	// Subtract #38
	float n38_Out = n37_Out - 0.5;
	// Multiply #39
	float n39_Out = n38_Out * 0.00300000003;
	// Add #40
	float n40_Out = n33_Out + n39_Out;
	// Combine #41
	vec4 n41_RGBA = vec4(n40_Out, 0.0, 0.0, 1.0);
	// Add #42
	vec2 n42_Out = sg_UV0 + n41_RGBA.xy;
	// Multiply #43
	float n43_Out = n19_Out * 0.0120000001;
	// Add #44
	float n44_Out = u_sgParams[4].x + n43_Out;
	// Combine #45
	vec4 n45_RGBA = vec4(n44_Out, 0.0, 0.0, 1.0);
	// Add #46
	vec2 n46_Out = n42_Out + n45_RGBA.xy;
	// Scene Color #47
	vec3 n47_Out = sgSceneColor(n46_Out);
	// Split #51
	vec3 n51_In = n47_Out;
	// Scene Color #48
	vec3 n48_Out = sgSceneColor(n42_Out);
	// Split #52
	vec3 n52_In = n48_Out;
	// Subtract #49
	vec2 n49_Out = n42_Out - n45_RGBA.xy;
	// Scene Color #50
	vec3 n50_Out = sgSceneColor(n49_Out);
	// Split #53
	vec3 n53_In = n50_Out;
	// Combine #54
	vec4 n54_RGBA = vec4(n51_In.x, n52_In.y, n53_In.z, 1.0);
	// Saturation #55
	vec3 n55_Out = sgSaturation(n54_RGBA.xyz, 0.850000024);
	// Split #12
	vec2 n12_In = sg_ScreenPixel;
	// Multiply #56
	float n56_Out = n12_In.y * 1.57079995;
	// Sine #57
	float n57_Out = sin(n56_Out);
	// One Minus #58
	float n58_Out = 1.0 - u_sgParams[5].x;
	// Remap #59
	float n59_Out = n58_Out + (n57_Out - (-1.0)) * (1.0 - n58_Out) / (1.0 - (-1.0));
	// Multiply #60
	float n60_Out = sg_Time * 0.109999999;
	// Add #61
	float n61_Out = n10_In.y + n60_Out;
	// Fraction #62
	float n62_Out = fract(n61_Out);
	// Subtract #63
	float n63_Out = n62_Out - 0.5;
	// Absolute #64
	float n64_Out = abs(n63_Out);
	// Smoothstep #65
	float n65_Out = smoothstep(0.0, 0.0599999987, n64_Out);
	// One Minus #66
	float n66_Out = 1.0 - n65_Out;
	// Multiply #67
	float n67_Out = n66_Out * 0.119999997;
	// Add #68
	float n68_Out = n67_Out + 1.0;
	// Multiply #79
	float n79_Out = n59_Out * n68_Out;
	// Multiply #80
	vec3 n80_Out = n55_Out * vec3_splat(n79_Out);
	// Multiply #69
	float n69_Out = n21_Out * 37.0;
	// Add #70
	float n70_Out = n12_In.x + n69_Out;
	// Multiply #71
	float n71_Out = n21_Out * 11.0;
	// Add #72
	float n72_Out = n12_In.y + n71_Out;
	// Combine #73
	vec4 n73_RGBA = vec4(n70_Out, n72_Out, 0.0, 1.0);
	// Value Noise #74
	float n74_Out = sgValueNoise(n73_RGBA.xy, 1.0);
	// Subtract #75
	float n75_Out = n74_Out - 0.5;
	// Multiply #76
	float n76_Out = n19_Out * 0.0799999982;
	// Add #77
	float n77_Out = u_sgParams[6].x + n76_Out;
	// Multiply #78
	float n78_Out = n75_Out * n77_Out;
	// Add #81
	vec3 n81_Out = n80_Out + vec3_splat(n78_Out);
	// Outputs
	vec3 sgOut_Color = n81_Out;

	gl_FragColor = vec4(sgOut_Color, 1.0);
}
