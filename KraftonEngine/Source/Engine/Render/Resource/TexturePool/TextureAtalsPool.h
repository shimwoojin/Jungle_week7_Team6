#pragma once
#include "TexturePool.h"
#include "UVManager/FUVManager.h"



class FTextureAtlasPool final : public FTexturePoolBase
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

public:
	TexturePoolHandleSet GetTextureHandle(TexturePoolHandleRequest HandleRequest) override;;
	void ReleaseHandle(TexturePoolHandle& InHandle) override;
	AtlasUV GetAtlasUV(const TexturePoolHandle& InHandle) { return UVManagers[InHandle.ArrayIndex].get()->GetAtlasUV(InHandle.InternalIndex); }

	ID3D11DepthStencilView* GetDSV(TexturePoolHandle* InHandle) { return DSVs[InHandle->ArrayIndex].Get(); }
	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) override;

protected:
	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;

	virtual void BroadCastEntries() override;

	virtual void OnSetTextureSize() override;
	virtual void OnSetTextureLayerSize() override;

private:
	TArray<std::unique_ptr<FUVManagerBase>> UVManagers;
};

