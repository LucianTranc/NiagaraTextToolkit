// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraRendererProperties.h"
#include "Particles/SubUVAnimation.h"
#include "NTPNiagaraFontRendererProperties.generated.h"

class UMaterialInstanceConstant;
class FVertexFactoryType;
class UFont;

USTRUCT(BlueprintType)
struct FNTPFontParameterBinding
{
	GENERATED_BODY()

	/** The name of the Texture Parameter in the material to set. */
	UPROPERTY(EditAnywhere, Category = "Font Binding")
	FName MaterialParameterName = FName("NTP_Font");

	/** The font asset to bind to the parameter. */
	UPROPERTY(EditAnywhere, Category = "Font Binding")
	TObjectPtr<UFont> Font;
};

/** This enum decides how a sprite particle will orient its "up" axis. Must keep these in sync with NiagaraSpriteVertexFactory.ush*/
UENUM()
enum class ENTPNiagaraSpriteAlignment : uint8
{
	/** Only Particles.SpriteRotation and FacingMode impact the alignment of the particle.*/
	Unaligned,
	/** Imagine the particle texture having an arrow pointing up, this mode makes the arrow point in the direction of the Particles.Velocity attribute. FacingMode is ignored unless CustomFacingVector is set.*/
	VelocityAligned,
	/** Imagine the particle texture having an arrow pointing up, this mode makes the arrow point towards the axis defined by the "Particles.SpriteAlignment" attribute. FacingMode is ignored unless CustomFacingVector is set. If the "Particles.SpriteAlignment" attribute is missing, this falls back to Unaligned mode.*/
	CustomAlignment,

	/** Automatically select between Unaligned & CustomAlignment depending on if SpriteAlignment Binding is valid. */
	Automatic
};


/** This enum decides how a sprite particle will orient its "facing" axis. Must keep these in sync with NiagaraSpriteVertexFactory.ush*/
UENUM()
enum class ENTPNiagaraSpriteFacingMode : uint8
{
	/** The sprite billboard origin is always "looking at" the camera origin, trying to keep its up axis aligned to the camera's up axis. */
	FaceCamera,
	/** The sprite billboard plane is completely parallel to the camera plane. Particle always looks "flat" */
	FaceCameraPlane,
	/** The sprite billboard faces toward the "Particles.SpriteFacing" vector attribute. If the "Particles.SpriteFacing" attribute is missing, this falls back to FaceCamera mode.*/
	CustomFacingVector,
	/** Faces the camera position, but is not dependent on the camera rotation.  This method produces more stable particles under camera rotation. Uses the up axis of (0,0,1).*/
	FaceCameraPosition,
	/** Blends between FaceCamera and FaceCameraPosition.*/
	FaceCameraDistanceBlend,

	/** Automatically select between FaceCamera & CustomFacingVector depending on if SpriteFacing binding is valid. */
	Automatic
};

UENUM()
enum class ENTPNiagaraRendererPixelCoverageMode : uint8
{
	/** Automatically determine if we want pixel coverage enabled or disabled, based on project setting and the material used on the renderer. */
	Automatic,
	/** Disable pixel coverage. */
	Disabled,
	/** Enable pixel coverage with no color adjustment based on coverage. */
	Enabled UMETA(DisplayName = "Enabled (No Color Adjustment)"),
	/** Enable pixel coverage and adjust the RGBA channels according to coverage. */
	Enabled_RGBA UMETA(DisplayName = "Enabled (RGBA)"),
	/** Enable pixel coverage and adjust the RGB channels according to coverage. */
	Enabled_RGB UMETA(DisplayName = "Enabled (RGB)"),
	/** Enable pixel coverage and adjust the Alpha channel only according to coverage. */
	Enabled_A UMETA(DisplayName = "Enabled (A)"),
};

namespace ENTPNiagaraSpriteVFLayout
{
	enum Type
	{
		Position,
		Color,
		Velocity,
		Rotation,
		Size,
		Facing,
		Alignment,
		MaterialParam0,
		MaterialParam1,
		MaterialParam2,
		MaterialParam3,
		CameraOffset,
		UVScale,
		PivotOffset,
		MaterialRandom,
		CustomSorting,
		NormalizedAge,
		UVRect,

		Num_Default,

		// The remaining layout params aren't needed unless accurate motion vectors are required
		PrevPosition = Num_Default,
		PrevVelocity,
		PrevRotation,
		PrevSize,
		PrevFacing,
		PrevAlignment,
		PrevCameraOffset,
		PrevPivotOffset,

		Num_Max,
	};
};

class FAssetThumbnailPool;
class SWidget;

UCLASS(editinlinenew, meta = (DisplayName = "Font Renderer"), MinimalAPI)
class UNTPNiagaraFontRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNTPNiagaraFontRendererProperties();

	//UObject Interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
