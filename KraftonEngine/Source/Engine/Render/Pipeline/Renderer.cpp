#include "Renderer.h"

#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/TexturePool/TextureAtalsPool.h"
#include "Core/Log.h"
#include "Render/Proxy/FScene.h"
#include "Render/Proxy/SceneEnvironment.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Materials/MaterialManager.h"
#include "Math/MathUtils.h"

#include <cmath>

namespace
{
	struct FShadowPassConstants
	{
		FMatrix LightVP;
	};

	class FShadowUtil
	{
	public:
		static FBoundingBox MakeSphereBounds(const FVector& Center, float Radius)
		{
			const FVector Extent(Radius, Radius, Radius);
			return FBoundingBox(Center - Extent, Center + Extent);
		}

		static FMatrix MakeAxesViewMatrix(const FVector& Eye, FVector Right, FVector Up, FVector Forward)
		{
			Right.Normalize();
			Up.Normalize();
			Forward.Normalize();

			return FMatrix(
				Right.X, Up.X, Forward.X, 0.0f,
				Right.Y, Up.Y, Forward.Y, 0.0f,
				Right.Z, Up.Z, Forward.Z, 0.0f,
				-Eye.Dot(Right), -Eye.Dot(Up), -Eye.Dot(Forward), 1.0f);
		}

		static FMatrix MakeReversedZPerspective(float VerticalFovRadians, float AspectRatio, float NearZ, float FarZ)
		{
			const float Cot = 1.0f / tanf(VerticalFovRadians * 0.5f);
			const float Denom = NearZ - FarZ;
			return FMatrix(
				Cot / AspectRatio, 0.0f, 0.0f, 0.0f,
				0.0f, Cot, 0.0f, 0.0f,
				0.0f, 0.0f, NearZ / Denom, 1.0f,
				0.0f, 0.0f, -(FarZ * NearZ) / Denom, 0.0f);
		}

		static FMatrix MakeReversedZOrthographic(float Width, float Height, float NearZ, float FarZ)
		{
			const float HalfW = Width * 0.5f;
			const float HalfH = Height * 0.5f;
			const float Denom = NearZ - FarZ;
			return FMatrix(
				1.0f / HalfW, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f / HalfH, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f / Denom, 0.0f,
				0.0f, 0.0f, -FarZ / Denom, 1.0f);
		}

		static D3D11_VIEWPORT MakeAtlasViewport(const FAtlasUV& AtlasUV, uint32 AtlasTextureSize)
		{
			D3D11_VIEWPORT Viewport = {};
			Viewport.TopLeftX = AtlasUV.u1 * AtlasTextureSize;
			Viewport.TopLeftY = AtlasUV.v1 * AtlasTextureSize;
			Viewport.Width = (AtlasUV.u2 - AtlasUV.u1) * AtlasTextureSize;
			Viewport.Height = (AtlasUV.v2 - AtlasUV.v1) * AtlasTextureSize;
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			return Viewport;
		}
	};

	bool IsOpaqueShadowCaster(const FPrimitiveSceneProxy* Proxy)
	{
		return Proxy
			&& Proxy->IsVisible()
			&& Proxy->GetRenderPass() == ERenderPass::Opaque
			&& Proxy->GetMeshBuffer()
			&& Proxy->GetMeshBuffer()->IsValid();
	}
}

void FRenderer::Create(HWND hWindow)
{
	Device.Create(hWindow);

	if (Device.GetDevice() == nullptr)
	{
		UE_LOG("Failed to create D3D Device.");
	}

	FShaderManager::Get().Initialize(Device.GetDevice());
	Resources.Create(Device.GetDevice());
	FTextureAtlasPool::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext(), 4096);

	TileBasedCulling.Initialize(Device.GetDevice());
	ClusteredLightCuller.Initialize(Device.GetDevice(), Device.GetDeviceContext());

	PassRenderStateTable.Initialize();

	Builder.Create(Device.GetDevice(), Device.GetDeviceContext(), &PassRenderStateTable);
	ShadowPassBuffer.Create(Device.GetDevice(), sizeof(FShadowPassConstants));

	FGPUProfiler::Get().Initialize(Device.GetDevice(), Device.GetDeviceContext());
}

void FRenderer::Release()
{
	FGPUProfiler::Get().Shutdown();

	ShadowPassBuffer.Release();
	Builder.Release();

	Resources.Release();
	TileBasedCulling.Release();
	ClusteredLightCuller.Release();
	FShaderManager::Get().Release();
	FMaterialManager::Get().Release();
	Device.Release();
}

