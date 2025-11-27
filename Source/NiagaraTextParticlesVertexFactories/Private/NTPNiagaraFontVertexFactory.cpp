// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParticleVertexFactory.cpp: Particle vertex factory implementation.
=============================================================================*/

#include "NTPNiagaraFontVertexFactory.h"
#include "ParticleHelper.h"
#include "ParticleResources.h"
#include "ShaderParameterUtils.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNTPNiagaraFontUniformParameters,"NTPNiagaraFontVF");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FNTPNiagaraFontVFLooseParameters, "NTPNiagaraFontVFLooseParameters");

// Commented this out since it's already defined in the Niagara plugin and was causing build errors.
// TGlobalResource<FNullDynamicParameterVertexBuffer> GNullNiagaraDynamicParameterVertexBuffer;

class FNTPNiagaraFontVertexFactoryShaderParametersVS : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FNTPNiagaraFontVertexFactoryShaderParametersVS, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		//  		NiagaraParticleDataInt.Bind(ParameterMap, TEXT("NiagaraParticleDataInt"));
		//  		Int32DataOffset.Bind(ParameterMap, TEXT("NiagaraInt32DataOffset"));
		//  		Int32DataStride.Bind(ParameterMap, TEXT("NiagaraInt3DataStride"));

		ParticleAlignmentMode.Bind(ParameterMap, TEXT("ParticleAlignmentMode"));
		ParticleFacingMode.Bind(ParameterMap, TEXT("ParticleFacingMode"));

		SortedIndices.Bind(ParameterMap, TEXT("SortedIndices"));
		SortedIndicesOffset.Bind(ParameterMap, TEXT("SortedIndicesOffset"));
	}

	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType VertexStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FNTPNiagaraFontVertexFactory* SpriteVF = static_cast<const FNTPNiagaraFontVertexFactory*>(VertexFactory);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNTPNiagaraFontUniformParameters>(), SpriteVF->GetSpriteUniformBuffer());

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNTPNiagaraFontVFLooseParameters>(), SpriteVF->LooseParameterUniformBuffer);

		ShaderBindings.Add(ParticleAlignmentMode, SpriteVF->GetAlignmentMode());
		ShaderBindings.Add(ParticleFacingMode, SpriteVF->GetFacingMode());

		ShaderBindings.Add(SortedIndices, SpriteVF->GetSortedIndicesSRV() ? SpriteVF->GetSortedIndicesSRV() : GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference());
		ShaderBindings.Add(SortedIndicesOffset, SpriteVF->GetSortedIndicesOffset());
	}

private:

	LAYOUT_FIELD(FShaderParameter, ParticleAlignmentMode);
	LAYOUT_FIELD(FShaderParameter, ParticleFacingMode);

	//  	LAYOUT_FIELD(FShaderResourceParameter, NiagaraParticleDataInt);
	//  	LAYOUT_FIELD(FShaderParameter, Int32DataOffset);
	//  	LAYOUT_FIELD(FShaderParameter, Int32DataStride);

	LAYOUT_FIELD(FShaderResourceParameter, SortedIndices);
	LAYOUT_FIELD(FShaderParameter, SortedIndicesOffset);

};

IMPLEMENT_TYPE_LAYOUT(FNTPNiagaraFontVertexFactoryShaderParametersVS);

class FNTPNiagaraFontVertexFactoryShaderParametersPS : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FNTPNiagaraFontVertexFactoryShaderParametersPS, NonVirtual);
public:
	void GetElementShaderBindings(
		const FSceneInterface* Scene,
		const FSceneView* View,
		const FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FNTPNiagaraFontVertexFactory* SpriteVF = static_cast<const FNTPNiagaraFontVertexFactory*>(VertexFactory);
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FNTPNiagaraFontUniformParameters>(), SpriteVF->GetSpriteUniformBuffer() );
	}
};

IMPLEMENT_TYPE_LAYOUT(FNTPNiagaraFontVertexFactoryShaderParametersPS);

/**
 * The particle system vertex declaration resource type.
 */
class FNTPNiagaraFontVertexDeclaration : public FRenderResource
{
public:

	FVertexDeclarationRHIRef VertexDeclarationRHI;

	// Constructor.
	FNTPNiagaraFontVertexDeclaration() {}

	// Destructor.
	virtual ~FNTPNiagaraFontVertexDeclaration() {}

	virtual void FillDeclElements(FVertexDeclarationElementList& Elements, int32& Offset)
	{
		uint32 InitialStride = sizeof(float) * 2;
		/** The stream to read the texture coordinates from. */
		check( Offset == 0 );
		Elements.Add(FVertexElement(0, Offset, VET_Float2, 0, InitialStride, false));
	}

	virtual void InitRHI(FRHICommandListBase& RHICmdList)
	{
		FVertexDeclarationElementList Elements;
		int32	Offset = 0;

		FillDeclElements(Elements, Offset);

		// Create the vertex declaration for rendering the factory normally
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

/** The simple element vertex declaration. */
static TGlobalResource<FNTPNiagaraFontVertexDeclaration> GParticleFontVertexDeclaration;

bool FNTPNiagaraFontVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	return (FNiagaraUtilities::SupportsNiagaraRendering(Parameters.Platform)) && (Parameters.MaterialParameters.bIsUsedWithNiagaraSprites || Parameters.MaterialParameters.bIsSpecialEngineMaterial);
}

/**
 * Can be overridden by FVertexFactory subclasses to modify their compile environment just before compilation occurs.
 */
void FNTPNiagaraFontVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	FNTPNiagaraVertexFactoryBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);

	OutEnvironment.SetDefine(TEXT("NiagaraVFLooseParameters"),TEXT("NTPNiagaraFontVFLooseParameters"));

	// Set a define so we can tell in MaterialTemplate.usf when we are compiling a sprite vertex factory
	OutEnvironment.SetDefine(TEXT("PARTICLE_SPRITE_FACTORY"),TEXT("1"));

	// Sprites are generated in world space and never have a matrix transform in raytracing, so it is safe to leave them in world space.
	OutEnvironment.SetDefine(TEXT("RAY_TRACING_DYNAMIC_MESH_IN_WORLD_SPACE"), TEXT("1"));
}

/**
* Get vertex elements used when during PSO precaching materials using this vertex factory type
*/
void FNTPNiagaraFontVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements)
{
	GParticleFontVertexDeclaration.VertexDeclarationRHI->GetInitializer(Elements);
}

/**
 *	Initialize the Render Hardware Interface for this vertex factory
 */
void FNTPNiagaraFontVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	InitStreams();
	SetDeclaration(GParticleFontVertexDeclaration.VertexDeclarationRHI);
}

void FNTPNiagaraFontVertexFactory::InitStreams()
{
	check(Streams.Num() == 0);
	FVertexStream* TexCoordStream = new(Streams) FVertexStream;
	TexCoordStream->VertexBuffer = VertexBufferOverride ? VertexBufferOverride : &GParticleTexCoordVertexBuffer;
	TexCoordStream->Stride = sizeof(FVector2f);
	TexCoordStream->Offset = 0;
}

void FNTPNiagaraFontVertexFactory::SetTexCoordBuffer(const FVertexBuffer* InTexCoordBuffer)
{
	FVertexStream& TexCoordStream = Streams[0];
	TexCoordStream.VertexBuffer = InTexCoordBuffer;
}

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNTPNiagaraFontVertexFactory, SF_Vertex, FNTPNiagaraFontVertexFactoryShaderParametersVS);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNTPNiagaraFontVertexFactory, SF_Pixel, FNTPNiagaraFontVertexFactoryShaderParametersPS);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNTPNiagaraFontVertexFactory, SF_Compute, FNTPNiagaraFontVertexFactoryShaderParametersVS);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FNTPNiagaraFontVertexFactory, SF_RayHitGroup, FNTPNiagaraFontVertexFactoryShaderParametersVS);
#endif // RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_TYPE(FNTPNiagaraFontVertexFactory,"/Plugin/NiagaraTextParticles/Private/NTPNiagaraFontVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	| EVertexFactoryFlags::SupportsPSOPrecaching
);


