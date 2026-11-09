#include "Game/AedirWorldDirector.h"
#include "Game/AedirClasses.h"
#include "Game/AedirSceneryActors.h"

#include "Engine/CollisionProfile.h"

#include "Graphics/AedirFoliageSubsystem.h"
#include "Graphics/AedirScannedMaterial.h"

#include "Aedir.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "Materials/MaterialInterface.h"
#include "Npc/AedirNpc.h"
#include "Npc/AedirTrainingDummy.h"

namespace
{
	const TCHAR* const DirectorRampMesh = TEXT("SM_Ramp");
	const TCHAR* const DirectorGreenMaterial = TEXT("MI_ThirdPersonColWay");
	constexpr float DirectorTraceHeightCm = 2000.f;
	constexpr float DirectorMeadowHalfCm = 1700.f;
	constexpr float DirectorMeadowDensity = 24.f;
	constexpr int32 DirectorMeadowSeed = 4711;
	constexpr float DirectorTreesInnerCm = 500.f;
	constexpr float DirectorTreesOuterCm = 1800.f;
	constexpr int32 DirectorTreeCount = 9;

	struct FDirectorDecor
	{
		const TCHAR* Path;
		FVector Offset;
		float Yaw;
	};

	// A small yard around the training pell, in real scanned wood and iron.
	const FDirectorDecor DirectorYard[] = {
		{ TEXT("/Game/Megascans/3D_Assets/OldWoodenBench/SM_OldWoodenBench.SM_OldWoodenBench"), FVector(-160.f, 170.f, 0.f), 30.f },
		{ TEXT("/Game/Megascans/3D_Assets/OldWoodenTable/SM_OldWoodenTable.SM_OldWoodenTable"), FVector(-230.f, -130.f, 0.f), -15.f },
		{ TEXT("/Game/Megascans/3D_Assets/OldWoodenTrough/SM_OldWoodenTrough.SM_OldWoodenTrough"), FVector(150.f, 220.f, 0.f), 70.f },
		{ TEXT("/Game/Megascans/3D_Assets/OldWoodenBucketA/SM_OldWoodenBucketA.SM_OldWoodenBucketA"), FVector(90.f, 250.f, 0.f), 0.f },
		{ TEXT("/Game/Megascans/3D_Assets/BarrelOnStand/SM_BarrelOnStand.SM_BarrelOnStand"), FVector(-300.f, 60.f, 0.f), 100.f },
		{ TEXT("/Game/Megascans/3D_Assets/PileOfChains/SM_PileOfChains.SM_PileOfChains"), FVector(190.f, -180.f, 0.f), 40.f }
	};

	bool DirectorGroundUnder(UWorld* World, const FVector& Where, FVector& OutGround)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirDirectorGround), false);
		const bool bHit = World->LineTraceSingleByChannel(Hit, Where + FVector(0.f, 0.f, DirectorTraceHeightCm), Where - FVector(0.f, 0.f, DirectorTraceHeightCm), ECC_Visibility, Params);
		OutGround = bHit ? Hit.ImpactPoint : Where;
		return bHit;
	}

	// The middle of the highest part of a ramp.
	FVector DirectorRampTop(UWorld* World, const AStaticMeshActor* Ramp)
	{
		const FBox Bounds = Ramp->GetComponentsBoundingBox();
		FVector Best = Bounds.GetCenter();
		double BestZ = -UE_BIG_NUMBER;
		for (int32 X = 1; X < 8; ++X)
		{
			for (int32 Y = 1; Y < 8; ++Y)
			{
				const FVector Sample(FMath::Lerp(Bounds.Min.X, Bounds.Max.X, X / 8.0), FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, Y / 8.0), Bounds.Max.Z);
				FVector Ground;
				if (DirectorGroundUnder(World, Sample, Ground) && Ground.Z > BestZ + 2.0)
				{
					BestZ = Ground.Z;
					Best = Ground;
				}
			}
		}
		return Best;
	}

	AAedirNpc* DirectorSpawnNpc(UWorld* World, const FVector& Ground, const FVector& LookFrom, EAedirNpcRole Role, const FText& Name, const FText& Biography, const TCHAR* WeaponId)
	{
		// Facing away from the middle of the map, so the three of them have their backs to each other.
		const float Yaw = static_cast<float>((Ground - LookFrom).Rotation().Yaw);
		// Set up the way one placed in the editor is: the NPC takes who it is and its weapon from these as it begins play.
		const FTransform Where(FRotator(0.f, Yaw, 0.f), Ground + FVector(0.f, 0.f, 100.f));
		AAedirNpc* Npc = World->SpawnActorDeferred<AAedirNpc>(AedirClasses::Find<AAedirNpc>(TEXT("BP_AedirNpc")), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Npc)
		{
			return nullptr;
		}
		Npc->Role_Npc = Role;
		Npc->NpcName = Name;
		Npc->Biography = Biography;
		Npc->StartWeapon = WeaponId ? FName(WeaponId) : NAME_None;
		Npc->FinishSpawning(Where);
#if WITH_EDITOR
		if (!World->IsGameWorld())
		{
			Npc->SetActorLabel(Name.ToString());
		}
#endif
		UE_LOG(LogAedir, Log, TEXT("Placed %s at %s, facing %.0f"), *Name.ToString(), *Ground.ToString(), Yaw);
		return Npc;
	}
}

