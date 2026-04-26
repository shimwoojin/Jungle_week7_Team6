#include "TexturePool.h"

void FTexturePoolBase::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InTextureSize)
{
	Device = InDevice;
	DeviceContext = InDeviceContext;

	SetTextureLayerSize(1);
	SetTextureSize(InTextureSize);

	Texture = CreateTexture(Device);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
}

void FTexturePoolBase::RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	DSVs.clear();
	DSVs.resize(TextureLayerSize);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;

	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	const uint32 TotalSlices = GetTextureLayerSize();
	for (UINT slice = 0; slice < TotalSlices; ++slice)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = slice;

		HRESULT hr = Device->CreateDepthStencilView(
			InTexture,
			&dsvDesc,
			DSVs[slice].GetAddressOf()
		);

		assert(SUCCEEDED(hr));
	}
}

void FTexturePoolBase::ResizeLayer(uint32 InNewLayerSize)
{
	const uint32 OldTotalSlices = TextureLayerSize;

	TComPtr<ID3D11Texture2D> NewTexture = CreateTexture(Device);

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

	SetTextureLayerSize(InNewLayerSize);

	Texture = std::move(NewTexture);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
}

void FTexturePoolBase::SetTextureLayerSize(uint32 InTextureLayerSize)
{
	TextureLayerSize = InTextureLayerSize;
	OnSetTextureLayerSize();
}

void FTexturePoolBase::SetTextureSize(uint32 InTextureSize)
{
	TextureLayerSize = InTextureSize;
	OnSetTextureSize();
}
