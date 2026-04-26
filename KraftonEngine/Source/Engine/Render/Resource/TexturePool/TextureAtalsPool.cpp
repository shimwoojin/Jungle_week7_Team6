#include "TextureAtalsPool.h"

template<typename T>
using TComPtr = Microsoft::WRL::ComPtr<T>;
using TexturePoolHandle = FTexturePoolBase::TexturePoolHandle;
using TexturePoolHandleSet = FTexturePoolBase::TexturePoolHandleSet;


TexturePoolHandleSet* FTextureAtlasPool::GetTextureHandle(TexturePoolHandleRequest HandleRequest)
{
	uint32 ManagerCount = UVManagers.size();

	std::unique_ptr<TexturePoolHandleSet> HandleSet = std::make_unique<TexturePoolHandleSet>(this, AllocatedHandleList.size());

	for (uint32 Size : HandleRequest.Sizes)
	{
		TexturePoolHandle Handle;
		for (int i = 0; i < ManagerCount; i++)
		{
			if (UVManagers[i].get()->GetHandle(Size, Handle.InternalIndex))
			{
				Handle.ArrayIndex = i;
				break;
			}
		}

		HandleSet.get()->Handles.push_back(Handle);
	}
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
	TArray<TexturePoolHandle> Handles = HandleSet->Handles;

	for (const auto& Handle : Handles)
	{
		Result.push_back(DSVs[Handle.ArrayIndex].Get());
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
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.MiscFlags = 0;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;

	TComPtr<ID3D11Texture2D> NewTexture;
	HRESULT hr = Device->CreateTexture2D(&desc, nullptr, NewTexture.GetAddressOf());
	assert(SUCCEEDED(hr));

	return NewTexture;
}

void FTextureAtlasPool::RebuildSRV(ID3D11Device* Device, ID3D11Texture2D* InTexture)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // depth 읽을 때
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = TextureLayerSize;

	SRV.Reset();
	HRESULT hr = Device->CreateShaderResourceView(InTexture, &srvDesc, SRV.GetAddressOf());
	assert(SUCCEEDED(hr));
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
	uint32 CurrentManagersCount = UVManagers.size();
	uint32 TargetCount = GetTextureLayerSize();

	for (int i = CurrentManagersCount; i < TargetCount; i++)
	{
		std::unique_ptr<FUVManagerBase> NewManager = std::make_unique<FGridUVManager>();
		NewManager.get()->Initialize(TextureSize);

		UVManagers.push_back(std::move(NewManager));
	}
}

