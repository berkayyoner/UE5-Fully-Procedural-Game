#include "Graphics/AedirFoliageSubsystem.h"

#include "Aedir.h"
#include "Components/CapsuleComponent.h"
#include "Animation/TransformProviderData.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Creature/AedirWayFinder.h"
#include "DynamicWindParameters.h"
#include "DynamicWindSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Creature/AedirCreature.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialParameterCollection.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"

namespace
{
	const TCHAR* const FoliageCollectionPath = TEXT("/Game/PN_OpenWorldFoliage/Materials/MaterialCollection.MaterialCollection");
	const TCHAR* const FoliageMeshRoot = TEXT("/Game/PN_OpenWorldFoliage/Meshes/");
	constexpr float FoliageCullFromCm = 5000.f;
	constexpr float FoliageCullToCm = 7500.f;
	TAutoConsoleVariable<int32> CVarAedirGrassShadows(TEXT("aedir.Grass.Shadows"), 2, TEXT("Grass shadows: 0 off, 1 contact only, 2 cached, 3 swaying with the wind."));
	TAutoConsoleVariable<int32> CVarAedirGrassDistance(TEXT("aedir.Grass.Distance"), 2, TEXT("How far grass is drawn: 0 short, 1 medium, 2 far, 3 the whole meadow."));
	constexpr float FoliageSkipAboveCm = 160.f;
	// A blade is on bare ground when, looking down from this far over its root to this far under it, the ground comes first.
	constexpr float FoliageBareCheckAboveCm = 300.f;
	constexpr float FoliageBareCheckBelowCm = 30.f;
	constexpr float FoliageFlowerShare = 0.12f;
	constexpr float FoliageCrushSeconds = 0.1f;
	// Only what is under a foot that is down on the ground is pressed, a little more for every moment the foot stands on
	// it, and it bends down over a moment rather than at once.
	constexpr float FoliageCrushRadiusCm = 16.f;
	constexpr float FoliageCrushFootHeightCm = 14.f;
	constexpr float FoliagePressPerStep = 0.35f;
	constexpr float FoliageBendPerSecond = 2.5f;
	constexpr float FoliageFlatZ = 0.3f;
	constexpr float FoliageHoldSeconds = 7.f;
	constexpr float FoliageRisePerSecond = 0.1f;
	constexpr int32 FoliageMaxBend = 3;
	constexpr float FoliageBendDistance = 70.f;
	constexpr float FoliageWindFollow = 0.4f;
	const TCHAR* const FoliageNaniteGrassRoot = TEXT("/Game/OWD_Nanite_Base_Grass/Meshes/");
	const TCHAR* const FoliageScannedGrassRoot = TEXT("/Game/Aedir/Megascans/Grass/");
	// Each scanned plant and its variations; the tall meadow grass is grown low, as a grazed meadow's is.
	const TCHAR* const FoliageScannedGrass[] = { TEXT("WildGrass/SM_WildGrass_A"), TEXT("WildGrass/SM_WildGrass_B"), TEXT("WildGrass/SM_WildGrass_C"), TEXT("WildGrass/SM_WildGrass_D"),
		TEXT("WildGrass/SM_WildGrass_E"), TEXT("WildGrass/SM_WildGrass_F"), TEXT("WildGrass/SM_WildGrass_G"), TEXT("WildGrass/SM_WildGrass_H"),
		TEXT("MeadowGrass/SM_MeadowGrass_A"), TEXT("MeadowGrass/SM_MeadowGrass_D"), TEXT("MeadowGrass/SM_MeadowGrass_E"),
		TEXT("GrassClumps/SM_GrassClumps_A"), TEXT("GrassClumps/SM_GrassClumps_B"), TEXT("GrassClumps/SM_GrassClumps_C") };
	// A scanned clump is wider than a tuft of the older grass, but thinner: most of the places keep one.
	constexpr float FoliageScannedKeepShare = 0.8f;
	constexpr float FoliageScannedWidthScale = 1.f;
	constexpr float FoliageGrassMinCm = 14.f;
	constexpr float FoliageGrassMaxCm = 34.f;
	constexpr float FoliageFlowerMinCm = 22.f;
	constexpr float FoliageFlowerMaxCm = 40.f;
	const TCHAR* const FoliageTreeRoot = TEXT("/Game/Megaplant_Library/Tree_Norway_Spruce/Tree_Norway_Spruce_01/");
	const TCHAR* const FoliageTreeName = TEXT("Tree_Norway_Spruce_01");
	const TCHAR* const FoliageWindDataClass = TEXT("/Script/DynamicWind.DynamicWindData");
	constexpr int32 FoliageTreeTries = 6;
	constexpr float FoliageTreeLookCm = 800.f;
	constexpr float FoliageTreeSpacingCm = 450.f;
	constexpr float FoliageTrunkRadiusCm = 22.f;
	constexpr float FoliageTrunkHalfCm = 200.f;
	constexpr float FoliageWindExtentsCm = 100000.f;
	constexpr float FoliageWindYaw = 35.f;

