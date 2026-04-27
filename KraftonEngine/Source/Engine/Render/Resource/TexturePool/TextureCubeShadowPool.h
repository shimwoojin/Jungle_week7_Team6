#pragma once

#include "Core/CoreTypes.h"
#include <d3d11.h>
#include <wrl/client.h>

class FTextureCubeShadowPool final
{
	template<typename T>
	using TComPtr = Microsoft::WRL::ComPtr<T>;

public:
	static constexpr uint32 InvalidCubeIndex = static_cast<uint32>(-1);
	static constexpr uint32 CubeFaceCount = 6;

	struct FCubeShadowHandle
	{
		uint32 CubeIndex = InvalidCubeIndex;

		bool IsValid() const { return CubeIndex != InvalidCubeIndex; }
	};

	static FTextureCubeShadowPool& Get()
	{
		static FTextureCubeShadowPool Instance;
		return Instance;
	}

	FTextureCubeShadowPool(const FTextureCubeShadowPool&) = delete;
	FTextureCubeShadowPool& operator=(const FTextureCubeShadowPool&) = delete;
	FTextureCubeShadowPool(FTextureCubeShadowPool&&) = delete;
	FTextureCubeShadowPool& operator=(FTextureCubeShadowPool&&) = delete;

	void Initialize(ID3D11Device* InDevice, uint32 InResolution, uint32 InitialCubeCapacity = 1);
	void Release();

	FCubeShadowHandle Allocate();
	void ReleaseHandle(FCubeShadowHandle Handle);

	ID3D11ShaderResourceView* GetSRV() const { return SRV.Get(); }
	ID3D11DepthStencilView* GetFaceDSV(FCubeShadowHandle Handle, uint32 FaceIndex) const;

	uint32 GetResolution() const { return Resolution; }
	uint32 GetCapacity() const { return CubeCapacity; }
	uint32 GetAllocatedCount() const { return AllocatedCount; }
	bool IsInitialized() const { return Device != nullptr && Texture != nullptr; }

private:
	FTextureCubeShadowPool() = default;
	~FTextureCubeShadowPool() = default;

	void Resize(uint32 NewCubeCapacity);
	bool RebuildResources(uint32 NewCubeCapacity);
	uint32 GetSliceIndex(FCubeShadowHandle Handle, uint32 FaceIndex) const;

private:
	ID3D11Device* Device = nullptr;
	uint32 Resolution = 1024;
	uint32 CubeCapacity = 0;
	uint32 AllocatedCount = 0;

	TComPtr<ID3D11Texture2D> Texture;
	TComPtr<ID3D11ShaderResourceView> SRV;
	TArray<TComPtr<ID3D11DepthStencilView>> FaceDSVs;

	TArray<uint8> AllocationFlags;
	TArray<uint32> FreeCubeIndices;
};
