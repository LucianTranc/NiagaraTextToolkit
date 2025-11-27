// Copyright Epic Games, Inc. All Rights Reserved.

#include "NTPNiagaraRendererFonts.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialRenderProxy.h"
#include "Engine/Font.h"
#include "NiagaraComponent.h"
#include "NiagaraCutoutVertexBuffer.h"
#include "NiagaraDataSet.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraSceneProxy.h"
#include "NiagaraSettings.h"
#include "NiagaraSortingGPU.h"
#include "NTPNiagaraStats.h"
#include "NiagaraSystemInstance.h"
#include "UObject/UObjectGlobals.h" // questionable
#include "ParticleResources.h"
#include "RayTracingInstance.h"
#include "RenderGraphBuilder.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("NumSprites"), STAT_NTP_NiagaraNumSprites, STATGROUP_NTP_Niagara);

static int32 GbEnableNiagaraSpriteRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraSpriteRendering(
	TEXT("fx.EnableNiagaraSpriteRendering"),
	GbEnableNiagaraSpriteRendering,
	TEXT("If == 0, Niagara Sprite Renderers are disabled. \n"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarRayTracingNiagaraSprites(
	TEXT("r.RayTracing.Geometry.NiagaraSprites"),
	1,
	TEXT("Include Niagara sprites in ray tracing effects (default = 1 (Niagara sprites enabled in ray tracing))"));


/** Dynamic data for font renderers. */
struct FNTPNiagaraDynamicDataFonts : public FNiagaraDynamicDataBase
{
	FNTPNiagaraDynamicDataFonts(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
	{
	}

	virtual void ApplyMaterialOverride(int32 MaterialIndex, UMaterialInterface* MaterialOverride) override
	{
		if (MaterialIndex == 0 && MaterialOverride)
		{
			Material = MaterialOverride->GetRenderProxy();
		}
	}

	FMaterialRenderProxy* Material = nullptr;
	TArray<UNiagaraDataInterface*> DataInterfacesBound;
	TArray<UObject*> ObjectsBound;
	TArray<uint8> ParameterDataBound;
};

//////////////////////////////////////////////////////////////////////////

FNTPNiagaraRendererFonts::FNTPNiagaraRendererFonts(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, InProps, Emitter)
	, Alignment(ENTPNiagaraSpriteAlignment::Unaligned)
	, FacingMode(ENTPNiagaraSpriteFacingMode::FaceCamera)
	, SortMode(ENiagaraSortMode::ViewDistance)
	, PivotInUVSpace(0.5f, 0.5f)
	, MacroUVRadius(0.0f)
	, NumIndicesPerInstance(0)
	, bRemoveHMDRollInVR(false)
	, bSortHighPrecision(false)
	, bSortOnlyWhenTranslucent(true)
	, bGpuLowLatencyTranslucency(true)
	, MinFacingCameraBlendDistance(0.0f)
	, MaxFacingCameraBlendDistance(0.0f)
	, MaterialParamValidMask(0)
{
	check(InProps && Emitter);

	const UNTPNiagaraFontRendererProperties* Properties = CastChecked<const UNTPNiagaraFontRendererProperties>(InProps);
	SourceMode = Properties->SourceMode;
	Alignment = Properties->Alignment;
	FacingMode = Properties->FacingMode;
	PivotInUVSpace = FVector2f(Properties->PivotInUVSpace);	// LWC_TODO: Precision loss
	MacroUVRadius = Properties->MacroUVRadius;
	SortMode = Properties->SortMode;
	NumIndicesPerInstance = Properties->GetNumIndicesPerInstance();
	bRemoveHMDRollInVR = Properties->bRemoveHMDRollInVR;
	bSortHighPrecision = UNiagaraRendererProperties::IsSortHighPrecision(Properties->SortPrecision);
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bGpuLowLatencyTranslucency = UNiagaraRendererProperties::IsGpuTranslucentThisFrame(FeatureLevel, Properties->GpuTranslucentLatency);
	MinFacingCameraBlendDistance = Properties->MinFacingCameraBlendDistance;
	MaxFacingCameraBlendDistance = Properties->MaxFacingCameraBlendDistance;
	bAccurateMotionVectors = Properties->NeedsPreciseMotionVectors();

	PixelCoverageMode = Properties->PixelCoverageMode;
	if (PixelCoverageMode == ENTPNiagaraRendererPixelCoverageMode::Automatic)
	{
		if ( GetDefault<UNiagaraSettings>()->DefaultPixelCoverageMode != ENiagaraDefaultRendererPixelCoverageMode::Enabled )
		{
			PixelCoverageMode = ENTPNiagaraRendererPixelCoverageMode::Disabled;
		}
	}
	PixelCoverageBlend = FMath::Clamp(Properties->PixelCoverageBlend, 0.0f, 1.0f);
	MaterialParamValidMask = Properties->MaterialParamValidMask;

	RendererLayoutWithCustomSort = &Properties->RendererLayoutWithCustomSort;
	RendererLayoutWithoutCustomSort = &Properties->RendererLayoutWithoutCustomSort;

	bSetAnyBoundVars = false;
	if (Emitter->GetRendererBoundVariables().IsEmpty() == false)
	{
		const TArray< const FNiagaraVariableAttributeBinding*>& VFBindings = Properties->GetAttributeBindings();
		const int32 NumBindings = bAccurateMotionVectors ? ENTPNiagaraSpriteVFLayout::Num_Max : ENTPNiagaraSpriteVFLayout::Num_Default;
		check(VFBindings.Num() >= ENTPNiagaraSpriteVFLayout::Type::Num_Max);

		for (int32 i = 0; i < ENTPNiagaraSpriteVFLayout::Type::Num_Max; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
			if (i < NumBindings && VFBindings[i] && VFBindings[i]->CanBindToHostParameterMap())
			{
				VFBoundOffsetsInParamStore[i] = Emitter->GetRendererBoundVariables().IndexOf(VFBindings[i]->GetParamMapBindableVariable());
				if (VFBoundOffsetsInParamStore[i] != INDEX_NONE)
					bSetAnyBoundVars = true;
			}
		}
	}
	else
	{
		for (int32 i = 0; i < ENTPNiagaraSpriteVFLayout::Type::Num_Max; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
		}
	}

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayoutWithoutCustomSort->GetVFVariables_GameThread();
	if (Alignment == ENTPNiagaraSpriteAlignment::Automatic)
	{
		const int32 RegisterIndex = SourceMode == ENiagaraRendererSourceDataMode::Particles ? VFVariables[ENTPNiagaraSpriteVFLayout::Alignment].GetGPUOffset() : VFBoundOffsetsInParamStore[ENTPNiagaraSpriteVFLayout::Alignment];
		Alignment = RegisterIndex == INDEX_NONE ? ENTPNiagaraSpriteAlignment::Unaligned : ENTPNiagaraSpriteAlignment::CustomAlignment;
	}
	if (FacingMode == ENTPNiagaraSpriteFacingMode::Automatic)
	{
		const int32 RegisterIndex = SourceMode == ENiagaraRendererSourceDataMode::Particles ? VFVariables[ENTPNiagaraSpriteVFLayout::Facing].GetGPUOffset() : VFBoundOffsetsInParamStore[ENTPNiagaraSpriteVFLayout::Facing];
		FacingMode = RegisterIndex == INDEX_NONE ? ENTPNiagaraSpriteFacingMode::FaceCamera : ENTPNiagaraSpriteFacingMode::CustomFacingVector;
	}
}

FNTPNiagaraRendererFonts::~FNTPNiagaraRendererFonts()
{
}

void FNTPNiagaraRendererFonts::ReleaseRenderThreadResources()
{
	FNiagaraRenderer::ReleaseRenderThreadResources();

#if RHI_RAYTRACING
	if (IsRayTracingAllowed())
	{
		RayTracingGeometry.ReleaseResource();
		RayTracingDynamicVertexBuffer.Release();
	}
#endif
}

void FNTPNiagaraRendererFonts::CreateRenderThreadResources()
{
	FNiagaraRenderer::CreateRenderThreadResources();
	FRHICommandListBase& RHICmdList = FRHICommandListImmediate::Get();

#if RHI_RAYTRACING
	if (IsRayTracingAllowed())
	{
		FRayTracingGeometryInitializer Initializer;
		static const FName DebugName("FNTPNiagaraRendererFonts");
		static int32 DebugNumber = 0;
		Initializer.DebugName = FDebugName(DebugName, DebugNumber++);
		Initializer.IndexBuffer = nullptr;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = false;
		RayTracingGeometry.SetInitializer(Initializer);
		RayTracingGeometry.InitResource(RHICmdList);
	}
#endif
}

bool FNTPNiagaraRendererFonts::AllowGPUSorting(EShaderPlatform ShaderPlatform)
{
	static const auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FX.AllowGPUSorting"));
	return CVar && (CVar->GetInt() != 0);
}

void FNTPNiagaraRendererFonts::PrepareParticleSpriteRenderData(FParticleSpriteRenderData& ParticleSpriteRenderData, const FSceneViewFamily& ViewFamily, FNiagaraDynamicDataBase* InDynamicData, const FNiagaraSceneProxy* SceneProxy, ENiagaraGpuComputeTickStage::Type GpuReadyTickStage) const
{
	ParticleSpriteRenderData.DynamicDataSprites = static_cast<FNTPNiagaraDynamicDataFonts*>(InDynamicData);
	if (!ParticleSpriteRenderData.DynamicDataSprites || !SceneProxy->GetComputeDispatchInterface())
	{
		ParticleSpriteRenderData.SourceParticleData = nullptr;
		return;
	}

	// Early out if we have no data or instances, this must be done before we read the material
	FNiagaraDataBuffer* CurrentParticleData = ParticleSpriteRenderData.DynamicDataSprites->GetParticleDataToRender(bGpuLowLatencyTranslucency);
	if (!CurrentParticleData || (SourceMode == ENiagaraRendererSourceDataMode::Particles && CurrentParticleData->GetNumInstances() == 0) || (GbEnableNiagaraSpriteRendering == 0))
	{
		return;
	}

	FMaterialRenderProxy* MaterialRenderProxy = ParticleSpriteRenderData.DynamicDataSprites->Material;
	check(MaterialRenderProxy);

	// Do we have anything to render?
	const FMaterial& Material = MaterialRenderProxy->GetIncompleteMaterialWithFallback(FeatureLevel);
	ParticleSpriteRenderData.BlendMode = Material.GetBlendMode();
	ParticleSpriteRenderData.bHasTranslucentMaterials = IsTranslucentBlendMode(Material);

	// If these conditions change please update the DebugHUD display also to reflect it
	bool bLowLatencyTranslucencyEnabled =
		ParticleSpriteRenderData.bHasTranslucentMaterials &&
		bGpuLowLatencyTranslucency &&
		GpuReadyTickStage >= CurrentParticleData->GetGPUDataReadyStage() &&
		!SceneProxy->CastsVolumetricTranslucentShadow() &&
		ViewFamilySupportLowLatencyTranslucency(ViewFamily);

	if (bLowLatencyTranslucencyEnabled && SceneProxy->ShouldRenderCustomDepth())
	{
		bLowLatencyTranslucencyEnabled &= !Material.IsTranslucencyWritingCustomDepth();
	}


	ParticleSpriteRenderData.SourceParticleData = ParticleSpriteRenderData.DynamicDataSprites->GetParticleDataToRender(bLowLatencyTranslucencyEnabled);
	if ( !ParticleSpriteRenderData.SourceParticleData || (SourceMode == ENiagaraRendererSourceDataMode::Particles && ParticleSpriteRenderData.SourceParticleData->GetNumInstances() == 0) )
	{
		ParticleSpriteRenderData.SourceParticleData = nullptr;
		return;
	}

	// Particle source mode
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		const EShaderPlatform ShaderPlatform = SceneProxy->GetComputeDispatchInterface()->GetShaderPlatform();

		// Determine if we need sorting
		ParticleSpriteRenderData.bNeedsSort = SortMode != ENiagaraSortMode::None && (IsAlphaCompositeBlendMode(Material) || IsAlphaHoldoutBlendMode(Material) || IsTranslucentOnlyBlendMode(Material) || !bSortOnlyWhenTranslucent);
		const bool bNeedCustomSort = ParticleSpriteRenderData.bNeedsSort && (SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending);
		ParticleSpriteRenderData.RendererLayout = bNeedCustomSort ? RendererLayoutWithCustomSort : RendererLayoutWithoutCustomSort;
		ParticleSpriteRenderData.SortVariable = bNeedCustomSort ? ENTPNiagaraSpriteVFLayout::CustomSorting : ENTPNiagaraSpriteVFLayout::Position;
		if (ParticleSpriteRenderData.bNeedsSort)
		{
			TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleSpriteRenderData.RendererLayout->GetVFVariables_RenderThread();
			const FNiagaraRendererVariableInfo& SortVariable = VFVariables[ParticleSpriteRenderData.SortVariable];
			ParticleSpriteRenderData.bNeedsSort = SortVariable.GetGPUOffset() != INDEX_NONE;
		}

		// No per-particle visibility or distance culling for this renderer.
		ParticleSpriteRenderData.bSortCullOnGpu = ParticleSpriteRenderData.bNeedsSort && AllowGPUSorting(ShaderPlatform);

		// Validate what we setup
		if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			if (!ensureMsgf(ParticleSpriteRenderData.bSortCullOnGpu, TEXT("Culling is requested on GPU but we don't support sorting, this will result in incorrect rendering.")))
			{
			}
			ParticleSpriteRenderData.bNeedsSort &= ParticleSpriteRenderData.bSortCullOnGpu;

			//-TODO: Culling and sorting from InitViewsAfterPrePass can not be respected if the culled entries have already been acquired
			if ((ParticleSpriteRenderData.bNeedsSort) && !SceneProxy->GetComputeDispatchInterface()->GetGPUInstanceCounterManager().CanAcquireCulledEntry())
			{
				//ensureMsgf(false, TEXT("Culling & sorting is not supported once the culled counts have been acquired, sorting & culling will be disabled for these draws."));
				ParticleSpriteRenderData.bNeedsSort = false;
			}
		}
		else
		{
			//-TODO: Culling and sorting from InitViewsAfterPrePass can not be respected if the culled entries have already been acquired
			if (ParticleSpriteRenderData.bSortCullOnGpu)
			{
				//ensureMsgf(false, TEXT("Culling & sorting is not supported once the culled counts have been acquired, sorting & culling will be disabled for these draws."));
				ParticleSpriteRenderData.bSortCullOnGpu &= SceneProxy->GetComputeDispatchInterface()->GetGPUInstanceCounterManager().CanAcquireCulledEntry();
			}

			// Should we GPU sort for CPU systems?
			if ( ParticleSpriteRenderData.bSortCullOnGpu )
			{
				const int32 NumInstances = ParticleSpriteRenderData.SourceParticleData->GetNumInstances();

				const int32 SortThreshold = GNiagaraGPUSortingCPUToGPUThreshold;
				const bool bSortMoveToGpu = (SortThreshold >= 0) && (NumInstances >= SortThreshold);

				ParticleSpriteRenderData.bSortCullOnGpu = bSortMoveToGpu;
			}
		}

		// Update layout as it could have changed
		ParticleSpriteRenderData.RendererLayout = bNeedCustomSort ? RendererLayoutWithCustomSort : RendererLayoutWithoutCustomSort;
	}
}

void FNTPNiagaraRendererFonts::PrepareParticleRenderBuffers(FRHICommandListBase& RHICmdList, FParticleSpriteRenderData& ParticleSpriteRenderData, FGlobalDynamicReadBuffer& DynamicReadBuffer) const
{
	if ( SourceMode == ENiagaraRendererSourceDataMode::Particles )
	{
		if ( SimTarget == ENiagaraSimTarget::CPUSim )
		{
			// For CPU simulations we do not gather int parameters inside TransferDataToGPU currently so we need to copy off
			// integrate attributes if we are culling on the GPU.
			TArray<uint32, TInlineAllocator<1>> IntParamsToCopy;
			
			FParticleRenderData ParticleRenderData = TransferDataToGPU(RHICmdList, DynamicReadBuffer, ParticleSpriteRenderData.RendererLayout, IntParamsToCopy, ParticleSpriteRenderData.SourceParticleData);
			const uint32 NumInstances = ParticleSpriteRenderData.SourceParticleData->GetNumInstances();

			ParticleSpriteRenderData.ParticleFloatSRV = GetSrvOrDefaultFloat(ParticleRenderData.FloatData);
			ParticleSpriteRenderData.ParticleHalfSRV = GetSrvOrDefaultHalf(ParticleRenderData.HalfData);
			ParticleSpriteRenderData.ParticleIntSRV = GetSrvOrDefaultInt(ParticleRenderData.IntData);
			ParticleSpriteRenderData.ParticleFloatDataStride = ParticleRenderData.FloatStride / sizeof(float);
			ParticleSpriteRenderData.ParticleHalfDataStride = ParticleRenderData.HalfStride / sizeof(FFloat16);
			ParticleSpriteRenderData.ParticleIntDataStride = ParticleRenderData.IntStride / sizeof(int32);
		}
		else
		{
			ParticleSpriteRenderData.ParticleFloatSRV = GetSrvOrDefaultFloat(ParticleSpriteRenderData.SourceParticleData->GetGPUBufferFloat());
			ParticleSpriteRenderData.ParticleHalfSRV = GetSrvOrDefaultHalf(ParticleSpriteRenderData.SourceParticleData->GetGPUBufferHalf());
			ParticleSpriteRenderData.ParticleIntSRV = GetSrvOrDefaultInt(ParticleSpriteRenderData.SourceParticleData->GetGPUBufferInt());
			ParticleSpriteRenderData.ParticleFloatDataStride = ParticleSpriteRenderData.SourceParticleData->GetFloatStride() / sizeof(float);
			ParticleSpriteRenderData.ParticleHalfDataStride = ParticleSpriteRenderData.SourceParticleData->GetHalfStride() / sizeof(FFloat16);
			ParticleSpriteRenderData.ParticleIntDataStride = ParticleSpriteRenderData.SourceParticleData->GetInt32Stride() / sizeof(int32);
		}
	}
	else
	{
		ParticleSpriteRenderData.ParticleFloatSRV = FNiagaraRenderer::GetDummyFloatBuffer();
		ParticleSpriteRenderData.ParticleHalfSRV = FNiagaraRenderer::GetDummyHalfBuffer();
		ParticleSpriteRenderData.ParticleIntSRV = FNiagaraRenderer::GetDummyIntBuffer();
		ParticleSpriteRenderData.ParticleFloatDataStride = 0;
		ParticleSpriteRenderData.ParticleHalfDataStride = 0;
		ParticleSpriteRenderData.ParticleIntDataStride = 0;
	}
}

void FNTPNiagaraRendererFonts::InitializeSortInfo(FParticleSpriteRenderData& ParticleSpriteRenderData, const FNiagaraSceneProxy& SceneProxy, const FSceneView& View, int32 ViewIndex, FNiagaraGPUSortInfo& OutSortInfo) const
{
	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleSpriteRenderData.RendererLayout->GetVFVariables_RenderThread();

	OutSortInfo.ParticleCount = ParticleSpriteRenderData.SourceParticleData->GetNumInstances();
	OutSortInfo.SortMode = SortMode;
	OutSortInfo.SetSortFlags(bSortHighPrecision, ParticleSpriteRenderData.SourceParticleData->GetGPUDataReadyStage());
	OutSortInfo.bEnableCulling = false;
	OutSortInfo.SystemLWCTile = UseLocalSpace(&SceneProxy) ? FVector3f::Zero() : SceneProxy.GetLWCRenderTile();

	OutSortInfo.CullPositionAttributeOffset = INDEX_NONE;

	auto GetViewMatrices =
		[](const FSceneView& View) -> const FViewMatrices&
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (const FViewMatrices* ViewMatrices = View.State ? View.State->GetFrozenViewMatrices() : nullptr)
			{
				// Don't retrieve the cached matrices for shadow views
				bool bIsShadow = View.GetDynamicMeshElementsShadowCullFrustum() != nullptr;
				if (!bIsShadow)
				{
					return *ViewMatrices;
				}
			}
#endif

			return View.ViewMatrices;
		};

	const FViewMatrices& ViewMatrices = GetViewMatrices(View);
	OutSortInfo.ViewOrigin = ViewMatrices.GetViewOrigin();
	OutSortInfo.ViewDirection = ViewMatrices.GetViewMatrix().GetColumn(2);

	if (UseLocalSpace(&SceneProxy))
	{
		OutSortInfo.ViewOrigin = SceneProxy.GetLocalToWorldInverse().TransformPosition(OutSortInfo.ViewOrigin);
		OutSortInfo.ViewDirection = SceneProxy.GetLocalToWorld().GetTransposed().TransformVector(OutSortInfo.ViewDirection);
	}

	if (ParticleSpriteRenderData.bSortCullOnGpu)
	{
		FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface = SceneProxy.GetComputeDispatchInterface();

		OutSortInfo.ParticleDataFloatSRV = ParticleSpriteRenderData.ParticleFloatSRV;
		OutSortInfo.ParticleDataHalfSRV = ParticleSpriteRenderData.ParticleHalfSRV;
		OutSortInfo.ParticleDataIntSRV = ParticleSpriteRenderData.ParticleIntSRV;
		OutSortInfo.FloatDataStride = ParticleSpriteRenderData.ParticleFloatDataStride;
		OutSortInfo.HalfDataStride = ParticleSpriteRenderData.ParticleHalfDataStride;
		OutSortInfo.IntDataStride = ParticleSpriteRenderData.ParticleIntDataStride;
		OutSortInfo.GPUParticleCountSRV = GetSrvOrDefaultUInt(ComputeDispatchInterface->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
		OutSortInfo.GPUParticleCountOffset = ParticleSpriteRenderData.SourceParticleData->GetGPUInstanceCountBufferOffset();
	}

	if (ParticleSpriteRenderData.SortVariable != INDEX_NONE)
	{
		const FNiagaraRendererVariableInfo& SortVariable = VFVariables[ParticleSpriteRenderData.SortVariable];
		OutSortInfo.SortAttributeOffset = ParticleSpriteRenderData.bSortCullOnGpu ? SortVariable.GetGPUOffset() : SortVariable.GetEncodedDatasetOffset();
	}
}

void FNTPNiagaraRendererFonts::SetupVertexFactory(FRHICommandListBase& RHICmdList, FParticleSpriteRenderData& ParticleSpriteRenderData, FNTPNiagaraFontVertexFactory& VertexFactory) const
{
	VertexFactory.SetParticleFactoryType(NVFT_Sprite);

	// Set facing / alignment
	{
		ENTPNiagaraSpriteFacingMode ActualFacingMode = FacingMode;
		ENTPNiagaraSpriteAlignment ActualAlignmentMode = Alignment;

		int32 FacingVarOffset = INDEX_NONE;
		int32 AlignmentVarOffset = INDEX_NONE;
		if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
		{
			TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleSpriteRenderData.RendererLayout->GetVFVariables_RenderThread();
			FacingVarOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Facing].GetGPUOffset();
			AlignmentVarOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Alignment].GetGPUOffset();
		}

		if ((FacingVarOffset == INDEX_NONE) && (VFBoundOffsetsInParamStore[ENTPNiagaraSpriteVFLayout::Facing] == INDEX_NONE) && (ActualFacingMode == ENTPNiagaraSpriteFacingMode::CustomFacingVector))
		{
			ActualFacingMode = ENTPNiagaraSpriteFacingMode::FaceCamera;
		}

		if ((AlignmentVarOffset == INDEX_NONE) && (VFBoundOffsetsInParamStore[ENTPNiagaraSpriteVFLayout::Alignment] == INDEX_NONE) && (ActualAlignmentMode == ENTPNiagaraSpriteAlignment::CustomAlignment))
		{
			ActualAlignmentMode = ENTPNiagaraSpriteAlignment::Unaligned;
		}

		VertexFactory.SetAlignmentMode((uint32)ActualAlignmentMode);
		VertexFactory.SetFacingMode((uint32)ActualFacingMode);
	}
	
	// The InitResource needs to happen at the end here as SetVertexBufferOverride will set the UV buffers.
	VertexFactory.InitResource(RHICmdList);
}