	// Grass is too small and too many to light the world around it: kept out of Lumen's scene and the ray-traced one, or every
	// stirred blade has the whole meadow re-captured into the surface cache.
	void KeepOutOfLumen(UInstancedStaticMeshComponent* Patch)
	{
		Patch->SetAffectDynamicIndirectLighting(false);
		Patch->SetAffectDistanceFieldLighting(false);
		Patch->SetVisibleInRayTracing(false);
	}
}

TStatId UAedirFoliageSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAedirFoliageSubsystem, STATGROUP_Tickables);
}

int32 UAedirFoliageSubsystem::GetInstanceCount() const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Patch : Patches)
	{
		Count += Patch ? Patch->GetInstanceCount() : 0;
	}
	return Count;
}

const FName UAedirFoliageSubsystem::MeadowTag(TEXT("AedirMeadow"));

void UAedirFoliageSubsystem::EnsureMeadowActor()
{
	if (MeadowActor)
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(MeadowTag))
		{
			MeadowActor = *It;
			return;
		}
	}
	FActorSpawnParameters Params;
	Params.Name = TEXT("AedirMeadow");
	MeadowActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	MeadowActor->Tags.Add(MeadowTag);
	USceneComponent* Root = NewObject<USceneComponent>(MeadowActor, TEXT("Root"));
	MeadowActor->SetRootComponent(Root);
	Root->RegisterComponent();
	if (!GetWorld()->IsGameWorld())
	{
		MeadowActor->AddInstanceComponent(Root);
#if WITH_EDITOR
		MeadowActor->SetActorLabel(TEXT("AedirMeadow"));
#endif
	}
}

void UAedirFoliageSubsystem::AddToMeadow(USceneComponent* Component)
{
	Component->SetupAttachment(MeadowActor->GetRootComponent());
	Component->RegisterComponent();
	if (!GetWorld()->IsGameWorld())
	{
		MeadowActor->AddInstanceComponent(Component);
	}
}

void UAedirFoliageSubsystem::EnsureTreeWindData()
{
	// The wind reaches the branches through this data, which the plugin keeps to itself.
	if (!TreeWindData)
	{
		if (UClass* WindClass = LoadClass<UObject>(nullptr, FoliageWindDataClass))
		{
			TreeWindData = NewObject<UObject>(this, WindClass);
		}
	}
}

bool UAedirFoliageSubsystem::IsBareGround(const UWorld& InWorld, const FVector& Point)
{
	// Looking down on the spot from a little above, the first solid thing met is the ground itself.
	FHitResult Hit;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(AedirBareGround), false);
	if (!InWorld.LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, FoliageBareCheckAboveCm), Point - FVector(0.f, 0.f, FoliageBareCheckBelowCm), Objects, Query))
	{
		return true;
	}
	return Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>();
}

void UAedirFoliageSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (!It->ActorHasTag(MeadowTag))
		{
			continue;
		}
		MeadowActor = *It;
		EnsureTreeWindData();
		TArray<UInstancedStaticMeshComponent*> Grass;
		It->GetComponents(Grass);
		int32 Cleared = 0;
		for (UInstancedStaticMeshComponent* Patch : Grass)
		{
			Patches.AddUnique(Patch);
			Patch->SetReceivesDecals(false);
			// Grass planted before the rule that it only grows on bare ground is pulled out of anything standing over it.
			TArray<int32> Covered;
			for (int32 Index = 0; Index < Patch->GetInstanceCount(); ++Index)
			{
				FTransform Where;
				if (Patch->GetInstanceTransform(Index, Where, true) && !IsBareGround(InWorld, Where.GetLocation()))
				{
					Covered.Add(Index);
				}
			}
			if (!Covered.IsEmpty())
			{
				Patch->RemoveInstances(Covered);
				Cleared += Covered.Num();
			}
		}
		UE_CLOG(Cleared > 0, LogAedir, Log, TEXT("Cleared %d grass blades growing on props."), Cleared);
		ReplantWithScannedGrass(*It);
		TArray<UInstancedSkinnedMeshComponent*> Trees;
		It->GetComponents(Trees);
		for (UInstancedSkinnedMeshComponent* Tree : Trees)
		{
			Tree->SetTransformProvider(Cast<UTransformProviderData>(TreeWindData));
			TreeMeshes.AddUnique(Tree);
			TreeCount += Tree->GetInstanceCount();
		}
		UE_LOG(LogAedir, Log, TEXT("Took up the saved meadow: %d grass patches, %d trees."), Patches.Num(), TreeCount);
	}
}

void UAedirFoliageSubsystem::PlantTrees(const FVector& Around, float InnerCm, float OuterCm, int32 Count, int32 RandomSeed)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<USkeletalMesh*> Models;
	for (const TCHAR* Variant : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") })
	{
		const FString Name = FString::Printf(TEXT("%s_%s"), FoliageTreeName, Variant);
		if (USkeletalMesh* Model = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s%s.%s"), FoliageTreeRoot, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Models.Add(Model);
		}
	}
	if (Models.IsEmpty())
	{
		UE_LOG(LogAedir, Warning, TEXT("No tree models found under %s"), FoliageTreeRoot);
		return;
	}
	EnsureMeadowActor();

	EnsureTreeWindData();

	FRandomStream Random(RandomSeed);
	TMap<USkeletalMesh*, UInstancedSkinnedMeshComponent*> ByModel;
	TArray<FVector> Taken;
	for (int32 Try = 0; Try < Count * FoliageTreeTries && TreeCount < Count; ++Try)
	{
		const float Angle = Random.FRandRange(0.f, UE_TWO_PI);
		const float Radius = Random.FRandRange(InnerCm, OuterCm);
		const FVector Spot = Around + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
		FVector Ground;
		if (!FAedirWayFinder::FindGround(World, Spot, FoliageTreeLookCm, FoliageTreeLookCm, nullptr, Ground) || Taken.ContainsByPredicate([&Ground](const FVector& Other) { return FVector::DistSquared2D(Ground, Other) < FMath::Square(FoliageTreeSpacingCm); }))
		{
			continue;
		}
		Taken.Add(Ground);

		USkeletalMesh* Model = Models[Random.RandHelper(Models.Num())];
		UInstancedSkinnedMeshComponent*& Trees = ByModel.FindOrAdd(Model);
		if (!Trees)
		{
			Trees = NewObject<UInstancedSkinnedMeshComponent>(MeadowActor);
			Trees->SetSkinnedAsset(Model);
			Trees->SetMobility(EComponentMobility::Movable);
			Trees->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// The wind data lives with the game; a tree saved in the level gets it when the game begins.
			if (World->IsGameWorld())
			{
				Trees->SetTransformProvider(Cast<UTransformProviderData>(TreeWindData));
			}
			AddToMeadow(Trees);
			TreeMeshes.Add(Trees);
		}
		const float Scale = Random.FRandRange(0.85f, 1.15f);
		Trees->AddInstance(FTransform(FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f), Ground, FVector(Scale)), 0, true);

		// The trunk stops whoever walks into it. The branches give way.
		UCapsuleComponent* Trunk = NewObject<UCapsuleComponent>(MeadowActor);
		Trunk->InitCapsuleSize(FoliageTrunkRadiusCm * Scale, FoliageTrunkHalfCm);
		Trunk->SetCollisionProfileName(TEXT("BlockAll"));
		Trunk->SetWorldLocation(Ground + FVector(0.f, 0.f, FoliageTrunkHalfCm));
		AddToMeadow(Trunk);
		++TreeCount;
	}
	UE_LOG(LogAedir, Log, TEXT("Planted %d trees around %s, wind data %s"), TreeCount, *Around.ToCompactString(), *GetNameSafe(TreeWindData));
}

