//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/DataAsset.h"
#include "NTTDataInterface.h"
#include "NiagaraTextToolkitHelpers.generated.h"

UCLASS()
class NIAGARATEXTTOOLKIT_API UNiagaraTextToolkitHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Niagara Text Toolkit Plugin", meta = (DisplayName = "Set Niagara Variable (NTT Text)"))
	static void SetNiagaraNTTTextVariable(UNiagaraComponent* System, FString TextToDisplay);

	UFUNCTION(BlueprintCallable, Category = "Niagara Text Toolkit Plugin", meta = (DisplayName = "Set Niagara Variable (NTT Font)"))
	static void SetNiagaraNTTFontVariable(UNiagaraComponent* System, UFont* Font);

private:

	static UNTTDataInterface* FindNTTDataInterface(UNiagaraComponent* System);

};
