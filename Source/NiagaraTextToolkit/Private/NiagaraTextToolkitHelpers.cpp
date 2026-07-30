//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#include "NiagaraTextToolkitHelpers.h"

#include "NiagaraComponent.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NTTDataInterface.h"

void UNiagaraTextToolkitHelpers::SetNiagaraNTTTextVariable(UNiagaraComponent* System, FString TextToDisplay)
{
	UNTTDataInterface* FoundDI = FindNTTDataInterface(System);

	if (FoundDI)
	{
		FoundDI->InputText = TextToDisplay;

		// Only reinitialize if the component is currently active
		if (System && System->IsActive() && System->GetSystemInstanceController())
		{
			System->ReinitializeSystem();
		}
	}
}

void UNiagaraTextToolkitHelpers::SetNiagaraNTTFontVariable(UNiagaraComponent* System, UFont* Font)
{
	UNTTDataInterface* FoundDI = FindNTTDataInterface(System);

	if (FoundDI)
	{
		FoundDI->FontAsset = Font;

		// Only reinitialize if the component is currently active
		if (System && System->IsActive() && System->GetSystemInstanceController())
		{
			System->ReinitializeSystem();
		}
	}
}

UNTTDataInterface* UNiagaraTextToolkitHelpers::FindNTTDataInterface(UNiagaraComponent* System)
{
	if (!System)
	{
		UE_LOG(LogNiagaraTextToolkit, Warning, TEXT("FontFXHelpers: Niagara component is null"));
		return nullptr;
	}

	UNTTDataInterface* FoundDI = nullptr;

	FNiagaraUserRedirectionParameterStore& Overrides = System->GetOverrideParameters();
	TArray<FNiagaraVariable> OutParameters;

	Overrides.GetUserParameters(OutParameters);

	const int32 NumParams = OutParameters.Num();
	for (int32 i = 0; i < NumParams; ++i)
	{
		const FNiagaraVariable& Var = OutParameters[i];
		if (Var.GetType() == FNiagaraTypeDefinition(UNTTDataInterface::StaticClass()))
		{
			if (UNiagaraDataInterface* DI = Overrides.GetDataInterface(Var))
			{
				FoundDI = Cast<UNTTDataInterface>(DI);
				if (FoundDI)
				{
					UE_LOG(LogNiagaraTextToolkit, Log, TEXT("FontFXHelpers: Found NTT DI on component overrides: %s (Param: %s)"),
						*GetNameSafe(FoundDI), *Var.GetName().ToString());
					break;
				}
			}
		}
	}

	if (!FoundDI)
	{
		UE_LOG(LogNiagaraTextToolkit, Warning, TEXT("FontFXHelpers: No UNTTDataInterface user variable found on component or system"));
		return nullptr;
	}

	return FoundDI;
}