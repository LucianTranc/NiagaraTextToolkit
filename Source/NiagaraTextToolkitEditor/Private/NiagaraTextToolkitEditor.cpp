//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#include "NiagaraTextToolkitEditor.h"
#include "NiagaraSettings.h"
#include "Misc/Paths.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Customizations/NTTHorizontalAlignmentCustomization.h"
#include "Customizations/NTTVerticalAlignmentCustomization.h"

#define LOCTEXT_NAMESPACE "FNiagaraTextToolkitEditorModule"

void FNiagaraTextToolkitEditorModule::StartupModule()
{
	// Editor-only startup logic for NiagaraTextToolkit

	// Register ESpawnTextParticleMode as a Niagara Additional Parameter Enum
	if (UNiagaraSettings* NiagaraSettings = GetMutableDefault<UNiagaraSettings>())
	{
		const FSoftObjectPath EnumPath(TEXT("/NiagaraTextToolkit/Enums/ESpawnTextParticleMode.ESpawnTextParticleMode"));

		if (!NiagaraSettings->AdditionalParameterEnums.Contains(EnumPath))
		{
			NiagaraSettings->AdditionalParameterEnums.Add(EnumPath);
			NiagaraSettings->SaveConfig();
		}
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		TEXT("ENTTTextHorizontalAlignment"),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNTTHorizontalAlignmentCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(
		TEXT("ENTTTextVerticalAlignment"),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNTTVerticalAlignmentCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FNiagaraTextToolkitEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("ENTTTextHorizontalAlignment"));
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("ENTTTextVerticalAlignment"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNiagaraTextToolkitEditorModule, NiagaraTextToolkitEditor)