void FRenderer::BeginFrame()
{
	Device.BeginFrame();
}

//ShadowMap을 그리기 위한 ShadowRenderTask생성하는 부분
void FRenderer::BuildShadowPassData(const FFrameContext& Frame, const FScene& Scene, FShadowPassData& OutShadowPassData)
{
	const FSceneEnvironment& Env = Scene.GetEnvironment();
	OutShadowPassData.BindingData.PointLightShadowIndices.assign(Env.GetNumPointLights(), -1);
	OutShadowPassData.BindingData.SpotLightShadowIndices.assign(Env.GetNumSpotLights(), -1);
	OutShadowPassData.BindingData.DirectionalShadowIndex = -1;

	if (Frame.RenderOptions.ViewMode == EViewMode::Unlit)
	{
		return;
	}

	const uint32 AtlasTextureSize = FTextureAtlasPool::Get().GetTextureSize();
	const uint32 NumPointLights = Env.GetNumPointLights();
	const bool bShadowDirectional = [&Env]()
		{
			const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
			return DirectionalLight && DirectionalLight->IsCastShadow();
		}();

	//현재는 전체 중 쉐도우 옵션 켜지고 화면에 영향을 주는 놈들만, 
	//추후에 광 범위에 오브젝트가 들어와서 Depth에 변화가 생기는 놈들 까지 검사 추가
	//중요도에 따라서 컷하는 것도 추가
#pragma region SearchUpdateNeededSpotLight
	TArray<uint32> ShadowedSpotIndices;
	ShadowedSpotIndices.reserve(Env.GetNumSpotLights());

	for (uint32 SpotIndex = 0; SpotIndex < Env.GetNumSpotLights(); ++SpotIndex)
	{
		const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
		if (!SpotLight || !SpotLight->IsCastShadow())
		{
			continue;
		}

		const FSpotLightParams& Params = Env.GetSpotLight(SpotIndex);
		if (!Frame.FrustumVolume.IntersectAABB(FShadowUtil::MakeSphereBounds(Params.Position, Params.AttenuationRadius)))
		{
			continue;
		}

		ShadowedSpotIndices.push_back(SpotIndex);
	}
#pragma endregion	

	// 1차 패스: 필요한 핸들을 모두 확보해 atlas resize를 먼저 끝낸다.
	//라고는 하는데 Atlas 크기 제한있어서 이거 나중에 제한 걸거임, 애초에 위의 오브젝트 컬링 단계에서 필요한 놈들만 골랐으면 안걸어도 괜찮을지도
#pragma region ResizeState
	if (bShadowDirectional)
	{
		const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
		if (DirectionalLight)
		{
			const_cast<UDirectionalLightComponent*>(DirectionalLight)->GetShadowHandleSet();
		}
	}
	for (uint32 SpotIndex : ShadowedSpotIndices)
	{
		const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
		if (SpotLight)
		{
			const_cast<USpotLightComponent*>(SpotLight)->GetShadowHandleSet();
		}
	}
#pragma endregion

	// 2차 패스: 최종적으로 안정화된 handle/DSV/UV로 render task를 만든다.
#pragma region CreateRenderTask

	//SpotLightTask 생성 관련
#pragma region SpotLightTask
	for (uint32 SpotIndex : ShadowedSpotIndices)
	{
		const USpotLightComponent* SpotLight = Env.GetSpotLightOwner(SpotIndex);
		const FSpotLightParams& Params = Env.GetSpotLight(SpotIndex);
		FShadowHandleSet* HandleSet = SpotLight ? const_cast<USpotLightComponent*>(SpotLight)->GetShadowHandleSet() : nullptr;
		if (!HandleSet)
		{
			continue;
		}

		TArray<FAtlasUV> AtlasUVs = FTextureAtlasPool::Get().GetAtlasUVArray(HandleSet);
		TArray<ID3D11DepthStencilView*> DSVs = FTextureAtlasPool::Get().GetDSVs(HandleSet);
		if (AtlasUVs.empty() || DSVs.empty() || !DSVs[0])
		{
			continue;
		}

		const float OuterHalfAngle = acosf(FMath::Clamp(Params.OuterConeCos, -1.0f, 1.0f));
		const float NearZ = FMath::Clamp(Params.AttenuationRadius * 0.01f, 0.05f, 5.0f);
		const float FarZ = (Params.AttenuationRadius > NearZ + 1.0f) ? Params.AttenuationRadius : (NearZ + 1.0f);

		FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(
			SpotLight->GetWorldLocation(),
			SpotLight->GetRightVector(),
			SpotLight->GetUpVector(),
			SpotLight->GetForwardVector());
		FMatrix LightProj = FShadowUtil::MakeReversedZPerspective(OuterHalfAngle * 2.0f, 1.0f, NearZ, FarZ);
		FMatrix LightVP = LightView * LightProj;

		FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
		Task.LightVP = LightVP;
		Task.ShadowFrustum.UpdateFromMatrix(LightVP);
		Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
		Task.DSV = DSVs[0];

		FShadowInfo Info = {};
		Info.Type = 0;
		Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
		Info.LightIndex = NumPointLights + SpotIndex;
		Info.LightVP = LightVP;
		Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
		Info.ShadowParams = FVector4(SpotLight->GetShadowBias(), SpotLight->GetShadowSharpen(), 0.0f, 0.0f);

		const int32 ShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
		OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
		OutShadowPassData.BindingData.SpotLightShadowIndices[SpotIndex] = ShadowInfoIndex;
	}
#pragma endregion
	//DirectionLightTask 생성 관련
#pragma region DirectionLightTask
	//현재는 반복을 HandleSet의 첫번째 인덱스만 읽고 그놈을 기준으로 Light VP하나 생성하고 한번만 렌더링 하고있어 CSM은 불가,
	// PSM도 아닌 그냥 카메라 시점에서 멀리 떨어져서 HandleSet의 첫번째 해상도를 기준으로 그리는중
	const UDirectionalLightComponent* DirectionalLight = Env.GetGlobalDirectionalLightOwner();
	if (bShadowDirectional && DirectionalLight)
	{
		FShadowHandleSet* HandleSet = const_cast<UDirectionalLightComponent*>(DirectionalLight)->GetShadowHandleSet();
		TArray<FAtlasUV> AtlasUVs = HandleSet ? FTextureAtlasPool::Get().GetAtlasUVArray(HandleSet) : TArray<FAtlasUV>();
		TArray<ID3D11DepthStencilView*> DSVs = HandleSet ? FTextureAtlasPool::Get().GetDSVs(HandleSet) : TArray<ID3D11DepthStencilView*>();
		if (!AtlasUVs.empty() && !DSVs.empty() && DSVs[0])
		{
			const float ShadowDistance = FMath::Clamp(Frame.FarClip * 0.15f, 15.0f, 80.0f);
			const float ShadowExtent = FMath::Clamp(Frame.FarClip * 0.2f, 20.0f, 120.0f);
			const FVector CameraCenter = Frame.CameraPosition + Frame.CameraForward * (ShadowExtent * 0.5f);
			const FVector Eye = CameraCenter - DirectionalLight->GetForwardVector() * ShadowDistance;

			FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(
				Eye,
				DirectionalLight->GetRightVector(),
				DirectionalLight->GetUpVector(),
				DirectionalLight->GetForwardVector());
			FMatrix LightProj = FShadowUtil::MakeReversedZOrthographic(
				ShadowExtent * 2.0f,
				ShadowExtent * 2.0f,
				0.1f,
				ShadowDistance + ShadowExtent * 2.0f);
			FMatrix LightVP = LightView * LightProj;

			FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
			Task.LightVP = LightVP;
			Task.ShadowFrustum.UpdateFromMatrix(LightVP);
			Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
			Task.DSV = DSVs[0];

			FShadowInfo Info = {};
			Info.Type = 0;
			Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
			Info.LightIndex = 0xffffffffu;
			Info.LightVP = LightVP;
			Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
			Info.ShadowParams = FVector4(DirectionalLight->GetShadowBias(), DirectionalLight->GetShadowSharpen(), 0.0f, 0.0f);

			OutShadowPassData.BindingData.DirectionalShadowIndex =
				static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
			OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
		}
	}
#pragma endregion	
#pragma endregion
}

