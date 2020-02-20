Texture2D ObjTexture;
SamplerState ObjSamplerState;

// A constant buffer that stores the three basic column-major matrices for composing geometry.
cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix modelInv;
	matrix view;
	matrix projection;
	matrix LightSpaceMatrix;
	float id;
	float padding;
	bool hasNormalMap;
	bool hasAlphaMap;
	bool hasSpecMap;
	float3 DiffuseColor;
};

// Per-vertex data used as input to the vertex shader.
struct VertexShaderInput
{
	float3 pos : POSITION;
	float3 normal : NORMAL;
	float2 texcoord : TEXCOORD0;
	float3 tangent : TANGENT;
};

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float3 normal : NORMAL;
	float2 texcoord : TEXCOORD0;
};

// Simple shader to do vertex processing on the GPU.
PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput output;
	float4 pos = float4(input.pos, 1.0f);

	// Transform the vertex position into projected space.
	pos = mul(model, pos);
	pos = mul(pos, view);
	pos = mul(pos, projection);
	output.pos = pos;

	output.normal = input.normal;

	// Pass the color through without modification.
	output.texcoord = input.texcoord;

	return output;
}
// A pass-through function for the (interpolated) color data.
float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	return ObjTexture.Sample(ObjSamplerState, input.texcoord);
}