#if WITH_EDITORONLY_DATA
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const FVersionedNiagaraEmitter& InEmitter) override;
	virtual void RemoveVariable(const FNiagaraVariableBase& OldVariable, const FVersionedNiagaraEmitter& InEmitter) override;
	virtual TArray<FNiagaraVariable> GetBoundAttributes() const override;
#endif // WITH_EDITORONLY_DATA
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//UNiagaraRendererProperties interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override;
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual const FVertexFactoryType* GetVertexFactoryType() const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return true; };
	virtual bool PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore)  override;
	virtual void CollectPSOPrecacheData(FPSOPrecacheParamsList& OutParams) override;
#if WITH_EDITOR
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
	virtual void GetAdditionalVariables(TArray<FNiagaraVariableBase>& OutArray) const override;
	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const override;
	virtual void GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FNiagaraRendererFeedback>& OutErrors, TArray<FNiagaraRendererFeedback>& OutWarnings, TArray<FNiagaraRendererFeedback>& OutInfo) const override;
#endif
	virtual ENiagaraRendererSourceDataMode GetCurrentSourceMode() const override { return SourceMode; }

	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) override;
	//UNiagaraMaterialRendererProperties interface END

	int32 GetNumCutoutVertexPerSubimage() const { return 4; }
	uint32 GetNumIndicesPerInstance() const { return 6; }

	/** The material used to render the particle. Note that it must have the Use with Niagara Sprites flag checked.*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	TObjectPtr<UMaterialInterface> Material;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	TObjectPtr<UMaterialInstanceConstant> MICMaterial;
#endif

	/** Use the UMaterialInterface bound to this user variable if it is set to a valid value. If this is bound to a valid value and Material is also set, UserParamBinding wins.*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	FNiagaraUserParameterBinding MaterialUserParamBinding;

	/** Whether or not to draw a single element for the Emitter or to draw the particles.*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	ENiagaraRendererSourceDataMode SourceMode = ENiagaraRendererSourceDataMode::Particles;

	/** Imagine the particle texture having an arrow pointing up, these modes define how the particle aligns that texture to other particle attributes.*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	ENTPNiagaraSpriteAlignment Alignment = ENTPNiagaraSpriteAlignment::Automatic;

	/** Determines how the particle billboard orients itself relative to the camera.*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	ENTPNiagaraSpriteFacingMode FacingMode = ENTPNiagaraSpriteFacingMode::Automatic;

	/** Determines how we sort the particles prior to rendering.*/
	UPROPERTY(EditAnywhere, Category = "Sorting")
	ENiagaraSortMode SortMode = ENiagaraSortMode::None;

	/** World space radius that UVs generated with the ParticleMacroUV material node will tile based on. */
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	float MacroUVRadius = 0.0f;

	/**
	 * Determines the location of the pivot point of this particle. It follows Unreal's UV space, which has the upper left of the image at 0,0 and bottom right at 1,1. The middle is at 0.5, 0.5.
	 * NOTE: This value is ignored if "Pivot Offset Binding" is bound to a valid attribute
	 */
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering", meta = (DisplayName = "Default Pivot in UV Space"))
	FVector2D PivotInUVSpace = FVector2D(0.5f, 0.5f);

	/** If true, removes the HMD view roll (e.g. in VR) */
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering", meta = (DisplayName = "Remove HMD Roll"))
	uint8 bRemoveHMDRollInVR : 1;

	/** If true, the particles are only sorted when using a translucent material. */
	UPROPERTY(EditAnywhere, Category = "Sorting", meta = (EditCondition = "SortMode != ENiagaraSortMode::None", EditConditionHides))
	uint8 bSortOnlyWhenTranslucent : 1;

	/** Sort precision to use when sorting is active. */
	UPROPERTY(EditAnywhere, Category = "Sorting", meta = (EditCondition = "SortMode != ENiagaraSortMode::None", EditConditionHides))
	ENiagaraRendererSortPrecision SortPrecision = ENiagaraRendererSortPrecision::Default;

	/**
	Gpu simulations run at different points in the frame depending on what features are used, i.e. depth buffer, distance fields, etc.
	Opaque materials will run latent when these features are used.
	Translucent materials can choose if they want to use this frames or the previous frames data to match opaque draws.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Sprite Rendering")
	ENiagaraRendererGpuTranslucentLatency GpuTranslucentLatency = ENiagaraRendererGpuTranslucentLatency::ProjectDefault;

	/**
	This setting controls what happens when a sprite becomes less than a pixel in size.
	Disabling will apply nothing and can result in flickering issues, especially with Temporal Super Resolution.
	Automatic will enable the appropriate settings when the material blend mode is some form of translucency, project setting must also be enabled.
	When coverage is less than a pixel, we also calculate a percentage of coverage, and then darken or reduce opacity
	to visually compensate.	The different enabled settings allow you to control how the coverage amount is applied to
	your particle color.  If particle color is not connected to your material the compensation will not be applied.
	*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering")
	ENTPNiagaraRendererPixelCoverageMode PixelCoverageMode = ENTPNiagaraRendererPixelCoverageMode::Automatic;

	/**
	When pixel coverage is enabled this allows you to control the blend of the pixel coverage color adjustment.
	i.e. 1.0 = full, 0.5 = 50/50 blend, 0.0 = none
	*/
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering", meta = (EditCondition = "PixelCoverageMode != ENTPNiagaraRendererPixelCoverageMode::Disabled", UIMin=0.0f, UIMax=1.0f, EditConditionHides))
	float PixelCoverageBlend = 1.0f;

	/** When FacingMode is FacingCameraDistanceBlend, the distance at which the sprite is fully facing the camera plane. */
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering", meta = (UIMin = "0"))
	float MinFacingCameraBlendDistance = 0.0f;

	/** When FacingMode is FacingCameraDistanceBlend, the distance at which the sprite is fully facing the camera position */
	UPROPERTY(EditAnywhere, Category = "Sprite Rendering", meta = (UIMin = "0"))
	float MaxFacingCameraBlendDistance = 0.0f;

	/** Which attribute should we use for position when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Which attribute should we use for color when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding ColorBinding;

	/** Which attribute should we use for velocity when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding VelocityBinding;

	/** Which attribute should we use for sprite rotation (in degrees) when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding SpriteRotationBinding;

	/** Which attribute should we use for sprite size when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding SpriteSizeBinding;

	/** Which attribute should we use for sprite facing when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding SpriteFacingBinding;

	/** Which attribute should we use for sprite alignment when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding SpriteAlignmentBinding;

	/** Which attribute should we use for dynamic material parameters when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterialBinding;

	/** Which attribute should we use for dynamic material parameters when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial1Binding;

	/** Which attribute should we use for dynamic material parameters when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial2Binding;

	/** Which attribute should we use for dynamic material parameters when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial3Binding;

	/** Which attribute should we use for camera offset when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding CameraOffsetBinding;

	/** Which attribute should we use for UV scale when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding UVScaleBinding;

	/** Which attribute should we use for pivot offset? (NOTE: Values are expected to be in UV space). */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding PivotOffsetBinding;

	/** Which attribute should we use for the UV Rect (Offset + Size)? */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding UVRectBinding;

	/** Which attribute should we use for material randoms when generating sprites?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding MaterialRandomBinding;

	/** Which attribute should we use for custom sorting? Defaults to Particles.NormalizedAge. */
	UPROPERTY(EditAnywhere, Category = "Bindings", meta = (EditCondition = "SourceMode != ENiagaraRendererSourceDataMode::Emitter"))
	FNiagaraVariableAttributeBinding CustomSortingBinding;

	/** Which attribute should we use for Normalized Age? */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding NormalizedAgeBinding;

	/** If this array has entries, we will create a MaterialInstanceDynamic per Emitter instance from Material and set the Material parameters using the Niagara simulation variables listed.*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraRendererMaterialParameters MaterialParameters;

	/** Bind a specific Font Asset's texture to a named Material Parameter. */
	UPROPERTY(EditAnywhere, Category = "Bindings")
	TArray<FNTPFontParameterBinding> FontBindings;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FNiagaraMaterialAttributeBinding> MaterialParameterBindings_DEPRECATED;