void AedirWorldDirector::PopulateTestMap(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client || World->GetMapName().Find(TEXT("Lvl_ThirdPerson")) == INDEX_NONE)
	{
		return;
	}

	// The green ramps of the map, with the three furthest out taken as its corners.
	TArray<AStaticMeshActor*> Ramps;
	FVector Middle = FVector::ZeroVector;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
		if (Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == DirectorRampMesh && Material && Material->GetName().Contains(DirectorGreenMaterial))
		{
			Ramps.Add(*It);
			Middle += It->GetActorLocation();
		}
	}
	if (Ramps.Num() < 3)
	{
		UE_LOG(LogAedir, Warning, TEXT("World director: only %d green ramps found."), Ramps.Num());
		return;
	}
	Middle /= Ramps.Num();

	// Greedily the three most apart from each other: first the furthest from the middle, then whichever is furthest from those picked.
	TArray<AStaticMeshActor*> Corners;
	Ramps.Sort([&Middle](const AStaticMeshActor& A, const AStaticMeshActor& B) { return FVector::DistSquared2D(A.GetActorLocation(), Middle) > FVector::DistSquared2D(B.GetActorLocation(), Middle); });
	Corners.Add(Ramps[0]);
	while (Corners.Num() < 3)
	{
		AStaticMeshActor* Best = nullptr;
		double BestScore = -1.0;
		for (AStaticMeshActor* Ramp : Ramps)
		{
			if (Corners.Contains(Ramp))
			{
				continue;
			}
			double Nearest = UE_BIG_NUMBER;
			for (const AStaticMeshActor* Corner : Corners)
			{
				Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Ramp->GetActorLocation(), Corner->GetActorLocation()));
			}
			if (Nearest > BestScore)
			{
				BestScore = Nearest;
				Best = Ramp;
			}
		}
		Corners.Add(Best);
	}

	const FVector TopA = DirectorRampTop(World, Corners[0]);
	const FVector TopB = DirectorRampTop(World, Corners[1]);
	const FVector TopC = DirectorRampTop(World, Corners[2]);
	const FVector Center = (TopA + TopB + TopC) / 3.0;

	DirectorSpawnNpc(World, TopA, Center, EAedirNpcRole::Neutral,
		NSLOCTEXT("AedirWorld", "NeutralName", "Marra the Well-Keeper"),
		NSLOCTEXT("AedirWorld", "NeutralBio", "I keep the water sweet and my nose out of things. Cael's work, the well. Mine is only the rope."), nullptr);
	DirectorSpawnNpc(World, TopB, Center, EAedirNpcRole::Follower,
		NSLOCTEXT("AedirWorld", "FollowerName", "Tobren Ashward"),
		NSLOCTEXT("AedirWorld", "FollowerBio", "Sold my sword to three lords and outlived them all. I've no lord now, and I'm tired of standing around."), TEXT("Sword"));
	DirectorSpawnNpc(World, TopC, Center, EAedirNpcRole::Enemy,
		NSLOCTEXT("AedirWorld", "EnemyName", "Vask the Cutthroat"), FText::GetEmpty(), TEXT("ShortSword"));

	// The pell and its yard near where the player comes in.
	FVector Yard = Center;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Yard = It->GetActorLocation() + It->GetActorForwardVector() * 180.f + It->GetActorRightVector() * 520.f;
		break;
	}
	FVector Ground;
	if (DirectorGroundUnder(World, Yard, Ground))
	{
		if (UAedirFoliageSubsystem* Foliage = World->GetSubsystem<UAedirFoliageSubsystem>())
		{
			Foliage->PlantMeadow(Ground, DirectorMeadowHalfCm, DirectorMeadowDensity, DirectorMeadowSeed);
			Foliage->PlantTrees(Ground, DirectorTreesInnerCm, DirectorTreesOuterCm, DirectorTreeCount, DirectorMeadowSeed);
		}
		World->SpawnActor<AAedirTrainingDummy>(AedirClasses::Find<AAedirTrainingDummy>(TEXT("BP_AedirTrainingDummy")), Ground, FRotator::ZeroRotator);
		for (const FDirectorDecor& Decor : DirectorYard)
		{
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Decor.Path);
			FVector Spot;
			if (!Mesh)
			{
				UE_LOG(LogAedir, Warning, TEXT("World director: decor %s is missing."), Decor.Path);
			}
			else if (DirectorGroundUnder(World, Ground + Decor.Offset, Spot))
			{
				if (AStaticMeshActor* Prop = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Spot, FRotator(0.f, Decor.Yaw, 0.f)))
				{
					Prop->SetMobility(EComponentMobility::Movable);
					Prop->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					// Props stand in the way of bodies, items and blades alike.
					Prop->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
					AedirScannedMaterial::Apply(Prop->GetStaticMeshComponent(), AedirScannedMaterial::FolderOfPath(Decor.Path));
				}
			}
		}
	}
}

namespace
{
	const TCHAR* const DirectorNatureMap = TEXT("Lvl_AedirTest");
	const TCHAR* const DirectorScanned = TEXT("/Game/Megascans/3D_Assets/");
	constexpr int32 DirectorNatureSeed = 9137;
	constexpr float DirectorNatureMeadowHalfCm = 3200.f;
	constexpr float DirectorNatureMeadowDensity = 16.f;
	constexpr float DirectorNatureTreesInnerCm = 2600.f;
	constexpr float DirectorNatureTreesOuterCm = 9000.f;
	constexpr int32 DirectorNatureTreeCount = 45;
	constexpr int32 DirectorNatureRockCount = 70;
	constexpr float DirectorNatureRockInnerCm = 1600.f;
	constexpr float DirectorNatureRockOuterCm = 9500.f;

	// A scanned model by its folder name, or null with a warning when it is not there.
	UStaticMesh* DirectorScannedMesh(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s%s/SM_%s.SM_%s"), DirectorScanned, Name, Name, Name);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Mesh)
		{
			UE_LOG(LogAedir, Warning, TEXT("World director: %s is missing."), *Path);
		}
		return Mesh;
	}

