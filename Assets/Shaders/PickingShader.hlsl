Texture2D ObjTexture : register(t0);
Texture2D ObjNormalMap : register(t1);
Texture2D ObjAlphaMap : register(t2);
Texture2D ObjSpecularMap : register(t3);
Texture2D ObjPositionMap : register(t4);
SamplerState ObjSamplerState;

cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix view;
	matrix projection;
	float id;
    float3 padding;
};

// Per-vertex data used as input to the vertex shader.
struct VertexShaderInput
{
    float3 pos : POSITION;
};

// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
};

// Simple shader to do vertex processing on the GPU.
PixelShaderInput main_vs(VertexShaderInput input)
{
    PixelShaderInput output;
	float4 pos = float4(input.pos, 1.0f);
	pos = mul(model, pos);
	pos = mul(pos, view);
	pos = mul(pos, projection);
	output.pos = pos;

    output.normal = float3(id, 0.0f, 0.0f);
    return output;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
    //output.color = float4(normalize(input.normal), 1.0f);
	//output.color = float4(1.0f - (input.normal.x / 255.0f), 0.0f, 0.0f, 1.0f);
    return float4(input.normal.x / 255.0f, 0.0f, 0.0f, 1.0f);
}
