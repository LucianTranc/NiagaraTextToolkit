// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTextToolkitEditor.h"
#include "NiagaraSettings.h"
#include "Misc/Paths.h"

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
}

void FNiagaraTextToolkitEditorModule::ShutdownModule()
{
	// Editor-only shutdown logic for NiagaraTextToolkit can be added here
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNiagaraTextToolkitEditorModule, NiagaraTextToolkitEditor)
