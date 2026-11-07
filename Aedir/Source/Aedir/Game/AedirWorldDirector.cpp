#include "Game/AedirWorldDirector.h"

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
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AAedirNpc* Npc = World->SpawnActor<AAedirNpc>(AAedirNpc::StaticClass(), Ground + FVector(0.f, 0.f, 100.f), FRotator(0.f, Yaw, 0.f), Params);
		if (!Npc)
		{
			return nullptr;
		}

		Npc->InitNpc(Role, Name, Biography);
		Npc->SetHome(Npc->GetActorLocation(), Yaw);
		if (WeaponId && Npc->Interaction)
		{
			if (AAedirCatalogItem* Weapon = AAedirCatalogItem::SpawnCatalogItem(World, FName(WeaponId), FTransform(Npc->GetActorLocation() + FVector(0.f, 0.f, 80.f))))
			{
				if (!Npc->Interaction->HolsterImmediate(0, Weapon))
				{
					Npc->Interaction->EquipImmediate(EAedirHand::Right, Weapon);
				}
			}
		}
		UE_LOG(LogAedir, Log, TEXT("Placed %s on a ramp at %s, facing %.0f"), *Name.ToString(), *Ground.ToString(), Yaw);
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
		World->SpawnActor<AAedirTrainingDummy>(AAedirTrainingDummy::StaticClass(), Ground, FRotator::ZeroRotator);
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
