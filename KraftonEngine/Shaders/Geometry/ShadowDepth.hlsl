#include "Common/ConstantBuffers.hlsli"

cbuffer ShadowPassBuffer : register(b2)
{
    row_major float4x4 LightVP;
};

struct VSInput
{
    float3 Position : POSITION;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
};

VSOutput VS(VSInput input)
{
    VSOutput output;
    float4 worldPos = mul(float4(input.Position, 1.0f), Model);
    output.Position = mul(worldPos, LightVP);
    return output;
}

float4 PS(VSOutput input) : SV_TARGET
{
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
    float depth = saturate(1.0f - input.Position.z);
    return float4(depth, depth * depth, 0.0, 0.0);
#else
    return float4(0.0, 0.0, 0.0, 0.0);
#endif
}
