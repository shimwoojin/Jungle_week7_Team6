#include "Component/Light/LightComponent.h"
#include "Object/ObjectFactory.h"
#include "Engine/Serialization/Archive.h"

IMPLEMENT_ABSTRACT_CLASS(ULightComponent, ULightComponentBase)

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