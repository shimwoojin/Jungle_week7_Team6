#include "TextureAtalsPool.h"

template<typename T>
using TComPtr = Microsoft::WRL::ComPtr<T>;
using TexturePoolHandle = FTexturePoolBase::TexturePoolHandle;
using TexturePoolHandleSet = FTexturePoolBase::TexturePoolHandleSet;

void FTextureAtlasPool::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, uint32 InTextureSize)
{
	CurrentFilterMode = EShadowFilterMode::PCF;
	FTexturePoolBase::Initialize(InDevice, InDeviceContext, InTextureSize);
}

void FTextureAtlasPool::EnsureAtlasMode(EShadowFilterMode InFilterMode)
{
	if (CurrentFilterMode == InFilterMode)
	{
		return;
	}

	CurrentFilterMode = InFilterMode;
	RecreateAtlasResources();
}

void FTextureAtlasPool::RecreateAtlasResources()
{
	ID3D11Device* Device = GetDevice();
	if (!Device)
	{
		return;
	}

	Texture = CreateTexture(Device);
	RebuildSRV(Device, Texture.Get());
	RebuildDSV(Device, Texture.Get());
	BroadCastHandlesUnvalid();
}

//들어갈 곳이 없으면 자동으로 Resize하는 로직이 추가 되었는데 일단은 그냥 두기
TexturePoolHandleSet* FTextureAtlasPool::GetTextureHandle(TexturePoolHandleRequest HandleRequest)
{
	uint32 ManagerCount = static_cast<uint32>(UVManagers.size());

	std::unique_ptr<TexturePoolHandleSet> HandleSet = std::make_unique<TexturePoolHandleSet>(
		this,
		static_cast<uint32>(AllocatedHandleList.size()));

	for (uint32 Size : HandleRequest.Sizes)
	{
		TexturePoolHandle Handle;
		bool bAllocated = false;
		while (!bAllocated)
		{
			for (uint32 i = 0; i < ManagerCount; ++i)
			{
				if (UVManagers[i].get()->GetHandle(static_cast<float>(Size), Handle.InternalIndex))
				{
					Handle.ArrayIndex = i;
					bAllocated = true;
					break;
				}
			}

			if (!bAllocated)
			{
				ResizeLayer();
				ManagerCount = static_cast<uint32>(UVManagers.size());
			}
		}

		HandleSet.get()->Handles.push_back(Handle);
	}
	HandleSet->bIsValid = true;
	AllocatedHandleList.push_back(std::move(HandleSet));
	return AllocatedHandleList.back().get();
}

void FTextureAtlasPool::ReleaseHandle(TexturePoolHandle& InHandle)
{
	UVManagers[InHandle.ArrayIndex].get()->ReleaseUV(InHandle.InternalIndex);
}

TArray<FAtlasUV> FTextureAtlasPool::GetAtlasUVArray(const TexturePoolHandleSet* InHandleSet)
{
	TArray<FAtlasUV> Result;
	for (const auto& Handle : InHandleSet->Handles)
	{
		Result.push_back(GetAtlasUV(Handle));
	}

	return Result;
}

TArray<ID3D11DepthStencilView*> FTextureAtlasPool::GetDSVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11DepthStencilView*> Result;
	if (!HandleSet) return Result;

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;

	for (const auto& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(DSVs.size()))
		{
			Result.push_back(DSVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

TArray<ID3D11RenderTargetView*> FTextureAtlasPool::GetRTVs(TexturePoolHandleSet* HandleSet)
{
	TArray<ID3D11RenderTargetView*> Result;
	if (!HandleSet || !IsVSMMode())
	{
		return Result;
	}

	TArray<TexturePoolHandle> Handles = HandleSet->Handles;
	for (const auto& Handle : Handles)
	{
		if (Handle.ArrayIndex < static_cast<uint32>(RTVs.size()))
		{
			Result.push_back(RTVs[Handle.ArrayIndex].Get());
		}
		else
		{
			Result.push_back(nullptr);
		}
	}

	return Result;
}

ID3D11ShaderResourceView* FTextureAtlasPool::GetDebugSRV(const TexturePoolHandle& InHandle)
{
	return nullptr;
}

TComPtr<ID3D11Texture2D> FTextureAtlasPool::CreateTexture(ID3D11Device* Device)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = TextureSize;
	desc.Height = TextureSize;
	desc.MipLevels = 1;
	desc.ArraySize = TextureLayerSize;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.CPUAccessFlags = 0;

	if (IsVSMMode())
	{
		desc.Format = DXGI_FORMAT_R32G32_FLOAT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	}
	else
	{
		desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
	}

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

TComPtr<ID3D11Texture2D> FTextureAtlasPool::CreateVSMDepthTexture(ID3D11Device* Device)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = TextureSize;
	desc.Height = TextureSize;
	desc.MipLevels = 1;
	desc.ArraySize = TextureLayerSize;
	desc.Format = DXGI_FORMAT_D32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

void FTextureAtlasPool::RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = TextureLayerSize;
	srvDesc.Format = IsVSMMode() ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT;

	SRV.Reset();
	HRESULT hr = Device->CreateShaderResourceView(InTexture, &srvDesc, SRV.GetAddressOf());
	assert(SUCCEEDED(hr));

	if (IsVSMMode())
	{
		RebuildRTVs(Device, InTexture);
	}
	else
	{
		RTVs.clear();
	}
}

void FTextureAtlasPool::RebuildDSV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	if (!IsVSMMode())
	{
		VSMDepthTexture.Reset();
		FTexturePoolBase::RebuildDSV(Device, InTexture);
		return;
	}

	VSMDepthTexture = CreateVSMDepthTexture(Device);

	DSVs.clear();
	DSVs.resize(TextureLayerSize);

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	for (uint32 SliceIndex = 0; SliceIndex < TextureLayerSize; ++SliceIndex)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = SliceIndex;

		HRESULT hr = Device->CreateDepthStencilView(
			VSMDepthTexture.Get(),
			&dsvDesc,
			DSVs[SliceIndex].GetAddressOf()
		);
		assert(SUCCEEDED(hr));
	}
}

void FTextureAtlasPool::RebuildRTVs(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	RTVs.clear();
	RTVs.resize(TextureLayerSize);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
	rtvDesc.Texture2DArray.MipSlice = 0;
	rtvDesc.Texture2DArray.ArraySize = 1;

	for (uint32 SliceIndex = 0; SliceIndex < TextureLayerSize; ++SliceIndex)
	{
		rtvDesc.Texture2DArray.FirstArraySlice = SliceIndex;

		HRESULT hr = Device->CreateRenderTargetView(
			InTexture,
			&rtvDesc,
			RTVs[SliceIndex].GetAddressOf()
		);

		assert(SUCCEEDED(hr));
	}
}

void FTextureAtlasPool::OnSetTextureSize()
{
	for (const auto& UVManager : UVManagers)
	{
		UVManager.get()->SetSize(TextureSize);
	}
}

void FTextureAtlasPool::OnSetTextureLayerSize()
{
	uint32 CurrentManagersCount = static_cast<uint32>(UVManagers.size());
	uint32 TargetCount = GetTextureLayerSize();

	for (uint32 i = CurrentManagersCount; i < TargetCount; ++i)
	{
		auto NewManager = std::make_unique<FGridUVManager>();
		NewManager->Initialize(TextureSize, 1024);

		UVManagers.push_back(std::move(NewManager));
	}
}
