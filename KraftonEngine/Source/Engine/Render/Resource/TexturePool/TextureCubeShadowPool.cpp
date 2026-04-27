#include "TextureCubeShadowPool.h"

void FTextureCubeShadowPool::Initialize(ID3D11Device* InDevice, uint32 InResolution, uint32 InitialCubeCapacity)
{
	Release();

	Device = InDevice;
	Resolution = InResolution > 0 ? InResolution : 1;

	const uint32 SafeInitialCapacity = InitialCubeCapacity > 0 ? InitialCubeCapacity : 1;
	RebuildResources(SafeInitialCapacity);
}

void FTextureCubeShadowPool::Release()
{
	FaceDSVs.clear();
	SRV.Reset();
	Texture.Reset();
	AllocationFlags.clear();
	FreeCubeIndices.clear();

	Device = nullptr;
	CubeCapacity = 0;
	AllocatedCount = 0;
}

FTextureCubeShadowPool::FCubeShadowHandle FTextureCubeShadowPool::Allocate()
{
	if (!Device)
	{
		return {};
	}

	if (FreeCubeIndices.empty())
	{
		const uint32 NewCapacity = CubeCapacity > 0 ? CubeCapacity * 2 : 1;
		Resize(NewCapacity);
	}

	if (FreeCubeIndices.empty())
	{
		return {};
	}

	const uint32 CubeIndex = FreeCubeIndices.back();
	FreeCubeIndices.pop_back();

	if (CubeIndex >= AllocationFlags.size())
	{
		return {};
	}

	AllocationFlags[CubeIndex] = 1;
	++AllocatedCount;

	FCubeShadowHandle Handle;
	Handle.CubeIndex = CubeIndex;
	return Handle;
}

void FTextureCubeShadowPool::ReleaseHandle(FCubeShadowHandle Handle)
{
	if (!Handle.IsValid() || Handle.CubeIndex >= AllocationFlags.size())
	{
		return;
	}

	if (AllocationFlags[Handle.CubeIndex] == 0)
	{
		return;
	}

	AllocationFlags[Handle.CubeIndex] = 0;
	FreeCubeIndices.push_back(Handle.CubeIndex);

	if (AllocatedCount > 0)
	{
		--AllocatedCount;
	}
}

ID3D11DepthStencilView* FTextureCubeShadowPool::GetFaceDSV(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	if (!Handle.IsValid() || FaceIndex >= CubeFaceCount)
	{
		return nullptr;
	}

	const uint32 SliceIndex = GetSliceIndex(Handle, FaceIndex);
	if (SliceIndex >= FaceDSVs.size())
	{
		return nullptr;
	}

	return FaceDSVs[SliceIndex].Get();
}

void FTextureCubeShadowPool::Resize(uint32 NewCubeCapacity)
{
	if (NewCubeCapacity <= CubeCapacity)
	{
		return;
	}

	RebuildResources(NewCubeCapacity);
}

bool FTextureCubeShadowPool::RebuildResources(uint32 NewCubeCapacity)
{
	if (!Device || NewCubeCapacity == 0)
	{
		return false;
	}

	TComPtr<ID3D11Texture2D> NewTexture;
	TComPtr<ID3D11ShaderResourceView> NewSRV;
	TArray<TComPtr<ID3D11DepthStencilView>> NewFaceDSVs;

	const uint32 TotalSlices = NewCubeCapacity * CubeFaceCount;

	D3D11_TEXTURE2D_DESC TextureDesc = {};
	TextureDesc.Width = Resolution;
	TextureDesc.Height = Resolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.ArraySize = TotalSlices;
	TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
	TextureDesc.CPUAccessFlags = 0;
	TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	HRESULT hr = Device->CreateTexture2D(&TextureDesc, nullptr, NewTexture.GetAddressOf());
	if (FAILED(hr))
	{
		assert(false);
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	SRVDesc.TextureCubeArray.MostDetailedMip = 0;
	SRVDesc.TextureCubeArray.MipLevels = 1;
	SRVDesc.TextureCubeArray.First2DArrayFace = 0;
	SRVDesc.TextureCubeArray.NumCubes = NewCubeCapacity;

	hr = Device->CreateShaderResourceView(NewTexture.Get(), &SRVDesc, NewSRV.GetAddressOf());
	if (FAILED(hr))
	{
		assert(false);
		return false;
	}

	NewFaceDSVs.resize(TotalSlices);
	for (uint32 SliceIndex = 0; SliceIndex < TotalSlices; ++SliceIndex)
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
		DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
		DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = SliceIndex;
		DSVDesc.Texture2DArray.ArraySize = 1;

		hr = Device->CreateDepthStencilView(NewTexture.Get(), &DSVDesc, NewFaceDSVs[SliceIndex].GetAddressOf());
		if (FAILED(hr))
		{
			assert(false);
			return false;
		}
	}

	const uint32 OldCapacity = CubeCapacity;

	Texture = std::move(NewTexture);
	SRV = std::move(NewSRV);
	FaceDSVs = std::move(NewFaceDSVs);
	CubeCapacity = NewCubeCapacity;

	AllocationFlags.resize(CubeCapacity, 0);
	for (uint32 CubeIndex = CubeCapacity; CubeIndex > OldCapacity; --CubeIndex)
	{
		FreeCubeIndices.push_back(CubeIndex - 1);
	}

	return true;
}

uint32 FTextureCubeShadowPool::GetSliceIndex(FCubeShadowHandle Handle, uint32 FaceIndex) const
{
	return Handle.CubeIndex * CubeFaceCount + FaceIndex;
}