#endif

	// The following bindings are only needed for accurate motion vectors

	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevPositionBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevVelocityBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevSpriteRotationBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevSpriteSizeBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevSpriteFacingBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevSpriteAlignmentBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevCameraOffsetBinding;
	UPROPERTY(Transient)
	FNiagaraVariableAttributeBinding PrevPivotOffsetBinding;

	virtual bool NeedsMIDsForMaterials() const override { return MaterialParameters.HasAnyBindings() || FontBindings.Num() > 0; }

	UPROPERTY()
	uint32 MaterialParamValidMask = 0;

	FNiagaraRendererLayout RendererLayoutWithCustomSort;
	FNiagaraRendererLayout RendererLayoutWithoutCustomSort;

protected:
	void InitBindings();
	void SetPreviousBindings(const FVersionedNiagaraEmitter& SrcEmitter, ENiagaraRendererSourceDataMode InSourceMode);
	virtual void UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit = false) override;

	void UpdateMICs();

#if WITH_EDITORONLY_DATA
	virtual FNiagaraVariable GetBoundAttribute(const FNiagaraVariableAttributeBinding* Binding) const override;
#endif

private:
	static TArray<TWeakObjectPtr<UNTPNiagaraFontRendererProperties>> FontRendererPropertiesToDeferredInit;
};