	// Stands a scanned model on the ground at a spot, sunk a little into it so it does not float on a slope.
	AStaticMeshActor* DirectorPlaceProp(UWorld* World, const TCHAR* Name, const FVector& Where, float Yaw, float Scale = 1.f, float SinkCm = 4.f)
	{
		UStaticMesh* Mesh = DirectorScannedMesh(Name);
		FVector Ground;
		if (!Mesh || !DirectorGroundUnder(World, Where, Ground))
		{
			return nullptr;
		}
		AStaticMeshActor* Prop = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Ground - FVector(0.f, 0.f, SinkCm * Scale), FRotator(0.f, Yaw, 0.f));
		if (!Prop)
		{
			return nullptr;
		}
		Prop->SetMobility(EComponentMobility::Movable);
		Prop->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Prop->SetActorScale3D(FVector(Scale));
		Prop->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		AedirScannedMaterial::Apply(Prop->GetStaticMeshComponent(), AedirScannedMaterial::FolderOfPath(FString(DirectorScanned) + Name + TEXT("/")));
#if WITH_EDITOR
		if (!World->IsGameWorld())
		{
			Prop->SetActorLabel(Name);
		}
#endif
		return Prop;
	}

	// The height of the top of a placed prop, for laying things on it.
	float DirectorTopOf(const AStaticMeshActor* Prop)
	{
		return Prop ? static_cast<float>(Prop->GetComponentsBoundingBox().Max.Z) : 0.f;
	}

	// Lays a catalog item down at a point, lying along a yaw.
	void DirectorLayItem(UWorld* World, const TCHAR* Id, const FVector& Where, float Yaw)
	{
		AAedirCatalogItem* Item = AAedirCatalogItem::SpawnCatalogItem(World, FName(Id), FTransform(FRotator(0.f, Yaw, 0.f), Where));
#if WITH_EDITOR
		if (Item && !World->IsGameWorld())
		{
			Item->SetActorLabel(FString(TEXT("Item_")) + Id);
		}
#else
		(void)Item;
#endif
	}
}

void AedirWorldDirector::PopulateNatureMap(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client || World->GetMapName().Find(DirectorNatureMap) == INDEX_NONE)
	{
		return;
	}
	// A level that was laid out in the editor keeps what was placed there, as it was edited.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(LaidOutTag))
		{
			return;
		}
	}
	LayOutNatureMap(World);
}

