// Copyright Epic Games, Inc. All Rights Reserved.

#include "NTPNiagaraFontRendererProperties.h"
#include "NTPNiagaraFontVertexFactory.h"
#include "NTPDataInterface.h"
#include "Materials/Material.h"
#include "NiagaraRenderer.h"
#include "NiagaraConstants.h"
#include "NiagaraGPUSortInfo.h"
#include "NTPNiagaraRendererFonts.h"
#include "NTPNiagaraBoundsCalculatorHelper.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"

#include "Engine/Texture2D.h"
#include "Internationalization/Internationalization.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "UObject/UE5MainStreamObjectVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NTPNiagaraFontRendererProperties)

#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheInterface.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "NiagaraModule.h"
#include "Widgets/Images/SImage.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SWidget.h"
#include "AssetThumbnail.h"
#include "Widgets/Text/STextBlock.h"
#endif

#define LOCTEXT_NAMESPACE "UNTPNiagaraFontRendererProperties"

TArray<TWeakObjectPtr<UNTPNiagaraFontRendererProperties>> UNTPNiagaraFontRendererProperties::FontRendererPropertiesToDeferredInit;

UNTPNiagaraFontRendererProperties::UNTPNiagaraFontRendererProperties()
	: Material(nullptr)
	, MaterialUserParamBinding(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()))
	, NTPDataInterfaceBinding(FNiagaraTypeDefinition(UNTPDataInterface::StaticClass()))
	, bRemoveHMDRollInVR(false)
	, bSortOnlyWhenTranslucent(true)
{
	AttributeBindings =
	{
		// NOTE: These bindings' indices have to align to their counterpart in ENTPNiagaraSpriteVFLayout
		&PositionBinding,
		&ColorBinding,
		&VelocityBinding,
		&SpriteRotationBinding,
		&SpriteSizeBinding,
		&SpriteFacingBinding,
		&SpriteAlignmentBinding,
		&DynamicMaterialBinding,
		&DynamicMaterial1Binding,
		&DynamicMaterial2Binding,
		&DynamicMaterial3Binding,
		&CameraOffsetBinding,
		&UVScaleBinding,
		&PivotOffsetBinding,
		&MaterialRandomBinding,
		&CustomSortingBinding,
		&NormalizedAgeBinding,
		&UVRectBinding,

		// These bindings are only actually used with accurate motion vectors (indices still need to align)
		&PrevPositionBinding,
		&PrevVelocityBinding,
		&PrevSpriteRotationBinding,
		&PrevSpriteSizeBinding,
		&PrevSpriteFacingBinding,
		&PrevSpriteAlignmentBinding,
		&PrevCameraOffsetBinding,
		&PrevPivotOffsetBinding,
	};
}

FNiagaraRenderer* UNTPNiagaraFontRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController)
{
	FNiagaraRenderer* NewRenderer = new FNTPNiagaraRendererFonts(FeatureLevel, this, Emitter);
	NewRenderer->Initialize(this, Emitter, InController);
	return NewRenderer;
}

FNiagaraBoundsCalculator* UNTPNiagaraFontRendererProperties::CreateBoundsCalculator()
{
	if (GetCurrentSourceMode() == ENiagaraRendererSourceDataMode::Emitter)
	{
		return nullptr;
	}

	return new FNTPNiagaraBoundsCalculatorHelper<false, false, false>();
}

void UNTPNiagaraFontRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	UMaterialInterface* MaterialInterface = nullptr;
	if (InEmitter != nullptr)
	{
		MaterialInterface = Cast<UMaterialInterface>(InEmitter->FindBinding(MaterialUserParamBinding.Parameter));
	}

#if WITH_EDITORONLY_DATA
	MaterialInterface = MaterialInterface ? MaterialInterface : ToRawPtr(MICMaterial);
#endif

	OutMaterials.Add(MaterialInterface ? MaterialInterface : ToRawPtr(Material));
}

void UNTPNiagaraFontRendererProperties::CollectPSOPrecacheData(FPSOPrecacheParamsList& OutParams)
{
	const FVertexFactoryType* VFType = GetVertexFactoryType();
	UMaterialInterface* MaterialInterface = ToRawPtr(Material);

	if (MaterialInterface)
	{
		FPSOPrecacheParams& PSOPrecacheParams = OutParams.AddDefaulted_GetRef();
		PSOPrecacheParams.MaterialInterface = MaterialInterface;
		// Spite VF is the same for MVF and non-MVF cases
		PSOPrecacheParams.VertexFactoryDataList.Add(FPSOPrecacheVertexFactoryData(VFType));
	}
}

