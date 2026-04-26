#pragma once
#include "Component/Light/LightComponentBase.h"
#include "Render/Resource/TexturePool/TextureAtalsPool.h"

using FShadowHandle = FTexturePoolBase::TexturePoolHandle;
using FShadowHandleSet = FTexturePoolBase::TexturePoolHandleSet;

struct FShadowMapKey
{
	bool IsAtlas() { return !Atlas.empty(); }
	TArray<FAtlasUV> Atlas;
	//Todo: CubMap 그거 추가

};

class ULightComponent : public ULightComponentBase
{
public:
	DECLARE_CLASS(ULightComponent, ULightComponentBase)

	~ULightComponent() { if(ShadowHandleSet) ShadowHandleSet->Release(); }

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }

	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void Serialize(FArchive& Ar) override;

	virtual FShadowHandleSet* GetShadowHandleSet() { return ShadowHandleSet; }
	virtual FShadowMapKey GetShadowMapKey() { return FShadowMapKey(); }

protected:
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.01f;
	float ShadowSlopeBias = 0.01f;
	float ShadowSharpen = 0.0f;

	FShadowHandleSet* ShadowHandleSet;
};