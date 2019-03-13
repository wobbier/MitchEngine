struct PixelShaderInput
{
	float4 pos : SV_POSITION;
	float4 vertpos : POSITION;
};

float4 main(PixelShaderInput input) : SV_TARGET
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