FNTPNiagaraFontUniformBufferRef FNTPNiagaraRendererFonts::CreateViewUniformBuffer(FParticleSpriteRenderData& ParticleSpriteRenderData, const FSceneView& View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy& SceneProxy, FNTPNiagaraFontVertexFactory& VertexFactory) const
{
	FNTPNiagaraFontUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters, sizeof(PerViewUniformParameters)); // Clear unset bytes

	const bool bUseLocalSpace = UseLocalSpace(&SceneProxy);
	PerViewUniformParameters.bLocalSpace = bUseLocalSpace;
	PerViewUniformParameters.RotationBias = 0.0f;
	PerViewUniformParameters.RotationScale = 1.0f;
	PerViewUniformParameters.TangentSelector = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.DeltaSeconds = ViewFamily.Time.GetDeltaWorldTimeSeconds();
	PerViewUniformParameters.NormalsType = 0.0f;
	PerViewUniformParameters.NormalsSphereCenter = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.NormalsCylinderUnitDirection = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
	PerViewUniformParameters.MacroUVParameters = CalcMacroUVParameters(View, SceneProxy.GetActorPosition(), MacroUVRadius);
	PerViewUniformParameters.CameraFacingBlend = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.RemoveHMDRoll = bRemoveHMDRollInVR ? 0.0f : 1.0f;

	if (bUseLocalSpace)
	{
		PerViewUniformParameters.DefaultPos = FVector4f(0.0f, 0.0f, 0.0f, 1.0f);
	}
	else
	{
		PerViewUniformParameters.DefaultPos = FVector3f(SceneProxy.GetLocalToWorld().GetOrigin() - FVector(SceneProxy.GetLWCRenderTile()) * FLargeWorldRenderScalar::GetTileSize());  // LWC_TODO: precision loss
	}
	PerViewUniformParameters.DefaultPrevPos = PerViewUniformParameters.DefaultPos;
	PerViewUniformParameters.DefaultSize = FVector2f(50.f, 50.0f);
	PerViewUniformParameters.DefaultPrevSize = PerViewUniformParameters.DefaultSize;
	PerViewUniformParameters.DefaultUVScale = FVector2f(1.0f, 1.0f);
	PerViewUniformParameters.DefaultUVRect = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultPivotOffset = PivotInUVSpace;
	PerViewUniformParameters.DefaultPrevPivotOffset = PerViewUniformParameters.DefaultPivotOffset;
	PerViewUniformParameters.DefaultVelocity = FVector3f(0.f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultPrevVelocity = PerViewUniformParameters.DefaultVelocity;
	PerViewUniformParameters.SystemLWCTile = SceneProxy.GetLWCRenderTile();
	PerViewUniformParameters.DefaultRotation = 0.0f;
	PerViewUniformParameters.DefaultPrevRotation = PerViewUniformParameters.DefaultRotation;
	PerViewUniformParameters.DefaultColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultMatRandom = 0.0f;
	PerViewUniformParameters.DefaultCamOffset = 0.0f;
	PerViewUniformParameters.DefaultPrevCamOffset = PerViewUniformParameters.DefaultCamOffset;
	PerViewUniformParameters.DefaultNormAge = 0.0f;
	PerViewUniformParameters.DefaultFacing = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultPrevFacing = PerViewUniformParameters.DefaultFacing;
	PerViewUniformParameters.DefaultAlignment = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultPrevAlignment = PerViewUniformParameters.DefaultAlignment;
	PerViewUniformParameters.DefaultDynamicMaterialParameter0 = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter1 = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter2 = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter3 = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);

	PerViewUniformParameters.PrevPositionDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevVelocityDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevRotationDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevSizeDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevFacingDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevAlignmentDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevCameraOffsetDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevPivotOffsetDataOffset = INDEX_NONE;

	// Determine pixel coverage settings
	const bool PixelCoverageEnabled = View.IsPerspectiveProjection() && (PixelCoverageMode != ENTPNiagaraRendererPixelCoverageMode::Disabled);
	PerViewUniformParameters.PixelCoverageEnabled = PixelCoverageEnabled;
	PerViewUniformParameters.PixelCoverageColorBlend = FVector4f::Zero();
	if (PixelCoverageEnabled)
	{
		if ( PixelCoverageMode == ENTPNiagaraRendererPixelCoverageMode::Automatic )
		{
			PerViewUniformParameters.PixelCoverageEnabled = ParticleSpriteRenderData.bHasTranslucentMaterials;
			if (PerViewUniformParameters.PixelCoverageEnabled)
			{
				if (IsTranslucentOnlyBlendMode(ParticleSpriteRenderData.BlendMode))
				{
					ParticleSpriteRenderData.bHasTranslucentMaterials = true;
					PerViewUniformParameters.PixelCoverageColorBlend = FVector4f(PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend, 0.0f);
				}
				else if (IsAdditiveBlendMode(ParticleSpriteRenderData.BlendMode))
				{
					ParticleSpriteRenderData.bHasTranslucentMaterials = true;
					PerViewUniformParameters.PixelCoverageColorBlend = FVector4f(PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend);
				}
				else
				{
					//-TODO: Support these blend modes
					//BLEND_Modulate
					//BLEND_AlphaComposite
					//BLEND_AlphaHoldout
					ParticleSpriteRenderData.bHasTranslucentMaterials = false;
				}
			}
		}
		else
		{
			PerViewUniformParameters.PixelCoverageEnabled = true;
			switch (PixelCoverageMode)
			{
				case ENTPNiagaraRendererPixelCoverageMode::Enabled_RGBA:	PerViewUniformParameters.PixelCoverageColorBlend = FVector4f(PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend); break;
				case ENTPNiagaraRendererPixelCoverageMode::Enabled_RGB:	PerViewUniformParameters.PixelCoverageColorBlend = FVector4f(PixelCoverageBlend, PixelCoverageBlend, PixelCoverageBlend, 0.0f); break;
				case ENTPNiagaraRendererPixelCoverageMode::Enabled_A:		PerViewUniformParameters.PixelCoverageColorBlend = FVector4f(0.0f, 0.0f, 0.0f, PixelCoverageBlend); break;
				default: break;
			}
		}
	}

	PerViewUniformParameters.AccurateMotionVectors = false;
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleSpriteRenderData.RendererLayout->GetVFVariables_RenderThread();
		PerViewUniformParameters.PositionDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Position].GetGPUOffset();
		PerViewUniformParameters.VelocityDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Velocity].GetGPUOffset();
		PerViewUniformParameters.RotationDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Rotation].GetGPUOffset();
		PerViewUniformParameters.SizeDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Size].GetGPUOffset();
		PerViewUniformParameters.ColorDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Color].GetGPUOffset();
		PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::MaterialParam0].GetGPUOffset();
		PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::MaterialParam1].GetGPUOffset();
		PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::MaterialParam2].GetGPUOffset();
		PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::MaterialParam3].GetGPUOffset();
		PerViewUniformParameters.FacingDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Facing].GetGPUOffset();
		PerViewUniformParameters.AlignmentDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::Alignment].GetGPUOffset();
		PerViewUniformParameters.CameraOffsetDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::CameraOffset].GetGPUOffset();
		PerViewUniformParameters.UVScaleDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::UVScale].GetGPUOffset();
		PerViewUniformParameters.PivotOffsetDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PivotOffset].GetGPUOffset();
		PerViewUniformParameters.UVRectDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::UVRect].GetGPUOffset();
		PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::NormalizedAge].GetGPUOffset();
		PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::MaterialRandom].GetGPUOffset();
		if (bAccurateMotionVectors)
		{
			PerViewUniformParameters.AccurateMotionVectors = true;
			PerViewUniformParameters.PrevPositionDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevPosition].GetGPUOffset();
			PerViewUniformParameters.PrevVelocityDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevVelocity].GetGPUOffset();
			PerViewUniformParameters.PrevRotationDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevRotation].GetGPUOffset();
			PerViewUniformParameters.PrevSizeDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevSize].GetGPUOffset();
			PerViewUniformParameters.PrevFacingDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevFacing].GetGPUOffset();
			PerViewUniformParameters.PrevAlignmentDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevAlignment].GetGPUOffset();
			PerViewUniformParameters.PrevCameraOffsetDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevCameraOffset].GetGPUOffset();
			PerViewUniformParameters.PrevPivotOffsetDataOffset = VFVariables[ENTPNiagaraSpriteVFLayout::PrevPivotOffset].GetGPUOffset();
		}
	}
	else if (SourceMode == ENiagaraRendererSourceDataMode::Emitter) // Clear all these out because we will be using the defaults to specify them
	{
		PerViewUniformParameters.PositionDataOffset = INDEX_NONE;
		PerViewUniformParameters.VelocityDataOffset = INDEX_NONE;
		PerViewUniformParameters.RotationDataOffset = INDEX_NONE;
		PerViewUniformParameters.SizeDataOffset = INDEX_NONE;
		PerViewUniformParameters.ColorDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParamDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam1DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam2DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam3DataOffset = INDEX_NONE;
		PerViewUniformParameters.FacingDataOffset = INDEX_NONE;
		PerViewUniformParameters.AlignmentDataOffset = INDEX_NONE;
		PerViewUniformParameters.CameraOffsetDataOffset = INDEX_NONE;
		PerViewUniformParameters.UVScaleDataOffset = INDEX_NONE;
		PerViewUniformParameters.PivotOffsetDataOffset = INDEX_NONE;
		PerViewUniformParameters.UVRectDataOffset = INDEX_NONE;
		PerViewUniformParameters.NormalizedAgeDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialRandomDataOffset = INDEX_NONE;
	}
	else
	{
		// Unsupported source data mode detected
		check(SourceMode <= ENiagaraRendererSourceDataMode::Emitter);
	}

	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;

	if (bSetAnyBoundVars)
	{
		const FNTPNiagaraDynamicDataFonts* DynamicDataSprites = ParticleSpriteRenderData.DynamicDataSprites;
		const int32 NumLayoutVars = bAccurateMotionVectors ? ENTPNiagaraSpriteVFLayout::Num_Max : ENTPNiagaraSpriteVFLayout::Num_Default;
		for (int32 i = 0; i < NumLayoutVars; i++)
		{
			if (VFBoundOffsetsInParamStore[i] != INDEX_NONE && DynamicDataSprites->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[i]))
			{
				switch (i)
				{
				case ENTPNiagaraSpriteVFLayout::Type::Position:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPos, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Color:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultColor, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FLinearColor));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Velocity:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultVelocity, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Rotation:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultRotation, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Size:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultSize, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Facing:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultFacing, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::Alignment:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultAlignment, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::MaterialParam0:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter0, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4f));
					PerViewUniformParameters.MaterialParamValidMask |= 0x000f;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::MaterialParam1:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter1, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4f));
					PerViewUniformParameters.MaterialParamValidMask |= 0x00f0;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::MaterialParam2:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter2, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4f));
					PerViewUniformParameters.MaterialParamValidMask |= 0x0f00;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::MaterialParam3:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter3, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4f));
					PerViewUniformParameters.MaterialParamValidMask |= 0xf000;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::CameraOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultCamOffset, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::UVScale:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultUVScale, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PivotOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPivotOffset, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::UVRect:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultUVRect, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector4f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::MaterialRandom:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultMatRandom, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::CustomSorting:
					// unsupport for now...
					break;
				case ENTPNiagaraSpriteVFLayout::Type::NormalizedAge:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultNormAge, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevPosition:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevPos, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevVelocity:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevVelocity, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevRotation:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevRotation, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevSize:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevSize, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevFacing:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevFacing, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevAlignment:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevAlignment, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector3f));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevCameraOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevCamOffset, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevPivotOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevPivotOffset, DynamicDataSprites->ParameterDataBound.GetData() + VFBoundOffsetsInParamStore[i], sizeof(FVector2f));
					break;
				}
			}
			else
			{
				switch (i)
				{
				case ENTPNiagaraSpriteVFLayout::Type::PrevPosition:
					PerViewUniformParameters.DefaultPrevPos = PerViewUniformParameters.DefaultPos;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevVelocity:
					PerViewUniformParameters.DefaultPrevVelocity = PerViewUniformParameters.DefaultVelocity;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevRotation:
					PerViewUniformParameters.DefaultPrevRotation = PerViewUniformParameters.DefaultRotation;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevSize:
					PerViewUniformParameters.DefaultPrevSize = PerViewUniformParameters.DefaultSize;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevFacing:
					PerViewUniformParameters.DefaultPrevFacing = PerViewUniformParameters.DefaultFacing;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevAlignment:
					PerViewUniformParameters.DefaultPrevAlignment = PerViewUniformParameters.DefaultAlignment;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevCameraOffset:
					PerViewUniformParameters.DefaultPrevCamOffset = PerViewUniformParameters.DefaultCamOffset;
					break;
				case ENTPNiagaraSpriteVFLayout::Type::PrevPivotOffset:
					PerViewUniformParameters.DefaultPrevPivotOffset = PerViewUniformParameters.DefaultPivotOffset;
					break;
				default:
					break;
				}
			}
		}
	}

	if (VertexFactory.GetFacingMode() == uint32(ENTPNiagaraSpriteFacingMode::FaceCameraDistanceBlend))
	{
		float DistanceBlendMinSq = MinFacingCameraBlendDistance * MinFacingCameraBlendDistance;
		float DistanceBlendMaxSq = MaxFacingCameraBlendDistance * MaxFacingCameraBlendDistance;
		float InvBlendRange = 1.0f / FMath::Max(DistanceBlendMaxSq - DistanceBlendMinSq, 1.0f);
		float BlendScaledMinDistance = DistanceBlendMinSq * InvBlendRange;

		PerViewUniformParameters.CameraFacingBlend.X = 1.0f;
		PerViewUniformParameters.CameraFacingBlend.Y = InvBlendRange;
		PerViewUniformParameters.CameraFacingBlend.Z = BlendScaledMinDistance;
	}

	if (VertexFactory.GetAlignmentMode() == uint32(ENTPNiagaraSpriteAlignment::VelocityAligned))
	{
		// velocity aligned
		PerViewUniformParameters.RotationScale = 0.0f;
		PerViewUniformParameters.TangentSelector = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
	}

	return FNTPNiagaraFontUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
}

