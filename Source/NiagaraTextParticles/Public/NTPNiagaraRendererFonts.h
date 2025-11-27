// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NTPNiagaraRendererFonts.h: Renderer for rendering Niagara particles as fonts.
==============================================================================*/

#pragma once

#include "Engine/EngineTypes.h"
#include "NiagaraRenderer.h"
#include "NTPNiagaraFontRendererProperties.h"
#include "NTPNiagaraFontVertexFactory.h"

struct FNTPNiagaraDynamicDataFonts;

/**
* FNTPNiagaraRendererFonts renders an FNiagaraEmitterInstance as font particles
*/
class FNTPNiagaraRendererFonts : public FNiagaraRenderer
{
public:
	FNTPNiagaraRendererFonts(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	~FNTPNiagaraRendererFonts();

	//FNiagaraRenderer interface
	virtual void CreateRenderThreadResources() override;
	virtual void ReleaseRenderThreadResources() override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int GetDynamicDataSize()const override;
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const override;

#if RHI_RAYTRACING
		virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraRenderer interface END

private:
	struct FParticleSpriteRenderData
	{
		const FNTPNiagaraDynamicDataFonts*	DynamicDataSprites = nullptr;
		class FNiagaraDataBuffer*			SourceParticleData = nullptr;

		EBlendMode							BlendMode = BLEND_Opaque;
		bool								bHasTranslucentMaterials = false;
		bool								bSortCullOnGpu = false;
		bool								bNeedsSort = false;

		const FNiagaraRendererLayout*		RendererLayout = nullptr;
		ENTPNiagaraSpriteVFLayout::Type		SortVariable = ENTPNiagaraSpriteVFLayout::Type(INDEX_NONE);

		FRHIShaderResourceView*				ParticleFloatSRV = nullptr;
		FRHIShaderResourceView*				ParticleHalfSRV = nullptr;
		FRHIShaderResourceView*				ParticleIntSRV = nullptr;
		uint32								ParticleFloatDataStride = 0;
		uint32								ParticleHalfDataStride = 0;
		uint32								ParticleIntDataStride = 0;
	};

	/* Mesh collector classes */
	class FMeshCollectorResources : public FOneFrameResource
	{
	public:
		~FMeshCollectorResources() override { VertexFactory.ReleaseResource(); }

		FNTPNiagaraFontVertexFactory		VertexFactory;
		FNTPNiagaraFontUniformBufferRef		UniformBuffer;
	};

    static bool AllowComputeShaders(EShaderPlatform ShaderPlatform);
    static bool AllowGPUSorting(EShaderPlatform ShaderPlatform);
	void PrepareParticleSpriteRenderData(FParticleSpriteRenderData& ParticleSpriteRenderData, const FSceneViewFamily& ViewFamily, FNiagaraDynamicDataBase* InDynamicData, const FNiagaraSceneProxy* SceneProxy, ENiagaraGpuComputeTickStage::Type GpuReadyTickStage) const;
	void PrepareParticleRenderBuffers(FRHICommandListBase& RHICmdList, FParticleSpriteRenderData& ParticleSpriteRenderData, FGlobalDynamicReadBuffer& DynamicReadBuffer) const;
	void InitializeSortInfo(FParticleSpriteRenderData& ParticleSpriteRenderData, const FNiagaraSceneProxy& SceneProxy, const FSceneView& View, int32 ViewIndex, FNiagaraGPUSortInfo& OutSortInfo) const;
	void SetupVertexFactory(FRHICommandListBase& RHICmdList, FParticleSpriteRenderData& ParticleSpriteRenderData, FNTPNiagaraFontVertexFactory& VertexFactory) const;
	FNTPNiagaraFontUniformBufferRef CreateViewUniformBuffer(FParticleSpriteRenderData& ParticleSpriteRenderData, const FSceneView& View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy& SceneProxy, FNTPNiagaraFontVertexFactory& VertexFactory) const;

	void CreateMeshBatchForView(
		FRHICommandListBase& RHICmdList,
		FParticleSpriteRenderData& ParticleSpriteRenderData,
		FMeshBatch& MeshBatch,
		const FSceneView& View,
		const FNiagaraSceneProxy& SceneProxy,
		FNTPNiagaraFontVertexFactory& VertexFactory,
		uint32 NumInstances
	) const;

	//Cached data from the properties struct.
	ENiagaraRendererSourceDataMode SourceMode;
	ENTPNiagaraSpriteAlignment Alignment;
	ENTPNiagaraSpriteFacingMode FacingMode;
	ENiagaraSortMode SortMode;
	FVector2f PivotInUVSpace;
	float MacroUVRadius;

	uint32 NumIndicesPerInstance;

	uint32 bRemoveHMDRollInVR : 1;
	uint32 bSortHighPrecision : 1;
	uint32 bSortOnlyWhenTranslucent : 1;
	uint32 bGpuLowLatencyTranslucency : 1;
	uint32 bAccurateMotionVectors : 1;
	uint32 bSetAnyBoundVars : 1;

	ENTPNiagaraRendererPixelCoverageMode PixelCoverageMode = ENTPNiagaraRendererPixelCoverageMode::Automatic;
	float PixelCoverageBlend = 0.0f;

	float MinFacingCameraBlendDistance;
	float MaxFacingCameraBlendDistance;
	uint32 MaterialParamValidMask = 0;

	int32 VFBoundOffsetsInParamStore[ENTPNiagaraSpriteVFLayout::Type::Num_Max];

	const FNiagaraRendererLayout* RendererLayoutWithCustomSort;
	const FNiagaraRendererLayout* RendererLayoutWithoutCustomSort;
};