const FVertexFactoryType* UNTPNiagaraFontRendererProperties::GetVertexFactoryType() const
{
	return &FNTPNiagaraFontVertexFactory::StaticType;
}

void UNTPNiagaraFontRendererProperties::PostLoad()
{
	Super::PostLoad();

	if ( Material )
	{
		Material->ConditionalPostLoad();
	}

#if WITH_EDITORONLY_DATA
	if (MaterialUserParamBinding.Parameter.GetType().GetClass() != UMaterialInterface::StaticClass())
	{
		FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
		MaterialUserParamBinding.Parameter.SetType(MaterialDef);
	}

	ChangeToPositionBinding(PositionBinding);
	ChangeToPositionBinding(PrevPositionBinding);
	PostLoadBindings(SourceMode);

	// Fix up these bindings from their loaded source bindings
	SetPreviousBindings(FVersionedNiagaraEmitter(), SourceMode);

	if (MaterialParameterBindings_DEPRECATED.Num() > 0)
	{
		MaterialParameters.AttributeBindings = MaterialParameterBindings_DEPRECATED;
		MaterialParameterBindings_DEPRECATED.Empty();
	}
#endif
}

void UNTPNiagaraFontRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			FontRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		InitBindings();
	}
}

void UNTPNiagaraFontRendererProperties::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
	const int32 NiagaraVersion = Ar.CustomVer(FNiagaraCustomVersion::GUID);
	const int32 UE5MainVersion = Ar.CustomVer(FUE5MainStreamObjectVersion::GUID);

	if (Ar.IsLoading() && (NiagaraVersion < FNiagaraCustomVersion::DisableSortingByDefault))
	{
		SortMode = ENiagaraSortMode::ViewDistance;
	}

	if (Ar.IsLoading() && (UE5MainVersion < FUE5MainStreamObjectVersion::NiagaraSpriteRendererFacingAlignmentAutoDefault))
	{
		Alignment = ENTPNiagaraSpriteAlignment::Unaligned;
		FacingMode = ENTPNiagaraSpriteFacingMode::FaceCamera;
	}

	// MIC will replace the main material during serialize
	// Be careful if adding code that looks at the material to make sure you get the correct one
	{
	#if WITH_EDITORONLY_DATA
		TOptional<TGuardValue<TObjectPtr<UMaterialInterface>>> MICGuard;
		if (Ar.IsSaving() && Ar.IsCooking() && MICMaterial)
		{
			MICGuard.Emplace(Material, MICMaterial);
		}
	#endif

		Super::Serialize(Record);
	}

	bool bIsCookedForEditor = false;
#if WITH_EDITORONLY_DATA
	bIsCookedForEditor = ((Ar.GetPortFlags() & PPF_Duplicate) == 0) && GetPackage()->HasAnyPackageFlags(PKG_Cooked);
#endif // WITH_EDITORONLY_DATA

	if (Ar.IsCooking() || (FPlatformProperties::RequiresCookedData() && Ar.IsLoading()) || bIsCookedForEditor)
	{
		//DerivedData.Serialize(Record.EnterField(TEXT("DerivedData")));
	}
}

void UNTPNiagaraFontRendererProperties::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RendererLayoutWithCustomSort.GetAllocatedSize());
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RendererLayoutWithoutCustomSort.GetAllocatedSize());
}

/** The bindings depend on variables that are created during the NiagaraModule startup. However, the CDO's are build prior to this being initialized, so we defer setting these values until later.*/
void UNTPNiagaraFontRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNTPNiagaraFontRendererProperties* CDO = CastChecked<UNTPNiagaraFontRendererProperties>(UNTPNiagaraFontRendererProperties::StaticClass()->GetDefaultObject());
	CDO->InitBindings();

	for (TWeakObjectPtr<UNTPNiagaraFontRendererProperties>& WeakFontRendererProperties : FontRendererPropertiesToDeferredInit)
	{
		if (WeakFontRendererProperties.Get())
		{
			WeakFontRendererProperties->InitBindings();
		}
	}
}

