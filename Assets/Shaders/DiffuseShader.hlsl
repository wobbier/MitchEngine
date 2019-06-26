Texture2D ObjTexture;
Texture2D ObjNormalMap;
SamplerState ObjSamplerState;

cbuffer ModelViewProjectionConstantBuffer : register(b0)
{
	matrix model;
	matrix view;
	matrix projection;
	float3 padding;
	bool hasNormalMap;
};
struct Light
{
	float3 dir;
	float4 ambient;
	float4 diffuse;
};

cbuffer LightCommand
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

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	input.normal = normalize(input.normal);

	float4 diffuse = ObjTexture.Sample(ObjSamplerState, input.texcoord);

	{

		float4 normalMap = ObjNormalMap.Sample(ObjSamplerState, input.texcoord);

		normalMap = (2.0f * normalMap) - 1.0f;

		input.tangent = normalize(input.tangent - dot(input.tangent, input.normal) * input.normal);

		float3 biTangent = cross(input.normal, input.tangent);

		float3x3 texSpace = float3x3(input.tangent, biTangent, input.normal);

		input.normal = normalize(mul(normalMap, texSpace));

	}

	float3 finalColor;

	finalColor = diffuse * float3(0.2, 0.2, 0.2);//light.ambient;
	finalColor += saturate(dot(float4(0.25, 0.5, -1.0, 1.0), input.normal) * float4(1.0, 1.0, 1.0, 1.0) * diffuse);
	//finalColor += saturate(dot(light.dir, input.normal) * light.diffuse * diffuse);

	return float4(finalColor, diffuse.a);
}
