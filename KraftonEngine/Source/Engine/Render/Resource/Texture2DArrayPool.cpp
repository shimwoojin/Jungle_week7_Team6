#include "Texture2DArrayPool.h"

FTexture2DArrayPool::FTexture2DArrayPool(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InSize, uint32 InInitialSize, ArrayType InType = ArrayType::Default)
	: Device(InDevice), DeviceContext(InDeviceContext), Type(InType)
{
	assert(InInitialSize >= 1);
	assert(InSize >= 1);

	TextureArraySize = InInitialSize;

	DsvClusterSize = InType == ArrayType::CubeMap ? 6 : 1;
	Size = InSize;

	Texture = CreateTexture(TextureArraySize);
	CreateSRV(SRV);
	CreateDSV(DSVs);
}

FTexture2DArrayPool::Entry* FTexture2DArrayPool::GetEntry()
{
	Entry* ToReturn;
	if (LastFreeEntry)
	{
		ToReturn = LastFreeEntry;
		LastFreeEntry = LastFreeEntry->NextFreeEntry;
		ToReturn->NextFreeEntry = nullptr;
		ToReturn->bInUsed = true;
		return ToReturn;
	}

	uint32 index = static_cast<uint32>(Entries.size());
	if (index >= TextureArraySize)
		Resize(TextureArraySize * 2);

	auto NewEntry = std::make_unique<Entry>(index);
	ToReturn = NewEntry.get();

	SetEntry(NewEntry.get());
	Entries.push_back(std::move(NewEntry));

	ToReturn->bInUsed = true;
	return ToReturn;
}

void FTexture2DArrayPool::Resize(uint32 InNewSize)
{
	const uint32 OldTextureArraySize = TextureArraySize;
	const uint32 OldTotalSlices = OldTextureArraySize * DsvClusterSize;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> NewTexture = CreateTexture(InNewSize);

	for (uint32 slice = 0; slice < OldTotalSlices; ++slice)
	{
		DeviceContext->CopySubresourceRegion(
			NewTexture.Get(),
			slice,
			0, 0, 0,
			Texture.Get(),
			slice,
			nullptr
		);
	}

	Texture = std::move(NewTexture);
	TextureArraySize = InNewSize;

	CreateSRV(SRV);
	CreateDSV(DSVs);

	for (auto& Entry : Entries)
	{
		SetEntry(Entry.get());
	}
}

void FTexture2DArrayPool::ReuseEntry(Entry* Entry)
{
	if (!Entry || !Entry->bInUsed)
		return;

	Entry->bInUsed = false;
	Entry->NextFreeEntry = LastFreeEntry;
	
	LastFreeEntry = Entry;
}

Microsoft::WRL::ComPtr<ID3D11Texture2D> FTexture2DArrayPool::CreateTexture(uint32 InArraySize)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = Size;
	desc.Height = Size;
	desc.MipLevels = 1;
	desc.ArraySize = InArraySize * DsvClusterSize;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;

	if (Type == ArrayType::CubeMap)
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

void FTexture2DArrayPool::CreateSRV(Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& InSRV)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // depth 읽을 때

	if (Type == ArrayType::CubeMap)
	{
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
		srvDesc.TextureCubeArray.MostDetailedMip = 0;
		srvDesc.TextureCubeArray.MipLevels = 1;
		srvDesc.TextureCubeArray.First2DArrayFace = 0;
		srvDesc.TextureCubeArray.NumCubes = TextureArraySize;
	}
	else
	{
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize = TextureArraySize;
	}

	InSRV.Reset();
	HRESULT hr = Device->CreateShaderResourceView(Texture.Get(), &srvDesc, InSRV.GetAddressOf());
	assert(SUCCEEDED(hr));
}

void FTexture2DArrayPool::CreateDSV(TArray<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>>& DSVs)
{
	DSVs.clear();
	DSVs.resize(TextureArraySize * DsvClusterSize);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;

	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	const uint32 TotalSlices = TextureArraySize * DsvClusterSize;
	for (UINT slice = 0; slice < TotalSlices; ++slice)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = slice;

		HRESULT hr = Device->CreateDepthStencilView(
			Texture.Get(),
			&dsvDesc,
			DSVs[slice].GetAddressOf()
		);

		assert(SUCCEEDED(hr));
	}
}

void FTexture2DArrayPool::SetEntry(Entry* Entry)
{
	Entry->SRV = SRV.Get();

	uint32 Index = Entry->Index;
	Entry->DSV.resize(DsvClusterSize);

	for (UINT i = 0; i < DsvClusterSize; ++i)
	{
		Entry->DSV[i] = DSVs[DsvClusterSize * Index + i].Get();
	}
}

void FTexture2DArrayPool::Entry::ClearDSV(ID3D11DeviceContext* InDeviceContext)
{
	for (uint32 i = 0; i < DSV.size(); ++i)
	{
		if (!DSV[i]) 
			continue;

		InDeviceContext->ClearDepthStencilView(
			DSV[i],
			D3D11_CLEAR_DEPTH,
			1.0f,
			0
		);
	}
}