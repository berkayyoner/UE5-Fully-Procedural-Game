#include "AedirContentCommandlet.h"

#include "AedirEditor.h"
#include "AedirTestMapBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Items/AedirCatalogItem.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Npc/AedirNpc.h"
#include "Npc/AedirTrainingDummy.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshEditorSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Factories/SoundFactory.h"
#include "HAL/FileManager.h"
#include "Sound/SoundWave.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Game/AedirMenuGameMode.h"
#include "GameFramework/WorldSettings.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "RHIGlobals.h"
#include "RenderingThread.h"
#include "Misc/PackageName.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionLength.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const ContentFolder = TEXT("/Game/Aedir/Materials/");

	template <typename TExpression>
	TExpression* ContentNode(UMaterial* Material, int32 X, int32 Y)
	{
		return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
	}

	UMaterialExpressionTextureSampleParameter2D* ContentTexture(UMaterial* Material, const TCHAR* Name, const TCHAR* Default, EMaterialSamplerType Sampler, int32 Y)
	{
		UMaterialExpressionTextureSampleParameter2D* Node = ContentNode<UMaterialExpressionTextureSampleParameter2D>(Material, -500, Y);
		Node->ParameterName = Name;
		Node->Texture = LoadObject<UTexture>(nullptr, Default);
		Node->SamplerType = Sampler;
		return Node;
	}

	// A package already on disk is saved again as it was rather than as rebuilt, so the old file goes before the new one is made.
	UPackage* ContentFreshPackage(const FString& PackageName)
	{
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (IFileManager::Get().FileExists(*File))
		{
			IFileManager::Get().Delete(*File, false, true);
		}
		return CreatePackage(*PackageName);
	}

	UMaterial* ContentNewMaterial(const TCHAR* Name, TArray<UPackage*>& ToSave)
	{
		const FString PackageName = FString(ContentFolder) + Name;
		UPackage* Package = ContentFreshPackage(PackageName);
		UMaterial* Material = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone | RF_Transactional);
		ToSave.Add(Package);
		return Material;
	}

	// Everything scanned has its base colour, its normals and its roughness in the red of a third map, its metal in the green.
	void ContentBuildScanned(TArray<UPackage*>& ToSave)
	{
		UMaterial* Material = ContentNewMaterial(TEXT("M_AedirScanned"), ToSave);
		Material->bUsedWithNanite = true;
		Material->bUsedWithStaticLighting = true;
		Material->bUsedWithSkeletalMesh = true;
		UMaterialExpressionTextureSampleParameter2D* Base = ContentTexture(Material, TEXT("BaseColor"), TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"), SAMPLERTYPE_Color, -300);
		UMaterialExpressionTextureSampleParameter2D* Normal = ContentTexture(Material, TEXT("Normal"), TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"), SAMPLERTYPE_Normal, 0);
		UMaterialExpressionTextureSampleParameter2D* Masks = ContentTexture(Material, TEXT("Masks"), TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"), SAMPLERTYPE_Color, 300);
		UMaterialEditingLibrary::ConnectMaterialProperty(Base, TEXT("RGB"), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Normal, TEXT("RGB"), MP_Normal);
		UMaterialEditingLibrary::ConnectMaterialProperty(Masks, TEXT("R"), MP_Roughness);
		UMaterialExpressionScalarParameter* MetalScale = ContentNode<UMaterialExpressionScalarParameter>(Material, -300, 450);
		MetalScale->ParameterName = TEXT("MetalScale");
		MetalScale->DefaultValue = 1.f;
		UMaterialExpressionMultiply* Metal = ContentNode<UMaterialExpressionMultiply>(Material, -150, 400);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Masks, TEXT("G"), Metal, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(MetalScale, TEXT(""), Metal, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Metal, TEXT(""), MP_Metallic);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		UE_LOG(LogAedirEditor, Display, TEXT("Built M_AedirScanned."));
	}

	// A sole pressed in the ground, whose colour and strength the game sets: dust, mud, blood or water.
	// The sole of a left boot seen from above, toes at the top: a broad forefoot with chevron lugs inside a pressed rim, the
	// shank under the arch barely touching, and a separate heel block with cross bars. The game mirrors it for the right foot.
	UTexture2D* ContentBuildFootprintTexture(TArray<UPackage*>& ToSave)
	{
		constexpr int32 Width = 128;
		constexpr int32 Height = 256;
		// Width of the sole down its length, as a share of the whole print, and how much of it lies on the inner side.
		static const FVector2f Profile[] = { { 0.f, 0.f }, { 0.03f, 0.5f }, { 0.1f, 0.82f }, { 0.24f, 0.96f }, { 0.38f, 0.9f }, { 0.55f, 0.66f }, { 0.7f, 0.66f }, { 0.84f, 0.72f }, { 0.93f, 0.62f }, { 0.98f, 0.4f }, { 1.f, 0.f } };
		const auto WidthAt = [](float V)
		{
			for (int32 Index = 1; Index < UE_ARRAY_COUNT(Profile); ++Index)
			{
				if (V <= Profile[Index].X)
				{
					const float Share = (V - Profile[Index - 1].X) / FMath::Max(Profile[Index].X - Profile[Index - 1].X, KINDA_SMALL_NUMBER);
					return FMath::Lerp(Profile[Index - 1].Y, Profile[Index].Y, FMath::Sin(Share * HALF_PI));
				}
			}
			return 0.f;
		};

		TArray<uint8> Pixels;
		Pixels.SetNumZeroed(Width * Height);
		FRandomStream Random(7);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const float V = (Y + 0.5f) / Height;
			const float Half = WidthAt(V) * 0.5f;
			// The big toe side reaches further forward and the arch curves in, so the inner edge is drawn apart from the outer.
			const float Arch = FMath::Clamp(1.f - FMath::Abs(V - 0.6f) / 0.16f, 0.f, 1.f);
			const float Inner = Half * (1.f - 0.45f * Arch);
			const float Outer = Half;
			for (int32 X = 0; X < Width; ++X)
			{
				// The toe leans to the big toe's side.
				const float U = (X + 0.5f) / Width - 0.5f + 0.06f * FMath::SmoothStep(0.f, 1.f, FMath::Clamp(1.f - V / 0.32f, 0.f, 1.f));
				const float Edge = U < 0.f ? Inner : Outer;
				const float FromEdge = Edge - FMath::Abs(U);
				const float Inside = FMath::Clamp(FromEdge * 70.f, 0.f, 1.f);
				// The rim of the sole bites deepest; inside it the lugs press and the grooves between them stay shallow.
				const float Rim = FromEdge < 0.035f ? 1.f : 0.f;
				float Press;
				if (V < 0.58f)
				{
					// Forefoot: lugs in chevrons pointing to the toe, split down the middle.
					const float Chevron = FMath::Fmod(V * 16.f + FMath::Abs(U) * 5.f, 1.f);
					const bool bSplit = FMath::Abs(U) < 0.02f;
					Press = Rim > 0.f ? 1.f : (Chevron < 0.55f && !bSplit ? 0.92f : 0.38f);
				}
				else if (V < 0.72f)
				{
					// Shank under the arch: only the outer edge and a faint middle touch the ground.
					Press = U > 0.f ? FMath::Clamp(0.5f - (Outer - U) * 7.f, 0.f, 0.5f) : 0.f;
				}
				else
				{
					// Heel block, set off from the shank by its front edge, with bars across it.
					const bool bFront = V < 0.745f;
					const float Bar = FMath::Fmod((V - 0.72f) * 26.f, 1.f);
					Press = Rim > 0.f || bFront ? 1.f : (Bar < 0.6f ? 0.9f : 0.4f);
				}
				const float Grain = Random.FRandRange(0.82f, 1.f);
				Pixels[Y * Width + X] = static_cast<uint8>(FMath::Clamp(Inside * Press * Grain, 0.f, 1.f) * 255.f);
			}
		}

		const FString PackageName = FString(ContentFolder) + TEXT("T_Footprint");
		UPackage* Package = ContentFreshPackage(PackageName);
		UTexture2D* Texture = NewObject<UTexture2D>(Package, TEXT("T_Footprint"), RF_Public | RF_Standalone | RF_Transactional);
		Texture->Source.Init(Width, Height, 1, 1, TSF_G8, Pixels.GetData());
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_Grayscale;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		ToSave.Add(Package);
		return Texture;
	}

	void ContentBuildFootprint(TArray<UPackage*>& ToSave)
	{
		UTexture2D* Sole = ContentBuildFootprintTexture(ToSave);
		UMaterial* Material = ContentNewMaterial(TEXT("M_Footprint"), ToSave);
		Material->MaterialDomain = MD_DeferredDecal;
		Material->BlendMode = BLEND_Translucent;

		// Mirror 1 turns the left sole into a right one.
		UMaterialExpressionTextureCoordinate* Coord = ContentNode<UMaterialExpressionTextureCoordinate>(Material, -1200, 0);
		UMaterialExpressionComponentMask* U = ContentNode<UMaterialExpressionComponentMask>(Material, -1000, -60);
		U->R = true;
		U->G = false;
		U->B = false;
		U->A = false;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Coord, TEXT(""), U, TEXT(""));
		UMaterialExpressionComponentMask* V = ContentNode<UMaterialExpressionComponentMask>(Material, -1000, 60);
		V->R = false;
		V->G = true;
		V->B = false;
		V->A = false;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Coord, TEXT(""), V, TEXT(""));
		UMaterialExpressionScalarParameter* Mirror = ContentNode<UMaterialExpressionScalarParameter>(Material, -1000, -200);
		Mirror->ParameterName = TEXT("Mirror");
		Mirror->DefaultValue = 0.f;
		UMaterialExpressionOneMinus* Flipped = ContentNode<UMaterialExpressionOneMinus>(Material, -850, -120);
		UMaterialEditingLibrary::ConnectMaterialExpressions(U, TEXT(""), Flipped, TEXT(""));
		UMaterialExpressionLinearInterpolate* Across = ContentNode<UMaterialExpressionLinearInterpolate>(Material, -700, -100);
		UMaterialEditingLibrary::ConnectMaterialExpressions(U, TEXT(""), Across, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Flipped, TEXT(""), Across, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Mirror, TEXT(""), Across, TEXT("Alpha"));
		UMaterialExpressionAppendVector* Uv = ContentNode<UMaterialExpressionAppendVector>(Material, -550, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Across, TEXT(""), Uv, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(V, TEXT(""), Uv, TEXT("B"));
		UMaterialExpressionTextureSample* Sample = ContentNode<UMaterialExpressionTextureSample>(Material, -400, 0);
		Sample->Texture = Sole;
		Sample->SamplerType = SAMPLERTYPE_LinearGrayscale;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Uv, TEXT(""), Sample, TEXT("UVs"));

		UMaterialExpressionScalarParameter* Strength = ContentNode<UMaterialExpressionScalarParameter>(Material, -200, 200);
		Strength->ParameterName = TEXT("Strength");
		Strength->DefaultValue = 0.6f;
		UMaterialExpressionMultiply* Opacity = ContentNode<UMaterialExpressionMultiply>(Material, 0, 100);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sample, TEXT("R"), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Strength, TEXT(""), Opacity, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);

		UMaterialExpressionVectorParameter* Color = ContentNode<UMaterialExpressionVectorParameter>(Material, 250, 0);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(0.08f, 0.06f, 0.04f);
		UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_BaseColor);

		UMaterialExpressionScalarParameter* Roughness = ContentNode<UMaterialExpressionScalarParameter>(Material, 250, 150);
		Roughness->ParameterName = TEXT("Roughness");
		Roughness->DefaultValue = 0.9f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, TEXT(""), MP_Roughness);

		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		UE_LOG(LogAedirEditor, Display, TEXT("Built M_Footprint."));
	}

	// A drop or a flake: a soft spot on a plane, stretched by the game as far as it likes. Colour and strength are set by the game.
	void ContentBuildPrecip(TArray<UPackage*>& ToSave)
	{
		UMaterial* Material = ContentNewMaterial(TEXT("M_AedirPrecip"), ToSave);
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->bUsedWithInstancedStaticMeshes = true;

		UMaterialExpressionTextureCoordinate* Coord = ContentNode<UMaterialExpressionTextureCoordinate>(Material, -1000, 0);
		UMaterialExpressionConstant* Half = ContentNode<UMaterialExpressionConstant>(Material, -1000, 200);
		Half->R = 0.5f;
		UMaterialExpressionSubtract* Centered = ContentNode<UMaterialExpressionSubtract>(Material, -800, 100);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Coord, TEXT(""), Centered, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Half, TEXT(""), Centered, TEXT("B"));
		UMaterialExpressionConstant* Grow = ContentNode<UMaterialExpressionConstant>(Material, -800, 300);
		Grow->R = 2.f;
		UMaterialExpressionMultiply* Scaled = ContentNode<UMaterialExpressionMultiply>(Material, -600, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Centered, TEXT(""), Scaled, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Grow, TEXT(""), Scaled, TEXT("B"));
		UMaterialExpressionLength* Length = ContentNode<UMaterialExpressionLength>(Material, -400, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Scaled, TEXT(""), Length, TEXT(""));
		UMaterialExpressionOneMinus* Inside = ContentNode<UMaterialExpressionOneMinus>(Material, -250, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Length, TEXT(""), Inside, TEXT(""));
		UMaterialExpressionSaturate* Shape = ContentNode<UMaterialExpressionSaturate>(Material, -100, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Inside, TEXT(""), Shape, TEXT(""));
		UMaterialExpressionScalarParameter* Strength = ContentNode<UMaterialExpressionScalarParameter>(Material, -100, 350);
		Strength->ParameterName = TEXT("Strength");
		Strength->DefaultValue = 0.5f;
		UMaterialExpressionMultiply* Opacity = ContentNode<UMaterialExpressionMultiply>(Material, 100, 250);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Shape, TEXT(""), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Strength, TEXT(""), Opacity, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		UMaterialExpressionVectorParameter* Color = ContentNode<UMaterialExpressionVectorParameter>(Material, 100, 0);
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(0.6f, 0.65f, 0.7f);
		UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		UE_LOG(LogAedirEditor, Display, TEXT("Built M_AedirPrecip."));
	}

	// The inside of a cut.
	void ContentBuildFlesh(TArray<UPackage*>& ToSave)
	{
		UMaterial* Material = ContentNewMaterial(TEXT("M_AedirFlesh"), ToSave);
		Material->TwoSided = true;
		UMaterialExpressionTextureCoordinate* Coord = ContentNode<UMaterialExpressionTextureCoordinate>(Material, -900, 0);
		UMaterialExpressionConstant* Flat = ContentNode<UMaterialExpressionConstant>(Material, -900, 150);
		UMaterialExpressionAppendVector* Position = ContentNode<UMaterialExpressionAppendVector>(Material, -750, 50);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Coord, TEXT(""), Position, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Flat, TEXT(""), Position, TEXT("B"));
		UMaterialExpressionNoise* Marbling = ContentNode<UMaterialExpressionNoise>(Material, -600, 0);
		Marbling->Scale = 6.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Position, TEXT(""), Marbling, TEXT("Position"));
		UMaterialExpressionVectorParameter* Dark = ContentNode<UMaterialExpressionVectorParameter>(Material, -600, 200);
		Dark->ParameterName = TEXT("Dark");
		Dark->DefaultValue = FLinearColor(0.16f, 0.008f, 0.008f);
		UMaterialExpressionVectorParameter* Pale = ContentNode<UMaterialExpressionVectorParameter>(Material, -600, 400);
		Pale->ParameterName = TEXT("Pale");
		Pale->DefaultValue = FLinearColor(0.42f, 0.06f, 0.05f);
		UMaterialExpressionLinearInterpolate* Mix = ContentNode<UMaterialExpressionLinearInterpolate>(Material, -300, 200);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Dark, TEXT(""), Mix, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Pale, TEXT(""), Mix, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Marbling, TEXT(""), Mix, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Mix, TEXT(""), MP_BaseColor);
		UMaterialExpressionConstant* Wet = ContentNode<UMaterialExpressionConstant>(Material, -300, 400);
		Wet->R = 0.28f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Wet, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Material->MarkPackageDirty();
		UE_LOG(LogAedirEditor, Display, TEXT("Built M_AedirFlesh."));
	}

	void ContentBuildMenuMap()
	{
		const FString MapPath = TEXT("/Game/Aedir/Maps/Lvl_MainMenu");
		UPackage* Package = CreatePackage(*MapPath);
		UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Lvl_MainMenu"), Package);
		if (!World)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Could not create the menu world."));
			return;
		}
		World->SetFlags(RF_Public | RF_Standalone);
		if (AWorldSettings* Settings = World->GetWorldSettings())
		{
			Settings->DefaultGameMode = AAedirMenuGameMode::StaticClass();
		}
		const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, MapPath);
		UE_LOG(LogAedirEditor, Display, TEXT("Menu map saved: %d"), bSaved ? 1 : 0);
	}

	// Slicing reads the skinned vertices of the body on the CPU.
	void ContentOpenMesh(const TCHAR* Path, TArray<UPackage*>& ToSave)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Path);
		if (!Mesh)
		{
			UE_LOG(LogAedirEditor, Warning, TEXT("Mesh %s not found."), Path);
			return;
		}
		bool bChanged = false;
		for (int32 Lod = 0; Lod < Mesh->GetLODNum(); ++Lod)
		{
			if (FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(Lod); Info && !Info->bAllowCPUAccess)
			{
				Info->bAllowCPUAccess = true;
				bChanged = true;
			}
		}
		if (bChanged)
		{
			Mesh->PostEditChange();
			Mesh->MarkPackageDirty();
			ToSave.Add(Mesh->GetOutermost());
			UE_LOG(LogAedirEditor, Display, TEXT("%s can now be read on the CPU."), Path);
		}
	}
}

