#pragma once
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "d3d11.h"
#include <wrl/client.h>
#include <memory>


class FTexturePoolBase : public TSingleton<FTexturePoolBase>
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

#pragma region InternalStruct
public:
	struct TexturePoolHandle
	{
		TexturePoolHandle() = default;
		TexturePoolHandle(uint32 InInternalIndex, uint32 InArrayIndex)
			: InternalIndex(InInternalIndex), ArrayIndex(InArrayIndex) {
		}

		float GetIndex() { return InternalIndex; }
		void Release() { Pool->ReleaseHandle(*this); }

		bool operator==(const TexturePoolHandle& Other) const
		{
			return InternalIndex == Other.InternalIndex &&
				ArrayIndex == Other.ArrayIndex;
		}

		uint32 InternalIndex = -1;
		uint32 ArrayIndex = -1;

	private:
		FTexturePoolBase* Pool = nullptr;
	};

	struct TexturePoolHandleSet
	{
		void Release() { for (auto Handle : Handles) Handle.Release(); }

		bool bIsValid = false;
		TArray<TexturePoolHandle> Handles;
	};

	struct TexturePoolHandleRequest
	{
		TexturePoolHandleRequest() = default;

		template<typename... Args>
		TexturePoolHandleRequest(Args... args)
		{
			(Sizes.push_back(static_cast<uint32>(args)), ...);
		}

		TArray<uint32> Sizes;
	};

private:
	struct SRVResource
	{
		TComPtr<ID3D11Texture2D> Texture;
		TComPtr<ID3D11ShaderResourceView> SRV;
	};
#pragma endregion

public:
	void Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InTextureSize);

	virtual TexturePoolHandleSet GetTextureHandle(TexturePoolHandleRequest HandleRequest) { return TexturePoolHandleSet(); }
	virtual void ReleaseHandle(TexturePoolHandle& InHandle) {};
	virtual ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) { return nullptr; }

protected:
	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) { return nullptr; }
	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) {};
	void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture);

	void ResizeLayer() { ResizeLayer(TextureLayerSize * 2); }
	void ResizeLayer(uint32 InNewLayerSize);

	virtual void BroadCastEntries() {};

	virtual void OnSetTextureSize() {};
	virtual void OnSetTextureLayerSize() {};

	virtual uint32 GetTextureLayerSize() { return TextureLayerSize; }

private:
	void SetTextureLayerSize(uint32 InTextureLayerSize);
	void SetTextureSize(uint32 InTextureSize);

protected:
	uint32 TextureLayerSize = 1;
	uint32 TextureSize = -1;

	TComPtr<ID3D11ShaderResourceView> SRV;
	TArray<TComPtr<ID3D11DepthStencilView>> DSVs;

	TMap<float, std::unique_ptr<SRVResource>> DebugResource; //ImGui::Image에 렌더링할 대표 이미지, Size

private:
	TComPtr<ID3D11Texture2D> Texture;

	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
};
