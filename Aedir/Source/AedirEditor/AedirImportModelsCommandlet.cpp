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
#include "ClothingAsset.h"
#include "ClothingAssetFactoryInterface.h"
#include "ClothingSystemEditorInterfaceModule.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "SkeletalMeshClothingSystemUtilities.h"

namespace
{
	const TCHAR* const ImportBodyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* const ImportSkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
	const TCHAR* const ImportScannedPath = TEXT("/Game/Aedir/Materials/M_AedirScanned.M_AedirScanned");
	const TCHAR* const ImportBodyPhysicsPath = TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin");

	// A worn piece's first section is made cloth: held to the body at the top, and let hang free of its skinned place the
	// more the further down (to HemCm at the hem, from HemFromCm) and out along the sleeves (to CuffCm at the cuffs, from
	// CuffFromCm across), the body's capsules keeping it off the body.
	int32 MakeCloth(const FString& MeshPath, float HemFromCm, float HemCm, float CuffFromCm, float CuffCm)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("No skeletal mesh %s"), *MeshPath);
			return 1;
		}
		{
			FScopedSkeletalMeshPostEditChange PostEdit(Mesh);
			for (int32 Lod = 0; Lod < Mesh->GetImportedModel()->LODModels.Num(); ++Lod)
			{
				for (int32 Section = 0; Section < Mesh->GetImportedModel()->LODModels[Lod].Sections.Num(); ++Section)
				{
					FSkeletalMeshClothingSystemUtilities::RemoveClothingFromSection(Mesh, Lod, Section, nullptr);
				}
			}
			for (int32 Index = Mesh->GetMeshClothingAssets().Num() - 1; Index >= 0; --Index)
			{
				Mesh->GetMeshClothingAssets().RemoveAt(Index);
			}
			FClothingSystemEditorInterfaceModule& ClothingEditor = FModuleManager::LoadModuleChecked<FClothingSystemEditorInterfaceModule>(TEXT("ClothingSystemEditorInterface"));
			UClothingAssetFactoryBase* Factory = ClothingEditor.GetClothingAssetFactory();
			FSkeletalMeshClothBuildParams Params;
			Params.AssetName = FPaths::GetBaseFilename(MeshPath) + TEXT("_Cloth");
			Params.LodIndex = 0;
			Params.SourceSection = 0;
			Params.bRemoveFromMesh = false;
			Params.PhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(ImportBodyPhysicsPath));
			// The triangles the cloth cannot be made of, as they lie in the imported model.
			{
				const FSkeletalMeshLODModel& Model = Mesh->GetImportedModel()->LODModels[0];
				const FSkelMeshSection& First = Model.Sections[0];
				int32 Bad = 0;
				for (uint32 Tri = 0; Tri < First.NumTriangles; ++Tri)
				{
					const uint32 Base = First.BaseIndex + Tri * 3;
					const FVector A = (FVector)First.SoftVertices[Model.IndexBuffer[Base] - First.BaseVertexIndex].Position;
					const FVector B = (FVector)First.SoftVertices[Model.IndexBuffer[Base + 1] - First.BaseVertexIndex].Position;
					const FVector C = (FVector)First.SoftVertices[Model.IndexBuffer[Base + 2] - First.BaseVertexIndex].Position;
					if (((B - A) ^ (C - A)).SizeSquared() <= SMALL_NUMBER && Bad++ < 5)
					{
						UE_LOG(LogAedirEditor, Display, TEXT("Bad triangle %u: %u %u %u at %s %s %s"), Tri, Model.IndexBuffer[Base], Model.IndexBuffer[Base + 1], Model.IndexBuffer[Base + 2], *A.ToString(), *B.ToString(), *C.ToString());
					}
				}
				UE_LOG(LogAedirEditor, Display, TEXT("%d bad triangles of %u, %d vertices, %d sections"), Bad, First.NumTriangles, First.SoftVertices.Num(), Model.Sections.Num());
			}
			UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(Factory ? Factory->CreateFromSkeletalMesh(Mesh, Params) : nullptr);
			if (!Cloth || Cloth->LodData.IsEmpty())
			{
				UE_LOG(LogAedirEditor, Error, TEXT("Could not make cloth of %s"), *MeshPath);
				return 1;
			}
			Mesh->AddClothingAsset(Cloth);
			FClothPhysicalMeshData& Physical = Cloth->LodData[0].PhysicalMeshData;
			FPointWeightMap& MaxDistance = Physical.FindOrAddWeightMap(EWeightMapTargetCommon::MaxDistance);
			MaxDistance.Initialize(Physical.Vertices.Num());
			int32 Loose = 0;
			for (int32 Point = 0; Point < Physical.Vertices.Num(); ++Point)
			{
				const FVector3f& At = Physical.Vertices[Point];
				// Held at the collar and shoulders, freer the further down the body and out along the sleeves.
				const float Hem = FMath::Pow(FMath::Clamp((HemFromCm - At.Z) / FMath::Max(HemFromCm - 88.f, 1.f), 0.f, 1.f), 1.3f) * HemCm;
				const float Cuff = FMath::Pow(FMath::Clamp((FMath::Abs(At.X) - CuffFromCm) / FMath::Max(48.f - CuffFromCm, 1.f), 0.f, 1.f), 1.2f) * CuffCm;
				MaxDistance.Values[Point] = FMath::Max(Hem, Cuff);
				Loose += MaxDistance.Values[Point] > 0.f ? 1 : 0;
			}
			Cloth->ApplyParameterMasks();
			FString Error;
			if (!FSkeletalMeshClothingSystemUtilities::AssignClothingToSection(Mesh, Cloth, 0, 0, 0, &Error))
			{
				UE_LOG(LogAedirEditor, Error, TEXT("Could not put the cloth on %s: %s"), *MeshPath, *Error);
				return 1;
			}
			UE_LOG(LogAedirEditor, Display, TEXT("Made %s cloth: %d points, %d of them loose"), *MeshPath, Physical.Vertices.Num(), Loose);
		}
		TArray<UPackage*> ToSave = { Mesh->GetPackage() };
		// The material scanned models wear is worn by cloth now as well.
		if (UMaterial* Scanned = LoadObject<UMaterial>(nullptr, ImportScannedPath))
		{
			bool bNeedsRecompile = false;
			if (!Scanned->GetUsageByFlag(MATUSAGE_Clothing))
			{
				Scanned->SetMaterialUsage(bNeedsRecompile, MATUSAGE_Clothing);
				ToSave.Add(Scanned->GetPackage());
			}
		}
		return UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
	}
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

	// -Cloth=<skeletal mesh path> [-HemFrom=140 -Hem=8 -CuffFrom=22 -Cuff=8]: its first section made cloth, free of where its
	// bones put it from nothing at HemFrom down to Hem cm at the hem, and from nothing at CuffFrom across to Cuff at the cuffs.
	FString ClothMesh;
	if (FParse::Value(*Params, TEXT("Cloth="), ClothMesh))
	{
		float HemFrom = 140.f, Hem = 8.f, CuffFrom = 22.f, Cuff = 8.f;
		FParse::Value(*Params, TEXT("HemFrom="), HemFrom);
		FParse::Value(*Params, TEXT("Hem="), Hem);
		FParse::Value(*Params, TEXT("CuffFrom="), CuffFrom);
		FParse::Value(*Params, TEXT("Cuff="), Cuff);
		return MakeCloth(ClothMesh, HemFrom, Hem, CuffFrom, Cuff);
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
		Scanned->SetMaterialUsage(bNeedsRecompile, MATUSAGE_Clothing);
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
