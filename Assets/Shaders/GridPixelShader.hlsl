struct PixelShaderInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 tex : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    // Red   B93549 - 185, 53,  73
    // Green 6FA413 - 111, 164, 19
    // Blue  2B82DF - 43,  130, 223
    float depth;

    depth = input.pos.z / input.pos.w;
    depth *= 10;

    if (input.tex.x > 0.495f && input.tex.x < 0.505f)
    {
        if (frac(input.tex.x / 0.01f) < 0.01f)
        {
            return float4((43.0f / 255.0f), (130.0f / 255.0f), (223.0f / 255.0f), depth);
        }
    }

    if (input.tex.y > 0.495f && input.tex.y < 0.505f)
    {
        if (frac(input.tex.y / 0.01f) < 0.01f)
        {
            return float4((185.0f / 255.0f), (53.0f / 255.0f), (73.0f / 255.0f), depth);
        }
    }

    if (frac(input.tex.x / 0.01f) < 0.01f || frac(input.tex.y / 0.01f) < 0.01f)
    {
        float lightGrey = (250.0f / 255.0f); //(82.0f / 255.0f);
        return float4(lightGrey, lightGrey, lightGrey, depth);
    }

    float darkGrey = (57.0f / 255.0f);
    return float4(darkGrey, darkGrey, darkGrey, 0.0f);
}