void FNTPNiagaraRendererFonts::CreateMeshBatchForView(
	FRHICommandListBase& RHICmdList,
	FParticleSpriteRenderData& ParticleSpriteRenderData,
	FMeshBatch& MeshBatch,
	const FSceneView& View,
	const FNiagaraSceneProxy& SceneProxy,
	FNTPNiagaraFontVertexFactory& VertexFactory,
	uint32 NumInstances
) const
{
	FNTPNiagaraFontVFLooseParameters VFLooseParams;
	VFLooseParams.NiagaraParticleDataFloat = ParticleSpriteRenderData.ParticleFloatSRV;
	VFLooseParams.NiagaraParticleDataHalf = ParticleSpriteRenderData.ParticleHalfSRV;
	VFLooseParams.NiagaraFloatDataStride = FMath::Max(ParticleSpriteRenderData.ParticleFloatDataStride, ParticleSpriteRenderData.ParticleHalfDataStride);

	FMaterialRenderProxy* MaterialRenderProxy = ParticleSpriteRenderData.DynamicDataSprites->Material;
	check(MaterialRenderProxy);

	VFLooseParams.ParticleAlignmentMode = VertexFactory.GetAlignmentMode();
	VFLooseParams.ParticleFacingMode = VertexFactory.GetFacingMode();
	VFLooseParams.SortedIndices = VertexFactory.GetSortedIndicesSRV() ? VertexFactory.GetSortedIndicesSRV() : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference();
	VFLooseParams.SortedIndicesOffset = VertexFactory.GetSortedIndicesOffset();

	VFLooseParams.IndirectArgsBuffer = GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV;
	VFLooseParams.IndirectArgsOffset = 0;

	VertexFactory.LooseParameterUniformBuffer = FNTPNiagaraFontVFLooseParametersRef::CreateUniformBufferImmediate(VFLooseParams, UniformBuffer_SingleFrame);

	MeshBatch.VertexFactory = &VertexFactory;
	MeshBatch.CastShadow = SceneProxy.CastsDynamicShadow();
#if RHI_RAYTRACING
	MeshBatch.CastRayTracedShadow = SceneProxy.CastsDynamicShadow();
#endif
	MeshBatch.bUseAsOccluder = false;
	MeshBatch.ReverseCulling = SceneProxy.IsLocalToWorldDeterminantNegative();
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SceneProxy.GetDepthPriorityGroup(&View);
	MeshBatch.bCanApplyViewModeOverrides = true;
	MeshBatch.bUseWireframeSelectionColoring = SceneProxy.IsSelected();
	MeshBatch.SegmentIndex = 0;

	const bool bIsWireframe = View.Family->EngineShowFlags.Wireframe;
	if (bIsWireframe)
	{
		MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
	}
	else
	{
		MeshBatch.MaterialRenderProxy = MaterialRenderProxy;
	}

	FMeshBatchElement& MeshElement = MeshBatch.Elements[0];
	MeshElement.IndexBuffer = &GParticleIndexBuffer;
	MeshElement.FirstIndex = 0;
	MeshElement.NumPrimitives = NumIndicesPerInstance / 3;
	MeshElement.NumInstances = FMath::Max(0u, NumInstances);
	MeshElement.MinVertexIndex = 0;
	MeshElement.MaxVertexIndex = 0;
	MeshElement.PrimitiveUniformBuffer = SceneProxy.GetCustomUniformBuffer(IsMotionBlurEnabled());

	INC_DWORD_STAT_BY(STAT_NTP_NiagaraNumSprites, NumInstances);
}

