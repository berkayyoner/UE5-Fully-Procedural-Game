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
#include "ChaosCloth/ChaosClothConfig.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace
{
	const TCHAR* const ImportBodyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* const ImportSkeletonPath = TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
	const TCHAR* const ImportScannedPath = TEXT("/Game/Aedir/Materials/M_AedirScanned.M_AedirScanned");
	const TCHAR* const ImportBodyPhysicsPath = TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin");

	// A worn piece's first section is made cloth: held at the collar, and let hang free of its skinned place the more the
	// further down the body and out along the sleeves, the body's capsules keeping it off the body.
	int32 MakeCloth(const FString& MeshPath, float PinAboveCm, float TorsoFromCm, float BaseCm, float TorsoCm, float SleeveFromCm, float SleeveCm, float BendStiffness, float AnimDrive)
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
			// A section with the ClothProxy material is a sparser copy of the garment: the cloth is simulated on it, it is
			// taken out of what is drawn, and the full garment drawn follows it.
			int32 Proxy = INDEX_NONE;
			const FSkeletalMeshLODModel& Lod0 = Mesh->GetImportedModel()->LODModels[0];
			for (int32 Section = 0; Section < Lod0.Sections.Num(); ++Section)
			{
				const TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
				if (Slots.IsValidIndex(Lod0.Sections[Section].MaterialIndex) && Slots[Lod0.Sections[Section].MaterialIndex].MaterialSlotName == FName(TEXT("ClothProxy")))
				{
					Proxy = Section;
				}
			}
			Params.LodIndex = 0;
			Params.SourceSection = Proxy == INDEX_NONE ? 0 : Proxy;
			Params.bRemoveFromMesh = Proxy != INDEX_NONE;
			UE_LOG(LogAedirEditor, Display, TEXT("Cloth simulated on section %d of %d (%s), %d points to simulate for %d drawn"), Params.SourceSection, Lod0.Sections.Num(), Proxy == INDEX_NONE ? TEXT("the garment itself") : TEXT("a sparser copy"),
				Lod0.Sections[Params.SourceSection].SoftVertices.Num(), Lod0.Sections[Proxy == 0 && Lod0.Sections.Num() > 1 ? 1 : 0].SoftVertices.Num());
			Params.PhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(ImportBodyPhysicsPath));
			// The triangles the cloth cannot be made of, as they lie in the imported model.
			{
				const FSkeletalMeshLODModel& Model = Mesh->GetImportedModel()->LODModels[0];
				const FSkelMeshSection& First = Model.Sections[Params.SourceSection];
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
			FClothLODDataCommon& ClothLod = Cloth->LodData[0];
			FClothPhysicalMeshData& Physical = ClothLod.PhysicalMeshData;
			// The values go in the LOD's painted mask: applying the masks copies them over the simulated mesh's own.
			FPointWeightMap* Mask = ClothLod.PointWeightMaps.FindByPredicate([](const FPointWeightMap& Map) { return Map.CurrentTarget == static_cast<uint8>(EWeightMapTargetCommon::MaxDistance); });
			if (!Mask)
			{
				Mask = &ClothLod.PointWeightMaps.AddDefaulted_GetRef();
			}
			Mask->Initialize(Physical.Vertices.Num());
			Mask->CurrentTarget = static_cast<uint8>(EWeightMapTargetCommon::MaxDistance);
			Mask->bEnabled = true;
			for (int32 Point = 0; Point < Physical.Vertices.Num(); ++Point)
			{
				const FVector3f& At = Physical.Vertices[Point];
				// Held at the collar, the shoulders and the upper arms, where it lies close on the body; free from the lower
				// chest down to the hem and from the elbows out to the cuffs.
				const float Across = FMath::Abs(At.X);
				const bool bCollar = At.Z > PinAboveCm && Across < 14.f;
				const float Down = FMath::Clamp((TorsoFromCm - At.Z) / FMath::Max(TorsoFromCm - 88.f, 1.f), 0.f, 1.f);
				const float Out = FMath::Clamp((Across - SleeveFromCm) / FMath::Max(48.f - SleeveFromCm, 1.f), 0.f, 1.f);
				const float Torso = BaseCm + FMath::Pow(Down, 1.6f) * (TorsoCm - BaseCm);
				const float Sleeve = Across > SleeveFromCm ? BaseCm + FMath::Pow(Out, 1.2f) * (SleeveCm - BaseCm) : 0.f;
				Mask->Values[Point] = bCollar ? 0.f : FMath::Max(Torso, Sleeve);
			}
			Cloth->ApplyParameterMasks();
			// Read back from what the simulation will use.
			const FPointWeightMap* Used = Physical.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
			int32 Loose = 0;
			float Most = 0.f;
			for (int32 Point = 0; Used && Point < Used->Values.Num(); ++Point)
			{
				Loose += Used->Values[Point] > 0.f ? 1 : 0;
				Most = FMath::Max(Most, Used->Values[Point]);
			}
			UE_LOG(LogAedirEditor, Display, TEXT("The simulated mesh lets %d points move, up to %.1f cm"), Loose, Most);
			// Linen: it does not stretch, bends easily, and swings a little behind the body.
			for (const TPair<FName, TObjectPtr<UClothConfigBase>>& Config : Cloth->ClothConfigs)
			{
				if (UChaosClothConfig* Chaos = Cast<UChaosClothConfig>(Config.Value))
				{
					Chaos->EdgeStiffnessWeighted = { 1.f, 1.f };
					Chaos->BendingStiffnessWeighted = { BendStiffness, BendStiffness };
					Chaos->DampingCoefficient = 0.06f;
					// A light pull back to the garment's own shape, so it hangs as cut instead of slumping into a fold where it
					// meets the close-fitted top, and still swings.
					Chaos->AnimDriveStiffness = { AnimDrive, AnimDrive };
					Chaos->AnimDriveDamping = { AnimDrive * 2.f, AnimDrive * 2.f };
					Chaos->LinearVelocityScale = FVector(0.6f);
					UE_LOG(LogAedirEditor, Display, TEXT("Cloth config %s: bending %.2f"), *Config.Key.ToString(), BendStiffness);
				}
			}
			FString Error;
			if (!FSkeletalMeshClothingSystemUtilities::AssignClothingToSection(Mesh, Cloth, 0, 0, 0, &Error))
			{
				UE_LOG(LogAedirEditor, Error, TEXT("Could not put the cloth on %s: %s"), *MeshPath, *Error);
				return 1;
			}
			UE_LOG(LogAedirEditor, Display, TEXT("Made %s cloth: %d points, %d of them loose"), *MeshPath, Physical.Vertices.Num(), Loose);
		}
		TArray<UPackage*> ToSave = { Mesh->GetPackage() };
		// The material scanned models wear is worn by cloth now as well, and clothes wear it two-sided, so the inside of a
		// sleeve or a hem lifting shows the cloth instead of nothing.
		if (UMaterial* Scanned = LoadObject<UMaterial>(nullptr, ImportScannedPath))
		{
			bool bNeedsRecompile = false;
			if (!Scanned->GetUsageByFlag(MATUSAGE_Clothing))
			{
				Scanned->SetMaterialUsage(bNeedsRecompile, MATUSAGE_Clothing);
				ToSave.Add(Scanned->GetPackage());
			}
			const FString TwoSidedName = TEXT("MI_AedirScannedTwoSided");
			const FString TwoSidedPackage = FString(TEXT("/Game/Aedir/Materials/")) + TwoSidedName;
			UMaterialInstanceConstant* TwoSided = LoadObject<UMaterialInstanceConstant>(nullptr, *(TwoSidedPackage + TEXT(".") + TwoSidedName), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!TwoSided)
			{
				UPackage* Package = CreatePackage(*TwoSidedPackage);
				TwoSided = NewObject<UMaterialInstanceConstant>(Package, FName(*TwoSidedName), RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(TwoSided);
			}
			TwoSided->SetParentEditorOnly(Scanned);
			TwoSided->BasePropertyOverrides.bOverride_TwoSided = true;
			TwoSided->BasePropertyOverrides.TwoSided = true;
			TwoSided->UpdateOverridableBaseProperties();
			TwoSided->PostEditChange();
			ToSave.Add(TwoSided->GetPackage());
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

	// -Cloth=<skeletal mesh path> [-Pin=146 -TorsoFrom=145 -Base=1.5 -Torso=9 -SleeveFrom=16 -Sleeve=9 -Bend=0.3 -Drive=0.12]: its first
	// section made cloth, held at the collar above Pin, Base cm free over the chest, to Torso cm at the hem from TorsoFrom
	// down, and to Sleeve cm at the cuffs from SleeveFrom across; Bend is how stiffly it bends.
	FString ClothMesh;
	if (FParse::Value(*Params, TEXT("Cloth="), ClothMesh))
	{
		float Pin = 146.f, TorsoFrom = 120.f, Base = 0.f, Torso = 9.f, SleeveFrom = 26.f, Sleeve = 9.f, Bend = 0.3f, Drive = 0.25f;
		FParse::Value(*Params, TEXT("Pin="), Pin);
		FParse::Value(*Params, TEXT("TorsoFrom="), TorsoFrom);
		FParse::Value(*Params, TEXT("Base="), Base);
		FParse::Value(*Params, TEXT("Torso="), Torso);
		FParse::Value(*Params, TEXT("SleeveFrom="), SleeveFrom);
		FParse::Value(*Params, TEXT("Sleeve="), Sleeve);
		FParse::Value(*Params, TEXT("Bend="), Bend);
		FParse::Value(*Params, TEXT("Drive="), Drive);
		return MakeCloth(ClothMesh, Pin, TorsoFrom, Base, Torso, SleeveFrom, Sleeve, Bend, Drive);
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
