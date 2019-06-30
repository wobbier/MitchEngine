// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak

// Tone-mapping & gamma correction.

static const float Gamma     = 2.2;
static const float Exposure  = 1.0;
static const float PureWhite = 1.0;

struct PixelShaderInput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

Texture2D SceneColor: register(t0);
SamplerState DefaultSampler : register(s0);

PixelShaderInput main_vs(uint vertexID : SV_VertexID)
{
	PixelShaderInput vout;

	if(vertexID == 0) {
		vout.TexCoord = float2(1.0, -1.0);
		vout.Position = float4(1.0, 3.0, 0.0, 1.0);
	}
	else if(vertexID == 1) {
		vout.TexCoord = float2(-1.0, 1.0);
		vout.Position = float4(-3.0, -1.0, 0.0, 1.0);
	}
	else /* if(vertexID == 2) */ {
		vout.TexCoord = float2(1.0, 1.0);
		vout.Position = float4(1.0, -1.0, 0.0, 1.0);
	}
	return vout;
}

float4 main_ps(PixelShaderInput pin) : SV_Target
{
	float3 color = SceneColor.Sample(DefaultSampler, pin.TexCoord).rgb * Exposure;
	
	// Reinhard tonemapping operator.
	// see: "Photographic Tone Reproduction for Digital Images", eq. 4
	float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
	float mappedLuminance = (luminance * (1.0 + luminance/(PureWhite*PureWhite))) / (1.0 + luminance);

	// Scale color by ratio of average luminances.
	float3 mappedColor = (mappedLuminance / luminance) * color;

	// Gamma correction.
	return float4(pow(mappedColor, 1.0/Gamma), 1.0);
}