void UAedirFoliageSubsystem::PlantMeadow(const FVector& Around, float HalfSizeCm, float DensityPerSquareMeter, int32 RandomSeed)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// The scanned Nanite grass when it is there: single tufts of three kinds and denser clusters, every size and variation.
	TArray<UStaticMesh*> Grasses = LoadScannedGrass();
	TArray<UStaticMesh*> Flowers;
	for (int32 Kind = 1; Kind <= 3 && Grasses.IsEmpty(); ++Kind)
	{
		for (int32 Size = 1; Size <= 5; ++Size)
		{
			for (const TCHAR* Variant : { TEXT("a"), TEXT("b"), TEXT("c") })
			{
				const FString Name = FString::Printf(TEXT("SM_Grass_%02d_%d%s"), Kind, Size, Variant);
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%sGrass_%02d/%s.%s"), FoliageNaniteGrassRoot, Kind, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					Grasses.Add(Mesh);
				}
			}
		}
		for (const TCHAR* Variant : { TEXT("a"), TEXT("b"), TEXT("c") })
		{
			const FString Name = FString::Printf(TEXT("SM_GrassCL_%02d%s"), Kind, Variant);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%sGrass_Cluster/%s.%s"), FoliageNaniteGrassRoot, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Grasses.Add(Mesh);
			}
		}
	}

	// Otherwise the stylised pack, whose meshes are numbered by kind, then by size and variation.
	const bool bStylised = Grasses.IsEmpty();
	for (int32 Kind = 1; Kind <= 2 && bStylised; ++Kind)
	{
		for (int32 Size = 1; Size <= 3; ++Size)
		{
			for (int32 Variant = 1; Variant <= 4; ++Variant)
			{
				const FString Name = FString::Printf(TEXT("openWorld_grass_%02d_%02d_%02d"), Kind, Size, Variant);
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s%s.%s"), FoliageMeshRoot, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					Grasses.Add(Mesh);
				}
			}
		}
	}
	for (int32 Kind = 1; Kind <= 6; ++Kind)
	{
		for (int32 Variant = 1; Variant <= 2; ++Variant)
		{
			const FString Name = FString::Printf(TEXT("openWorld_flower_%02d_%02d"), Kind, Variant);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s%s.%s"), FoliageMeshRoot, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Flowers.Add(Mesh);
			}
		}
	}
	if (Grasses.IsEmpty())
	{
		UE_LOG(LogAedir, Warning, TEXT("No foliage meshes found under %s"), FoliageMeshRoot);
		return;
	}

	EnsureMeadowActor();

	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> ByMesh;
	const auto PatchOf = [&](UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent*& Patch = ByMesh.FindOrAdd(Mesh);
		if (!Patch)
		{
			Patch = NewObject<UInstancedStaticMeshComponent>(MeadowActor);
			Patch->SetStaticMesh(Mesh);
			Patch->SetMobility(EComponentMobility::Movable);
			Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Patch->SetCastShadow(false);
			// Prints, blood and wounds lie on the ground under the blades, not wrapped over them.
			Patch->SetReceivesDecals(false);
			Patch->SetCullDistances(FoliageCullFromCm, FoliageCullToCm);
			KeepOutOfLumen(Patch);
			AddToMeadow(Patch);
			Patches.Add(Patch);
		}
		return Patch;
	};

	FRandomStream Random(RandomSeed);
	const int32 Count = FMath::RoundToInt(FMath::Square(HalfSizeCm * 2.f / 100.f) * DensityPerSquareMeter);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(AedirMeadow), false);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	int32 Planted = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Spot = Around + FVector(Random.FRandRange(-HalfSizeCm, HalfSizeCm), Random.FRandRange(-HalfSizeCm, HalfSizeCm), 0.f);
		FHitResult Hit;
		// Grass grows on the bare ground only, never on a rock, a table or a roof.
		if (!World->LineTraceSingleByObjectType(Hit, Spot + FVector(0.f, 0.f, 1500.f), Spot - FVector(0.f, 0.f, 1500.f), Objects, Query) || Hit.ImpactNormal.Z < 0.8f || FMath::Abs(Hit.ImpactPoint.Z - Around.Z) > FoliageSkipAboveCm
			|| !Hit.GetActor() || !Hit.GetActor()->IsA<ALandscapeProxy>())
		{
			continue;
		}

		const bool bFlower = !Flowers.IsEmpty() && Random.FRand() < FoliageFlowerShare;
		const TArray<UStaticMesh*>& Pool = bFlower ? Flowers : Grasses;
		UStaticMesh* Mesh = Pool[Random.RandHelper(Pool.Num())];
		const FRotator Turn(0.f, Random.FRandRange(0.f, 360.f), 0.f);
		// A meadow that has been grazed and walked: shin-high at most, whatever height the model was made at.
		const float MadeHeight = FMath::Max(static_cast<float>(Mesh->GetBoundingBox().GetSize().Z), 1.f);
		const float WantedHeight = bFlower ? Random.FRandRange(FoliageFlowerMinCm, FoliageFlowerMaxCm) : FoliageGrassMinCm + (FoliageGrassMaxCm - FoliageGrassMinCm) * FMath::Square(Random.FRand());
		const float Spread = Random.FRandRange(0.85f, 1.15f);
		const FVector Scale(Spread, Spread, FMath::Clamp(WantedHeight / MadeHeight, 0.25f, 1.1f));
		const FQuat ToSlope = FQuat::FindBetweenNormals(FVector::UpVector, Hit.ImpactNormal);
		PatchOf(Mesh)->AddInstance(FTransform(ToSlope * Turn.Quaternion(), Hit.ImpactPoint, Scale), true);
		++Planted;
	}
	UE_LOG(LogAedir, Log, TEXT("Planted %d grass and flowers around %s"), Planted, *Around.ToCompactString());
}

