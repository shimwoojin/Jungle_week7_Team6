#ifndef FORWARD_LIGHTING_HLSLI
#define FORWARD_LIGHTING_HLSLI

#include "Common/ForwardLightData.hlsli"
#include "Common/ConstantBuffers.hlsli"
#include "Common/SystemSamplers.hlsli"

float GetShadowDepthBias(FShadowInfo info)
{
    return max(info.ShadowParams.x, 0.00001f);
}

float GetShadowSlopeBias(FShadowInfo info)
{
    return info.ShadowParams.y;       
}

float GetShadowNearZ(FShadowInfo info)
{
    return max(info.ShadowParams.w, 0.0001f);
}

float ApplyShadowSharpen(float shadow, FShadowInfo info)
{
    float sharpen = saturate(info.ShadowParams.z);
    float contrast = 1.0f + sharpen * 4.0f;
    return saturate((shadow - 0.5f) * contrast + 0.5f);
}

float CalcAttenuation(float dist, float radius, float falloff)
{
    float ratio = saturate(dist / max(radius, 0.0001f));
    return pow(1.0f - ratio, falloff);
}

float3 CalcAmbient(float3 lightColor, float intensity)
{
    return lightColor * intensity;
}

#define SHADOW_METHOD_STANDARD 0
#define SHADOW_METHOD_PSM 1
#define SHADOW_METHOD_CSM 2

