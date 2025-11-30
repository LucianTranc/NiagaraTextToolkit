// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParticleVertexFactory.h: Particle vertex factory definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "UniformBuffer.h"
#include "NiagaraVertexFactory.h"
#include "NiagaraDataSet.h"
#include "SceneView.h"

class FMaterial;

/**
 * Uniform buffer for particle sprite vertex factories.
 */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT( FNTPNiagaraFontUniformParameters, NIAGARATEXTPARTICLESVERTEXFACTORIES_API)
	SHADER_PARAMETER( uint32, bLocalSpace)
	SHADER_PARAMETER_EX( FVector4f, TangentSelector, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( FVector4f, NormalsSphereCenter, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( FVector4f, NormalsCylinderUnitDirection, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( FVector3f, CameraFacingBlend, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( float, RemoveHMDRoll, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER( FVector4f, MacroUVParameters )
	SHADER_PARAMETER_EX( float, RotationScale, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( float, RotationBias, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( float, NormalsType, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( float, DeltaSeconds, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( FVector2f, DefaultPivotOffset, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER_EX( FVector2f, DefaultPrevPivotOffset, EShaderPrecisionModifier::Half )
	SHADER_PARAMETER(int, PositionDataOffset)
	SHADER_PARAMETER(int, PrevPositionDataOffset)
	SHADER_PARAMETER(int, VelocityDataOffset)
	SHADER_PARAMETER(int, PrevVelocityDataOffset)
	SHADER_PARAMETER(int, RotationDataOffset)
	SHADER_PARAMETER(int, PrevRotationDataOffset)
	SHADER_PARAMETER(int, SizeDataOffset)
	SHADER_PARAMETER(int, PrevSizeDataOffset)
	SHADER_PARAMETER(int, ColorDataOffset)
	SHADER_PARAMETER(uint32, MaterialParamValidMask)
	SHADER_PARAMETER(int, MaterialParamDataOffset)
	SHADER_PARAMETER(int, MaterialParam1DataOffset)
	SHADER_PARAMETER(int, MaterialParam2DataOffset)
	SHADER_PARAMETER(int, MaterialParam3DataOffset)
	SHADER_PARAMETER(int, FacingDataOffset)
	SHADER_PARAMETER(int, PrevFacingDataOffset)
	SHADER_PARAMETER(int, AlignmentDataOffset)
	SHADER_PARAMETER(int, PrevAlignmentDataOffset)
	SHADER_PARAMETER(int, CameraOffsetDataOffset)
	SHADER_PARAMETER(int, PrevCameraOffsetDataOffset)
	SHADER_PARAMETER(int, UVScaleDataOffset)
	SHADER_PARAMETER(int, PivotOffsetDataOffset)
	SHADER_PARAMETER(int, PrevPivotOffsetDataOffset)
	SHADER_PARAMETER(int, NormalizedAgeDataOffset)
	SHADER_PARAMETER(int, CharacterIndexDataOffset)
	SHADER_PARAMETER(int, MaterialRandomDataOffset)
	SHADER_PARAMETER(FVector4f, DefaultPos)
	SHADER_PARAMETER(FVector4f, DefaultPrevPos)
	SHADER_PARAMETER(FVector2f, DefaultSize)
	SHADER_PARAMETER(FVector2f, DefaultPrevSize)
	SHADER_PARAMETER(FVector2f, DefaultUVScale)
	SHADER_PARAMETER(FVector3f, DefaultVelocity)
	SHADER_PARAMETER(FVector3f, DefaultPrevVelocity)
	SHADER_PARAMETER(FVector3f, SystemLWCTile)
	SHADER_PARAMETER(float, DefaultRotation)
	SHADER_PARAMETER(float, DefaultPrevRotation)
	SHADER_PARAMETER(FVector4f, DefaultColor)
	SHADER_PARAMETER(float, DefaultMatRandom)
	SHADER_PARAMETER(float, DefaultCamOffset)
	SHADER_PARAMETER(float, DefaultPrevCamOffset)
	SHADER_PARAMETER(float, DefaultNormAge)
	SHADER_PARAMETER(float, DefaultCharacterIndex)
	SHADER_PARAMETER(FVector4f, DefaultFacing)
	SHADER_PARAMETER(FVector4f, DefaultPrevFacing)
	SHADER_PARAMETER(FVector4f, DefaultAlignment)
	SHADER_PARAMETER(FVector4f, DefaultPrevAlignment)
	SHADER_PARAMETER(FVector4f, DefaultDynamicMaterialParameter0)
	SHADER_PARAMETER(FVector4f, DefaultDynamicMaterialParameter1)
	SHADER_PARAMETER(FVector4f, DefaultDynamicMaterialParameter2)
	SHADER_PARAMETER(FVector4f, DefaultDynamicMaterialParameter3)
	SHADER_PARAMETER(int, PixelCoverageEnabled)
	SHADER_PARAMETER(FVector4f, PixelCoverageColorBlend)
	SHADER_PARAMETER(int, AccurateMotionVectors)
	END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FNTPNiagaraFontUniformParameters> FNTPNiagaraFontUniformBufferRef;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FNTPNiagaraFontVFLooseParameters, NIAGARATEXTPARTICLESVERTEXFACTORIES_API)
	SHADER_PARAMETER(uint32, NiagaraFloatDataStride)
	SHADER_PARAMETER(uint32, NiagaraIntDataStride)
	SHADER_PARAMETER(uint32, ParticleAlignmentMode)
	SHADER_PARAMETER(uint32, ParticleFacingMode)
	SHADER_PARAMETER(uint32, SortedIndicesOffset)
	SHADER_PARAMETER(uint32, IndirectArgsOffset)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataFloat)
	SHADER_PARAMETER_SRV(Buffer<float>, NiagaraParticleDataHalf)
	SHADER_PARAMETER_SRV(Buffer<int>, NiagaraParticleDataInt)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, UnicodeBuffer)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, CharacterTextureUvsBuffer)
	SHADER_PARAMETER_SRV(Buffer<uint>, SortedIndices)
	SHADER_PARAMETER_SRV(Buffer<uint>, IndirectArgsBuffer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FNTPNiagaraFontVFLooseParameters> FNTPNiagaraFontVFLooseParametersRef;

/**
* Base class for particle vertex factories.
*/
class FNTPNiagaraVertexFactoryBase : public FVertexFactory
{
public:
	/** Default constructor. */
	explicit FNTPNiagaraVertexFactoryBase(ENiagaraVertexFactoryType Type, ERHIFeatureLevel::Type InFeatureLevel)
		: FVertexFactory(InFeatureLevel)
		, LastFrameSetup(MAX_uint32)
		, LastViewFamily(nullptr)
		, LastView(nullptr)
		, LastFrameRealTime(-1.0)
		, ParticleFactoryType(Type)
		, bInUse(false)
	{
		bNeedsDeclaration = false;
	}

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVertexFactory::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_PARTICLE_FACTORY"), TEXT("1"));
	}
	/** Return the vertex factory type */
	FORCEINLINE ENiagaraVertexFactoryType GetParticleFactoryType() const
	{
		return ParticleFactoryType;
	}
	inline void SetParticleFactoryType(ENiagaraVertexFactoryType InType)
	{
		ParticleFactoryType = InType;
	}
	/** Specify whether the factory is in use or not. */
	FORCEINLINE void SetInUse(bool bInInUse)
	{
		bInUse = bInInUse;
	}
	/** Return the vertex factory type */
	FORCEINLINE bool GetInUse() const
	{
		return bInUse;
	}
	ERHIFeatureLevel::Type GetFeatureLevel() const { check(HasValidFeatureLevel());  return FRenderResource::GetFeatureLevel(); }
	bool CheckAndUpdateLastFrame(const FSceneViewFamily& ViewFamily, const FSceneView* View = nullptr) const
	{
		if (LastFrameSetup != MAX_uint32 && (&ViewFamily == LastViewFamily) && (View == LastView) && ViewFamily.FrameNumber == LastFrameSetup && LastFrameRealTime == ViewFamily.Time.GetRealTimeSeconds())
		{
			return false;
		}
		LastFrameSetup = ViewFamily.FrameNumber;
		LastFrameRealTime = ViewFamily.Time.GetRealTimeSeconds();
		LastViewFamily = &ViewFamily;
		LastView = View;
		return true;
	}
private:
	/** Last state where we set this. We only need to setup these once per frame, so detemine same frame by number, time, and view family. */
	mutable uint32 LastFrameSetup;
	mutable const FSceneViewFamily* LastViewFamily;
	mutable const FSceneView* LastView;
	mutable double LastFrameRealTime;
	/** The type of the vertex factory. */
	ENiagaraVertexFactoryType ParticleFactoryType;
	/** Whether the vertex factory is in use. */
	bool bInUse;
};

/**
 * Vertex factory for rendering particle sprites.
 */
class FNTPNiagaraFontVertexFactory : public FNTPNiagaraVertexFactoryBase
{
public:
	DECLARE_VERTEX_FACTORY_TYPE_API(FNTPNiagaraFontVertexFactory, NIAGARATEXTPARTICLESVERTEXFACTORIES_API);

	/** Default constructor. */
	FNTPNiagaraFontVertexFactory(ENiagaraVertexFactoryType InType, ERHIFeatureLevel::Type InFeatureLevel)
		: FNTPNiagaraVertexFactoryBase(InType, InFeatureLevel)
		, LooseParameterUniformBuffer(nullptr)
		, AlignmentMode(0)
		, FacingMode(0)
		, SortedIndicesSRV(nullptr)
		, SortedIndicesOffset(0)
	{}

	FNTPNiagaraFontVertexFactory()
		: FNTPNiagaraVertexFactoryBase(NVFT_MAX, ERHIFeatureLevel::Num)
		, LooseParameterUniformBuffer(nullptr)
		, AlignmentMode(0)
		, FacingMode(0)
		, SortedIndicesSRV(nullptr)
		, SortedIndicesOffset(0)
	{}

	// FRenderResource interface.
	NIAGARATEXTPARTICLESVERTEXFACTORIES_API virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	virtual bool RendersPrimitivesAsCameraFacingSprites() const override { return true; }

	/**
	 * Should we cache the material's shadertype on this platform with this vertex factory? 
	 */
	static NIAGARATEXTPARTICLESVERTEXFACTORIES_API bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	/**
	 * Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
	 */
	static NIAGARATEXTPARTICLESVERTEXFACTORIES_API void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/**
	* Get vertex elements used when during PSO precaching materials using this vertex factory type
	*/
	static NIAGARATEXTPARTICLESVERTEXFACTORIES_API void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements);

	NIAGARATEXTPARTICLESVERTEXFACTORIES_API void SetTexCoordBuffer(const FVertexBuffer* InTexCoordBuffer);
	
	/**
	 * Set the uniform buffer for this vertex factory.
	 */
	FORCEINLINE void SetSpriteUniformBuffer( const FNTPNiagaraFontUniformBufferRef& InSpriteUniformBuffer )
	{
		SpriteUniformBuffer = InSpriteUniformBuffer;
	}

	/**
	 * Retrieve the uniform buffer for this vertex factory.
	 */
	FORCEINLINE FRHIUniformBuffer* GetSpriteUniformBuffer() const
	{
		return SpriteUniformBuffer;
	}

	void SetSortedIndices(const FShaderResourceViewRHIRef& InSortedIndicesSRV, uint32 InSortedIndicesOffset)
	{
		SortedIndicesSRV = InSortedIndicesSRV;
		SortedIndicesOffset = InSortedIndicesOffset;
	}

	FORCEINLINE FRHIShaderResourceView* GetSortedIndicesSRV() const
	{
		return SortedIndicesSRV;
	}

	FORCEINLINE int32 GetSortedIndicesOffset() const
	{
		return SortedIndicesOffset;
	}

	void SetFacingMode(uint32 InMode)
	{
		FacingMode = InMode;
	}

	uint32 GetFacingMode() const
	{
		return FacingMode;
	}

	void SetAlignmentMode(uint32 InMode)
	{
		AlignmentMode = InMode;
	}

	uint32 GetAlignmentMode() const
	{
		return AlignmentMode;
	}

	void SetVertexBufferOverride(const FVertexBuffer* InVertexBufferOverride)
	{
		VertexBufferOverride = InVertexBufferOverride;
	}
	FUniformBufferRHIRef LooseParameterUniformBuffer;

protected:
	/** Initialize streams for this vertex factory. */
	NIAGARATEXTPARTICLESVERTEXFACTORIES_API void InitStreams();

private:

	const FVertexBuffer* VertexBufferOverride = nullptr;

	/** Uniform buffer with sprite parameters. */
	FUniformBufferRHIRef SpriteUniformBuffer;

	uint32 AlignmentMode;
	uint32 FacingMode;

	FShaderResourceViewRHIRef SortedIndicesSRV;
	uint32 SortedIndicesOffset;
};