bool UAedirFoliageSubsystem::IsGrassAt(const FVector& Point, float RadiusCm) const
{
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Patch : Patches)
	{
		if (Patch && Patch->Bounds.GetSphere().IsInside(Point, RadiusCm) && !Patch->GetInstancesOverlappingSphere(Point, RadiusCm, false).IsEmpty())
		{
			return true;
		}
	}
	return false;
}

void UAedirFoliageSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || (Patches.IsEmpty() && TreeMeshes.IsEmpty()))
	{
		return;
	}
	if (!Collection)
	{
		Collection = LoadObject<UMaterialParameterCollection>(nullptr, FoliageCollectionPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	if (Collection && !bBendSet)
	{
		// Grass leans out of the way close to the legs, not in a wide ring around the body.
		bBendSet = true;
		UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Bending_Distance"), FoliageBendDistance);
	}
	ApplyGrassSettings();
	UpdateBending();
	UpdateWind(DeltaTime);
	CrushTimer -= DeltaTime;
	if (CrushTimer <= 0.f)
	{
		CrushTimer = FoliageCrushSeconds;
		CrushUnderFeet();
	}
	Recover(DeltaTime);
}

void UAedirFoliageSubsystem::ApplyGrassSettings()
{
	// The player's choice of grass shadows and draw distance, put on every patch whenever it changes or a patch is added.
	const int32 Shadows = CVarAedirGrassShadows.GetValueOnGameThread();
	const int32 Distance = CVarAedirGrassDistance.GetValueOnGameThread();
	if (Shadows == AppliedGrassShadows && Distance == AppliedGrassDistance && Patches.Num() == AppliedGrassPatches)
	{
		return;
	}
	AppliedGrassShadows = Shadows;
	AppliedGrassDistance = Distance;
	AppliedGrassPatches = Patches.Num();
	static const float CullFrom[] = { 2000.f, 3000.f, 4500.f, FoliageCullFromCm };
	static const float CullTo[] = { 3000.f, 4500.f, 6000.f, FoliageCullToCm };
	const int32 DistanceIndex = FMath::Clamp(Distance, 0, 3);
	for (UInstancedStaticMeshComponent* Patch : Patches)
	{
		if (!Patch)
		{
			continue;
		}
		// Off: none. Low: only the short contact shadow at the foot of each blade, which costs no shadow map. Medium: real
		// shadows, cached as if the grass stood still. High: real shadows that sway with the wind.
		Patch->SetCastShadow(Shadows > 0);
		Patch->bCastDynamicShadow = Shadows >= 2;
		Patch->bCastContactShadow = Shadows >= 1;
		Patch->ShadowCacheInvalidationBehavior = Shadows >= 3 ? EShadowCacheInvalidationBehavior::Auto : EShadowCacheInvalidationBehavior::Rigid;
		Patch->SetWorldPositionOffsetDisableDistance(Shadows >= 3 ? 3000 : 2000);
		Patch->SetCullDistances(CullFrom[DistanceIndex], CullTo[DistanceIndex]);
		Patch->MarkRenderStateDirty();
	}
	UE_LOG(LogAedir, Log, TEXT("[Grass] %d patches: shadows %d, distance %d"), Patches.Num(), Shadows, Distance);
}

void UAedirFoliageSubsystem::UpdateBending()
{
	UWorld* World = GetWorld();
	const APlayerController* Viewer = World->GetFirstPlayerController();
	const FVector Center = Viewer && Viewer->GetPawn() ? Viewer->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	if (!Collection)
	{
		return;
	}

	// The material knows three places to lean away from: the bodies closest to whoever is looking.
	TArray<const AAedirCreature*, TInlineAllocator<16>> Bodies;
	for (TActorIterator<AAedirCreature> It(World); It; ++It)
	{
		Bodies.Add(*It);
	}
	Bodies.Sort([&Center](const AAedirCreature& A, const AAedirCreature& B) { return FVector::DistSquared(A.GetActorLocation(), Center) < FVector::DistSquared(B.GetActorLocation(), Center); });
	for (int32 Index = 0; Index < FoliageMaxBend; ++Index)
	{
		const FName Name(*FString::Printf(TEXT("PlayerPos%d"), Index));
		const FVector Where = Bodies.IsValidIndex(Index) ? Bodies[Index]->GetActorLocation() - FVector(0.f, 0.f, Bodies[Index]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) : FVector(0.f, 0.f, -100000.f);
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, Name, FLinearColor(Where));
	}
	if (Bodies.IsValidIndex(0))
	{
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, TEXT("PlayerDir0"), FLinearColor(Bodies[0]->GetActorForwardVector()));
	}
}