//생성된 ShadowTask들에 대해서 렌더링해서 ShadowMap 생성하는 과정. VSM 아직 불가
void FRenderer::RenderShadowPass(const FFrameContext& Frame, const FScene& Scene, const FShadowPassData& ShadowPassData)
{
	if (ShadowPassData.RenderTasks.empty())
	{
		return;
	}

	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();

	//VSM이면 ShadowDepthShader 쉐이더를 다르게 줘야함, RTV에 E[z], E[z^2] 그리는 Shader필요
	FShader* ShadowDepthShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowDepth);
	FShader* ShadowClearShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowClear);
	if (!ShadowDepthShader || !ShadowClearShader)
	{
		return;
	}

	Resources.UnbindShadowResources(Device);

	Resources.SetBlendState(Device, EBlendState::NoColor);
	Resources.SetRasterizerState(Device, ERasterizerState::SolidBackCull);

	ID3D11Buffer* ShadowPassCBHandle = ShadowPassBuffer.GetBuffer();

	for (const FShadowRenderTask& Task : ShadowPassData.RenderTasks)
	{
		//VSM이면 RTV도 설정해 줘야함 RTV는 TexturePool에서 관리 할거임 근데 아직 없음
		Ctx->OMSetRenderTargets(0, nullptr, Task.DSV);
		Ctx->RSSetViewports(1, &Task.Viewport);

		Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowClear);
		ShadowClearShader->Bind(Ctx);
		Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Ctx->Draw(3, 0);

		Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowDepth);
		ShadowDepthShader->Bind(Ctx);
		Ctx->PSSetShader(nullptr, nullptr, 0);

		FShadowPassConstants ShadowPassConstants = {};
		ShadowPassConstants.LightVP = Task.LightVP;
		ShadowPassBuffer.Update(Ctx, &ShadowPassConstants, sizeof(FShadowPassConstants));
		Ctx->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &ShadowPassCBHandle);

		ID3D11Buffer* CurrentVB = nullptr;
		ID3D11Buffer* CurrentIB = nullptr;
		uint32 CurrentStride = 0;
		FConstantBuffer* CurrentPerObjectCB = nullptr;

		//단순 반복 Frustum컬링 후 DrawCall
		for (FPrimitiveSceneProxy* Proxy : Scene.GetAllProxies())
		{
			if (!IsOpaqueShadowCaster(Proxy) || !Task.ShadowFrustum.IntersectAABB(Proxy->GetCachedBounds()))
			{
				continue;
			}

			FConstantBuffer* PerObjectCB = Builder.GetPerObjectCBForShadowPass(*Proxy);
			if (PerObjectCB && Proxy->NeedsPerObjectCBUpload())
			{
				PerObjectCB->Update(Ctx, &Proxy->GetPerObjectConstants(), sizeof(FPerObjectConstants));
				Proxy->ClearPerObjectCBDirty();
			}

			if (PerObjectCB && PerObjectCB != CurrentPerObjectCB)
			{
				ID3D11Buffer* RawPerObjectCB = PerObjectCB->GetBuffer();
				Ctx->VSSetConstantBuffers(ECBSlot::PerObject, 1, &RawPerObjectCB);
				CurrentPerObjectCB = PerObjectCB;
			}

			ID3D11Buffer* VB = Proxy->GetMeshBuffer()->GetVertexBuffer().GetBuffer();
			ID3D11Buffer* IB = Proxy->GetMeshBuffer()->GetIndexBuffer().GetBuffer();
			uint32 Stride = Proxy->GetMeshBuffer()->GetVertexBuffer().GetStride();
			if (!VB || !IB)
			{
				continue;
			}

			if (VB != CurrentVB || Stride != CurrentStride)
			{
				UINT Offset = 0;
				Ctx->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
				CurrentVB = VB;
				CurrentStride = Stride;
			}

			if (IB != CurrentIB)
			{
				Ctx->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);
				CurrentIB = IB;
			}

			Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			for (const FMeshSectionDraw& Section : Proxy->GetSectionDraws())
			{
				if (Section.IndexCount == 0)
				{
					continue;
				}

				Ctx->DrawIndexed(Section.IndexCount, Section.FirstIndex, 0);
			}
		}

		//VSM이면 이후 블러 작업 필요
		//한 요쯤에 들어갈듯?
	}

	if (Frame.ViewportRTV)
	{
		Ctx->OMSetRenderTargets(1, &Frame.ViewportRTV, Frame.ViewportDSV);
	}
	else if (Frame.ViewportDSV)
	{
		Ctx->OMSetRenderTargets(0, nullptr, Frame.ViewportDSV);
	}
	else
	{
		Ctx->OMSetRenderTargets(0, nullptr, nullptr);
	}
	D3D11_VIEWPORT MainViewport = {};
	MainViewport.Width = Frame.ViewportWidth;
	MainViewport.Height = Frame.ViewportHeight;
	MainViewport.MinDepth = 0.0f;
	MainViewport.MaxDepth = 1.0f;
	Ctx->RSSetViewports(1, &MainViewport);
}