void UNTPNiagaraFontRendererProperties::InitBindings()
{
	if (PositionBinding.GetParamMapBindableVariable().GetName() == NAME_None)
	{
		PositionBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		ColorBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COLOR);
		VelocityBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VELOCITY);
		SpriteRotationBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_ROTATION);
		SpriteSizeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_SIZE);
		SpriteFacingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_FACING);
		SpriteAlignmentBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT);
		DynamicMaterialBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		DynamicMaterial1Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		DynamicMaterial2Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		DynamicMaterial3Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		CameraOffsetBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_CAMERA_OFFSET);
		UVScaleBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_UV_SCALE);
		PivotOffsetBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_PIVOT_OFFSET);
		MaterialRandomBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
		NormalizedAgeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		// Initialize UVRectBinding
		FNiagaraVariable UVRectVar(FNiagaraTypeDefinition::GetVec4Def(), FName("Particles.UVRect"));
		UVRectBinding = FNiagaraConstants::GetAttributeDefaultBinding(UVRectVar);

		//Default custom sorting to age
		CustomSortingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
	}

	SetPreviousBindings(FVersionedNiagaraEmitter(), SourceMode);
}

void UNTPNiagaraFontRendererProperties::SetPreviousBindings(const FVersionedNiagaraEmitter& SrcEmitter, ENiagaraRendererSourceDataMode InSourceMode)
{
	PrevPositionBinding.SetAsPreviousValue(PositionBinding, SrcEmitter, InSourceMode);
	PrevVelocityBinding.SetAsPreviousValue(VelocityBinding, SrcEmitter, InSourceMode);
	PrevSpriteRotationBinding.SetAsPreviousValue(SpriteRotationBinding, SrcEmitter, InSourceMode);
	PrevSpriteSizeBinding.SetAsPreviousValue(SpriteSizeBinding, SrcEmitter, InSourceMode);
	PrevSpriteFacingBinding.SetAsPreviousValue(SpriteFacingBinding, SrcEmitter, InSourceMode);
	PrevSpriteAlignmentBinding.SetAsPreviousValue(SpriteAlignmentBinding, SrcEmitter, InSourceMode);
	PrevCameraOffsetBinding.SetAsPreviousValue(CameraOffsetBinding, SrcEmitter, InSourceMode);
	PrevPivotOffsetBinding.SetAsPreviousValue(PivotOffsetBinding, SrcEmitter, InSourceMode);
}

