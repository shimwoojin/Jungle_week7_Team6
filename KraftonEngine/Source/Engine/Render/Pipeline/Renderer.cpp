#include "Renderer.h"

#include "Render/Types/RenderTypes.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/TexturePool/TextureAtalsPool.h"
#include "Render/Resource/TexturePool/TextureCubeShadowPool.h"
#include "Core/Log.h"
#include "Render/Proxy/FScene.h"
#include "Render/Proxy/SceneEnvironment.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Profiling/Stats.h"
#include "Profiling/GPUProfiler.h"
#include "Materials/MaterialManager.h"
#include "Math/MathUtils.h"

#include <cfloat>
#include <cmath>

namespace
{
	struct FShadowPassConstants
	{
		FMatrix LightVP;
		FMatrix CameraVP;
		uint32  bIsPSM;
		uint32  _pad[3];
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

		static FMatrix MakePointShadowProjection(float AttenuationRadius, float& OutNearZ, float& OutFarZ)
		{
			OutNearZ = FMath::Clamp(AttenuationRadius * 0.01f, 0.05f, 5.0f);
			OutFarZ = AttenuationRadius > OutNearZ ? AttenuationRadius : (OutNearZ + 1.0f);
			return MakeReversedZPerspective(FMath::Pi * 0.5f, 1.0f, OutNearZ, OutFarZ);
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

		static bool MakeDirectionalShadowMatrix(
			const FFrameContext& Frame,
			const UDirectionalLightComponent& DirectionalLight,
			FMatrix& OutLightVP,
			float& OutNearZ)
		{
			const float ShadowDistance = FMath::Clamp(Frame.FarClip * 0.15f, 15.0f, 80.0f);
			const float ShadowExtent = FMath::Clamp(Frame.FarClip * 0.2f, 20.0f, 120.0f);
			const FVector CameraCenter = Frame.CameraPosition + Frame.CameraForward * (ShadowExtent * 0.5f);
			const FVector Eye = CameraCenter - DirectionalLight.GetForwardVector() * ShadowDistance;

			const FMatrix LightView = MakeAxesViewMatrix(
				Eye,
				DirectionalLight.GetRightVector(),
				DirectionalLight.GetUpVector(),
				DirectionalLight.GetForwardVector());

			const float NearZ = 0.1f;
			const FMatrix LightProj = MakeReversedZOrthographic(
				ShadowExtent * 2.0f,
				ShadowExtent * 2.0f,
				NearZ,
				ShadowDistance + ShadowExtent * 2.0f);

			OutLightVP = LightView * LightProj;
			OutNearZ = NearZ;
			return true;
		}

		static bool MakePerspectiveShadowMatrix(
			const FFrameContext& Frame,
			const UDirectionalLightComponent& DirectionalLight,
			FMatrix& OutLightVP,
			float& OutNearZ)
		{
			if (Frame.bIsOrtho)
			{
				return false;
			}

			const FMatrix CameraVP = Frame.View * Frame.Proj;
			const FVector LightDir = DirectionalLight.GetForwardVector().Normalized();
			const float FocusDistance = FMath::Clamp(Frame.FarClip * 0.05f, Frame.NearClip + 1.0f, 50.0f);
			const FVector FocusPoint = Frame.CameraPosition + Frame.CameraForward * FocusDistance;
			const FVector PSMDir = (CameraVP.TransformPositionWithW(FocusPoint + LightDir * FocusDistance)
				- CameraVP.TransformPositionWithW(FocusPoint)).Normalized();

			if (PSMDir.Length() < 0.0001f)
			{
				return false;
			}

			FVector UpHint(0.0f, 1.0f, 0.0f);
			if (fabsf(PSMDir.Dot(UpHint)) > 0.95f)
			{
				UpHint = FVector(1.0f, 0.0f, 0.0f);
			}

			FVector Right = UpHint.Cross(PSMDir).Normalized();
			FVector Up = PSMDir.Cross(Right).Normalized();

			const FVector Corners[8] =
			{
				FVector(-1.0f, -1.0f, 0.0f), FVector(-1.0f,  1.0f, 0.0f),
				FVector( 1.0f, -1.0f, 0.0f), FVector( 1.0f,  1.0f, 0.0f),
				FVector(-1.0f, -1.0f, 1.0f), FVector(-1.0f,  1.0f, 1.0f),
				FVector( 1.0f, -1.0f, 1.0f), FVector( 1.0f,  1.0f, 1.0f)
			};

			float MinX = FLT_MAX;
			float MinY = FLT_MAX;
			float MinZ = FLT_MAX;
			float MaxX = -FLT_MAX;
			float MaxY = -FLT_MAX;
			float MaxZ = -FLT_MAX;

			const FMatrix BasisView = MakeAxesViewMatrix(FVector(0.0f, 0.0f, 0.0f), Right, Up, PSMDir);
			for (const FVector& Corner : Corners)
			{
				const FVector LightSpaceCorner = BasisView.TransformPositionWithW(Corner);
				MinX = std::min(MinX, LightSpaceCorner.X);
				MinY = std::min(MinY, LightSpaceCorner.Y);
				MinZ = std::min(MinZ, LightSpaceCorner.Z);
				MaxX = std::max(MaxX, LightSpaceCorner.X);
				MaxY = std::max(MaxY, LightSpaceCorner.Y);
				MaxZ = std::max(MaxZ, LightSpaceCorner.Z);
			}

			const float XYPadding = 0.02f;
			const float ZPadding = 0.05f;
			const float Width = std::max(MaxX - MinX + XYPadding * 2.0f, 0.001f);
			const float Height = std::max(MaxY - MinY + XYPadding * 2.0f, 0.001f);
			const float Depth = std::max(MaxZ - MinZ + ZPadding * 2.0f, 0.001f);
			const FVector Eye = Right * ((MinX + MaxX) * 0.5f)
				+ Up * ((MinY + MaxY) * 0.5f)
				+ PSMDir * (MinZ - ZPadding);

			OutNearZ = ZPadding;
			OutLightVP = MakeAxesViewMatrix(Eye, Right, Up, PSMDir)
				* MakeReversedZOrthographic(Width, Height, OutNearZ, Depth + OutNearZ);
			return true;
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

		static D3D11_VIEWPORT MakeFullViewport(uint32 TextureSize)
		{
			D3D11_VIEWPORT Viewport = {};
			Viewport.TopLeftX = 0.0f;
			Viewport.TopLeftY = 0.0f;
			Viewport.Width = static_cast<float>(TextureSize);
			Viewport.Height = static_cast<float>(TextureSize);
			Viewport.MinDepth = 0.0f;
			Viewport.MaxDepth = 1.0f;
			return Viewport;
		}
	};

	bool IsOpaqueShadowCaster(const FPrimitiveSceneProxy* Proxy)
	{
		return Proxy
			&& Proxy->IsVisible()
			&& Proxy->CastsShadow()
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
	FTextureCubeShadowPool::Get().Initialize(Device.GetDevice(), 1024, 4);

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
	FTextureCubeShadowPool::Get().Release();
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

	const bool bUseVSM = Frame.RenderOptions.ShadowFilterMode == EShadowFilterMode::VSM;
	FTextureAtlasPool::Get().EnsureAtlasMode(Frame.RenderOptions.ShadowFilterMode);

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
#pragma region SearchUpdateNeededPointLight
	TArray<uint32> ShadowedPointIndices;
	ShadowedPointIndices.reserve(Env.GetNumPointLights());

	for (uint32 PointIndex = 0; PointIndex < Env.GetNumPointLights(); ++PointIndex)
	{
		const UPointLightComponent* PointLight = Env.GetPointLightOwner(PointIndex);
		if (!PointLight || !PointLight->IsCastShadow())
		{
			continue;
		}

		const FPointLightParams& Params = Env.GetPointLight(PointIndex);
		if (!Frame.FrustumVolume.IntersectAABB(FShadowUtil::MakeSphereBounds(Params.Position, Params.AttenuationRadius)))
		{
			continue;
		}

		ShadowedPointIndices.push_back(PointIndex);
	}
#pragma endregion

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
	for (uint32 PointIndex : ShadowedPointIndices)
	{
		const UPointLightComponent* PointLight = Env.GetPointLightOwner(PointIndex);
		if (PointLight)
		{
			const_cast<UPointLightComponent*>(PointLight)->GetShadowMapKey();
		}
	}
#pragma endregion

	// 2차 패스: 최종적으로 안정화된 handle/DSV/UV로 render task를 만든다.
#pragma region CreateRenderTask

	//PointLightTask 생성 관련
#pragma region PointLightTask
	for (uint32 PointIndex : ShadowedPointIndices)
	{
		const UPointLightComponent* PointLight = Env.GetPointLightOwner(PointIndex);
		const FPointLightParams& Params = Env.GetPointLight(PointIndex);
		if (!PointLight)
		{
			continue;
		}

		const FShadowMapKey ShadowMapKey = const_cast<UPointLightComponent*>(PointLight)->GetShadowMapKey();
		FShadowCubeHandle CubeHandle = ShadowMapKey.CubeMap;
		if (!CubeHandle.IsValid())
		{
			continue;
		}

		float NearZ = 0.0f;
		float FarZ = 0.0f;
		const FMatrix LightProj = FShadowUtil::MakePointShadowProjection(Params.AttenuationRadius, NearZ, FarZ);
		const D3D11_VIEWPORT CubeViewport = FShadowUtil::MakeFullViewport(FTextureCubeShadowPool::Get().GetResolution(CubeHandle));

		bool bAllFacesValid = true;
		for (uint32 FaceIndex = 0; FaceIndex < FTextureCubeShadowPool::CubeFaceCount; ++FaceIndex)
		{
			if (!FTextureCubeShadowPool::Get().GetFaceDSV(CubeHandle, FaceIndex))
			{
				bAllFacesValid = false;
				break;
			}
		}
		if (!bAllFacesValid)
		{
			continue;
		}

		for (uint32 FaceIndex = 0; FaceIndex < FTextureCubeShadowPool::CubeFaceCount; ++FaceIndex)
		{
			const FPointShadowFaceBasis FaceBasis = FTextureCubeShadowPool::GetFaceBasis(FaceIndex);
			const FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(
				Params.Position,
				FaceBasis.Right,
				FaceBasis.Up,
				FaceBasis.Forward);
			const FMatrix LightVP = LightView * LightProj;

			FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
			Task.TargetType = EShadowRenderTargetType::CubeFace;
			Task.LightVP = LightVP;
			Task.ShadowFrustum.UpdateFromMatrix(LightVP);
			Task.Viewport = CubeViewport;
			Task.DSV = FTextureCubeShadowPool::Get().GetFaceDSV(CubeHandle, FaceIndex);
			Task.CubeIndex = CubeHandle.CubeIndex;
			Task.CubeFaceIndex = FaceIndex;
			Task.ShadowDepthBias = PointLight->GetShadowBias();
			Task.ShadowSlopeBias = PointLight->GetShadowSlopeBias();
		}

		FShadowInfo Info = {};
		Info.Type = EShadowInfoType::CubeMap;
		Info.ArrayIndex = CubeHandle.CubeIndex;
		Info.LightIndex = PointIndex;
		Info.bIsPSM = 0;
		Info.CubeTierIndex = CubeHandle.TierIndex;
		Info.LightVP = FMatrix::Identity;
		Info.SampleData = FVector4(Params.Position.X, Params.Position.Y, Params.Position.Z, FarZ);
		Info.ShadowParams = FVector4(
			PointLight->GetShadowBias(),
			PointLight->GetShadowSlopeBias(),
			PointLight->GetShadowSharpen(),
			NearZ);

		const int32 ShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
		OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
		OutShadowPassData.BindingData.PointLightShadowIndices[PointIndex] = ShadowInfoIndex;
	}
#pragma endregion

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
		TArray<ID3D11RenderTargetView*> RTVs = bUseVSM ? FTextureAtlasPool::Get().GetRTVs(HandleSet) : TArray<ID3D11RenderTargetView*>();
		const bool bMissingVSMTarget = bUseVSM && (RTVs.empty() || !RTVs[0]);
		if (AtlasUVs.empty() || DSVs.empty() || !DSVs[0] || bMissingVSMTarget)
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
		Task.TargetType = EShadowRenderTargetType::Atlas2D;
		Task.LightVP = LightVP;
		Task.ShadowFrustum.UpdateFromMatrix(LightVP);
		Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
		Task.DSV = DSVs[0];
		Task.RTV = bUseVSM ? RTVs[0] : nullptr;
		Task.ShadowDepthBias = SpotLight->GetShadowBias();
		Task.ShadowSlopeBias = SpotLight->GetShadowSlopeBias();

		FShadowInfo Info = {};
		Info.Type = EShadowInfoType::Atlas2D;
		Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
		Info.LightIndex = NumPointLights + SpotIndex;
		Info.LightVP = LightVP;
		Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
		Info.ShadowParams = FVector4(SpotLight->GetShadowBias(), SpotLight->GetShadowSlopeBias(), SpotLight->GetShadowSharpen(), NearZ);

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
		TArray<ID3D11RenderTargetView*> RTVs = (bUseVSM && HandleSet) ? FTextureAtlasPool::Get().GetRTVs(HandleSet) : TArray<ID3D11RenderTargetView*>();
		OutShadowPassData.BindingData.ShadowMethod = static_cast<uint32>(Frame.RenderOptions.ShadowMethod);

		if (!AtlasUVs.empty() && !DSVs.empty() && DSVs[0] && (!bUseVSM || (!RTVs.empty() && RTVs[0])))
		{
			if (Frame.RenderOptions.ShadowMethod == EShadowMethod::Standard || Frame.RenderOptions.ShadowMethod == EShadowMethod::PSM)
			{
				FMatrix FinalLightVP = FMatrix::Identity;
				float ShadowNearZ = 0.1f;
				uint32 bIsPSM_Flag = 0;

				if (Frame.RenderOptions.ShadowMethod == EShadowMethod::PSM
					&& FShadowUtil::MakePerspectiveShadowMatrix(Frame, *DirectionalLight, FinalLightVP, ShadowNearZ))
				{
					bIsPSM_Flag = 1;
				}
				else
				{
					FShadowUtil::MakeDirectionalShadowMatrix(Frame, *DirectionalLight, FinalLightVP, ShadowNearZ);
				}

				FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
				Task.TargetType = EShadowRenderTargetType::Atlas2D;
				Task.LightVP = FinalLightVP;
				Task.bIsPSM = (bIsPSM_Flag != 0);
				Task.CameraVP = Frame.View * Frame.Proj;
				Task.bCullWithShadowFrustum = !Task.bIsPSM;
				Task.ShadowDepthBias = DirectionalLight->GetShadowBias();
				Task.ShadowSlopeBias = DirectionalLight->GetShadowSlopeBias();

				if (Task.bIsPSM)
				{
					Task.ShadowFrustum = Frame.FrustumVolume;
				}
				else
				{
					Task.ShadowFrustum.UpdateFromMatrix(FinalLightVP);
				}

				if (!AtlasUVs.empty() && !DSVs.empty())
				{
					Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[0], AtlasTextureSize);
					Task.DSV = DSVs[0];
					Task.RTV = (bUseVSM && !RTVs.empty()) ? RTVs[0] : nullptr;

					FShadowInfo Info = {};
					Info.Type = EShadowInfoType::Atlas2D;
					Info.ArrayIndex = AtlasUVs[0].ArrayIndex;
					Info.LightIndex = 0xffffffffu;
					Info.bIsPSM = bIsPSM_Flag;
					Info.LightVP = FinalLightVP;
					Info.SampleData = FVector4(AtlasUVs[0].u1, AtlasUVs[0].v1, AtlasUVs[0].u2, AtlasUVs[0].v2);
					Info.ShadowParams = FVector4(DirectionalLight->GetShadowBias(), DirectionalLight->GetShadowSlopeBias(), DirectionalLight->GetShadowSharpen(), ShadowNearZ);

					OutShadowPassData.BindingData.DirectionalShadowIndex =
						static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());
					OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
				}
				else
				{
					// If we can't allocate a shadow map, remove the task we just added
					OutShadowPassData.RenderTasks.pop_back();
				}
			}
			else if (Frame.RenderOptions.ShadowMethod == EShadowMethod::CSM)
			{
				int32 MaxCascades = static_cast<int32>(std::min(AtlasUVs.size(), DSVs.size()));
				if (bUseVSM && !RTVs.empty()) MaxCascades = static_cast<int32>(std::min(static_cast<size_t>(MaxCascades), RTVs.size()));
				const int32 NumCascades = std::min(MaxCascades, 4);
				OutShadowPassData.BindingData.NumCascades = NumCascades;
				const int32 BaseShadowInfoIndex = static_cast<int32>(OutShadowPassData.BindingData.ShadowInfos.size());

				if (NumCascades > 0)
				{
					float CascadeRanges[5]; // Near, Split1, Split2, Split3, Far
					CascadeRanges[0] = Frame.NearClip;
					CascadeRanges[NumCascades] = std::min(Frame.FarClip, 200.0f); // Limit shadow distance

					// Logarithmic split scheme
					float Lambda = 0.85f; // More weight on logarithmic split for better near resolution
					for (int i = 1; i < NumCascades; ++i)
					{
						float f = (float)i / (float)NumCascades;
						float LogSplit = CascadeRanges[0] * powf(CascadeRanges[NumCascades] / CascadeRanges[0], f);
						float UniSplit = CascadeRanges[0] + (CascadeRanges[NumCascades] - CascadeRanges[0]) * f;
						CascadeRanges[i] = Lambda * LogSplit + (1.0f - Lambda) * UniSplit;
					}

					FMatrix InvView = Frame.View.GetInverse();
					// Extract FOV and Aspect from Projection matrix
					// Proj.M[1][1] = 1/tan(FovY/2), Proj.M[0][0] = Cot/Aspect
					float TanHalfFovY = 1.0f / Frame.Proj.M[1][1];
					float Aspect = Frame.Proj.M[1][1] / Frame.Proj.M[0][0];

					FVector LightDir = DirectionalLight->GetForwardVector();
					FVector LightUp = DirectionalLight->GetUpVector();
					FVector LightRight = DirectionalLight->GetRightVector();

					for (int i = 0; i < NumCascades; ++i)
					{
						float zNear = CascadeRanges[i];
						float zFar = CascadeRanges[i + 1];

						// Calculate 8 corners of sub-frustum in View Space
						float yNear = zNear * TanHalfFovY;
						float xNear = yNear * Aspect;
						float yFar = zFar * TanHalfFovY;
						float xFar = yFar * Aspect;

						FVector Corners[8] = {
							{-xNear,  yNear, zNear}, { xNear,  yNear, zNear}, { xNear, -yNear, zNear}, {-xNear, -yNear, zNear},
							{-xFar,   yFar,  zFar }, { xFar,   yFar,  zFar }, { xFar,  -yFar,  zFar }, {-xFar,  -yFar,  zFar }
						};

						// Transform corners to World Space and find center
						FVector Center(0, 0, 0);
						for (int j = 0; j < 8; ++j)
						{
							Corners[j] = InvView.TransformPositionWithW(Corners[j]);
							Center += Corners[j];
						}
						Center /= 8.0f;

						// Find radius for tight bounding sphere
						float Radius = 0.0f;
						for (int j = 0; j < 8; ++j) Radius = std::max(Radius, (Corners[j] - Center).Length());

						// Create tight Orthographic projection
						FVector Eye = Center - LightDir * Radius * 2.0f;
						FMatrix LightView = FShadowUtil::MakeAxesViewMatrix(Eye, LightRight, LightUp, LightDir);
						FMatrix LightProj = FShadowUtil::MakeReversedZOrthographic(Radius * 2.0f, Radius * 2.0f, 0.1f, Radius * 6.0f);
						FMatrix LightVP = LightView * LightProj;

						FShadowRenderTask& Task = OutShadowPassData.RenderTasks.emplace_back();
						Task.TargetType = EShadowRenderTargetType::Atlas2D;
						Task.LightVP = LightVP;
						Task.bIsPSM = false;
						Task.CameraVP = Frame.View * Frame.Proj;
						Task.ShadowFrustum.UpdateFromMatrix(LightVP);
						Task.Viewport = FShadowUtil::MakeAtlasViewport(AtlasUVs[i], AtlasTextureSize);
						Task.DSV = DSVs[i];
						Task.RTV = (bUseVSM && i < RTVs.size()) ? RTVs[i] : nullptr;
						Task.ShadowDepthBias = DirectionalLight->GetShadowBias();
						Task.ShadowSlopeBias = DirectionalLight->GetShadowSlopeBias();

						FShadowInfo Info = {};
						Info.Type = EShadowInfoType::Atlas2D;
						Info.ArrayIndex = AtlasUVs[i].ArrayIndex;
						Info.LightIndex = 0xffffffffu;
						Info.bIsPSM = 0;
						Info.LightVP = LightVP;
						Info.SampleData = FVector4(AtlasUVs[i].u1, AtlasUVs[i].v1, AtlasUVs[i].u2, AtlasUVs[i].v2);
						Info.ShadowParams = FVector4(DirectionalLight->GetShadowBias(), DirectionalLight->GetShadowSlopeBias(), DirectionalLight->GetShadowSharpen(), 0.1f);

						OutShadowPassData.BindingData.CascadeMatrices[i] = LightVP;
						OutShadowPassData.BindingData.ShadowInfos.push_back(Info);
					}

					// Fill cascade splits for shader
					OutShadowPassData.BindingData.CascadeSplits = FVector4(CascadeRanges[1], CascadeRanges[2], CascadeRanges[3], CascadeRanges[4]);
					OutShadowPassData.BindingData.DirectionalShadowIndex = BaseShadowInfoIndex;
				}
			}
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
	const bool bUseVSM = Frame.RenderOptions.ShadowFilterMode == EShadowFilterMode::VSM;

	FShader* ShadowDepthShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowDepth);
	FShader* ShadowClearShader = FShaderManager::Get().GetOrCreate(EShaderPath::ShadowClear);
	FShader* ShadowDepthShaderVSM = bUseVSM
		? FShaderManager::Get().GetOrCreate(FShaderKey(EShaderPath::ShadowDepth, EShadowPassDefines::VSM))
		: nullptr;
	FShader* ShadowClearShaderVSM = bUseVSM
		? FShaderManager::Get().GetOrCreate(FShaderKey(EShaderPath::ShadowClear, EShadowPassDefines::VSM))
		: nullptr;
	if (!ShadowDepthShader || !ShadowClearShader || (bUseVSM && (!ShadowDepthShaderVSM || !ShadowClearShaderVSM)))
	{
		return;
	}

