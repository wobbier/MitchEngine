Texture2D ObjTexture;
SamplerState ObjSamplerState;

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
	return ObjTexture.Sample(ObjSamplerState, input.texcoord);
	//return float4(input.color, 1.0f);
}