namespace
{
	// Scanned props came in without collision, so bodies and items went through tables and barrels. Each gets a few convex
	// hulls around its shape: the legs and top of a table, not one box around it all.
	constexpr int32 ContentPropHulls = 8;
	constexpr int32 ContentPropHullVerts = 16;
	constexpr int32 ContentPropHullPrecision = 100000;

	void ContentBuildPropCollision(TArray<UPackage*>& ToSave)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.SearchAllAssets(true);
		TArray<FAssetData> Assets;
		Registry.ScanPathsSynchronous({ TEXT("/Game/Megascans") }, true);
		Registry.GetAssetsByPath(FName(TEXT("/Game/Megascans/3D_Assets")), Assets, true);
		FModuleManager::Get().LoadModuleChecked(TEXT("StaticMeshEditor"));
		UStaticMeshEditorSubsystem* MeshEditor = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
		if (!MeshEditor)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("No static mesh editor to build collision with."));
			return;
		}
		int32 Built = 0;
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetClassPath.GetAssetName() != FName(TEXT("StaticMesh")))
			{
				continue;
			}
			UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (!Mesh || !MeshEditor)
			{
				continue;
			}
			// A mesh told to use its full shape as its simple one has nothing to block with once Nanite holds that shape.
			UBodySetup* Body = Mesh->GetBodySetup();
			const bool bComplexOnly = Body && Body->CollisionTraceFlag == CTF_UseComplexAsSimple;
			UE_LOG(LogAedirEditor, Display, TEXT("Prop %s: %d simple shapes, trace flag %d"), *Mesh->GetName(), MeshEditor->GetSimpleCollisionCount(Mesh), Body ? static_cast<int32>(Body->CollisionTraceFlag.GetValue()) : -1);
			if (MeshEditor->GetSimpleCollisionCount(Mesh) > 0 && !bComplexOnly)
			{
				continue;
			}
			if (Body)
			{
				Body->CollisionTraceFlag = CTF_UseDefault;
			}
			if (MeshEditor->SetConvexDecompositionCollisions(Mesh, ContentPropHulls, ContentPropHullVerts, ContentPropHullPrecision))
			{
				Mesh->MarkPackageDirty();
				ToSave.AddUnique(Mesh->GetOutermost());
				++Built;
			}
		}
		UE_LOG(LogAedirEditor, Display, TEXT("Gave %d of %d scanned assets collision."), Built, Assets.Num());
	}
}

