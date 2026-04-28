#include "EditorShadowPropertyWidget.h"
#include "imgui.h"

void FEditorShadowPropertyWidget::ShowShadowProperty(ULightComponent* LightComponent)
{
	if (CurrentShowLightComponent != LightComponent)
		CurrentShowLightComponent = LightComponent;

	if (!ImGui::Begin("Where there is light, there is also shadow."))
	{
		ImGui::End();
		return;
	}

	ShowShadowMapPropertWindow();
	ImGui::End();
}

void FEditorShadowPropertyWidget::ShowShadowMapPropertWindow()
{
	auto Handle = CurrentShowLightComponent->GetShadowHandleSet();
	if (Handle)
	{
		auto ShadowMap = FTextureAtlasPool::Get().GetDebugSRV(Handle);
		if (ShadowMap) ImGui::Image(ShadowMap, ImVec2(500, 500));
	}

	auto AtlasSlice = FTextureAtlasPool::Get().GetDebugLayerSRV(0);
	if (AtlasSlice) ImGui::Image(AtlasSlice, ImVec2(500, 500));
}
