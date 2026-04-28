#include "EditorShadowPropertyWidget.h"
#include "imgui.h"
#include "Engine/Component/Light/PointLightComponent.h"

void FEditorShadowPropertyWidget::ShowShadowProperty(ULightComponent* LightComponent)
{
	if (LightComponent->StaticClass() == UPointLightComponent::StaticClass())
		return;

	if (CurrentShowLightComponent != LightComponent)
		CurrentShowLightComponent = LightComponent;

	if (!ImGui::Begin("Where there is light, there is also shadow."))
	{
		ImGui::End();
		return;
	}

	ShowShadowParameter();
	ShowShadowMapPropertWindow();
	ImGui::End();
}

void FEditorShadowPropertyWidget::ShowShadowParameter()
{
	if (!CurrentShowLightComponent)
	{
		ImGui::TextUnformatted("No light selected.");
		return;
	}

	static constexpr int32 ResolutionOptions[] = { 256, 512, 1024, 2048 };
	static constexpr const char* ResolutionLabels[] = { "256", "512", "1024", "2048" };
	static constexpr float ResolutionScales[] = { 0.25f, 0.5f, 1.0f, 2.0f };

	int32 ResolutionIndex = 2;
	for (int32 Index = 0; Index < static_cast<int32>(std::size(ResolutionOptions)); ++Index)
	{
		if (CurrentShowLightComponent->GetShadowResolutionScale() <= ResolutionScales[Index])
		{
			ResolutionIndex = Index;
			break;
		}
	}

	if (ImGui::BeginCombo("Shadow Map Resolution", ResolutionLabels[ResolutionIndex]))
	{
		for (int32 Index = 0; Index < static_cast<int32>(std::size(ResolutionOptions)); ++Index)
		{
			const bool bSelected = (ResolutionIndex == Index);
			if (ImGui::Selectable(ResolutionLabels[Index], bSelected))
			{
				CurrentShowLightComponent->SetShadowResolutionScale(ResolutionScales[Index]);
				ResolutionIndex = Index;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	float ShadowBias = CurrentShowLightComponent->GetShadowBias();
	if (ImGui::DragFloat("Shadow Bias", &ShadowBias, 0.001f, 0.0f, 1.0f, "%.3f"))
	{
		CurrentShowLightComponent->SetShadowBias(ShadowBias);
	}

	float ShadowSlopeBias = CurrentShowLightComponent->GetShadowSlopeBias();
	if (ImGui::DragFloat("Shadow Slope Bias", &ShadowSlopeBias, 0.001f, 0.0f, 1.0f, "%.3f"))
	{
		CurrentShowLightComponent->SetShadowSlopeBias(ShadowSlopeBias);
	}

	float ShadowSharpen = CurrentShowLightComponent->GetShadowSharpen();
	if (ImGui::DragFloat("Shadow Sharpen", &ShadowSharpen, 0.01f, 0.0f, 1.0f, "%.2f"))
	{
		CurrentShowLightComponent->SetShadowSharpen(ShadowSharpen);
	}
}


void FEditorShadowPropertyWidget::ShowShadowMapPropertWindow()
{
	if (!CurrentShowLightComponent)
	{
		ImGui::TextUnformatted("No light selected.");
		return;
	}

	if (ImGui::RadioButton("Selected Light ShadowMap", PreviewMode == EShadowPreviewMode::SelectedLight))
	{
		PreviewMode = EShadowPreviewMode::SelectedLight;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Atlas Texture Layer", PreviewMode == EShadowPreviewMode::AtlasLayer))
	{
		PreviewMode = EShadowPreviewMode::AtlasLayer;
	}

	FTextureAtlasPool& AtlasPool = FTextureAtlasPool::Get();
	const uint32 AllocatedLayerCount = AtlasPool.GetAllocatedLayerCount();
	const int32 MaxLayerIndex = AllocatedLayerCount > 0 ? static_cast<int32>(AllocatedLayerCount - 1) : 0;

	if (PreviewAtlasLayerIndex < 0)
	{
		PreviewAtlasLayerIndex = 0;
	}
	else if (PreviewAtlasLayerIndex > MaxLayerIndex)
	{
		PreviewAtlasLayerIndex = MaxLayerIndex;
	}

	if (PreviewMode == EShadowPreviewMode::AtlasLayer)
	{
		ImGui::SliderInt("Atlas Layer", &PreviewAtlasLayerIndex, 0, MaxLayerIndex);
	}

	ID3D11ShaderResourceView* PreviewSRV = nullptr;
	if (PreviewMode == EShadowPreviewMode::SelectedLight)
	{
		FShadowHandleSet* Handle = CurrentShowLightComponent->GetShadowHandleSet();
		PreviewSRV = Handle ? AtlasPool.GetDebugSRV(Handle) : nullptr;
		if (!PreviewSRV)
		{
			ImGui::TextUnformatted("No shadow map for selected light.");
			return;
		}
	}
	else
	{
		PreviewSRV = AtlasPool.GetDebugLayerSRV(static_cast<uint32>(PreviewAtlasLayerIndex));
		if (!PreviewSRV)
		{
			ImGui::TextUnformatted("Atlas layer preview unavailable.");
			return;
		}
	}

	ImGui::Image(PreviewSRV, ImVec2(500, 500));
}
