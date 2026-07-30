//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NiagaraTextToolkitEditorHelpers.generated.h"

class UFont;

UCLASS()
class NIAGARATEXTTOOLKITEDITOR_API UNiagaraTextToolkitEditorHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Niagara Text Toolkit Plugin")
	static bool SaveFontTexturesToAssets(UFont* FontAsset, const FString& FontAssetPath);

private:

	static void ShowSlateNotification(const FText& Message, float Duration = 3.0f);
};


