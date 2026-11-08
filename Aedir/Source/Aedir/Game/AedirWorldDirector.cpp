#include "Game/AedirWorldDirector.h"
#include "Game/AedirClasses.h"

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
