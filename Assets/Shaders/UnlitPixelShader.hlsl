Texture2D ObjTexture;
SamplerState ObjSamplerState;

struct Light
{
	float3 dir;
	float4 ambient;
	float4 diffuse;
};

cbuffer LightCommand
{
	Light light;
}

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float3 normal : NORMAL;
	float2 texcoord : TEXCOORD0;
};

// A pass-through function for the (interpolated) color data.
float4 main(PixelShaderInput input) : SV_TARGET
{
	//return ObjTexture.Sample(ObjSamplerState, input.texcoord);
	//return float4(input.color, 1.0f);

	return ObjTexture.Sample(ObjSamplerState, input.texcoord);
}
