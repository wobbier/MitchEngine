
//Grid VS
cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
    matrix model;
    matrix view;
    matrix projection;
};

struct VertexShaderInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float4 pos2 : POSITION;
    float3 normal : NORMAL;
    float2 tex : TEXCOORD0;
};

PixelShaderInput main(VertexShaderInput input)
{
    PixelShaderInput output;
    float4 pos = float4(input.pos, 1.0f);

    pos = mul(pos, model);
    pos = mul(pos, view);
    pos = mul(pos, projection);
    output.pos = pos;
    output.pos2 = pos;

    output.normal = input.normal;

    output.tex = input.texcoord;

    return output;
}
