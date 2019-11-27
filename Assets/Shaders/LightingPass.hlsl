Texture2D ObjTexture : register(t0);
Texture2D ObjNormalTexture : register(t1);
Texture2D ObjSpecularMap : register(t2);
Texture2D ObjDepthTexture : register(t3);
Texture2D ObjUITexture : register(t4);
Texture2D ObjPositionTexture : register(t5);
Texture2D ObjShadowTexture : register(t6);
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
	float4 pos;
	float4 dir;
	float4 cameraPos;
	float4 ambient;
	float4 diffuse;
};

cbuffer LightCommand : register(b0)
{
	Light light;
	float2 padding;
	matrix LightSpaceMatrix;
}

PixelShaderInput main_vs(VertexShaderInput input)
{
	PixelShaderInput vout;
    vout.Position = float4(input.Position, 1.0f);
    vout.TexCoord = input.TexCoord;
	return vout;
}

float ShadowCalculation(float4 fragPosLightSpace)
{
	float3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
	projCoords = projCoords * 0.5f + 0.5f;

	float closestDepth = ObjShadowTexture.Sample(ObjSamplerState, projCoords.xy).r;
	float currentDepth = projCoords.z;
	float shadow = currentDepth > closestDepth ? 1.0f : 0.0f;
	return shadow;
}

float4 main_ps(PixelShaderInput input) : SV_TARGET
{
	float4 ui = ObjUITexture.Sample(ObjSamplerState, input.TexCoord);
	float4 color = ObjTexture.Sample(ObjSamplerState, input.TexCoord);
	float4 normal = ObjNormalTexture.Sample(ObjSamplerState, input.TexCoord);
	float4 position = ObjPositionTexture.Sample(ObjSamplerState, input.TexCoord);

	float3 lightColor = light.diffuse.xyz;

	float3 ambient = light.ambient.xyz * color.xyz;
	float3 lightDir = normalize(light.pos.xyz - position.xyz);
	float diff = max(dot(lightDir, lightColor), 0.0f);
	float3 diffuse = diff * lightColor;

	float3 viewDir = normalize(light.cameraPos - position.xyz);
	float spec = 0.0f;

	float3 halfwayDir = normalize(lightDir + viewDir);
	spec = pow(max(dot(normal.xyz, halfwayDir), 0.0f), 64.0f);

	float3 specular = spec * lightColor;
	float4 lightSpaceObject = mul(LightSpaceMatrix, position);
	float shadow = ShadowCalculation(lightSpaceObject);

	float shadowInv = (1.0f - shadow);
	float3 diffuseSpec = diffuse + specular;
	float3 shadowDiffuseSpec = mul(diffuseSpec, shadowInv);
	float3 finalColor = (ambient + (1.0 - shadow) * (diffuse + specular)) * color;// (ambient + shadowDiffuseSpec)* color;
	finalColor += ui.xyz;
	return float4(finalColor, 1.0);
}