void UAedirFoliageSubsystem::UpdateWind(float DeltaTime)
{
	UWorld* World = GetWorld();
	const UAedirWeatherSubsystem* Weather = World->GetSubsystem<UAedirWeatherSubsystem>();
	const AAedirWeatherManager* Manager = Weather ? Weather->GetManager() : nullptr;
	const float Wanted = Manager ? Manager->GetWindShare() : 0.35f;
	WindNow = FMath::FInterpTo(WindNow, Wanted, DeltaTime, FoliageWindFollow);

	if (UDynamicWindSubsystem* TreeWind = World->GetSubsystem<UDynamicWindSubsystem>())
	{
		const APlayerController* Viewer = World->GetFirstPlayerController();
		FDynamicWindParameters Parameters;
		Parameters.SimulationCenter = Viewer && Viewer->GetPawn() ? Viewer->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		Parameters.SimulationExtents = FoliageWindExtentsCm;
		Parameters.WindDirection = FRotator(0.f, FoliageWindYaw, 0.f).Vector();
		// Even a storm only sways a spruce and bends its crown a little; a gale that lays trees over is beyond any weather here.
		Parameters.WindSpeed = FMath::Lerp(3.f, 16.f, WindNow);
		Parameters.WindAmplitude = FMath::Lerp(0.4f, 0.9f, WindNow);
		TreeWind->UpdateWindParameters(Parameters);
	}

	if (!Collection)
	{
		return;
	}
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Wind_Intensity"), FMath::Lerp(0.12f, 0.7f, WindNow));
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Wind_Speed"), FMath::Lerp(0.35f, 0.85f, WindNow));
}

