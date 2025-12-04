// Copyright Epic Games, Inc. All Rights Reserved.

// NiagaraTextToolkit.cpp

#include "NiagaraTextToolkit.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FNiagaraTextToolkitModule"

void FNiagaraTextToolkitModule::StartupModule()
{
    FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NiagaraTextToolkit"))->GetBaseDir(), TEXT("Shaders"));
    AddShaderSourceDirectoryMapping(TEXT("/Plugin/NiagaraTextToolkit"), PluginShaderDir);
}

void FNiagaraTextToolkitModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNiagaraTextToolkitModule, NiagaraTextToolkit)