void AedirWorldDirector::LayOutNatureMap(UWorld* World)
{
	FVector Start = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Start = It->GetActorLocation();
		Facing = It->GetActorRotation();
		break;
	}
	FVector Center;
	DirectorGroundUnder(World, Start, Center);
	const FRotator Flat(0.f, Facing.Yaw, 0.f);
	const auto At = [&Center, &Flat](float Forward, float Right) { return Center + Flat.RotateVector(FVector(Forward, Right, 0.f)); };
	FRandomStream Random(DirectorNatureSeed);

	// The camp: a table laid with food and drink, another with weapons, benches, barrels and the odds and ends of a yard.
	if (AStaticMeshActor* FoodTable = DirectorPlaceProp(World, TEXT("OldWoodenTable"), At(650.f, 300.f), Flat.Yaw + 10.f))
	{
		const float Top = DirectorTopOf(FoodTable) + 6.f;
		const FVector Mid = FoodTable->GetActorLocation();
		const FRotator Along = FoodTable->GetActorRotation();
		const auto OnTable = [&](float X, float Y) { return FVector(Mid.X, Mid.Y, Top) + Along.RotateVector(FVector(X, Y, 0.f)); };
		DirectorLayItem(World, TEXT("Bread"), OnTable(-45.f, -12.f), Along.Yaw);
		DirectorLayItem(World, TEXT("Apple"), OnTable(-10.f, 14.f), 0.f);
		DirectorLayItem(World, TEXT("Apple"), OnTable(2.f, 20.f), 40.f);
		DirectorLayItem(World, TEXT("SmokedMeat"), OnTable(25.f, -10.f), Along.Yaw + 70.f);
		DirectorLayItem(World, TEXT("WaterJug"), OnTable(55.f, 12.f), 0.f);
		DirectorLayItem(World, TEXT("Carafe"), OnTable(-30.f, 18.f), 0.f);
	}
	DirectorPlaceProp(World, TEXT("OldWoodenBench"), At(650.f, 420.f), Flat.Yaw + 10.f);
	DirectorPlaceProp(World, TEXT("OldWoodenBench"), At(650.f, 180.f), Flat.Yaw + 190.f);
	if (AStaticMeshActor* ArmsTable = DirectorPlaceProp(World, TEXT("OldWoodenTable"), At(420.f, -380.f), Flat.Yaw - 75.f))
	{
		const float Top = DirectorTopOf(ArmsTable) + 8.f;
		const FVector Mid = ArmsTable->GetActorLocation();
		const FRotator Along = ArmsTable->GetActorRotation();
		const auto OnTable = [&](float X, float Y) { return FVector(Mid.X, Mid.Y, Top) + Along.RotateVector(FVector(X, Y, 0.f)); };
		DirectorLayItem(World, TEXT("Sword"), OnTable(0.f, -22.f), Along.Yaw);
		DirectorLayItem(World, TEXT("Longsword"), OnTable(0.f, -5.f), Along.Yaw);
		DirectorLayItem(World, TEXT("ShortSword"), OnTable(-10.f, 12.f), Along.Yaw);
		DirectorLayItem(World, TEXT("Dagger"), OnTable(40.f, 24.f), Along.Yaw + 15.f);
		DirectorLayItem(World, TEXT("Mace"), OnTable(-50.f, 26.f), Along.Yaw + 80.f);
		DirectorLayItem(World, TEXT("BattleAxe"), OnTable(50.f, -24.f), Along.Yaw + 95.f);
	}
	const FVector Rack = At(420.f, -560.f);
	DirectorLayItem(World, TEXT("Spear"), Rack + FVector(0.f, 0.f, 30.f), Flat.Yaw);
	DirectorLayItem(World, TEXT("Halberd"), Rack + Flat.RotateVector(FVector(0.f, 40.f, 30.f)), Flat.Yaw);
	DirectorLayItem(World, TEXT("Quarterstaff"), Rack + Flat.RotateVector(FVector(0.f, -40.f, 30.f)), Flat.Yaw);
	DirectorLayItem(World, TEXT("Warhammer"), Rack + Flat.RotateVector(FVector(-60.f, 0.f, 30.f)), Flat.Yaw + 90.f);
	if (AStaticMeshActor* ArmourBench = DirectorPlaceProp(World, TEXT("OldWoodenBench"), At(250.f, -600.f), Flat.Yaw - 20.f))
	{
		const float Top = DirectorTopOf(ArmourBench) + 15.f;
		const FVector Mid = ArmourBench->GetActorLocation();
		const FRotator Along = ArmourBench->GetActorRotation();
		const auto OnBench = [&](float X) { return FVector(Mid.X, Mid.Y, Top) + Along.RotateVector(FVector(X, 0.f, 0.f)); };
		DirectorLayItem(World, TEXT("IronCuirass"), OnBench(-50.f), Along.Yaw);
		DirectorLayItem(World, TEXT("Bracers"), OnBench(0.f), Along.Yaw);
		DirectorLayItem(World, TEXT("IronHelm"), OnBench(40.f), Along.Yaw);
		DirectorLayItem(World, TEXT("LeatherCap"), OnBench(70.f), Along.Yaw);
	}
	DirectorLayItem(World, TEXT("IronGreaves"), At(230.f, -700.f) + FVector(0.f, 0.f, 40.f), Flat.Yaw);
	DirectorLayItem(World, TEXT("LeatherArmor"), At(300.f, -720.f) + FVector(0.f, 0.f, 40.f), Flat.Yaw);
	DirectorPlaceProp(World, TEXT("BarrelOnStand"), At(800.f, 520.f), Random.FRandRange(0.f, 360.f));
	DirectorPlaceProp(World, TEXT("OldWoodenTrough"), At(900.f, 120.f), Flat.Yaw + 75.f);
	DirectorLayItem(World, TEXT("Bucket"), At(860.f, 30.f) + FVector(0.f, 0.f, 60.f), 0.f);
	DirectorPlaceProp(World, TEXT("OldWoodenChest"), At(520.f, -180.f), Flat.Yaw + 100.f);
	DirectorLayItem(World, TEXT("Chains"), At(300.f, -450.f) + FVector(0.f, 0.f, 60.f), 30.f);
	DirectorPlaceProp(World, TEXT("BlacksmithAnvilA"), At(150.f, -800.f), Flat.Yaw + 20.f);
	DirectorLayItem(World, TEXT("Coal"), At(60.f, -880.f) + FVector(0.f, 0.f, 60.f), 0.f);
	DirectorPlaceProp(World, TEXT("HorseHitchingPost"), At(1000.f, -200.f), Flat.Yaw + 90.f);

	// The pell to try blows on.
	FVector Pell;
	if (DirectorGroundUnder(World, At(300.f, 650.f), Pell))
	{
		World->SpawnActor<AAedirTrainingDummy>(AedirClasses::Find<AAedirTrainingDummy>(TEXT("BP_AedirTrainingDummy")), Pell, FRotator::ZeroRotator);
	}

	// People: the keeper of the camp and a sellsword at hand, two villagers who talk, and cutthroats out on the hill.
	DirectorSpawnNpc(World, At(700.f, 600.f), Center, EAedirNpcRole::Neutral,
		NSLOCTEXT("AedirWorld", "NeutralName", "Marra the Well-Keeper"),
		NSLOCTEXT("AedirWorld", "NeutralBio", "I keep the water sweet and my nose out of things. Cael's work, the well. Mine is only the rope."), nullptr);
	DirectorSpawnNpc(World, At(250.f, 250.f), Center, EAedirNpcRole::Follower,
		NSLOCTEXT("AedirWorld", "FollowerName", "Tobren Ashward"),
		NSLOCTEXT("AedirWorld", "FollowerBio", "Sold my sword to three lords and outlived them all. I've no lord now, and I'm tired of standing around."), TEXT("Sword"));
	DirectorSpawnNpc(World, At(950.f, 380.f), At(1100.f, 380.f), EAedirNpcRole::Neutral, FText::FromString(TEXT("Hedda")), FText::FromString(TEXT("I bake for the camp. Mind the crust, it bites back.")), nullptr);
	DirectorSpawnNpc(World, At(1150.f, 380.f), At(950.f, 380.f), EAedirNpcRole::Neutral, FText::FromString(TEXT("Orvan")), FText::FromString(TEXT("Carter. Roads are bad and getting worse.")), nullptr);
	DirectorSpawnNpc(World, At(3800.f, -1500.f), Center, EAedirNpcRole::Enemy, NSLOCTEXT("AedirWorld", "EnemyName", "Vask the Cutthroat"), FText::GetEmpty(), TEXT("ShortSword"));
	DirectorSpawnNpc(World, At(4000.f, -1200.f), Center, EAedirNpcRole::Enemy, FText::FromString(TEXT("Brisk the Bandit")), FText::GetEmpty(), TEXT("Mace"));

	// Nature: a meadow around the clearing, a loose forest beyond it, rocks, stumps and fallen trunks scattered through it,
	// and an old wall on the rise to the east.
	if (UAedirFoliageSubsystem* Foliage = World->GetSubsystem<UAedirFoliageSubsystem>())
	{
		Foliage->PlantMeadow(Center, DirectorNatureMeadowHalfCm, DirectorNatureMeadowDensity, DirectorNatureSeed);
		Foliage->PlantTrees(Center, DirectorNatureTreesInnerCm, DirectorNatureTreesOuterCm, DirectorNatureTreeCount, DirectorNatureSeed);
	}
	static const TCHAR* const Scatter[] = {
		TEXT("IcelandicBoulder"), TEXT("GraniteRockAssembly"), TEXT("HovsBeachRock"), TEXT("IcelandicMossyRock"), TEXT("IcelandicMossyRocks"), TEXT("MossyRock"),
		TEXT("MossyRocksA"), TEXT("MossyRocksB"), TEXT("BrokenPineStump"), TEXT("BrokenTreeStump"), TEXT("FirTreeStump"), TEXT("LichenStump"),
		TEXT("OldStump"), TEXT("MossyLog"), TEXT("FallenFirTree"), TEXT("FallenPineTree"), TEXT("FallenTreeAssembly"), TEXT("ForestRootsA"), TEXT("ForestRootsB"),
		TEXT("Branch") };
	for (int32 Index = 0; Index < DirectorNatureRockCount; ++Index)
	{
		const float Angle = Random.FRandRange(0.f, 360.f);
		const float Distance = FMath::Lerp(DirectorNatureRockInnerCm, DirectorNatureRockOuterCm, FMath::Sqrt(Random.FRand()));
		const FVector Spot = Center + FRotator(0.f, Angle, 0.f).Vector() * Distance;
		DirectorPlaceProp(World, Scatter[Random.RandHelper(UE_ARRAY_COUNT(Scatter))], Spot, Random.FRandRange(0.f, 360.f), Random.FRandRange(0.8f, 1.35f), 12.f);
	}
	const FVector Ruin = At(-500.f, 2600.f);
	DirectorPlaceProp(World, TEXT("CastleWall"), Ruin, Flat.Yaw + 15.f, 1.f, 30.f);
	DirectorPlaceProp(World, TEXT("DamagedCastleStairs"), Ruin + Flat.RotateVector(FVector(600.f, 200.f, 0.f)), Flat.Yaw + 105.f, 1.f, 20.f);
	DirectorPlaceProp(World, TEXT("MossyStoneWallA"), Ruin + Flat.RotateVector(FVector(-700.f, -300.f, 0.f)), Flat.Yaw + 40.f, 1.f, 15.f);
	DirectorPlaceProp(World, TEXT("MossyStoneWallB"), Ruin + Flat.RotateVector(FVector(-300.f, 700.f, 0.f)), Flat.Yaw - 60.f, 1.f, 15.f);
	DirectorPlaceProp(World, TEXT("GraveA"), Ruin + Flat.RotateVector(FVector(300.f, -600.f, 0.f)), Flat.Yaw, 1.f, 5.f);
	DirectorPlaceProp(World, TEXT("GraveB"), Ruin + Flat.RotateVector(FVector(420.f, -680.f, 0.f)), Flat.Yaw + 8.f, 1.f, 5.f);
	UE_LOG(LogAedir, Log, TEXT("World director laid out the nature test map around %s."), *Center.ToCompactString());
}