void UAedirFoliageSubsystem::CrushUnderFeet()
{
	UWorld* World = GetWorld();
	for (TActorIterator<AAedirCreature> It(World); It; ++It)
	{
		const USkeletalMeshComponent* Body = It->GetMesh();
		if (!Body || It->GetVelocity().SizeSquared2D() < 1.f)
		{
			continue;
		}
		for (const TCHAR* Bone : { TEXT("foot_l"), TEXT("foot_r") })
		{
			const FVector Foot = Body->GetBoneLocation(FName(Bone));
			const FVector Feet = It->GetActorLocation() - FVector(0.f, 0.f, It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			if (Foot.Z > Feet.Z + FoliageCrushFootHeightCm)
			{
				continue;
			}
			for (UInstancedStaticMeshComponent* Patch : Patches)
			{
				for (const int32 Instance : Patch->GetInstancesOverlappingSphere(FVector(Foot.X, Foot.Y, Feet.Z), FoliageCrushRadiusCm, true))
				{
					const TPair<UInstancedStaticMeshComponent*, int32> Key(Patch, Instance);
					if (!CrushedKeys.Contains(Key))
					{
						FTransform Now;
						Patch->GetInstanceTransform(Instance, Now, true);
						FCrushed& Entry = Crushed.AddDefaulted_GetRef();
						Entry.Mesh = Patch;
						Entry.Instance = Instance;
						Entry.StandingZ = static_cast<float>(Now.GetScale3D().Z);
						Entry.Pressed = FoliagePressPerStep;
						Entry.HoldSeconds = FoliageHoldSeconds;
						CrushedKeys.Add(Key);
					}
					else if (FCrushed* Entry = Crushed.FindByPredicate([Patch, Instance](const FCrushed& Each) { return Each.Mesh.Get() == Patch && Each.Instance == Instance; }))
					{
						Entry->Pressed = FMath::Min(Entry->Pressed + FoliagePressPerStep * FoliageCrushSeconds, 1.f);
						Entry->HoldSeconds = FoliageHoldSeconds;
					}
				}
			}
		}
	}
}

void UAedirFoliageSubsystem::SetInstanceFlatness(FCrushed& Entry)
{
	if (UInstancedStaticMeshComponent* Patch = Entry.Mesh.Get())
	{
		FTransform Now;
		Patch->GetInstanceTransform(Entry.Instance, Now, true);
		FVector Scale = Now.GetScale3D();
		Scale.Z = Entry.StandingZ * FMath::Lerp(1.f, FoliageFlatZ, Entry.Share);
		Now.SetScale3D(Scale);
		Patch->UpdateInstanceTransform(Entry.Instance, Now, true, true, true);
	}
}

void UAedirFoliageSubsystem::Recover(float DeltaTime)
{
	for (int32 Index = Crushed.Num() - 1; Index >= 0; --Index)
	{
		FCrushed& Entry = Crushed[Index];
		Entry.HoldSeconds -= DeltaTime;
		if (Entry.HoldSeconds > 0.f)
		{
			if (Entry.Share < Entry.Pressed)
			{
				Entry.Share = FMath::Min(Entry.Share + FoliageBendPerSecond * DeltaTime, Entry.Pressed);
				SetInstanceFlatness(Entry);
			}
			continue;
		}
		Entry.Pressed = 0.f;
		Entry.Share = FMath::Max(Entry.Share - FoliageRisePerSecond * DeltaTime, 0.f);
		SetInstanceFlatness(Entry);
		if (Entry.Share <= 0.f)
		{
			CrushedKeys.Remove(TPair<UInstancedStaticMeshComponent*, int32>(Entry.Mesh.Get(), Entry.Instance));
			Crushed.RemoveAtSwap(Index);
		}
	}
}

TArray<UStaticMesh*> UAedirFoliageSubsystem::LoadScannedGrass()
{
	TArray<UStaticMesh*> Meshes;
	for (const TCHAR* Name : FoliageScannedGrass)
	{
		const FString Path = FString(FoliageScannedGrassRoot) + Name;
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *(Path + TEXT(".") + FPaths::GetBaseFilename(Path)), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Meshes.Add(Mesh);
		}
	}
	return Meshes;
}