	Resources.UnbindShadowResources(Device);
	Resources.SetRasterizerState(Device, ERasterizerState::SolidBackCull);

	ID3D11Buffer* ShadowPassCBHandle = ShadowPassBuffer.GetBuffer();
	ID3D11Device* D3DDevice = Device.GetDevice();
	auto CreateShadowRasterizerState = [D3DDevice](const FShadowRenderTask& Task) -> ID3D11RasterizerState*
	{
		if (!D3DDevice)
		{
			return nullptr;
		}

		constexpr float DepthBiasScale = 100000.0f;
		const float ClampedDepthBias = FMath::Clamp(Task.ShadowDepthBias, 0.0f, 0.05f);
		const float ClampedSlopeBias = FMath::Clamp(Task.ShadowSlopeBias, 0.0f, 10.0f);

		D3D11_RASTERIZER_DESC RasterizerDesc = {};
		RasterizerDesc.FillMode = D3D11_FILL_SOLID;
		RasterizerDesc.CullMode = D3D11_CULL_BACK;
		RasterizerDesc.FrontCounterClockwise = FALSE;
		RasterizerDesc.DepthBias = -static_cast<INT>(ClampedDepthBias * DepthBiasScale);
		RasterizerDesc.DepthBiasClamp = 0.0f;
		RasterizerDesc.SlopeScaledDepthBias = -ClampedSlopeBias;
		RasterizerDesc.DepthClipEnable = TRUE;
		RasterizerDesc.ScissorEnable = FALSE;
		RasterizerDesc.MultisampleEnable = FALSE;
		RasterizerDesc.AntialiasedLineEnable = FALSE;

		ID3D11RasterizerState* RasterizerState = nullptr;
		if (FAILED(D3DDevice->CreateRasterizerState(&RasterizerDesc, &RasterizerState)))
		{
			return nullptr;
		}
		return RasterizerState;
	};

