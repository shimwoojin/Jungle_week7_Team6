#pragma once

#include "TexturePool.h"
#include "Render/Types/ViewTypes.h"
#include "UVManager/FUVManager.h"

class FTextureAtlasPool final : public FTexturePoolBase
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

#pragma region DefineSingleton
public:
	static FTextureAtlasPool& Get()
	{
		static FTextureAtlasPool Instance;
		return Instance;
	}

	FTextureAtlasPool(const FTextureAtlasPool&) = delete;
	FTextureAtlasPool& operator=(const FTextureAtlasPool&) = delete;
	FTextureAtlasPool(FTextureAtlasPool&&) = delete;
	FTextureAtlasPool& operator=(FTextureAtlasPool&&) = delete;

protected:
	FTextureAtlasPool() = default;
	~FTextureAtlasPool() = default;
#pragma endregion

public:
	void Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InTextureSize) override;
	void EnsureAtlasMode(EShadowFilterMode InFilterMode);
	TexturePoolHandleSet* GetTextureHandle(TexturePoolHandleRequest HandleRequest) override;
	void ReleaseHandle(TexturePoolHandle& InHandle) override;
	FAtlasUV GetAtlasUV(const TexturePoolHandle& InHandle) { return UVManagers[InHandle.ArrayIndex].get()->GetAtlasUV(InHandle.InternalIndex); }
	TArray<FAtlasUV> GetAtlasUVArray(const TexturePoolHandleSet* InHandleSet);
	ID3D11ShaderResourceView* GetSRV() { return SRV.Get(); }
	TArray<ID3D11DepthStencilView*> GetDSVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetRTVs(TexturePoolHandleSet* HandleSet);
	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) override;

protected:
	TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
	void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture);

	void OnSetTextureSize() override;
	void OnSetTextureLayerSize() override;

private:
	void RecreateAtlasResources();
	TComPtr<ID3D11Texture2D> CreateVSMDepthTexture(ID3D11Device* Device);
	bool IsVSMMode() const { return CurrentFilterMode == EShadowFilterMode::VSM; }

private:
	TArray<std::unique_ptr<FUVManagerBase>> UVManagers;

	EShadowFilterMode CurrentFilterMode = EShadowFilterMode::PCF;
	TComPtr<ID3D11Texture2D> VSMDepthTexture;
	TArray<TComPtr<ID3D11RenderTargetView>> RTVs;
};