void FNTPNiagaraRendererFonts::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	check(SceneProxy);
	PARTICLE_PERF_STAT_CYCLES_RT(SceneProxy->GetProxyDynamicData().PerfStatsContext, GetDynamicMeshElements);

	FRHICommandListBase& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	// Prepare our particle render data
	// This will also determine if we have anything to render
	// ENiagaraGpuComputeTickStage::Last is used as the GPU ready stage as we can support reading translucent data after PostRenderOpaque sims have run
	FParticleSpriteRenderData ParticleSpriteRenderData;
	PrepareParticleSpriteRenderData(ParticleSpriteRenderData, ViewFamily, DynamicDataRender, SceneProxy, ENiagaraGpuComputeTickStage::Last);

	if (ParticleSpriteRenderData.SourceParticleData == nullptr)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	PrepareParticleRenderBuffers(RHICmdList, ParticleSpriteRenderData, Collector.GetDynamicReadBuffer());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			if (View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View) && !IStereoRendering::IsAPrimaryView(*View))
			{
				// We don't have to generate batches for non-primary views in stereo instance rendering
				continue;
			}

			FNiagaraGPUSortInfo SortInfo;
			if (ParticleSpriteRenderData.bNeedsSort)
			{
				InitializeSortInfo(ParticleSpriteRenderData, *SceneProxy, *View, ViewIndex, SortInfo);
			}

			FMeshCollectorResources* CollectorResources = &Collector.AllocateOneFrameResource<FMeshCollectorResources>();

			// Get the next vertex factory to use
			// TODO: Find a way to safely pool these such that they won't be concurrently accessed by multiple views
			FNTPNiagaraFontVertexFactory& VertexFactory = CollectorResources->VertexFactory;

			// Sort particles if needed.
			uint32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? ParticleSpriteRenderData.SourceParticleData->GetNumInstances() : 1;

			VertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);
			FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface = SceneProxy->GetComputeDispatchInterface();
			if (ParticleSpriteRenderData.bNeedsSort)
			{
				if (ParticleSpriteRenderData.bSortCullOnGpu)
				{
					if (ComputeDispatchInterface->AddSortedGPUSimulation(SortInfo))
					{
						VertexFactory.SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
					}
				}
				else
				{
					FGlobalDynamicReadBuffer::FAllocation SortedIndices;
					SortedIndices = Collector.GetDynamicReadBuffer().AllocateUInt32(RHICmdList, NumInstances);
					
					NumInstances = SortAndCullIndices(SortInfo, *ParticleSpriteRenderData.SourceParticleData, SortedIndices);

					VertexFactory.SetSortedIndices(SortedIndices.SRV, 0);
				}
			}

			if (NumInstances > 0)
			{
				SetupVertexFactory(RHICmdList, ParticleSpriteRenderData, VertexFactory);
				CollectorResources->UniformBuffer = CreateViewUniformBuffer(ParticleSpriteRenderData, *View, ViewFamily, *SceneProxy, VertexFactory);
				VertexFactory.SetSpriteUniformBuffer(CollectorResources->UniformBuffer);

				FMeshBatch& MeshBatch = Collector.AllocateMesh();
				CreateMeshBatchForView(RHICmdList, ParticleSpriteRenderData, MeshBatch, *View, *SceneProxy, VertexFactory, NumInstances);
				Collector.AddMesh(ViewIndex, MeshBatch);
			}
		}
	}
}