// ============================================================
// Render — 정렬 + GPU 제출
// BeginCollect + Collector + BuildDynamicCommands 이후에 호출.
// ============================================================
void FRenderer::Render(const FFrameContext& Frame, FScene& Scene)
{
	FDrawCallStats::Reset();

	{
		SCOPE_STAT_CAT("UpdateFrameBuffer", "4_ExecutePass");
		Resources.UpdateFrameBuffer(Device, Frame);
	}

	Resources.BindSystemSamplers(Device);

	FShadowPassData ShadowPassData;
	BuildShadowPassData(Frame, Scene, ShadowPassData);

	{
		SCOPE_STAT_CAT("ShadowPass", "4_ExecutePass");
		RenderShadowPass(Frame, Scene, ShadowPassData);
	}

	{
		SCOPE_STAT_CAT("UpdateLightBuffer", "4_ExecutePass");

		FClusterCullingState& ClusterState = ClusteredLightCuller.GetCullingState();
		ClusterState.NearZ = Frame.NearClip;
		ClusterState.FarZ = Frame.FarClip;
		ClusterState.ScreenWidth = static_cast<uint32>(Frame.ViewportWidth);
		ClusterState.ScreenHeight = static_cast<uint32>(Frame.ViewportHeight);

		Resources.UpdateLightBuffer(Device, Scene, Frame, &ClusterState, &ShadowPassData.BindingData);
	}

	Resources.BindShadowResources(Device);

	FDrawCommandList& CommandList = Builder.GetCommandList();
	CommandList.Sort();

	FStateCache Cache;
	Cache.Reset();
	Cache.RTV = Frame.ViewportRTV;
	Cache.DSV = Frame.ViewportDSV;

	TArray<FPassEvent> PrePassEvents;
	TArray<FPassEvent> PostPassEvents;
	PassEventBuilder.Build(Device, Frame, Cache, this, PrePassEvents, PostPassEvents);

	for (uint32 i = 0; i < (uint32)ERenderPass::MAX; ++i)
	{
		ERenderPass CurPass = static_cast<ERenderPass>(i);

		for (auto& PrePassEvent : PrePassEvents)
		{
			PrePassEvent.TryExecute(CurPass);
		}

		uint32 Start, End;
		CommandList.GetPassRange(CurPass, Start, End);
		if (Start < End)
		{
			const char* PassName = GetRenderPassName(CurPass);
			SCOPE_STAT_CAT(PassName, "4_ExecutePass");
			GPU_SCOPE_STAT(PassName);
			CommandList.SubmitRange(Start, End, Device, Resources, Cache);
		}

		for (auto& PostPassEvent : PostPassEvents)
		{
			PostPassEvent.TryExecute(CurPass);
		}
	}

	CleanupPassState(Cache);
}

