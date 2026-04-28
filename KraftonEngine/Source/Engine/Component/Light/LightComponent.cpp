#include "Component/Light/LightComponent.h"
#include "Object/ObjectFactory.h"
#include "Engine/Serialization/Archive.h"

IMPLEMENT_ABSTRACT_CLASS(ULightComponent, ULightComponentBase)

void ULightComponent::SetShadowResolutionScale(float InShadowResolutionScale)
{
	if (ShadowResolutionScale == InShadowResolutionScale)
	{
		return;
	}

	ShadowResolutionScale = InShadowResolutionScale;
	InvalidateShadowHandleSet();
}

uint32 ULightComponent::GetShadowResolution() const
{
	const float SafeScale = ShadowResolutionScale > 0.0f ? ShadowResolutionScale : 0.0f;
	if (SafeScale <= 0.25f)
	{
		return 256;
	}
	if (SafeScale <= 0.5f)
	{
		return 512;
	}
	if (SafeScale <= 1.0f)
	{
		return 1024;
	}
	return 2048;
}

void ULightComponent::InvalidateShadowHandleSet()
{
	if (ShadowHandleSet)
	{
		ShadowHandleSet->bIsValid = false;
	}
}

void ULightComponent::Serialize(FArchive& Ar)
{
	ULightComponentBase::Serialize(Ar);
	Ar << ShadowResolutionScale;
	Ar << ShadowBias;
	Ar << ShadowSlopeBias;
	Ar << ShadowSharpen;
}

void ULightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	ULightComponentBase::GetEditableProperties(OutProps);
	OutProps.push_back({ "ShadowResolutionScale", EPropertyType::Float, &ShadowResolutionScale, 0.0f, 4.0f,  0.01f });
	OutProps.push_back({ "ShadowBias",            EPropertyType::Float, &ShadowBias,            0.0f, 1.0f,  0.001f });
	OutProps.push_back({ "ShadowSlopeBias",        EPropertyType::Float, &ShadowSlopeBias,       0.0f, 1.0f,  0.001f });
	OutProps.push_back({ "ShadowSharpen",          EPropertyType::Float, &ShadowSharpen,         0.0f, 1.0f,  0.01f });
}