#if RHI_RAYTRACING
void FNTPNiagaraRendererFonts::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	if (!CVarRayTracingNiagaraSprites.GetValueOnRenderThread())
	{
		return;
	}

	check(SceneProxy);


	// Prepare our particle render data
	// This will also determine if we have anything to render
	// ENiagaraGpuComputeTickStage::PostInitViews is used as we need the data one InitViews is complete as the HWRT BVH will be generated before other sims have run
	FParticleSpriteRenderData ParticleSpriteRenderData;
	PrepareParticleSpriteRenderData(ParticleSpriteRenderData, *Context.ReferenceView->Family, DynamicDataRender, SceneProxy, ENiagaraGpuComputeTickStage::PostInitViews);

	if (ParticleSpriteRenderData.SourceParticleData == nullptr)
	{
		return;
	}

	FRHICommandListBase& RHICmdList = FRHICommandListImmediate::Get();

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	FGlobalDynamicReadBuffer& DynamicReadBuffer = Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer();
	PrepareParticleRenderBuffers(RHICmdList, ParticleSpriteRenderData, DynamicReadBuffer);
	
	FNiagaraGPUSortInfo SortInfo;
	if (ParticleSpriteRenderData.bNeedsSort)
	{
		InitializeSortInfo(ParticleSpriteRenderData, *SceneProxy, *Context.ReferenceView, 0, SortInfo);
	}

	if (!FNTPNiagaraFontVertexFactory::StaticType.SupportsRayTracingDynamicGeometry())
	{
		return;
	}

	FMeshCollectorResources* CollectorResources = &Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FMeshCollectorResources>();
	FNTPNiagaraFontVertexFactory& VertexFactory = CollectorResources->VertexFactory;

	// Sort particles if needed.
	uint32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? ParticleSpriteRenderData.SourceParticleData->GetNumInstances() : 1;

	VertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);
	FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface = SceneProxy->GetComputeDispatchInterface();
	if (ParticleSpriteRenderData.bNeedsSort)
	{
		if (ParticleSpriteRenderData.bSortCullOnGpu)
		{
			if (ComputeDispatchInterface->AddSortedGPUSimulation(SortInfo))
			{
				VertexFactory.SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
			}
		}
		else
		{
			FGlobalDynamicReadBuffer::FAllocation SortedIndices;
			SortedIndices = DynamicReadBuffer.AllocateUInt32(RHICmdList, NumInstances);
			NumInstances = SortAndCullIndices(SortInfo, *ParticleSpriteRenderData.SourceParticleData, SortedIndices);
			VertexFactory.SetSortedIndices(SortedIndices.SRV, 0);
		}
	}

	if (NumInstances > 0)
	{
		SetupVertexFactory(Context.GraphBuilder.RHICmdList, ParticleSpriteRenderData, VertexFactory);
		CollectorResources->UniformBuffer = CreateViewUniformBuffer(ParticleSpriteRenderData, *Context.ReferenceView, Context.ReferenceViewFamily, *SceneProxy, VertexFactory);
		VertexFactory.SetSpriteUniformBuffer(CollectorResources->UniformBuffer);

		FMeshBatch MeshBatch;
		CreateMeshBatchForView(RHICmdList, ParticleSpriteRenderData, MeshBatch, *Context.ReferenceView, *SceneProxy, VertexFactory, NumInstances);

		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = &RayTracingGeometry;
		RayTracingInstance.InstanceTransforms.Add(FMatrix::Identity);
		RayTracingInstance.Materials.Add(MeshBatch);

		// Use the internal vertex buffer only when initialized otherwise used the shared vertex buffer - needs to be updated every frame
		FRWBuffer* VertexBuffer = RayTracingDynamicVertexBuffer.NumBytes > 0 ? &RayTracingDynamicVertexBuffer : nullptr;

		const int32 NumVerticesPerInstance = 6;
		const int32 NumTrianglesPerInstance = 2;

		// Update dynamic ray tracing geometry
		Context.DynamicRayTracingGeometriesToUpdate.Add(
			FRayTracingDynamicGeometryUpdateParams
			{
				RayTracingInstance.Materials,
				MeshBatch.Elements[0].NumPrimitives == 0,
				NumVerticesPerInstance* NumInstances,
				NumVerticesPerInstance* NumInstances* (uint32)sizeof(FVector3f),
				NumTrianglesPerInstance * NumInstances,
				&RayTracingGeometry,
				VertexBuffer,
				true
			}
		);

		OutRayTracingInstances.Add(RayTracingInstance);
	}
}
#endif

