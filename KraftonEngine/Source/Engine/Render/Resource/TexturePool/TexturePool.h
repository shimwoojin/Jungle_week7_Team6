#pragma once
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "d3d11.h"
#include <wrl/client.h>
#include <memory>


class FTexturePoolBase
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

		uint32 InternalIndex = -1;
		uint32 ArrayIndex = -1;
	};

	struct TexturePoolHandleSet
	{
		TexturePoolHandleSet(FTexturePoolBase* InPool, uint32 InInternalIndex) : Pool(InPool), InternalIndex(InInternalIndex) {}
		void Release() { Pool->ReleaseHandleSet(this); }

		uint32 InternalIndex;
		bool bIsValid = false;
		TArray<TexturePoolHandle> Handles;

	private:
		FTexturePoolBase* Pool = nullptr;
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
	uint32 GetTextureSize() const { return TextureSize; }
	uint32 GetAllocatedLayerCount() const { return TextureLayerSize; }

	virtual TexturePoolHandleSet* GetTextureHandle(TexturePoolHandleRequest HandleRequest) { return nullptr; }
	virtual void ReleaseHandle(TexturePoolHandle& InHandle) {};
	virtual void ReleaseHandleSet(TexturePoolHandleSet* InHandleSet);
	virtual ID3D11ShaderResourceView* GetDebugSRV(const TexturePoolHandle& InHandle) { return nullptr; }

protected:
	virtual TComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* Device) { return nullptr; }
	virtual void RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture) {};
	void RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture);

	void ResizeLayer() { ResizeLayer(TextureLayerSize * 2); }
	void ResizeLayer(uint32 InNewLayerSize);

	virtual void BroadCastHandlesUnvalid();

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
	TMap<uint32, std::unique_ptr<SRVResource>> DebugResource; //ImGui::Image에 렌더링할 대표 이미지, Size

	TArray<std::unique_ptr<TexturePoolHandleSet>> AllocatedHandleList;

private:
	TComPtr<ID3D11Texture2D> Texture;

	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
};
