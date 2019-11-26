cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix view;
	matrix projection;
};

struct VertexShaderInput
{
	float3 pos : POSITION;
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

	output.vertpos = pos;

	output.pos = pos;

	return output;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	float4 color;
	float depth;

	depth = input.vertpos.z / input.vertpos.w;

	color = float4(depth, 0.0f, 0.0f, 1.0f);

	return color;
}
