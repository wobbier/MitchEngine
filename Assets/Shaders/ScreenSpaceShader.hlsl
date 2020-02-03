Texture2D ObjTexture : register(t0);
Texture2D ObjNormalMap : register(t1);
Texture2D ObjAlphaMap : register(t2);
Texture2D ObjSpecularMap : register(t3);
Texture2D ObjPositionMap : register(t4);
SamplerState ObjSamplerState;

cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix modelInv;
	matrix view;
	matrix projection;
	float2 tiling;
	bool hasNormalMap;
	bool hasAlphaMap;
	bool hasSpecMap;
	float3 DiffuseColor;
	float2 ViewportSize;
	float2 padding;
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
	float3 tangent : TANGENT;
	float4 fragPos : POSITION;
};

struct PSOUTPUT
{
	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
	float4 spec : SV_TARGET2;
	float4 position : SV_TARGET3;
};

// Simple shader to do vertex processing on the GPU.
PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput output;
	float4 pos = float4(input.pos, 1.0f);
	// Transform the vertex position into projected space.
	output.fragPos = mul(model, pos);


	pos = mul(model, pos);
	pos = mul(pos, view);
	pos = mul(pos, projection);
	output.pos = pos;


	//output.normal = mul(input.normal, model);
	output.normal = mul(input.normal, transpose(modelInv));
	output.tangent = mul(input.tangent, transpose(model));

	output.texcoord = input.texcoord;
	// Pass the color through without modification.
	return output;
}

PSOUTPUT main_ps(PixelShaderInput input)
{
	PSOUTPUT output;

	output.normal = float4(normalize(input.normal), 1.0f);

	output.position = input.fragPos;


	//The correct Screen Space Texture Coordinates.
	float2 TexCoord = input.texcoord;// / ViewportSize;

	float4 diffuse = ObjTexture.Sample(ObjSamplerState, TexCoord);
	if (!any(diffuse))
	{
		diffuse = float4(1.0f, 1.0f, 1.0f, 1.0f);
	}
	float diffuseAlpha = diffuse.a;

	output.color = diffuse;
	output.color = float4(saturate(output.color.xyz * DiffuseColor), diffuseAlpha);


	if (hasAlphaMap)
	{
		diffuseAlpha = ObjAlphaMap.Sample(ObjSamplerState, TexCoord).r;
	}
	output.color.a = diffuseAlpha;

	float4 normalMap = float4(input.normal, 1.0f);
	output.normal = normalMap;
	if (hasNormalMap)
	{
		normalMap = ObjNormalMap.Sample(ObjSamplerState, TexCoord);
		input.tangent = normalize(input.tangent - dot(input.tangent, input.normal) * input.normal);

		float3 biTangent = cross(input.tangent, input.normal);
		normalMap = normalize((normalMap * 2.0f) - 1.0f);

		float3x3 texSpace = float3x3(input.tangent, biTangent, input.normal);

		output.normal = float4(normalize(mul(normalMap.xyz, texSpace)), 1.0);
	}

	output.spec = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (hasSpecMap)
	{
		output.spec = ObjSpecularMap.Sample(ObjSamplerState, TexCoord);
	}
	return output;
}
