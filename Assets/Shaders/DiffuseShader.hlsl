Texture2D ObjTexture : register(t0);
Texture2D ObjNormalMap : register(t1);
Texture2D ObjAlphaMap : register(t2);
SamplerState ObjSamplerState;

cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix view;
	matrix projection;
	float2 padding;
	bool hasNormalMap;
	bool hasAlphaMap;
};
struct Light
{
	float3 dir;
	float4 ambient;
	float4 diffuse;
};

cbuffer LightCommand : register(b1)
{
	Light light;
}

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
	float3 tangent : TANGENT;
};

struct PSOUTPUT
{
	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
};

// Simple shader to do vertex processing on the GPU.
PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput output;
	float4 pos = float4(input.pos, 1.0f);

	// Transform the vertex position into projected space.
	pos = mul(pos, model);
	pos = mul(pos, view);
	pos = mul(pos, projection);
	output.pos = pos;

	output.normal = input.normal;

	output.tangent = input.tangent;
	// Pass the color through without modification.
	output.texcoord = input.texcoord;
	return output;
}

PSOUTPUT main_ps(PixelShaderInput input)
{
	PSOUTPUT output;
	output.normal = float4(normalize(input.normal), 1.0f);

	float4 diffuse = ObjTexture.Sample(ObjSamplerState, input.texcoord);
	output.color = diffuse;
	if(hasNormalMap)
	{

		float4 normalMap = ObjNormalMap.Sample(ObjSamplerState, input.texcoord);
		//output.normal = normalMap;
		normalMap = (2.0f * normalMap) - 1.0f;

		input.tangent = normalize(input.tangent - dot(input.tangent, input.normal) * input.normal);

		float3 biTangent = cross(input.normal, input.tangent);

		float3x3 texSpace = float3x3(input.tangent, biTangent, input.normal);

		input.normal = normalize(mul(normalMap, texSpace));
	}
	output.normal = float4(input.normal.xyz, 1.0f);


	//float3 finalColor;

	//finalColor = diffuse * light.ambient;
	//finalColor += saturate(dot(light.dir, input.normal) * light.diffuse * diffuse);

	float diffuseAlpha = diffuse.a;
	if (hasAlphaMap)
	{
		diffuseAlpha = ObjAlphaMap.Sample(ObjSamplerState, input.texcoord).r;
	}
	output.color.a = diffuseAlpha;
	return output;
	//return float4(finalColor, diffuseAlpha);
}