/** Update render data buffer from attributes */
FNiagaraDynamicDataBase *FNTPNiagaraRendererFonts::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	FNTPNiagaraDynamicDataFonts *DynamicData = nullptr;
	const UNTPNiagaraFontRendererProperties* Properties = CastChecked<const UNTPNiagaraFontRendererProperties>(InProperties);

	if (Properties)
	{
		if ( !IsRendererEnabled(Properties, Emitter) )
		{
			return nullptr;
		}
        
        if (Properties->bAllowInCullProxies == false)
		{
			check(Emitter);

			if (const FNiagaraSystemInstance* Inst = Emitter->GetParentSystemInstance())
			{
				if (const USceneComponent* AttachComponent = const_cast<FNiagaraSystemInstance*>(Inst)->GetAttachComponent())
				{
					// Check if this is a cull proxy component via reflection to avoid linker dependency.
					// Cache the class pointer so we only perform the lookup once.
					static UClass* CullProxyClass = nullptr;
					if (CullProxyClass == nullptr)
					{
                        // log the call to FindObject
						UE_LOG(LogTemp, Log, TEXT("FONT RENDERER: FindObject: /Script/Niagara.NiagaraCullProxyComponent"));
						CullProxyClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraCullProxyComponent"));
					}
					if (CullProxyClass && AttachComponent->IsA(CullProxyClass))
					{
						return nullptr;
					}
				}
			}
		}
        

		FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
		if(SimTarget == ENiagaraSimTarget::GPUComputeSim || (DataToRender != nullptr &&  (SourceMode == ENiagaraRendererSourceDataMode::Emitter || (SourceMode == ENiagaraRendererSourceDataMode::Particles && DataToRender->GetNumInstances() > 0))))
		{
			DynamicData = new FNTPNiagaraDynamicDataFonts(Emitter);

			//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
			//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
			//Any override feature must also do the same for materials that are set.
			check(BaseMaterials_GT.Num() == 1);
			check(BaseMaterials_GT[0]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraSprites));
			DynamicData->Material = BaseMaterials_GT[0]->GetRenderProxy();
			DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);
		}

		if (DynamicData)
		{
			const FNiagaraParameterStore& ParameterData = Emitter->GetRendererBoundVariables();
			DynamicData->DataInterfacesBound = ParameterData.GetDataInterfaces();
			DynamicData->ObjectsBound = ParameterData.GetUObjects();
			DynamicData->ParameterDataBound = ParameterData.GetParameterDataArray();
		}

		if (DynamicData && Properties->MaterialParameters.HasAnyBindings())
		{
			ProcessMaterialParameterBindings(Properties->MaterialParameters, Emitter, MakeArrayView(BaseMaterials_GT));
		}

		if (DynamicData && Properties->FontBindings.Num() > 0)
		{
			for (UMaterialInterface* Mat : BaseMaterials_GT)
			{
				if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat))
				{
					for (const FNTPFontParameterBinding& Binding : Properties->FontBindings)
					{
						// We can only bind if we have a valid parameter name and a font with a texture
						if (Binding.MaterialParameterName != NAME_None && Binding.Font && Binding.Font->Textures.Num() > 0)
						{
							// Bind page 0 of the font (the main atlas)
							MID->SetTextureParameterValue(Binding.MaterialParameterName, Binding.Font->Textures[0]);
						}
					}
				}
			}
		}
	}

	return DynamicData;  // for VF that can fetch from particle data directly
}

int FNTPNiagaraRendererFonts::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNTPNiagaraDynamicDataFonts);
	return Size;
}

bool FNTPNiagaraRendererFonts::IsMaterialValid(const UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraSprites);
}
