#pragma once
#include "TexturePool.h"
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
	TexturePoolHandleSet* GetTextureHandle(TexturePoolHandleRequest HandleRequest) override;;
	void ReleaseHandle(TexturePoolHandle& InHandle) override;
	FAtlasUV GetAtlasUV(const TexturePoolHandle& InHandle) { return UVManagers[InHandle.ArrayIndex].get()->GetAtlasUV(InHandle.InternalIndex); }
	TArray<FAtlasUV> GetAtlasUVArray(const TexturePoolHandleSet* InHandleSet);
	ID3D11ShaderResourceView* GetSRV() { return SRV.Get(); }
	TArray<ID3D11DepthStencilView*> GetDSVs(TexturePoolHandleSet* HandleSet);
	ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) override;

protected:
	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) override;
	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) override;

	virtual void OnSetTextureSize() override;
	virtual void OnSetTextureLayerSize() override;

private:
	TArray<std::unique_ptr<FUVManagerBase>> UVManagers;
};