namespace
{
	const FName SceneryTag(TEXT("AedirScenery"));
	const TCHAR* const SceneryWaterPath = TEXT("/Game/Aedir/Materials/M_AedirStream.M_AedirStream");
	const TCHAR* const SceneryPlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	constexpr int32 ScenerySeed = 52711;
	// The stream: the line its bed follows, how far it runs, how deep the water stands in it and how wide its surface is laid.
	constexpr float StreamFromCm = -48000.f;
	constexpr float StreamToCm = 48000.f;
	constexpr float StreamStepCm = 600.f;
	constexpr float StreamDepthCm = 70.f;
	constexpr float StreamHalfWidthCm = 650.f;

	float SceneryStreamY(float XCm)
	{
		return (95.f + 40.f * FMath::Sin(XCm / 100.f / 85.f)) * 100.f;
	}

	// The water's surface along the stream, point by point, for each world it was laid in.
	TMap<TWeakObjectPtr<const UWorld>, TArray<FVector>>& SceneryStreams()
	{
		static TMap<TWeakObjectPtr<const UWorld>, TArray<FVector>> Streams;
		return Streams;
	}

	AStaticMeshActor* SceneryProp(UWorld* World, const TCHAR* Name, const FVector& Where, float Yaw, float Scale, float SinkCm)
	{
		AStaticMeshActor* Prop = DirectorPlaceProp(World, Name, Where, Yaw, Scale, SinkCm);
		if (Prop)
		{
			Prop->Tags.Add(SceneryTag);
		}
		return Prop;
	}

	AAedirFlame* SceneryFire(UWorld* World, const FVector& Base, float HeightCm, float Candelas, bool bSmoke)
	{
		AAedirFlame* Fire = World->SpawnActorDeferred<AAedirFlame>(AAedirFlame::StaticClass(), FTransform(Base), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Fire)
		{
			Fire->Configure(HeightCm, Candelas, bSmoke);
			Fire->Tags.Add(SceneryTag);
			Fire->FinishSpawning(FTransform(Base));
		}
		return Fire;
	}

	// A torch: a pole stuck in the ground with a fire on top.
	void SceneryTorch(UWorld* World, const FVector& Where)
	{
		FVector Ground;
		if (!DirectorGroundUnder(World, Where, Ground))
		{
			return;
		}
		if (AStaticMeshActor* Pole = SceneryProp(World, TEXT("OldWoodenPole"), Ground, FMath::FRandRange(0.f, 360.f), 0.55f, 10.f))
		{
			SceneryFire(World, FVector(Ground.X, Ground.Y, DirectorTopOf(Pole) - 4.f), 26.f, 600.f, false);
		}
	}

	void SceneryBuildStream(UWorld* World)
	{
		UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, SceneryPlanePath);
		UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, SceneryWaterPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Plane)
		{
			return;
		}
		// The water lies on the bottom of the bed as deep as a stream runs; each stretch is laid from one point of its surface
		// to the next, so the surface follows the bed's fall without a step.
		TArray<FVector>& Surface = SceneryStreams().FindOrAdd(World);
		Surface.Reset();
		for (float X = StreamFromCm; X <= StreamToCm; X += StreamStepCm)
		{
			FVector Bed;
			if (DirectorGroundUnder(World, FVector(X, SceneryStreamY(X), 0.f), Bed))
			{
				Surface.Add(FVector(X, SceneryStreamY(X), Bed.Z + StreamDepthCm));
			}
		}
		for (int32 Index = 0; Index + 1 < Surface.Num(); ++Index)
		{
			const FVector A = Surface[Index];
			const FVector B = Surface[Index + 1];
			AStaticMeshActor* Stretch = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), (A + B) * 0.5, FRotationMatrix::MakeFromXZ((B - A).GetSafeNormal(), FVector::UpVector).Rotator());
			if (!Stretch)
			{
				continue;
			}
			Stretch->SetMobility(EComponentMobility::Movable);
			UStaticMeshComponent* Mesh = Stretch->GetStaticMeshComponent();
			Mesh->SetStaticMesh(Plane);
			if (Water)
			{
				Mesh->SetMaterial(0, Water);
			}
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Stretch->SetActorScale3D(FVector(FVector::Dist(A, B) * 1.04f / 100.f, StreamHalfWidthCm * 2.f / 100.f, 1.f));
			Stretch->Tags.Add(SceneryTag);
		}
		UE_LOG(LogAedir, Log, TEXT("[Scenery] Water laid along %d points of the stream."), Surface.Num());
	}
}

