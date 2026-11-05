#include "AedirOptimizeAssetsCommandlet.h"

#include "AedirAssetPolicySubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AedirEditor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

UAedirOptimizeAssetsCommandlet::UAedirOptimizeAssetsCommandlet()
{
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UAedirOptimizeAssetsCommandlet::Main(const FString& Params)
{
	const bool bDryRun = FParse::Param(*Params, TEXT("dryrun"));

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.SearchAllAssets(true);

	TArray<FAssetData> GameAssets;
	AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), GameAssets, true);

	const FTopLevelAssetPath TextureClassPath = UTexture2D::StaticClass()->GetClassPathName();
	const FTopLevelAssetPath StaticMeshClassPath = UStaticMesh::StaticClass()->GetClassPathName();

	int32 ScannedTextures = 0;
	int32 ScannedMeshes = 0;
	int32 ChangedTextures = 0;
	int32 ChangedMeshes = 0;
	TSet<UPackage*> PackagesToSave;

	for (const FAssetData& AssetData : GameAssets)
	{
		const bool bIsTexture = AssetData.AssetClassPath == TextureClassPath;
		const bool bIsStaticMesh = AssetData.AssetClassPath == StaticMeshClassPath;
		if (!bIsTexture && !bIsStaticMesh)
		{
			continue;
		}

		UObject* Asset = AssetData.GetAsset();
		if (!Asset)
		{
			UE_LOG(LogAedirEditor, Warning, TEXT("Failed to load %s."), *AssetData.GetObjectPathString());
			continue;
		}

		bool bChanged = false;
		if (bIsTexture)
		{
			++ScannedTextures;
			if (UAedirAssetPolicySubsystem::ApplyTexturePolicy(Cast<UTexture2D>(Asset)))
			{
				++ChangedTextures;
				bChanged = true;
			}
		}
		else
		{
			++ScannedMeshes;
			if (UAedirAssetPolicySubsystem::ApplyNanitePolicy(Cast<UStaticMesh>(Asset)))
			{
				++ChangedMeshes;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			PackagesToSave.Add(Asset->GetOutermost());
		}
	}

	UE_LOG(LogAedirEditor, Display, TEXT("Scanned %d textures and %d static meshes under /Game. Changed textures: %d, changed static meshes: %d. Mode: %s."),
		ScannedTextures, ScannedMeshes, ChangedTextures, ChangedMeshes, bDryRun ? TEXT("dry run, nothing saved") : TEXT("save"));

	if (!bDryRun && PackagesToSave.Num() > 0)
	{
		TArray<UPackage*> Packages = PackagesToSave.Array();
		if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true))
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Saving %d modified packages failed."), Packages.Num());
			return 1;
		}

		UE_LOG(LogAedirEditor, Display, TEXT("Saved %d modified packages."), Packages.Num());
	}

	return 0;
}