namespace
{
	// Sounds made by Scripts/MakeWeatherSounds.py, brought in as sound waves. One whose name says Loop plays on and on.
	void ContentImportSounds(const FString& Source, TArray<UPackage*>& ToSave)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Source / TEXT("*.wav")), true, false);
		for (const FString& File : Files)
		{
			const FString AssetName = FString(TEXT("S_")) + FPaths::GetBaseFilename(File);
			UPackage* Package = CreatePackage(*(FString(TEXT("/Game/Aedir/Sounds/")) + AssetName));
			USoundFactory* Factory = NewObject<USoundFactory>();
			Factory->AddToRoot();
			bool bCanceled = false;
			USoundWave* Sound = Cast<USoundWave>(UFactory::StaticImportObject(USoundWave::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone, bCanceled, *(Source / File), nullptr, Factory));
			Factory->RemoveFromRoot();
			if (!Sound)
			{
				UE_LOG(LogAedirEditor, Error, TEXT("Could not import %s"), *File);
				continue;
			}
			Sound->bLooping = File.Contains(TEXT("Loop"));
			Sound->MarkPackageDirty();
			ToSave.Add(Package);
			UE_LOG(LogAedirEditor, Display, TEXT("Imported %s"), *Sound->GetPathName());
		}
	}
}

