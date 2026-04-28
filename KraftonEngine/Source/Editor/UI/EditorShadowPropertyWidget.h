#pragma once

#include "Engine/Component/Light/LightComponent.h"
class FEditorShadowPropertyWidget
{
#pragma region DefineSingleton
public:
	static FEditorShadowPropertyWidget& Get()
	{
		static FEditorShadowPropertyWidget Instance;
		return Instance;
	}

	FEditorShadowPropertyWidget(const FEditorShadowPropertyWidget&) = delete;
	FEditorShadowPropertyWidget& operator=(const FEditorShadowPropertyWidget&) = delete;
	FEditorShadowPropertyWidget(FEditorShadowPropertyWidget&&) = delete;
	FEditorShadowPropertyWidget& operator=(FEditorShadowPropertyWidget&&) = delete;

protected:
	FEditorShadowPropertyWidget() = default;
	~FEditorShadowPropertyWidget() = default;
#pragma endregion

public:
	void ShowShadowProperty(ULightComponent* LightComponent);
	bool GetEnablePointer() { return bIsEnabled; }
	void SetEnabled(bool InEnable) { bIsEnabled = InEnable; }
private:
	void ShowShadowMapPropertWindow();

private:
	ULightComponent* CurrentShowLightComponent = nullptr;
	bool bIsEnabled;
};