	for (const FShadowRenderTask& Task : ShadowPassData.RenderTasks)
	{
		if (!Task.DSV)
		{
			continue;
		}

		const bool bWriteMoments = bUseVSM && Task.TargetType == EShadowRenderTargetType::Atlas2D && Task.RTV != nullptr;
		FShader* ActiveShadowClearShader = bWriteMoments ? ShadowClearShaderVSM : ShadowClearShader;
		FShader* ActiveShadowDepthShader = bWriteMoments ? ShadowDepthShaderVSM : ShadowDepthShader;

		Resources.SetBlendState(Device, bWriteMoments ? EBlendState::Opaque : EBlendState::NoColor);
		Resources.SetRasterizerState(Device, ERasterizerState::SolidBackCull);

		if (Task.RTV)
		{
			Ctx->OMSetRenderTargets(1, &Task.RTV, Task.DSV);
		}
		else
		{
			Ctx->OMSetRenderTargets(0, nullptr, Task.DSV);
		}
		Ctx->RSSetViewports(1, &Task.Viewport);

		Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowClear);
		ActiveShadowClearShader->Bind(Ctx);
		if (!bWriteMoments)
		{
			Ctx->PSSetShader(nullptr, nullptr, 0);
		}
		Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Ctx->Draw(3, 0);

