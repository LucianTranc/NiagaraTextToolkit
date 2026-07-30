//Copyright Tranc Software, Inc. All Rights Reserved (2026)

#include "NiagaraTextToolkitEditorHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "Misc/ScopedSlowTask.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

bool UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets(UFont* FontAsset, const FString& FontAssetPath)
{
	if (!FontAsset)
	{
		UE_LOG(LogTemp, Error, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: FontAsset is null"));
		return false;
	}

	if (FontAsset->Textures.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Font '%s' has no textures"), *FontAsset->GetName());
		return false;
	}

	FString PackageName = FPackageName::ObjectPathToPackageName(FontAssetPath);
	FString PackagePathForValidation = FPackageName::GetLongPackagePath(PackageName);
	FString ShortName = FPackageName::GetShortName(PackageName);
	FString BaseName = ShortName.StartsWith(TEXT("F_")) ? TEXT("T_") + ShortName.RightChop(2) : TEXT("T_") + ShortName;

	FText InvalidPathReason;
	if (!FPackageName::IsValidLongPackageName(PackagePathForValidation, false, &InvalidPathReason))
	{
		UE_LOG(LogTemp, Error, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Invalid package path '%s': %s"), *PackagePathForValidation, *InvalidPathReason.ToString());
		return false;
	}

	FString NormalizedPackagePath = PackagePathForValidation + TEXT("/");

	bool bAllSuccessful = true;
	TArray<UObject*> CreatedAssets;

	const FScopedTransaction Transaction(NSLOCTEXT("NiagaraTextToolkit", "SaveFontTexturesToAssets", "Save Font Textures To Assets"));

	FScopedSlowTask SlowTask(FontAsset->Textures.Num(), NSLOCTEXT("NiagaraTextToolkit", "SavingFontTextures", "Saving font textures to assets..."));
	SlowTask.MakeDialog(true);

	for (int32 i = 0; i < FontAsset->Textures.Num(); i++)
	{
		if (SlowTask.ShouldCancel())
		{
			bAllSuccessful = false;
			break;
		}
		SlowTask.EnterProgressFrame(1);

		UTexture2D* SourceTexture = FontAsset->Textures[i];
		if (!SourceTexture)
		{
			bAllSuccessful = false;
			continue;
		}

		FString AssetName = BaseName;

		if (FontAsset->Textures.Num() > 1)
		{
			AssetName += FString::Printf(TEXT("_%d"), i);
		}

		const FString BasePackageName = NormalizedPackagePath + AssetName;
		FString UniquePackageName, UniqueAssetName;
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().CreateUniqueAssetName(BasePackageName, TEXT(""), UniquePackageName, UniqueAssetName);
		}

		UPackage* Package = CreatePackage(*UniquePackageName);
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Failed to create package"));
			bAllSuccessful = false;
			continue;
		}
		Package->FullyLoad();

		UTexture2D* NewTexture = DuplicateObject<UTexture2D>(SourceTexture, Package, *UniqueAssetName);
		if (!NewTexture)
		{
			UE_LOG(LogTemp, Error, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Failed to duplicate texture %d"), i);
			bAllSuccessful = false;
			continue;
		}

		NewTexture->SetFlags(RF_Public | RF_Standalone);
		NewTexture->ClearFlags(RF_Transient);
		NewTexture->MarkPackageDirty();

		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			AssetRegistryModule.AssetCreated(NewTexture);
		}

		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;

			const FString PackageFilename = FPackageName::LongPackageNameToFilename(UniquePackageName, FPackageName::GetAssetPackageExtension());
			if (!UPackage::SavePackage(Package, NewTexture, *PackageFilename, SaveArgs))
			{
				UE_LOG(LogTemp, Warning, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Save failed for '%s'"), *UniquePackageName);
				bAllSuccessful = false;
			}
		}

		CreatedAssets.Add(NewTexture);
	}

	if (CreatedAssets.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(CreatedAssets);
	}

	if (bAllSuccessful)
	{
		UE_LOG(LogTemp, Log, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Successfully saved font textures to assets"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UNiagaraTextToolkitEditorHelpers::SaveFontTexturesToAssets: Failed to save some font textures to assets"));
	}

	ShowSlateNotification(FText::FromString(FString::Printf(TEXT("Font textures saved to assets at:\n%s"), *FontAssetPath)), 5.0f);

	return bAllSuccessful;
}

void UNiagaraTextToolkitEditorHelpers::ShowSlateNotification(const FText& Message, float Duration)
{
	FNotificationInfo Info(Message);

	Info.ExpireDuration = Duration;
	Info.Image = FCoreStyle::Get().GetBrush("icons.SuccessWithColor");

	FSlateNotificationManager::Get().AddNotification(Info);
}


