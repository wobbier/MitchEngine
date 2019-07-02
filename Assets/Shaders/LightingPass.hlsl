Texture2D ObjTexture : register(t0);
SamplerState ObjSamplerState;

struct PixelShaderInput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

PixelShaderInput main_vs(uint vertexID : SV_VertexID)
{
	PixelShaderInput vout;

	if (vertexID == 0) {
		vout.TexCoord = float2(1.0, -1.0);
		vout.Position = float4(1.0, 3.0, 0.0, 1.0);
	}
	else if (vertexID == 1) {
		vout.TexCoord = float2(-1.0, 1.0);
		vout.Position = float4(-3.0, -1.0, 0.0, 1.0);
	}
	else /* if(vertexID == 2) */ {
		vout.TexCoord = float2(1.0, 1.0);
		vout.Position = float4(1.0, -1.0, 0.0, 1.0);
	}

	return vout;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	float4 color = ObjTexture.Sample(ObjSamplerState, input.TexCoord);
	return color;
}