namespace
{
	// A Blueprint for each class the game spawns people and things from, so they can be opened, changed and extended in the
	// editor. One that is already there is kept as it is, with whatever was done to it.
	void ContentMakeBlueprints()
	{
		const TPair<UClass*, const TCHAR*> Made[] = {
			{ AAedirNpc::StaticClass(), TEXT("BP_AedirNpc") },
			{ AAedirCatalogItem::StaticClass(), TEXT("BP_AedirItem") },
			{ AAedirTrainingDummy::StaticClass(), TEXT("BP_AedirTrainingDummy") },
		};
		TArray<UPackage*> ToSave;
		for (const TPair<UClass*, const TCHAR*>& One : Made)
		{
			const FString PackageName = FString::Printf(TEXT("/Game/Aedir/Blueprints/%s"), One.Value);
			if (FPackageName::DoesPackageExist(PackageName))
			{
				continue;
			}
			UPackage* Package = CreatePackage(*PackageName);
			if (UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(One.Key, Package, One.Value, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()))
			{
				FAssetRegistryModule::AssetCreated(Blueprint);
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				ToSave.Add(Package);
				UE_LOG(LogAedirEditor, Display, TEXT("Made %s from %s."), One.Value, *One.Key->GetName());
			}
		}
		if (!ToSave.IsEmpty())
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		}
	}
}

