#pragma once
#include "Core/CoreTypes.h"
#include <wrl/client.h>
#include "d3d11.h"
#include <memory>

enum class ArrayType : uint32
{
	Default,
	CubeMap,
};

struct FTexture2DArrayPoolEntry
{
	ArrayType Type = ArrayType::Default;
	uint32 size;

	uint32 Index;
};

class FTexture2DArrayPool
{
public:
	FTexture2DArrayPool(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InSize, uint32 InInitialSize = 1, ArrayType InType = ArrayType::Default);
	struct Entry
	{
		Entry(uint32 InIndex) { Index = InIndex; }

		ArrayType Type = ArrayType::Default;

		Entry* NextFreeEntry = nullptr;
		uint32 Index = -1;

		ID3D11ShaderResourceView* SRV = nullptr;
		TArray<ID3D11DepthStencilView*> DSV;

		bool bInUsed = false;

		void ClearDSV(ID3D11DeviceContext* InDeviceContext);
	};

	FTexture2DArrayPool::Entry* GetEntry();
	ID3D11Texture2D* GetTexture() { return Texture.Get(); }
	ID3D11Texture2D** GetTextuasdre() { return nullptr; }

	void Resize(uint32 InNewSize);
	void Resize() { Resize(TextureArraySize * 2); }

	void ReuseEntry(Entry* Entry);

private:
	Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateTexture(uint32 InArraySize);
	void CreateSRV(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& InSRV);
	void CreateDSV(TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>>& DSVs);

	void SetEntry(Entry* Entry);


private:
	ArrayType Type = ArrayType::Default;

	Entry* LastFreeEntry = nullptr;
	uint32 Size;
	uint32 TextureArraySize;
	TArray<std::unique_ptr<Entry>> Entries;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
	TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>> DSVs;

	uint32 DsvClusterSize = 1;
	ID3D11Device* Device = nullptr;
	ID3D11DeviceContext* DeviceContext = nullptr;
};

