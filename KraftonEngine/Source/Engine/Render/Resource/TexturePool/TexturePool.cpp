#include "TexturePool.h"

void FTexturePoolBase::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InTextureSize)
{
	Device = InDevice;
	DeviceContext = InDeviceContext;

	SetTextureSize(InTextureSize);
	SetTextureLayerSize(1);

	Texture = CreateTexture(Device);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
}

void FTexturePoolBase::ReleaseHandleSet(TexturePoolHandleSet* InHandleSet)
{
	TArray<TexturePoolHandle> Handles = InHandleSet->Handles;
	for (auto& Handle : Handles)
	{
		ReleaseHandle(Handle);
	}

	uint32 TargetIndex = InHandleSet->InternalIndex;
	uint32 LastIndex = static_cast<uint32>(AllocatedHandleList.size() - 1);
	DebugResource.erase(TargetIndex);

	if (TargetIndex + 1 < AllocatedHandleList.size())
	{
		AllocatedHandleList[TargetIndex] = std::move(AllocatedHandleList.back());
		AllocatedHandleList[TargetIndex].get()->InternalIndex = TargetIndex;

		auto MovedDebugResource = DebugResource.find(LastIndex);
		if (MovedDebugResource != DebugResource.end())
		{
			DebugResource[TargetIndex] = std::move(MovedDebugResource->second);
			DebugResource.erase(MovedDebugResource);
		}
	}
	AllocatedHandleList.pop_back();
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
	TComPtr<ID3D11Texture2D> OldTexture = Texture;

	SetTextureLayerSize(InNewLayerSize);
	TComPtr<ID3D11Texture2D> NewTexture = CreateTexture(Device);

	for (uint32 slice = 0; slice < OldTotalSlices; ++slice)
	{
		DeviceContext->CopySubresourceRegion(
			NewTexture.Get(),
			slice,
			0, 0, 0,
			OldTexture.Get(),
			slice,
			nullptr
		);
	}

	Texture = std::move(NewTexture);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());

	BroadCastHandlesUnvalid();
}

void FTexturePoolBase::BroadCastHandlesUnvalid()
{
	DebugResource.clear();
	for (auto& HandleSet : AllocatedHandleList)
	{
		HandleSet.get()->bIsValid = false;
		++HandleSet.get()->DebugVersion;
	}
}

void FTexturePoolBase::MarkDebugDirty(TexturePoolHandleSet* InHandleSet)
{
	if (!InHandleSet)
	{
		return;
	}

	++InHandleSet->DebugVersion;
}

void FTexturePoolBase::SetTextureLayerSize(uint32 InTextureLayerSize)
{
	TextureLayerSize = InTextureLayerSize;
	OnSetTextureLayerSize();
}

void FTexturePoolBase::SetTextureSize(uint32 InTextureSize)
{
	TextureSize = InTextureSize;
	OnSetTextureSize();
}