// ============================================================
// CleanupPassState — 패스 루프 종료 후 시스템 텍스처 언바인딩 + 캐시 정리
// ============================================================
void FRenderer::CleanupPassState(FStateCache& Cache)
{
	Resources.UnbindShadowResources(Device);
	Resources.UnbindSystemTextures(Device);
	Resources.UnbindTileCullingBuffers(Device);
	UnbindClusterCullingResources();

	Cache.Cleanup(Device.GetDeviceContext());
	Builder.GetCommandList().Reset();
}

void FRenderer::DispatchClusterCullingResources()
{
	if (!ClusteredLightCuller.IsInitialized())
	{
		return;
	}

	Resources.UnbindTileCullingBuffers(Device);
	UnbindClusterCullingResources();

	{
		GPU_SCOPE_STAT_CAT("Cluster Culling Dispatch", "Culling Dispatch");
		ClusteredLightCuller.DispatchLightCullingCS(Resources.ForwardLights.LightBufferSRV);
	}

	BindClusterCullingResources();
}

void FRenderer::BindClusterCullingResources()
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* LightIndexList = ClusteredLightCuller.GetLightIndexListSRV();
	ID3D11ShaderResourceView* LightGridList = ClusteredLightCuller.GetLightGridSRV();
	Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 1, &LightIndexList);
	Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightGrid, 1, &LightGridList);
	Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 1, &LightIndexList);
	Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightGrid, 1, &LightGridList);
}

void FRenderer::UnbindClusterCullingResources()
{
	ID3D11DeviceContext* Ctx = Device.GetDeviceContext();
	ID3D11ShaderResourceView* NullSRVs[2] = {};
	Ctx->VSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
	Ctx->PSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
	Ctx->CSSetShaderResources(ELightTexSlot::ClusterLightIndexList, 2, NullSRVs);
}

void FRenderer::EndFrame()
{
	Device.Present();
}