		Resources.SetDepthStencilState(Device, EDepthStencilState::ShadowDepth);
		ActiveShadowDepthShader->Bind(Ctx);
		if (!bWriteMoments)
		{
			Ctx->PSSetShader(nullptr, nullptr, 0);
		}

		ID3D11RasterizerState* ShadowRasterizerState = CreateShadowRasterizerState(Task);
		if (ShadowRasterizerState)
		{
			Ctx->RSSetState(ShadowRasterizerState);
		}

		FShadowPassConstants ShadowPassConstants = {};
		ShadowPassConstants.LightVP = Task.LightVP;
		ShadowPassConstants.CameraVP = Task.CameraVP;
		ShadowPassConstants.bIsPSM = Task.bIsPSM ? 1u : 0u;
		ShadowPassBuffer.Update(Ctx, &ShadowPassConstants, sizeof(FShadowPassConstants));
		Ctx->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &ShadowPassCBHandle);

		ID3D11Buffer* CurrentVB = nullptr;
		ID3D11Buffer* CurrentIB = nullptr;
		uint32 CurrentStride = 0;
		FConstantBuffer* CurrentPerObjectCB = nullptr;

		//단순 반복 Frustum컬링 후 DrawCall
		for (FPrimitiveSceneProxy* Proxy : Scene.GetAllProxies())
		{
			if (!IsOpaqueShadowCaster(Proxy)
				|| (Task.bCullWithShadowFrustum && !Task.ShadowFrustum.IntersectAABB(Proxy->GetCachedBounds())))
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

		if (ShadowRasterizerState)
		{
			ShadowRasterizerState->Release();
		}
		Resources.ResetRenderStateCache();

		//VSM이면 이후 블러 작업 필요
		//한 요쯤에 들어갈듯?
	}

	Resources.ResetRenderStateCache();

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