float SampleAtlasShadow(FShadowInfo info, float3 worldPos, float4x4 lightVP)
{
    float4 shadowPos;
    if (info.bIsPSM)
    {
        float4 viewPos = mul(float4(worldPos, 1.0f), View);
        float4 cameraNDC = mul(viewPos, Projection);
        cameraNDC.xyz /= cameraNDC.w;
        cameraNDC.w = 1.0f;
        shadowPos = mul(cameraNDC, lightVP);
    }
    else
    {
        shadowPos = mul(float4(worldPos, 1.0f), lightVP);
    }

    if (abs(shadowPos.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 ndc = shadowPos.xyz / shadowPos.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = ndc.z + GetShadowDepthBias(info);

    if (any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
    {
        return 1.0f;
    }

    float2 atlasMin = info.SampleData.xy;
    float2 atlasMax = info.SampleData.zw;
    float2 atlasUV = lerp(atlasMin, atlasMax, uv);

    return gShadowAtlasArray.SampleCmpLevelZero(
        ShadowCmpSampler,
        float3(atlasUV, info.ArrayIndex),
        depth);
}

float ReduceLightBleed(float probability)
{
    const float bleedReduction = 0.2f;
    return saturate((probability - bleedReduction) / (1.0f - bleedReduction));
}

float SampleAtlasShadowVSM(FShadowInfo info, float3 worldPos)
{
    float4 lightClip = mul(float4(worldPos, 1.0f), info.LightVP);
    if (abs(lightClip.w) < 1e-5f)
    {
        return 1.0f;
    }

    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float depth = (1.0f - ndc.z) + GetShadowDepthBias(info);

    if (any(uv < 0.0f) || any(uv > 1.0f) || depth < 0.0f || depth > 1.0f)
    {
        return 1.0f;
    }

    float2 atlasMin = info.SampleData.xy;
    float2 atlasMax = info.SampleData.zw;
    float2 atlasUV = lerp(atlasMin, atlasMax, uv);
    float2 moments = gShadowAtlasArray.SampleLevel(
        LinearClampSampler,
        float3(atlasUV, info.ArrayIndex),
        0.0f).xy;

    if (depth <= moments.x)
    {
        return 1.0f;
    }

    float variance = max(moments.y - moments.x * moments.x, 0.00002f);
    float delta = depth - moments.x;
    float probability = variance / (variance + delta * delta);
    return ReduceLightBleed(probability);
}

float SampleCubeShadow(FShadowInfo info, float3 worldPos)
{
    float3 lightPos = info.SampleData.xyz;
    float nearZ = GetShadowNearZ(info);
    float farZ = max(info.SampleData.w, nearZ + 0.0001f);

    float3 toPixel = worldPos - lightPos;
    float3 absToPixel = abs(toPixel);
    float faceDepth = max(max(absToPixel.x, absToPixel.y), absToPixel.z);
    if (faceDepth < nearZ || faceDepth > farZ)
    {
        return 1.0f;
    }

    float3 dir = toPixel / max(faceDepth, 0.0001f);
    float depth = (nearZ * (farZ / faceDepth - 1.0f) / (farZ - nearZ)) + GetShadowDepthBias(info);

    return gShadowCubeArray.SampleCmpLevelZero(
        ShadowCmpSampler,
        float4(dir, info.ArrayIndex),
        depth);
}

float GetDirectionalShadow(float3 worldPos)
{
    if (DirectionalLight.ShadowIndex < 0)
    {
        return 1.0f;
    }

    float shadow = 1.0f;
    if (ShadowMethod == SHADOW_METHOD_CSM)
    {
        float4 viewPos = mul(float4(worldPos, 1.0f), View);
        float depth = abs(viewPos.z);
        
        int cascadeIdx = 0;
        if (depth > CascadeSplits.x) cascadeIdx = 1;
        if (depth > CascadeSplits.y) cascadeIdx = 2;
        if (depth > CascadeSplits.z) cascadeIdx = 3;
        
        if (cascadeIdx >= (int)NumCascades)
        {
            return 1.0f;
        }

        FShadowInfo info = gShadowInfos[DirectionalLight.ShadowIndex + cascadeIdx];
        shadow = SampleAtlasShadow(info, worldPos, CascadeMatrices[cascadeIdx]);
        return ApplyShadowSharpen(shadow, info);
    }
    
    FShadowInfo info = gShadowInfos[DirectionalLight.ShadowIndex];
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
    shadow = SampleAtlasShadowVSM(info, worldPos);
#else
    shadow = SampleAtlasShadow(info, worldPos, info.LightVP);
#endif
    return ApplyShadowSharpen(shadow, info);
}

float GetLightShadow(FLightInfo light, float3 worldPos)
{
    if (light.ShadowIndex < 0)
    {
        return 1.0f;
    }

    FShadowInfo info = gShadowInfos[light.ShadowIndex];
    float shadow = 1.0f;
    if (info.Type == SHADOW_INFO_TYPE_ATLAS2D)
    {
#if defined(SHADOW_ENABLE_VSM) && SHADOW_ENABLE_VSM
        shadow = SampleAtlasShadowVSM(info, worldPos);
#else
        shadow = SampleAtlasShadow(info, worldPos, info.LightVP);
#endif
    }
    else
    {
        shadow = SampleCubeShadow(info, worldPos);
    }
    return ApplyShadowSharpen(shadow, info);
}

float3 CalcDirectionalDiffuse(float3 lightColor, float3 lightDir, float intensity, float3 N)
{
    float NdotL = saturate(dot(N, -lightDir));
    return lightColor * intensity * NdotL;
}

float3 CalcDirectionalSpecular(float3 lightColor, float3 lightDir, float intensity,
                               float3 N, float3 V, float shininess)
{
    float3 H = normalize(-lightDir + V);
    float NdotH = saturate(dot(N, H));
    return lightColor * intensity * pow(NdotH, max(shininess, 1.0f));
}

float3 GetHeatmapColor(float value)
{
    float3 color;
    color.r = saturate(min(4.0 * value - 1.5, -4.0 * value + 4.5));
    color.g = saturate(min(4.0 * value - 0.5, -4.0 * value + 3.5));
    color.b = saturate(min(4.0 * value + 0.5, -4.0 * value + 2.5));
    return color;
}

uint DepthToClusterSlice(float viewDepth)
{
    float safeDepth = clamp(viewDepth, CullState.NearZ, CullState.FarZ);
    float logDepth = log(safeDepth / CullState.NearZ) / log(CullState.FarZ / CullState.NearZ);
    return min((uint) floor(logDepth * CullState.ClusterZ), CullState.ClusterZ - 1);
}

uint ComputeClusterIndex(float4 screenPos, float3 worldPos)
{
    float4 viewPos = mul(float4(worldPos, 1.0f), View);
    uint tileX = min((uint) (screenPos.x / CullState.ScreenWidth * CullState.ClusterX), CullState.ClusterX - 1);
    uint tileY = min((uint) (screenPos.y / CullState.ScreenHeight * CullState.ClusterY), CullState.ClusterY - 1);
    uint sliceZ = DepthToClusterSlice(abs(viewPos.z));

    return sliceZ * CullState.ClusterX * CullState.ClusterY
        + tileY * CullState.ClusterX
        + tileX;
}

float3 CalcLightDiffuse(FLightInfo light, float3 worldPos, float3 N)
{
    float3 L = light.Position - worldPos;
    float dist = length(L);
    L = normalize(L);

    float atten = CalcAttenuation(dist, light.AttenuationRadius, light.FalloffExponent);
    float NdotL = saturate(dot(N, L));

    float spotFactor = 1.0f;
    if (light.LightType == LIGHT_TYPE_SPOT)
    {
        float cosAngle = dot(-L, normalize(light.Direction));
        spotFactor = smoothstep(light.OuterConeCos, light.InnerConeCos, cosAngle);
    }

    return light.Color.rgb * light.Intensity * NdotL * atten * spotFactor;
}

float3 CalcLightSpecular(FLightInfo light, float3 worldPos, float3 N, float3 V, float shininess)
{
    float3 L = normalize(light.Position - worldPos);
    float dist = length(light.Position - worldPos);
    float atten = CalcAttenuation(dist, light.AttenuationRadius, light.FalloffExponent);

    float spotFactor = 1.0f;
    if (light.LightType == LIGHT_TYPE_SPOT)
    {
        float cosAngle = dot(-L, normalize(light.Direction));
        spotFactor = smoothstep(light.OuterConeCos, light.InnerConeCos, cosAngle);
    }

    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    return light.Color.rgb * light.Intensity * pow(NdotH, max(shininess, 1.0f)) * atten * spotFactor;
}

void AccumulatePointSpotDiffuse(float3 worldPos, float3 N, float4 screenPos, inout float3 result)
{
    if (LightCullingMode == LIGHT_CULLING_TILE && NumTilesX > 0 && NumTilesY > 0)
    {
        uint2 tileCoord = min(uint2(screenPos.xy) / TILE_SIZE, uint2(NumTilesX - 1, NumTilesY - 1));
        uint tileIdx = tileCoord.y * NumTilesX + tileCoord.x;
        uint2 gridData = TileLightGrid[tileIdx];
        for (uint t = 0; t < gridData.y; ++t)
        {
            FLightInfo light = AllLights[TileLightIndices[gridData.x + t]];
            result += CalcLightDiffuse(light, worldPos, N) * GetLightShadow(light, worldPos);
        }
    }
    else if (LightCullingMode == LIGHT_CULLING_CLUSTER)
    {
        uint clusterIdx = ComputeClusterIndex(screenPos, worldPos);
        uint2 gridData = g_ClusterLightGrid[clusterIdx];
        for (uint t = 0; t < gridData.y; ++t)
        {
            FLightInfo light = AllLights[g_ClusterLightIndices[gridData.x + t]];
            result += CalcLightDiffuse(light, worldPos, N) * GetLightShadow(light, worldPos);
        }
    }
    else
    {
        for (uint i = 0; i < NumActivePointLights + NumActiveSpotLights; ++i)
        {
            FLightInfo light = AllLights[i];
            result += CalcLightDiffuse(light, worldPos, N) * GetLightShadow(light, worldPos);
        }
    }
}

void AccumulatePointSpotSpecular(float3 worldPos, float3 N, float3 V, float shininess, float4 screenPos, inout float3 result)
{
    if (LightCullingMode == LIGHT_CULLING_TILE && NumTilesX > 0 && NumTilesY > 0)
    {
        uint2 tileCoord = min(uint2(screenPos.xy) / TILE_SIZE, uint2(NumTilesX - 1, NumTilesY - 1));
        uint tileIdx = tileCoord.y * NumTilesX + tileCoord.x;
        uint2 gridData = TileLightGrid[tileIdx];
        for (uint t = 0; t < gridData.y; ++t)
        {
            FLightInfo light = AllLights[TileLightIndices[gridData.x + t]];
            result += CalcLightSpecular(light, worldPos, N, V, shininess) * GetLightShadow(light, worldPos);
        }
    }
    else if (LightCullingMode == LIGHT_CULLING_CLUSTER)
    {
        uint clusterIdx = ComputeClusterIndex(screenPos, worldPos);
        uint2 gridData = g_ClusterLightGrid[clusterIdx];
        for (uint t = 0; t < gridData.y; ++t)
        {
            FLightInfo light = AllLights[g_ClusterLightIndices[gridData.x + t]];
            result += CalcLightSpecular(light, worldPos, N, V, shininess) * GetLightShadow(light, worldPos);
        }
    }
    else
    {
        for (uint i = 0; i < NumActivePointLights + NumActiveSpotLights; ++i)
        {
            FLightInfo light = AllLights[i];
            result += CalcLightSpecular(light, worldPos, N, V, shininess) * GetLightShadow(light, worldPos);
        }
    }
}

#if defined(LIGHTING_MODEL_TOON) && LIGHTING_MODEL_TOON
static const float g_ToonSteps = 4.0f;
static const float g_ToonDarknessFloor = 0.25f;
static const float g_ToonRimMin = 0.55f;
static const float g_ToonRimMax = 0.85f;
static const float g_ToonRimStrength = 0.25f;

float ToonStep(float NdotL)
{
    float x = saturate(NdotL);
    float stepped = smoothstep(g_ToonDarknessFloor, 1.0f, x * g_ToonSteps);
    stepped /= max(g_ToonSteps - 1.0f, 1.0f);
    return lerp(g_ToonDarknessFloor, 1.0f, saturate(stepped));
}

float3 CalcRimMask(float3 N, float3 V)
{
    float rimDot = 1.0f - saturate(dot(N, V));
    return smoothstep(g_ToonRimMin, g_ToonRimMax, rimDot);
}
#endif

float3 AccumulateDiffuse(float3 worldPos, float3 N, float4 screenPos)
{
    float3 result = float3(0, 0, 0);
    result += CalcAmbient(AmbientLight.Color.rgb, AmbientLight.Intensity);
    result += CalcDirectionalDiffuse(DirectionalLight.Color.rgb, DirectionalLight.Direction,
                                     DirectionalLight.Intensity, N) * GetDirectionalShadow(worldPos);
    AccumulatePointSpotDiffuse(worldPos, N, screenPos, result);
    return result;
}

float3 AccumulateSpecular(float3 worldPos, float3 N, float3 V, float shininess, float4 screenPos)
{
    float3 result = float3(0, 0, 0);
    result += CalcDirectionalSpecular(DirectionalLight.Color.rgb, DirectionalLight.Direction,
                                      DirectionalLight.Intensity, N, V, shininess) * GetDirectionalShadow(worldPos);
    AccumulatePointSpotSpecular(worldPos, N, V, shininess, screenPos, result);
    return result;
}

#endif // FORWARD_LIGHTING_HLSLI