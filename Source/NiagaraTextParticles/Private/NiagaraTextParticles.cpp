// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraTextParticles.h"
#include "Interfaces/IPluginManager.h"
#include "Shader.h"
#include "NiagaraSettings.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FNiagaraTextParticlesModule"

void FNiagaraTextParticlesModule::StartupModule()
{
	// Map plugin shader directory to the virtual path used by shaders
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NiagaraTextParticles"));
	if (Plugin.IsValid())
	{
		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/NiagaraTextParticles"), ShaderDir);
	}

	// Register ESpawnTextParticleMode as a Niagara Additional Parameter Enum
	if (UNiagaraSettings* NiagaraSettings = GetMutableDefault<UNiagaraSettings>())
	{
		const FSoftObjectPath EnumPath(TEXT("/NiagaraTextParticles/Enums/ESpawnTextParticleMode.ESpawnTextParticleMode"));

		if (!NiagaraSettings->AdditionalParameterEnums.Contains(EnumPath))
		{
			NiagaraSettings->AdditionalParameterEnums.Add(EnumPath);
			NiagaraSettings->SaveConfig();
		}
	}

}

void FNiagaraTextParticlesModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNiagaraTextParticlesModule, NiagaraTextParticles)