#pragma once

#include "TexturePool.h"
#include "Render/Resource/Buffer.h"
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
	ID3D11ShaderResourceView* GetRawSRV() { return SRV.Get(); }
	ID3D11ShaderResourceView* GetFilteredSRV() { return VSMFilteredSRV.Get(); }
	ID3D11ShaderResourceView* GetTempSRV() { return VSMTempSRV.Get(); }
	ID3D11Texture2D* GetRawTexture() { return Texture.Get(); }
	ID3D11Texture2D* GetFilteredTexture() { return VSMFilteredTexture.Get(); }
	ID3D11Texture2D* GetTempTexture() { return VSMTempTexture.Get(); }
	TArray<ID3D11DepthStencilView*> GetDSVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetRTVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetFilteredRTVs(TexturePoolHandleSet* HandleSet);
	TArray<ID3D11RenderTargetView*> GetTempRTVs(TexturePoolHandleSet* HandleSet);

	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) override;
	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandleSet* InHandleSet) override;
	ID3D11ShaderResourceView* GetDebugLayerSRV(uint32 SliceIndex);

protected:
	TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
	void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;
	void RebuildRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture);

	void OnSetTextureSize() override;
	void OnSetTextureLayerSize() override;

private:
	bool CreateDebugResource(SRVResource& OutResource, uint32 Width, uint32 Height);
	bool CreateDebugPassResources();

	void MarkSliceDebugDirty(uint32 SliceIndex);
	void MarkSliceDebugDirty(TexturePoolHandleSet* HandleSet);

	uint32 MakeHandleDebugKey(const TexturePoolHandle& InHandle);
	void RecreateAtlasResources();
	TComPtr<ID3D11Texture2D> CreateVSMDepthTexture(ID3D11Device* Device);
	TComPtr<ID3D11Texture2D> CreateVSMMomentTexture(ID3D11Device* Device);
	void RebuildVSMMomentSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture, TComPtr<ID3D11ShaderResourceView>& OutSRV);
	void RebuildVSMMomentRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture, TArray<TComPtr<ID3D11RenderTargetView>>& OutRTVs);
	void RebuildVSMBlurResources(ID3D11Device* Device);
	bool IsVSMMode() const { return CurrentFilterMode == EShadowFilterMode::VSM; }

private:
	TArray<std::unique_ptr<FUVManagerBase>> UVManagers;

	EShadowFilterMode CurrentFilterMode = EShadowFilterMode::PCF;
	TComPtr<ID3D11Texture2D> VSMDepthTexture;
	TComPtr<ID3D11Texture2D> VSMFilteredTexture;
	TComPtr<ID3D11Texture2D> VSMTempTexture;
	TComPtr<ID3D11ShaderResourceView> VSMFilteredSRV;
	TComPtr<ID3D11ShaderResourceView> VSMTempSRV;
	TArray<TComPtr<ID3D11RenderTargetView>> RTVs;
	TArray<TComPtr<ID3D11RenderTargetView>> VSMFilteredRTVs;
	TArray<TComPtr<ID3D11RenderTargetView>> VSMTempRTVs;
	TArray<uint64> SliceDebugVersions;
	FConstantBuffer DebugConstantBuffer;
	TComPtr<ID3D11SamplerState> DebugPointClampSampler;
	TComPtr<ID3D11RasterizerState> DebugRasterizerState;
	TComPtr<ID3D11DepthStencilState> DebugDepthStencilState;
};