UAedirContentCommandlet::UAedirContentCommandlet()
{
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UAedirContentCommandlet::Main(const FString& Params)
{
	TArray<UPackage*> ToSave;
	// -Only=Precip builds that one alone and leaves everything else as it is.
	FString Only;
	if (FParse::Value(*Params, TEXT("Only="), Only) && Only == TEXT("Precip"))
	{
		ContentBuildPrecip(ToSave);
		return UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
	}
	if (Only == TEXT("Blueprints") || Only == TEXT("TestMap"))
	{
		ContentMakeBlueprints();
	}
	if (Only == TEXT("Blueprints"))
	{
		return 0;
	}
	if (Only == TEXT("TestMap"))
	{
		AedirTestMap::BuildLandscapeMaterial(ToSave);
		const bool bMaterialSaved = UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
		AedirTestMap::BuildLevel();
		return bMaterialSaved ? 0 : 1;
	}
	if (Only == TEXT("Sounds"))
	{
		FString Source;
		FParse::Value(*Params, TEXT("Source="), Source);
		ContentImportSounds(Source, ToSave);
		return !ToSave.IsEmpty() && UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
	}
	if (Only == TEXT("Footprint"))
	{
		ContentBuildFootprint(ToSave);
		return UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
	}
	if (Only == TEXT("Collision"))
	{
		ContentBuildPropCollision(ToSave);
		return ToSave.IsEmpty() || UEditorLoadingAndSavingUtils::SavePackages(ToSave, false) ? 0 : 1;
	}
	ContentBuildPropCollision(ToSave);
	ContentBuildPrecip(ToSave);
	ContentBuildScanned(ToSave);
	ContentBuildFootprint(ToSave);
	ContentBuildFlesh(ToSave);
	ContentOpenMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"), ToSave);
	ContentOpenMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"), ToSave);
	if (!UEditorLoadingAndSavingUtils::SavePackages(ToSave, false))
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Saving the content failed."));
		return 1;
	}
	if (!FPackageName::DoesPackageExist(TEXT("/Game/Aedir/Maps/Lvl_MainMenu")))
	{
		ContentBuildMenuMap();
	}
	return 0;
}
