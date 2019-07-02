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
	float4 vertpos : POSITION;
};

PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput output;
	float4 pos = float4(input.pos, 1.0f);

	pos = mul(pos, model);
	pos = mul(pos, view);
	pos = mul(pos, projection);
	output.pos = pos;

	output.vertpos = pos;

	return output;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	float4 color;
	float depth;

	depth = input.vertpos.z / input.vertpos.w;

	if (depth <= 0.5)
	{
		color = float4(1.f, 0.f, 0.f, 1.f);
	}
	else
	{
		color = float4(0.f, 1.f, 0.f, 1.f);
	}

	return color;
}