void UNTPNiagaraFontRendererProperties::CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData)
{
	UpdateSourceModeDerivates(SourceMode);
	UpdateMICs();

	// Initialize layout
	const int32 NumLayoutVars = NeedsPreciseMotionVectors() ? ENTPNiagaraSpriteVFLayout::Num_Max : ENTPNiagaraSpriteVFLayout::Num_Default;
	RendererLayoutWithCustomSort.Initialize(NumLayoutVars);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PositionBinding, ENTPNiagaraSpriteVFLayout::Position);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, VelocityBinding, ENTPNiagaraSpriteVFLayout::Velocity);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, ColorBinding, ENTPNiagaraSpriteVFLayout::Color);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, SpriteRotationBinding, ENTPNiagaraSpriteVFLayout::Rotation);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, SpriteSizeBinding, ENTPNiagaraSpriteVFLayout::Size);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, SpriteFacingBinding, ENTPNiagaraSpriteVFLayout::Facing);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, SpriteAlignmentBinding, ENTPNiagaraSpriteVFLayout::Alignment);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, CameraOffsetBinding, ENTPNiagaraSpriteVFLayout::CameraOffset);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, UVScaleBinding, ENTPNiagaraSpriteVFLayout::UVScale);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PivotOffsetBinding, ENTPNiagaraSpriteVFLayout::PivotOffset);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, UVRectBinding, ENTPNiagaraSpriteVFLayout::UVRect);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, NormalizedAgeBinding, ENTPNiagaraSpriteVFLayout::NormalizedAge);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, MaterialRandomBinding, ENTPNiagaraSpriteVFLayout::MaterialRandom);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, CustomSortingBinding, ENTPNiagaraSpriteVFLayout::CustomSorting);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterialBinding, ENTPNiagaraSpriteVFLayout::MaterialParam0);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial1Binding, ENTPNiagaraSpriteVFLayout::MaterialParam1);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial2Binding, ENTPNiagaraSpriteVFLayout::MaterialParam2);
	RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial3Binding, ENTPNiagaraSpriteVFLayout::MaterialParam3);
	if (NeedsPreciseMotionVectors())
	{
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevPositionBinding, ENTPNiagaraSpriteVFLayout::PrevPosition);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevVelocityBinding, ENTPNiagaraSpriteVFLayout::PrevVelocity);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteRotationBinding, ENTPNiagaraSpriteVFLayout::PrevRotation);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteSizeBinding, ENTPNiagaraSpriteVFLayout::PrevSize);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteFacingBinding, ENTPNiagaraSpriteVFLayout::PrevFacing);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteAlignmentBinding, ENTPNiagaraSpriteVFLayout::PrevAlignment);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevCameraOffsetBinding, ENTPNiagaraSpriteVFLayout::PrevCameraOffset);
		RendererLayoutWithCustomSort.SetVariableFromBinding(CompiledData, PrevPivotOffsetBinding, ENTPNiagaraSpriteVFLayout::PrevPivotOffset);
	}
	RendererLayoutWithCustomSort.Finalize();

	RendererLayoutWithoutCustomSort.Initialize(NumLayoutVars);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PositionBinding, ENTPNiagaraSpriteVFLayout::Position);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, VelocityBinding, ENTPNiagaraSpriteVFLayout::Velocity);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, ColorBinding, ENTPNiagaraSpriteVFLayout::Color);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, SpriteRotationBinding, ENTPNiagaraSpriteVFLayout::Rotation);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, SpriteSizeBinding, ENTPNiagaraSpriteVFLayout::Size);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, SpriteFacingBinding, ENTPNiagaraSpriteVFLayout::Facing);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, SpriteAlignmentBinding, ENTPNiagaraSpriteVFLayout::Alignment);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, CameraOffsetBinding, ENTPNiagaraSpriteVFLayout::CameraOffset);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, UVScaleBinding, ENTPNiagaraSpriteVFLayout::UVScale);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PivotOffsetBinding, ENTPNiagaraSpriteVFLayout::PivotOffset);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, UVRectBinding, ENTPNiagaraSpriteVFLayout::UVRect);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, NormalizedAgeBinding, ENTPNiagaraSpriteVFLayout::NormalizedAge);
	RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, MaterialRandomBinding, ENTPNiagaraSpriteVFLayout::MaterialRandom);
	const bool bDynamicParam0Valid = RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterialBinding,  ENTPNiagaraSpriteVFLayout::MaterialParam0);
	const bool bDynamicParam1Valid = RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial1Binding, ENTPNiagaraSpriteVFLayout::MaterialParam1);
	const bool bDynamicParam2Valid = RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial2Binding, ENTPNiagaraSpriteVFLayout::MaterialParam2);
	const bool bDynamicParam3Valid = RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, DynamicMaterial3Binding, ENTPNiagaraSpriteVFLayout::MaterialParam3);
	if (NeedsPreciseMotionVectors())
	{
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevPositionBinding, ENTPNiagaraSpriteVFLayout::PrevPosition);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevVelocityBinding, ENTPNiagaraSpriteVFLayout::PrevVelocity);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteRotationBinding, ENTPNiagaraSpriteVFLayout::PrevRotation);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteSizeBinding, ENTPNiagaraSpriteVFLayout::PrevSize);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteFacingBinding, ENTPNiagaraSpriteVFLayout::PrevFacing);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevSpriteAlignmentBinding, ENTPNiagaraSpriteVFLayout::PrevAlignment);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevCameraOffsetBinding, ENTPNiagaraSpriteVFLayout::PrevCameraOffset);
		RendererLayoutWithoutCustomSort.SetVariableFromBinding(CompiledData, PrevPivotOffsetBinding, ENTPNiagaraSpriteVFLayout::PrevPivotOffset);
	}
	RendererLayoutWithoutCustomSort.Finalize();

#if WITH_EDITORONLY_DATA
	// Build dynamic parameter mask
	// Serialize in cooked builds
	const FVersionedNiagaraEmitterData* EmitterData = GetEmitterData();
	MaterialParamValidMask  = bDynamicParam0Valid ? GetDynamicParameterChannelMask(EmitterData, DynamicMaterialBinding.GetName(),  0xf) <<  0 : 0;
	MaterialParamValidMask |= bDynamicParam1Valid ? GetDynamicParameterChannelMask(EmitterData, DynamicMaterial1Binding.GetName(), 0xf) <<  4 : 0;
	MaterialParamValidMask |= bDynamicParam2Valid ? GetDynamicParameterChannelMask(EmitterData, DynamicMaterial2Binding.GetName(), 0xf) <<  8 : 0;
	MaterialParamValidMask |= bDynamicParam3Valid ? GetDynamicParameterChannelMask(EmitterData, DynamicMaterial3Binding.GetName(), 0xf) << 12 : 0;
