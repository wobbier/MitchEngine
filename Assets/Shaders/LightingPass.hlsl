Texture2D ObjTexture : register(t0);
Texture2D ObjNormalTexture : register(t1);
Texture2D ObjSpecularMap : register(t2);
Texture2D ObjDepthTexture : register(t3);
Texture2D ObjUITexture : register(t4);
SamplerState ObjSamplerState;

struct PixelShaderInput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

struct VertexShaderInput
{
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
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

PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput vout;
    vout.Position = float4(input.Position, 1.0f);
    vout.TexCoord = input.TexCoord;
	return vout;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
    float4 ui = ObjUITexture.Sample(ObjSamplerState, input.TexCoord);
    if (ui.a == 1.0f)
    {
        return ui;
    }
    float4 diffuse = ObjTexture.Sample(ObjSamplerState, input.TexCoord);
    float4 normal = ObjNormalTexture.Sample(ObjSamplerState, input.TexCoord);

    float3 finalColor;

    finalColor = diffuse * light.ambient;
    finalColor += saturate(dot(light.dir, normalize(normal.xyz)) * light.diffuse * diffuse);

    finalColor = saturate(finalColor + ui.xyz);
    
	return float4(finalColor, 1.0);
}

