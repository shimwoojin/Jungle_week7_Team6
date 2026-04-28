#pragma once
#include "Component/Light/LightComponentBase.h"
#include "Render/Resource/TexturePool/TextureAtlasPool.h"
#include "Render/Resource/TexturePool/TextureCubeShadowPool.h"

using FShadowHandle = FTexturePoolBase::TexturePoolHandle;
using FShadowHandleSet = FTexturePoolBase::TexturePoolHandleSet;
using FShadowCubeHandle = FTextureCubeShadowPool::FCubeShadowHandle;

struct FShadowMapKey
{
	bool IsAtlas() { return !Atlas.empty(); }
	bool IsCubeMap() { return CubeMap.IsValid(); }
	TArray<FAtlasUV> Atlas;
	FShadowCubeHandle CubeMap;

};

class ULightComponent : public ULightComponentBase
{
public:
	DECLARE_CLASS(ULightComponent, ULightComponentBase)

	~ULightComponent() { if(ShadowHandleSet) ShadowHandleSet->Release(); }

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	void SetShadowResolutionScale(float InShadowResolutionScale);
	uint32 GetShadowResolution() const;
	float GetShadowBias() const { return ShadowBias; }
	void SetShadowBias(float InShadowBias) { ShadowBias = InShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	void SetShadowSlopeBias(float InShadowSlopeBias) { ShadowSlopeBias = InShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }
	void SetShadowSharpen(float InShadowSharpen) { ShadowSharpen = InShadowSharpen; }
	void InvalidateShadowHandleSet();

	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void Serialize(FArchive& Ar) override;

	virtual FShadowHandleSet* GetShadowHandleSet() { return ShadowHandleSet; }
	virtual FShadowMapKey GetShadowMapKey() { return FShadowMapKey(); }

protected:
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.01f;
	float ShadowSlopeBias = 0.01f;
	float ShadowSharpen = 0.0f;

	FShadowHandleSet* ShadowHandleSet = nullptr;
};
