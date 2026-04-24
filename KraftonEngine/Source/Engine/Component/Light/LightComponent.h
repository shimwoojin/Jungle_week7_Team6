#pragma once
#include "Component/Light/LightComponentBase.h"
#include "Render/Resource/Texture2DArrayPool.h"

class ULightComponent : public ULightComponentBase
{
public:
	DECLARE_CLASS(ULightComponent, ULightComponentBase)

	~ULightComponent() { ShadowMapEntry->LetsGoHome(); }

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }

	virtual void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	virtual void Serialize(FArchive& Ar) override;

	FTexture2DArrayPool::Entry* GetShadowEntry();
	virtual ArrayType GetShadowMapTextureType() { return ArrayType::Default; }

protected:
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 0.01f;
	float ShadowSlopeBias = 0.01f;
	float ShadowSharpen = 0.0f;

	FTexture2DArrayPool::Entry* ShadowMapEntry = nullptr;
};