#endif
}

#if WITH_EDITORONLY_DATA
TArray<FNiagaraVariable> UNTPNiagaraFontRendererProperties::GetBoundAttributes() const
{
	TArray<FNiagaraVariable> BoundAttributes = Super::GetBoundAttributes();
	BoundAttributes.Reserve(BoundAttributes.Num() + MaterialParameters.AttributeBindings.Num());

	for (const FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
	{
		BoundAttributes.AddUnique(MaterialParamBinding.GetParamMapBindableVariable());
	}
	return BoundAttributes;
}
#endif

bool UNTPNiagaraFontRendererProperties::PopulateRequiredBindings(FNiagaraParameterStore& InParameterStore)
{
	bool bAnyAdded = Super::PopulateRequiredBindings(InParameterStore);

	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		if (Binding && Binding->CanBindToHostParameterMap())
		{
			InParameterStore.AddParameter(Binding->GetParamMapBindableVariable(), false);
			bAnyAdded = true;
		}
	}

	for (FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
	{
		InParameterStore.AddParameter(MaterialParamBinding.GetParamMapBindableVariable(), false);
		bAnyAdded = true;
	}

	return bAnyAdded;
}

void UNTPNiagaraFontRendererProperties::UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit)
{
	Super::UpdateSourceModeDerivates(InSourceMode, bFromPropertyEdit);
	
	UNiagaraEmitter* SrcEmitter = GetTypedOuter<UNiagaraEmitter>();
	if (SrcEmitter)
	{
		for (FNiagaraMaterialAttributeBinding& MaterialParamBinding : MaterialParameters.AttributeBindings)
		{
			MaterialParamBinding.CacheValues(SrcEmitter);
		}

		SetPreviousBindings(FVersionedNiagaraEmitter(), InSourceMode);
	}
}

void UNTPNiagaraFontRendererProperties::UpdateMICs()
{
#if WITH_EDITORONLY_DATA
	UpdateMaterialParametersMIC(MaterialParameters, Material, MICMaterial);
#endif
}

#if WITH_EDITORONLY_DATA
void UNTPNiagaraFontRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();

	// Update our MICs if we change material / material bindings
	//-OPT: Could narrow down further to only static materials
	if ((PropertyName == GET_MEMBER_NAME_CHECKED(UNTPNiagaraFontRendererProperties, Material)) ||
		(MemberPropertyName == GET_MEMBER_NAME_CHECKED(UNTPNiagaraFontRendererProperties, MaterialParameters)) )
	{
		UpdateMICs();
	}

	// If changing the source mode, we may need to update many of our values.
	if (PropertyChangedEvent.GetPropertyName() == TEXT("SourceMode"))
	{
		UpdateSourceModeDerivates(SourceMode, true);
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(PropertyChangedEvent.Property))
	{
		if (StructProp->Struct == FNiagaraVariableAttributeBinding::StaticStruct())
		{
			UpdateSourceModeDerivates(SourceMode, true);
		}
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(PropertyChangedEvent.Property))
	{
		if (ArrayProp->Inner)
		{
			FStructProperty* ChildStructProp = CastField<FStructProperty>(ArrayProp->Inner);
			if (ChildStructProp->Struct == FNiagaraMaterialAttributeBinding::StaticStruct())
			{
				UpdateSourceModeDerivates(SourceMode, true);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNTPNiagaraFontRendererProperties::RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const FVersionedNiagaraEmitter& InEmitter)
{
	Super::RenameVariable(OldVariable, NewVariable, InEmitter);
#if WITH_EDITORONLY_DATA
	MaterialParameters.RenameVariable(OldVariable, NewVariable, InEmitter, GetCurrentSourceMode());
#endif
}

void UNTPNiagaraFontRendererProperties::RemoveVariable(const FNiagaraVariableBase& OldVariable, const FVersionedNiagaraEmitter& InEmitter)
{
	Super::RemoveVariable(OldVariable, InEmitter);
#if WITH_EDITORONLY_DATA
	MaterialParameters.RemoveVariable(OldVariable, InEmitter, GetCurrentSourceMode());
#endif
}

const TArray<FNiagaraVariable>& UNTPNiagaraFontRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_POSITION);
		Attrs.Add(SYS_PARAM_PARTICLES_VELOCITY);
		Attrs.Add(SYS_PARAM_PARTICLES_COLOR);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_ROTATION);
		Attrs.Add(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_SIZE);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_FACING);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		Attrs.Add(SYS_PARAM_PARTICLES_CAMERA_OFFSET);
		Attrs.Add(SYS_PARAM_PARTICLES_UV_SCALE);
		Attrs.Add(SYS_PARAM_PARTICLES_PIVOT_OFFSET);
		Attrs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), FName("Particles.UVRect")));
		Attrs.Add(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
	}

	return Attrs;
}