bool AedirWorldDirector::GetWaterSurface(const UWorld* World, const FVector& Point, float& OutZ)
{
	const TArray<FVector>* Surface = SceneryStreams().Find(World);
	if (!Surface || Surface->Num() < 2)
	{
		return false;
	}
	const float X = static_cast<float>(Point.X);
	if (X < (*Surface)[0].X || X > Surface->Last().X || FMath::Abs(static_cast<float>(Point.Y) - SceneryStreamY(X)) > StreamHalfWidthCm)
	{
		return false;
	}
	const int32 Index = FMath::Clamp(static_cast<int32>((X - (*Surface)[0].X) / StreamStepCm), 0, Surface->Num() - 2);
	const FVector& A = (*Surface)[Index];
	const FVector& B = (*Surface)[Index + 1];
	const float Share = FMath::Clamp(static_cast<float>((X - A.X) / FMath::Max(B.X - A.X, 1.0)), 0.f, 1.f);
	OutZ = FMath::Lerp(static_cast<float>(A.Z), static_cast<float>(B.Z), Share);
	return Point.Z < OutZ + 30.f;
}

void AedirWorldDirector::AddScenery(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client || World->GetMapName().Find(DirectorNatureMap) == INDEX_NONE)
	{
		return;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(SceneryTag))
		{
			return;
		}
	}
	FRandomStream Random(ScenerySeed);
	FVector Center = FVector::ZeroVector;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Center = It->GetActorLocation();
		break;
	}
	DirectorGroundUnder(World, Center, Center);

	SceneryBuildStream(World);

	// The lie of the land: heights on a coarse grid, to find the highest hill and the steepest slopes.
	struct FSpot { FVector Ground; float Slope; };
	TArray<FSpot> Spots;
	for (float X = -42000.f; X <= 42000.f; X += 3000.f)
	{
		for (float Y = -42000.f; Y <= 42000.f; Y += 3000.f)
		{
			FVector Here, East, North;
			if (DirectorGroundUnder(World, FVector(X, Y, 0.f), Here) && DirectorGroundUnder(World, FVector(X + 800.f, Y, 0.f), East) && DirectorGroundUnder(World, FVector(X, Y + 800.f, 0.f), North))
			{
				Spots.Add({ Here, static_cast<float>(FVector2D(East.Z - Here.Z, North.Z - Here.Z).Size() / 800.0) });
			}
		}
	}
	const auto FarFrom = [](const FVector& A, const FVector& B, float Cm) { return FVector::Dist2D(A, B) > Cm; };
	const auto AwayFromStream = [](const FVector& P) { return FMath::Abs(static_cast<float>(P.Y) - SceneryStreamY(static_cast<float>(P.X))) > 2000.f; };

	// The waterfall where the stream begins: it pours over a rock ledge at the west end of its bed into a pool.
	const TArray<FVector>* StreamSurface = SceneryStreams().Find(World);
	if (StreamSurface && StreamSurface->Num() > 12)
	{
		const float X = static_cast<float>((*StreamSurface)[10].X);
		FVector Bed;
		if (DirectorGroundUnder(World, FVector(X, SceneryStreamY(X), 0.f), Bed))
		{
			// A cliff of rock across the head of the stream: the water falls from its top.
			AStaticMeshActor* Cliff = SceneryProp(World, TEXT("IcelandicBoulder"), FVector(X - 750.f, SceneryStreamY(X - 750.f), 0.f), 15.f, 5.5f, 10.f);
			SceneryProp(World, TEXT("IcelandicBoulder"), FVector(X - 650.f, SceneryStreamY(X - 650.f) + 450.f, 0.f), 140.f, 4.2f, 10.f);
			SceneryProp(World, TEXT("IcelandicBoulder"), FVector(X - 650.f, SceneryStreamY(X - 650.f) - 450.f, 0.f), 250.f, 4.2f, 10.f);
			SceneryProp(World, TEXT("IcelandicMossyRock"), FVector(X - 500.f, SceneryStreamY(X) + 700.f, 0.f), Random.FRandRange(0.f, 360.f), 3.5f, 20.f);
			SceneryProp(World, TEXT("IcelandicMossyRock"), FVector(X - 500.f, SceneryStreamY(X) - 700.f, 0.f), Random.FRandRange(0.f, 360.f), 3.5f, 20.f);
			SceneryProp(World, TEXT("IcelandicBoulder"), FVector(X - 100.f, SceneryStreamY(X) + 800.f, 0.f), 30.f, 2.4f, 20.f);
			SceneryProp(World, TEXT("MossyRocksA"), FVector(X + 300.f, SceneryStreamY(X) - 750.f, 0.f), 80.f, 2.f, 20.f);
			const float CliffTop = Cliff ? DirectorTopOf(Cliff) : Bed.Z + 700.f;
			const FVector Ledge(X - 380.f, SceneryStreamY(X - 380.f), FMath::Min(CliffTop - 60.f, static_cast<float>(Bed.Z) + 1100.f));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AAedirWaterfall* Fall = World->SpawnActor<AAedirWaterfall>(AAedirWaterfall::StaticClass(), FTransform(Ledge), Params))
			{
				Fall->Configure(Ledge, FVector(X, SceneryStreamY(X), Bed.Z + StreamDepthCm), 420.f);
				Fall->Tags.Add(SceneryTag);
			}
		}
	}

	// A ruined watchtower on the highest hill, its stair still standing, torches burning on it: it can be seen from the camp.
	FSpot High = Spots.Num() ? Spots[0] : FSpot{ Center, 0.f };
	for (const FSpot& Spot : Spots)
	{
		if (Spot.Ground.Z > High.Ground.Z && FarFrom(Spot.Ground, Center, 8000.f) && AwayFromStream(Spot.Ground))
		{
			High = Spot;
		}
	}
	{
		const FVector Tower = High.Ground;
		const float Yaw = static_cast<float>((Center - Tower).Rotation().Yaw);
		const FRotator Facing(0.f, Yaw, 0.f);
		static const TCHAR* const Walls[] = { TEXT("MedievalModularWall3x2M"), TEXT("MedievalModularWall2x2MD"), TEXT("MedievalModularCornerWall1x2M"), TEXT("MedievalModularWall2x2MG") };
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FRotator Around(0.f, Yaw + Side * 90.f, 0.f);
			SceneryProp(World, Walls[Side], Tower + Around.RotateVector(FVector(650.f, 0.f, 0.f)), Yaw + Side * 90.f + 90.f, 2.2f, 30.f);
		}
		SceneryProp(World, TEXT("DamagedCastleStairs"), Tower + Facing.RotateVector(FVector(-150.f, 0.f, 0.f)), Yaw, 1.8f, 20.f);
		SceneryProp(World, TEXT("IcelandicMossyRocks"), Tower + Facing.RotateVector(FVector(300.f, 900.f, 0.f)), Yaw, 2.f, 30.f);
		SceneryProp(World, TEXT("GraniteRockAssembly"), Tower + Facing.RotateVector(FVector(-400.f, -1100.f, 0.f)), Yaw + 60.f, 2.2f, 60.f);
		SceneryProp(World, TEXT("CastleWall"), Tower + Facing.RotateVector(FVector(-900.f, 700.f, 0.f)), Yaw + 30.f, 1.f, 40.f);
		SceneryProp(World, TEXT("MossyStoneWallB"), Tower + Facing.RotateVector(FVector(-700.f, -900.f, 0.f)), Yaw - 20.f, 1.2f, 20.f);
		SceneryTorch(World, Tower + Facing.RotateVector(FVector(700.f, 250.f, 0.f)));
		SceneryTorch(World, Tower + Facing.RotateVector(FVector(700.f, -250.f, 0.f)));
		DirectorLayItem(World, TEXT("SteelLongsword"), Tower + Facing.RotateVector(FVector(100.f, 80.f, 0.f)) + FVector(0.f, 0.f, 80.f), Yaw);
		UE_LOG(LogAedir, Log, TEXT("[Scenery] Ruined tower on the hill at %s."), *Tower.ToCompactString());
	}

	// A cave in the steepest hillside away from the camp: rock walls and a rock roof over a dark hollow, a fire burning in it,
	// a chest and a guard who does not like visitors.
	FSpot Steep = Spots.Num() ? Spots[0] : FSpot{ Center, 0.f };
	for (const FSpot& Spot : Spots)
	{
		if (Spot.Slope > Steep.Slope && FarFrom(Spot.Ground, Center, 9000.f) && FarFrom(Spot.Ground, High.Ground, 8000.f) && AwayFromStream(Spot.Ground) && FVector::Dist2D(Spot.Ground, Center) < 30000.f)
		{
			Steep = Spot;
		}
	}
	{
		const FVector Mouth = Steep.Ground;
		// It opens downhill, towards the camp.
		const float Yaw = static_cast<float>((Center - Mouth).Rotation().Yaw);
		const FRotator Facing(0.f, Yaw, 0.f);
		const auto At = [&Mouth, &Facing](float Forward, float Right) { return Mouth + Facing.RotateVector(FVector(Forward, Right, 0.f)); };
		FVector Inside = Mouth;
		DirectorGroundUnder(World, At(-450.f, 150.f), Inside);
		FVector Guard;
		if (DirectorGroundUnder(World, At(-150.f, 0.f), Guard))
		{
			DirectorSpawnNpc(World, Guard, At(-600.f, 0.f), EAedirNpcRole::Enemy, FText::FromString(TEXT("Grell of the Hollow")), FText::GetEmpty(), TEXT("BeardedGreataxe"));
		}
		AStaticMeshActor* LeftWall = SceneryProp(World, TEXT("IcelandicBoulder"), At(-350.f, -560.f), Yaw + 90.f, 3.f, 10.f);
		AStaticMeshActor* RightWall = SceneryProp(World, TEXT("IcelandicBoulder"), At(-350.f, 560.f), Yaw - 90.f, 3.f, 10.f);
		SceneryProp(World, TEXT("IcelandicBoulder"), At(-950.f, 0.f), Yaw + 180.f, 3.4f, 10.f);
		SceneryProp(World, TEXT("IcelandicMossyRocks"), At(-700.f, -500.f), Yaw, 2.f, 10.f);
		SceneryProp(World, TEXT("IcelandicMossyRocks"), At(-700.f, 500.f), Yaw + 180.f, 2.f, 10.f);
		// The roof: great boulders laid across the two walls.
		for (int32 Stone = 0; Stone < 2; ++Stone)
		{
			if (AStaticMeshActor* Roof = SceneryProp(World, TEXT("IcelandicBoulder"), At(-250.f - Stone * 380.f, Stone ? 120.f : -100.f), Yaw + Stone * 70.f, 3.6f, 0.f))
			{
				// Laid on the lower of the two walls' tops, sunk a little into them.
				const float Walls = FMath::Min(LeftWall ? DirectorTopOf(LeftWall) : Inside.Z + 450.f, RightWall ? DirectorTopOf(RightWall) : Inside.Z + 450.f);
				const float Under = static_cast<float>(Roof->GetActorLocation().Z - Roof->GetComponentsBoundingBox().Min.Z);
				Roof->SetActorLocation(FVector(Roof->GetActorLocation().X, Roof->GetActorLocation().Y, FMath::Max(Walls - 120.f, static_cast<float>(Inside.Z) + 300.f) + Under));
			}
		}
		SceneryProp(World, TEXT("IcelandicBoulder"), At(150.f, -650.f), Yaw + 20.f, 1.8f, 30.f);
		SceneryProp(World, TEXT("MossyRocksB"), At(200.f, 600.f), Yaw - 40.f, 1.6f, 20.f);
		SceneryFire(World, Inside + FVector(0.f, 0.f, 5.f), 30.f, 900.f, false);
		if (AStaticMeshActor* Chest = SceneryProp(World, TEXT("OldWoodenChest"), At(-650.f, -150.f), Yaw + 10.f, 1.f, 2.f))
		{
			const float Top = DirectorTopOf(Chest) + 10.f;
			const FVector Lid = Chest->GetActorLocation();
			DirectorLayItem(World, TEXT("BloodySword"), FVector(Lid.X, Lid.Y, Top), Yaw + 80.f);
		}
		DirectorLayItem(World, TEXT("IronSouthernHelmet"), FVector(At(-600.f, 120.f).X, At(-600.f, 120.f).Y, Inside.Z + 40.f), Yaw);
		DirectorLayItem(World, TEXT("RustyIronShield"), FVector(At(-700.f, 260.f).X, At(-700.f, 260.f).Y, Inside.Z + 40.f), Yaw);
		UE_LOG(LogAedir, Log, TEXT("[Scenery] Cave in the hillside at %s, slope %.2f."), *Mouth.ToCompactString(), Steep.Slope);
	}

	// The camp: a fire ringed with stones and logs to sit on, torches at its edges.
	{
		const FVector FirePlace = Center + FVector(-350.f, -250.f, 0.f);
		FVector Ground;
		if (DirectorGroundUnder(World, FirePlace, Ground))
		{
			for (int32 Stone = 0; Stone < 8; ++Stone)
			{
				const FVector Ring = Ground + FRotator(0.f, Stone * 45.f, 0.f).Vector() * 55.f;
				SceneryProp(World, TEXT("MossyRock"), Ring, Random.FRandRange(0.f, 360.f), 0.16f, 4.f);
			}
			SceneryProp(World, TEXT("CoalA"), Ground, 0.f, 1.6f, 2.f);
			SceneryProp(World, TEXT("CoalB"), Ground + FVector(15.f, 10.f, 0.f), 40.f, 1.6f, 2.f);
			SceneryFire(World, Ground + FVector(0.f, 0.f, 8.f), 55.f, 1800.f, false);
			SceneryProp(World, TEXT("MossyLog"), Ground + FVector(220.f, 0.f, 0.f), 90.f, 0.8f, 8.f);
			SceneryProp(World, TEXT("MossyLog"), Ground + FVector(-60.f, 220.f, 0.f), 10.f, 0.8f, 8.f);
		}
		SceneryTorch(World, Center + FVector(1100.f, 700.f, 0.f));
		SceneryTorch(World, Center + FVector(1100.f, -700.f, 0.f));
		SceneryTorch(World, Center + FVector(-300.f, 800.f, 0.f));
		SceneryTorch(World, Center + FVector(-600.f, -800.f, 0.f));
	}

	// Banks of moss and stone along the stream.
	static const TCHAR* const Banks[] = { TEXT("MossyEmbankmentB"), TEXT("MossyEmbankmentC"), TEXT("MossyEmbankmentD"), TEXT("MossyEmbankmentE"), TEXT("HovsBeachRock"), TEXT("MossyRocksA") };
	for (float X = StreamFromCm + 2500.f; X < StreamToCm - 1000.f; X += Random.FRandRange(1800.f, 3200.f))
	{
		const float Side = Random.FRandRange(0.f, 1.f) < 0.5f ? 1.f : -1.f;
		SceneryProp(World, Banks[Random.RandHelper(UE_ARRAY_COUNT(Banks))], FVector(X, SceneryStreamY(X) + Side * Random.FRandRange(700.f, 1000.f), 0.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(1.f, 1.8f), 30.f);
	}

	// Outcrops of rock on the hills, big ones far out, and the dead wood of the forest.
	static const TCHAR* const Outcrops[] = { TEXT("GraniteRockAssembly"), TEXT("IcelandicBoulder"), TEXT("IcelandicMossyRocks"), TEXT("IcelandicMossyRock"), TEXT("MossyRocksA"), TEXT("MossyRocksB"), TEXT("HovsBeachRock") };
	static const TCHAR* const DeadWood[] = { TEXT("FallenPineTree"), TEXT("FallenFirTree"), TEXT("FallenPineAssembly"), TEXT("FallenTreeAssembly"), TEXT("BrokenPineAssembly"), TEXT("BrokenPineStump"), TEXT("ForestRootsA"), TEXT("ForestRootsB"), TEXT("LichenStump"), TEXT("MossyLog") };
	int32 Placed = 0;
	for (int32 Try = 0; Try < 400 && Placed < 140; ++Try)
	{
		const FVector Spot = Center + FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f).Vector() * FMath::Lerp(4000.f, 42000.f, FMath::Sqrt(Random.FRand()));
		if (!AwayFromStream(Spot))
		{
			continue;
		}
		const bool bRock = Random.FRand() < 0.55f;
		const float Far = static_cast<float>(FVector::Dist2D(Spot, Center)) / 42000.f;
		const float Scale = bRock ? Random.FRandRange(1.f, 1.5f) * FMath::Lerp(1.f, 3.f, Far) : Random.FRandRange(0.9f, 1.2f);
		if (SceneryProp(World, bRock ? Outcrops[Random.RandHelper(UE_ARRAY_COUNT(Outcrops))] : DeadWood[Random.RandHelper(UE_ARRAY_COUNT(DeadWood))], Spot, Random.FRandRange(0.f, 360.f), Scale, bRock ? 40.f : 10.f))
		{
			++Placed;
		}
	}
	UE_LOG(LogAedir, Log, TEXT("[Scenery] Laid %d rocks and fallen trees about the land."), Placed);
}
