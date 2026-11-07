#include "AedirImportModelsCommandlet.h"

#include "AedirEditor.h"
#include "AssetImportTask.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

UAedirImportModelsCommandlet::UAedirImportModelsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAedirImportModelsCommandlet::Main(const FString& Params)
{
	FString Source;
	if (!FParse::Value(*Params, TEXT("Source="), Source))
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Usage: -run=AedirImportModels -Source=<folder>"));
		return 1;
	}

	TArray<FString> Models;
	IFileManager::Get().FindFiles(Models, *(Source / TEXT("SM_*.fbx")), true, false);
	TArray<UPackage*> ToSave;
	for (const FString& ModelFile : Models)
	{
		const FString Name = FPaths::GetBaseFilename(ModelFile).RightChop(3);
		const FString Destination = FString::Printf(TEXT("/Game/Megascans/3D_Assets/%s"), *Name);

		// Through the factories themselves: the import tasks want a content browser to show the result in, and there is none here.
		const auto Import = [&](UFactory* Factory, UClass* Class, const FString& File, const FString& AssetName, UFbxImportUI* Options)
		{
			UAssetImportTask* Task = NewObject<UAssetImportTask>();
			Task->Filename = File;
			Task->DestinationPath = Destination;
			Task->bAutomated = true;
			Task->bReplaceExisting = true;
			Task->Options = Options;
			Factory->SetAssetImportTask(Task);
			Factory->AddToRoot();
			UPackage* Package = CreatePackage(*(Destination / AssetName));
			bool bCanceled = false;
			UObject* Object = UFactory::StaticImportObject(Class, Package, FName(*AssetName), RF_Public | RF_Standalone, bCanceled, *File, nullptr, Factory);
			Factory->RemoveFromRoot();
			if (Object)
			{
				UE_LOG(LogAedirEditor, Display, TEXT("Imported %s"), *Object->GetPathName());
				ToSave.Add(Package);
			}
			return Object;
		};

		UFbxImportUI* Options = NewObject<UFbxImportUI>();
		Options->bAutomatedImportShouldDetectType = false;
		Options->MeshTypeToImport = FBXIT_StaticMesh;
		Options->OriginalImportType = FBXIT_StaticMesh;
		Options->bImportMesh = true;
		Options->bImportMaterials = false;
		Options->bImportTextures = false;
		Options->bImportAnimations = false;
		Options->StaticMeshImportData->bCombineMeshes = true;
		Options->StaticMeshImportData->bAutoGenerateCollision = false;
		Import(NewObject<UFbxFactory>(), UStaticMesh::StaticClass(), Source / ModelFile, FString::Printf(TEXT("SM_%s"), *Name), Options);

		for (const TCHAR* Suffix : { TEXT("A"), TEXT("N"), TEXT("RMF") })
		{
			const FString File = Source / FString::Printf(TEXT("T_%s_%s.png"), *Name, Suffix);
			if (!IFileManager::Get().FileExists(*File))
			{
				continue;
			}
			UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
			TextureFactory->SuppressImportOverwriteDialog();
			if (UTexture2D* Texture = Cast<UTexture2D>(Import(TextureFactory, UTexture2D::StaticClass(), File, FString::Printf(TEXT("T_%s_%s"), *Name, Suffix), nullptr)))
			{
				if (FCString::Strcmp(Suffix, TEXT("A")) != 0)
				{
					Texture->CompressionSettings = FCString::Strcmp(Suffix, TEXT("N")) == 0 ? TC_Normalmap : TC_Masks;
					Texture->SRGB = false;
					Texture->PostEditChange();
				}
			}
		}
	}

	return UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
}