void UNTPNiagaraFontRendererProperties::GetAdditionalVariables(TArray<FNiagaraVariableBase>& OutArray) const
{
	if (NeedsPreciseMotionVectors())
	{
		OutArray.Reserve(8);
		OutArray.Add(PrevPositionBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevVelocityBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevSpriteRotationBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevSpriteSizeBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevSpriteFacingBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevSpriteAlignmentBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevCameraOffsetBinding.GetParamMapBindableVariable());
		OutArray.Add(PrevPivotOffsetBinding.GetParamMapBindableVariable());		
	}
}

void UNTPNiagaraFontRendererProperties::GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TSharedRef<SWidget> ThumbnailWidget = SNullWidget::NullWidget;
	int32 ThumbnailSize = 32;
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	for (UMaterialInterface* PreviewedMaterial : Materials)
	{
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShareable(new FAssetThumbnail(PreviewedMaterial, ThumbnailSize, ThumbnailSize, InThumbnailPool));
		if (AssetThumbnail)
		{
			ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();
		}
		OutWidgets.Add(ThumbnailWidget);
	}

	if (Materials.Num() == 0)
	{
		TSharedRef<SWidget> SpriteWidget = SNew(SImage)
			.Image(FSlateIconFinder::FindIconBrushForClass(GetClass()));
		OutWidgets.Add(SpriteWidget);
	}
}


void UNTPNiagaraFontRendererProperties::GetRendererFeedback(const FVersionedNiagaraEmitter& InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);
}

void UNTPNiagaraFontRendererProperties::GetRendererFeedback(const FVersionedNiagaraEmitter & InEmitter, TArray<FNiagaraRendererFeedback>&OutErrors, TArray<FNiagaraRendererFeedback>&OutWarnings, TArray<FNiagaraRendererFeedback>&OutInfo) const
{
	Super::GetRendererFeedback(InEmitter, OutErrors, OutWarnings, OutInfo);

	if (MaterialParameters.HasAnyBindings())
	{
		TArray<UMaterialInterface*> Materials;
		GetUsedMaterials(nullptr, Materials);
		MaterialParameters.GetFeedback(Materials, OutWarnings);
	}
}

void UNTPNiagaraFontRendererProperties::GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const
{
	TArray<UMaterialInterface*> Materials;
	GetUsedMaterials(InEmitter, Materials);
	if (Materials.Num() > 0)
	{
		GetRendererWidgets(InEmitter, OutWidgets, InThumbnailPool);
	}
	else
	{
		TSharedRef<SWidget> SpriteTooltip = SNew(STextBlock)
			.Text(LOCTEXT("SpriteRendererNoMat", "Sprite Renderer (No Material Set)"));
		OutWidgets.Add(SpriteTooltip);
	}
}

FNiagaraVariable UNTPNiagaraFontRendererProperties::GetBoundAttribute(const FNiagaraVariableAttributeBinding* Binding) const
{
	if (!NeedsPreciseMotionVectors())
	{
		if (Binding == &PrevPositionBinding
			|| Binding == &PrevVelocityBinding
			|| Binding == &PrevSpriteRotationBinding
			|| Binding == &PrevSpriteSizeBinding
			|| Binding == &PrevSpriteFacingBinding
			|| Binding == &PrevSpriteAlignmentBinding
			|| Binding == &PrevCameraOffsetBinding
			|| Binding == &PrevPivotOffsetBinding)
		{
			return FNiagaraVariable();
		}
	}

	return Super::GetBoundAttribute(Binding);
}

#endif // WITH_EDITORONLY_DATA

