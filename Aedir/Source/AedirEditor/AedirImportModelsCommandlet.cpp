#include "AedirImportModelsCommandlet.h"

#include "AedirEditor.h"
#include "AssetImportTask.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* const ImportBodyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* const ImportSkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
	const TCHAR* const ImportScannedPath = TEXT("/Game/Aedir/Materials/M_AedirScanned.M_AedirScanned");
}

UAedirImportModelsCommandlet::UAedirImportModelsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAedirImportModelsCommandlet::Main(const FString& Params)
{
	// The body comes out for fitting clothes to it outside the editor.
	FString BodyFile;
	if (FParse::Value(*Params, TEXT("ExportBody="), BodyFile))
	{
		USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, ImportBodyPath);
		const FSkeletalMeshLODModel& Lod = Body->GetImportedModel()->LODModels[0];
		const FReferenceSkeleton& Bones = Body->GetRefSkeleton();
		TArray<FTransform> InModel;
		InModel.SetNum(Bones.GetNum());
		FString Out = TEXT("{\"bones\":[");
		for (int32 Bone = 0; Bone < Bones.GetNum(); ++Bone)
		{
			const int32 Parent = Bones.GetParentIndex(Bone);
			InModel[Bone] = Parent == INDEX_NONE ? Bones.GetRefBonePose()[Bone] : Bones.GetRefBonePose()[Bone] * InModel[Parent];
			const FMatrix Matrix = InModel[Bone].ToMatrixNoScale();
			Out += FString::Printf(TEXT("%s{\"name\":\"%s\",\"parent\":%d,\"m\":["), Bone ? TEXT(",") : TEXT(""), *Bones.GetBoneName(Bone).ToString(), Parent);
			for (int32 Row = 0; Row < 4; ++Row)
			{
				for (int32 Column = 0; Column < 4; ++Column)
				{
					Out += FString::Printf(TEXT("%s%f"), Row + Column ? TEXT(",") : TEXT(""), Matrix.M[Row][Column]);
				}
			}
			Out += TEXT("]}");
		}
		Out += TEXT("],\"verts\":[");
		bool bFirst = true;
		for (const FSkelMeshSection& Section : Lod.Sections)
		{
			for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
			{
				Out += FString::Printf(TEXT("%s[%.3f,%.3f,%.3f"), bFirst ? TEXT("") : TEXT(","), Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z);
				bFirst = false;
				for (int32 Influence = 0; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
				{
					if (Vertex.InfluenceWeights[Influence] > 0)
					{
						Out += FString::Printf(TEXT(",%d,%d"), static_cast<int32>(Section.BoneMap[Vertex.InfluenceBones[Influence]]), static_cast<int32>(Vertex.InfluenceWeights[Influence]));
					}
				}
				Out += TEXT("]");
			}
		}
		Out += TEXT("],\"tris\":[");
		for (int32 Index = 0; Index < Lod.IndexBuffer.Num(); ++Index)
		{
			Out += FString::Printf(TEXT("%s%u"), Index ? TEXT(",") : TEXT(""), Lod.IndexBuffer[Index]);
		}
		Out += TEXT("]}");
		const bool bDone = FFileHelper::SaveStringToFile(Out, *BodyFile);
		UE_LOG(LogAedirEditor, Display, TEXT("Exported the body to %s: %d, %d bones"), *BodyFile, bDone ? 1 : 0, Bones.GetNum());
		return bDone ? 0 : 1;
	}

	FString Source;
	if (!FParse::Value(*Params, TEXT("Source="), Source))
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Usage: -run=AedirImportModels -Source=<folder>"));
		return 1;
	}

	TArray<FString> Models;
	IFileManager::Get().FindFiles(Models, *(Source / TEXT("SM_*.fbx")), true, false);
	TArray<FString> Worn;
	IFileManager::Get().FindFiles(Worn, *(Source / TEXT("SK_*.fbx")), true, false);
	Models.Append(Worn);
	TArray<UPackage*> ToSave;

	// Clothes wear the material scanned models wear.
	if (UMaterial* Scanned = LoadObject<UMaterial>(nullptr, ImportScannedPath))
	{
		bool bNeedsRecompile = false;
		Scanned->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
		ToSave.Add(Scanned->GetPackage());
	}

	for (const FString& ModelFile : Models)
	{
		const bool bWorn = ModelFile.StartsWith(TEXT("SK_"));
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
		Options->MeshTypeToImport = bWorn ? FBXIT_SkeletalMesh : FBXIT_StaticMesh;
		Options->OriginalImportType = Options->MeshTypeToImport;
		Options->bImportAsSkeletal = bWorn;
		Options->bImportMesh = true;
		if (bWorn)
		{
			Options->Skeleton = LoadObject<USkeleton>(nullptr, ImportSkeletonPath);
			Options->bCreatePhysicsAsset = false;
			Options->SkeletalMeshImportData->bImportMeshesInBoneHierarchy = false;
		}
		Options->bImportMaterials = false;
		Options->bImportTextures = false;
		Options->bImportAnimations = false;
		Options->StaticMeshImportData->bCombineMeshes = true;
		Options->StaticMeshImportData->bAutoGenerateCollision = false;
		Import(NewObject<UFbxFactory>(), bWorn ? USkeletalMesh::StaticClass() : UStaticMesh::StaticClass(), Source / ModelFile, FString::Printf(TEXT("%s_%s"), bWorn ? TEXT("SK") : TEXT("SM"), *Name), Options);

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

	// Textures of a section without a model of its own, T_<Section>_A and so on, go in a folder of that name.
	TArray<FString> Loose;
	IFileManager::Get().FindFiles(Loose, *(Source / TEXT("T_*_A.png")), true, false);
	for (const FString& File : Loose)
	{
		const FString Section = FPaths::GetBaseFilename(File).Mid(2).LeftChop(2);
		const FString Destination = FString::Printf(TEXT("/Game/Megascans/3D_Assets/%s"), *Section);
		if (FPackageName::DoesPackageExist(Destination / FString::Printf(TEXT("T_%s_A"), *Section)))
		{
			continue;
		}
		for (const TCHAR* Suffix : { TEXT("A"), TEXT("N"), TEXT("RMF") })
		{
			const FString AssetName = FString::Printf(TEXT("T_%s_%s"), *Section, Suffix);
			UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
			TextureFactory->SuppressImportOverwriteDialog();
			TextureFactory->AddToRoot();
			UPackage* Package = CreatePackage(*(Destination / AssetName));
			bool bCanceled = false;
			const FString TextureFile = Source / (AssetName + TEXT(".png"));
			if (UTexture2D* Texture = Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone, bCanceled, *TextureFile, nullptr, TextureFactory)))
			{
				if (FCString::Strcmp(Suffix, TEXT("A")) != 0)
				{
					Texture->CompressionSettings = FCString::Strcmp(Suffix, TEXT("N")) == 0 ? TC_Normalmap : TC_Masks;
					Texture->SRGB = false;
					Texture->PostEditChange();
				}
				UE_LOG(LogAedirEditor, Display, TEXT("Imported %s"), *Texture->GetPathName());
				ToSave.Add(Package);
			}
			TextureFactory->RemoveFromRoot();
		}
	}

	return UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
}