void UAedirFoliageSubsystem::ReplantWithScannedGrass(AActor* Meadow)
{
	const TArray<UStaticMesh*> Scanned = LoadScannedGrass();
	if (Scanned.IsEmpty() || !Meadow)
	{
		return;
	}
	TArray<UInstancedStaticMeshComponent*> Old;
	Meadow->GetComponents(Old);
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> ByMesh;
	FRandomStream Random(Old.Num());
	int32 Replanted = 0;
	for (UInstancedStaticMeshComponent* Patch : Old)
	{
		const UStaticMesh* OldMesh = Patch ? Patch->GetStaticMesh().Get() : nullptr;
		const FString OldPath = OldMesh ? OldMesh->GetPathName() : FString();
		const bool bOldGrass = OldPath.StartsWith(FoliageNaniteGrassRoot) || OldPath.Contains(TEXT("openWorld_grass"));
		if (!bOldGrass)
		{
			continue;
		}
		const float OldHeight = FMath::Max(static_cast<float>(OldMesh->GetBoundingBox().GetSize().Z), 1.f);
		for (int32 Index = 0; Index < Patch->GetInstanceCount(); ++Index)
		{
			FTransform Where;
			if (Random.FRand() > FoliageScannedKeepShare || !Patch->GetInstanceTransform(Index, Where, true))
			{
				continue;
			}
			UStaticMesh* Mesh = Scanned[Random.RandHelper(Scanned.Num())];
			UInstancedStaticMeshComponent*& NewPatch = ByMesh.FindOrAdd(Mesh);
			if (!NewPatch)
			{
				NewPatch = NewObject<UInstancedStaticMeshComponent>(Meadow);
				NewPatch->SetStaticMesh(Mesh);
				NewPatch->SetMobility(EComponentMobility::Movable);
				NewPatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				NewPatch->SetCastShadow(false);
				NewPatch->SetReceivesDecals(false);
				NewPatch->SetCullDistances(FoliageCullFromCm, FoliageCullToCm);
				KeepOutOfLumen(NewPatch);
				NewPatch->SetupAttachment(Meadow->GetRootComponent());
				NewPatch->RegisterComponent();
				Meadow->AddInstanceComponent(NewPatch);
				Patches.Add(NewPatch);
			}
			// As tall as the blade it takes the place of, never taller than shin-high.
			const float Height = FMath::Min(static_cast<float>(Where.GetScale3D().Z) * OldHeight, FoliageGrassMaxCm);
			const float MadeHeight = FMath::Max(static_cast<float>(Mesh->GetBoundingBox().GetSize().Z), 1.f);
			const float Spread = static_cast<float>(Where.GetScale3D().X) * FoliageScannedWidthScale;
			Where.SetScale3D(FVector(Spread, Spread, FMath::Clamp(Height / MadeHeight, 0.15f, 1.2f)));
			Where.SetRotation(Where.GetRotation() * FQuat(FVector::UpVector, Random.FRandRange(0.f, UE_TWO_PI)));
			NewPatch->AddInstance(Where, true);
			++Replanted;
		}
		Patches.Remove(Patch);
		Patch->DestroyComponent();
	}
	UE_CLOG(Replanted > 0, LogAedir, Log, TEXT("Replanted the meadow with %d clumps of scanned grass."), Replanted);
}
