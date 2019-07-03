Texture2D ObjTexture : register(t0);
Texture2D ObjNormalTexture : register(t1);
SamplerState ObjSamplerState;

struct PixelShaderInput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

struct Light
{
	float3 dir;
	float4 ambient;
	float4 diffuse;
};

cbuffer LightCommand : register(b0)
{
	Light light;
}

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
	float4 diffuse = ObjTexture.Sample(ObjSamplerState, input.TexCoord);
	float4 normal = ObjNormalTexture.Sample(ObjSamplerState, input.TexCoord);

	float3 finalColor;
	{
		finalColor = diffuse.xyz * light.ambient;
		finalColor += saturate(dot(light.dir, normal.xyz) * light.diffuse * diffuse.xyz);
	}
	
	return float4(finalColor, 1.0f);
}

