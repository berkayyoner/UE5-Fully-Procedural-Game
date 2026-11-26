#include "Wounds/AedirBloodSubsystem.h"


#include "Aedir.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	// Drops of blood are a few millimetres across: each flying drop is under a centimetre.
	// A drop of the burst is drawn this much longer along its way for each cm/s it goes at.
	constexpr float AedirBurstStretchPerCmPerSecond = 0.012f;
	constexpr float BloodDropScale = 0.0055f;
	constexpr float BloodDropLifeSpan = 4.f;
	constexpr float BloodDropStartOutCm = 6.f;
	constexpr float BloodDropLinearDamping = 0.05f;
	constexpr float BloodCutSpeedMin = 280.f;
	constexpr float BloodCutSpeedMax = 900.f;
	constexpr float BloodPunctureSpeedMin = 60.f;
	constexpr float BloodPunctureSpeedMax = 180.f;
	constexpr float BloodSeverityMinSpeedScale = 0.6f;
	constexpr float BloodSplatDepth = 6.f;
	constexpr float BloodSplatRadiusPerScale = 8.f;
	constexpr float BloodSplatMinScale = 0.05f;
	constexpr float BloodSplatFadeStartFraction = 0.9f;
	constexpr float BloodSplatFadeDurationFraction = 0.1f;
	// A drop that lands leaves a spot a few centimetres across, not a hand-sized splash.
	constexpr float BloodDropHitSplatScale = 0.24f;

	struct FBloodMaterialSlot
	{
		explicit FBloodMaterialSlot(const TCHAR* Path)
			: Soft(FSoftObjectPath(FString(Path)))
		{
		}

		TSoftObjectPtr<UMaterialInterface> Soft;
		TWeakObjectPtr<UMaterialInterface> Material;
		bool bMissing = false;
	};

	// Loads the material once; a missing asset is logged a single time and the blood visual is skipped from then on.
	UMaterialInterface* BloodResolveMaterial(FBloodMaterialSlot& Slot)
	{
		if (UMaterialInterface* Loaded = Slot.Material.Get())
		{
			return Loaded;
		}

		if (Slot.bMissing)
		{
			return nullptr;
		}

		if (UMaterialInterface* Loaded = Slot.Soft.LoadSynchronous())
		{
			Slot.Material = Loaded;
			return Loaded;
		}

		Slot.bMissing = true;
		UE_LOG(LogAedir, Warning, TEXT("Blood material %s is missing, blood splats are skipped"), *Slot.Soft.ToSoftObjectPath().ToString());
		return nullptr;
	}

	FBloodMaterialSlot& BloodSplatSlot()
	{
		static FBloodMaterialSlot Slot(TEXT("/Game/Aedir/Materials/M_Blood_Splat.M_Blood_Splat"));
		return Slot;
	}

	FBloodMaterialSlot& BloodDropSlot()
	{
		static FBloodMaterialSlot Slot(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		return Slot;
	}

	bool BloodCanSpawn(const UWorld* World)
	{
		return World && World->GetNetMode() != NM_DedicatedServer;
	}
}

AAedirBloodDrop::AAedirBloodDrop()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = BloodDropLifeSpan;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;

	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}

	Mesh->SetRelativeScale3D(FVector(BloodDropScale));
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// A drop is of a kind nothing else blocks, not even another drop: as world-dynamic they struck each other leaving the
	// wound and all burst in the air within a few hundredths of a second, so no spray was ever seen.
	Mesh->SetCollisionObjectType(ECC_Destructible);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	// So small and so fast, a drop stepped through the ground between two frames and never left a mark: it is swept.
	Mesh->BodyInstance.bUseCCD = true;
	Mesh->SetSimulatePhysics(true);
	Mesh->SetEnableGravity(true);
	Mesh->SetLinearDamping(BloodDropLinearDamping);
	Mesh->SetNotifyRigidBodyCollision(true);
	Mesh->OnComponentHit.AddDynamic(this, &AAedirBloodDrop::OnDropHit);
}

void AAedirBloodDrop::Launch(const FVector& Velocity, UMaterialInterface* Material)
{
	if (Material)
	{
		Mesh->SetMaterial(0, Material);
	}

	Mesh->SetPhysicsLinearVelocity(Velocity);
	LaunchedFrom = GetActorLocation();
}

void AAedirBloodDrop::OnDropHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bSplatted)
	{
		return;
	}

	bSplatted = true;
	UE_LOG(LogAedir, Verbose, TEXT("[Blood] drop hit %s after %.2f s at %s, %.0f cm out along the ground from %s"), *GetNameSafe(OtherActor), GetGameTimeSinceCreation(), *Hit.ImpactPoint.ToCompactString(), FVector::Dist2D(LaunchedFrom, Hit.ImpactPoint), *LaunchedFrom.ToCompactString());

	if (UWorld* World = GetWorld())
	{
		if (UAedirBloodSubsystem* Blood = World->GetSubsystem<UAedirBloodSubsystem>())
		{
			Blood->SpawnSplat(Hit.ImpactPoint, Hit.ImpactNormal, BloodDropHitSplatScale);
		}
	}

	Destroy();
}

AAedirBloodBurst::AAedirBloodBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

void AAedirBloodBurst::Throw(TArray<FAedirBurstDrop>&& Thrown, UMaterialInterface* Material)
{
	Drops = MoveTemp(Thrown);
	Build(true);
	if (Material)
	{
		Mesh->SetMaterial(0, Material);
	}
}

void AAedirBloodBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FVector Gravity(0.f, 0.f, GetWorld() ? GetWorld()->GetGravityZ() : -980.f);
	bool bAny = false;
	for (FAedirBurstDrop& Drop : Drops)
	{
		Drop.Age += DeltaSeconds;
		Drop.Velocity += Gravity * DeltaSeconds;
		Drop.At += Drop.Velocity * DeltaSeconds;
		bAny |= Drop.Age < Drop.Life;
	}
	if (!bAny)
	{
		Destroy();
		return;
	}
	Build(false);
}

void AAedirBloodBurst::Build(bool bFirst)
{
	// Each drop a small eight-sided stone, drawn out the way it goes, shrinking away as its time runs out.
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<int32> Triangles;
	Vertices.Reserve(Drops.Num() * 6);
	Normals.Reserve(Drops.Num() * 6);
	for (const FAedirBurstDrop& Drop : Drops)
	{
		const int32 First = Vertices.Num();
		const float Left = FMath::Clamp(1.f - Drop.Age / FMath::Max(Drop.Life, KINDA_SMALL_NUMBER), 0.f, 1.f);
		const float Radius = Drop.Radius * FMath::Sqrt(Left);
		const FMatrix Frame = FRotationMatrix::MakeFromX(Drop.Velocity.IsNearlyZero() ? FVector::UpVector : Drop.Velocity);
		const float Stretch = 1.f + static_cast<float>(Drop.Velocity.Size()) * AedirBurstStretchPerCmPerSecond;
		const FVector X = Frame.GetUnitAxis(EAxis::X);
		const FVector Y = Frame.GetUnitAxis(EAxis::Y);
		const FVector Z = Frame.GetUnitAxis(EAxis::Z);
		for (const FVector& Way : { X, -X, Y, -Y, Z, -Z })
		{
			const bool bAlong = Vertices.Num() - First < 2;
			Vertices.Add(Drop.At + Way * (Radius * (bAlong ? Stretch : 1.f)));
			Normals.Add(Way);
		}
		if (bFirst)
		{
			static const int32 Faces[24] = { 0, 4, 2, 0, 3, 4, 0, 5, 3, 0, 2, 5, 1, 2, 4, 1, 4, 3, 1, 3, 5, 1, 5, 2 };
			for (const int32 Corner : Faces)
			{
				Triangles.Add(First + Corner);
			}
		}
	}
	const TArray<FVector2D> NoUvs;
	const TArray<FLinearColor> NoColors;
	const TArray<FProcMeshTangent> NoTangents;
	if (bFirst)
	{
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, NoUvs, NoColors, NoTangents, false);
	}
	else
	{
		Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, NoUvs, NoColors, NoTangents);
	}
}

bool UAedirBloodSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UAedirBloodSubsystem::SpawnDrop(const FVector& Location, const FVector& Velocity)
{
	UWorld* World = GetWorld();
	if (!BloodCanSpawn(World))
	{
		return;
	}

	PruneDrops();
	while (Drops.Num() >= MaxDrops)
	{
		if (AAedirBloodDrop* Oldest = Drops[0].Get())
		{
			Oldest->Destroy();
		}

		Drops.RemoveAt(0);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AAedirBloodDrop* Drop = World->SpawnActor<AAedirBloodDrop>(AAedirBloodDrop::StaticClass(), Location, FRotator::ZeroRotator, SpawnParameters);
	if (!Drop)
	{
		return;
	}

	Drop->Launch(Velocity, GetDropMaterial());
	Drops.Add(Drop);
}

void UAedirBloodSubsystem::SpawnSpray(const FVector& Location, const FVector& Direction, float Severity, EAedirWoundType Type, const FVector& Outward)
{
	if (Type == EAedirWoundType::Bruise || !BloodCanSpawn(GetWorld()))
	{
		return;
	}

	FVector SprayDirection = Direction.GetSafeNormal();
	if (SprayDirection.IsNearlyZero())
	{
		SprayDirection = FVector::UpVector;
	}

	// The burst of the moment, on the skin where it opened and off it.
	FActorSpawnParameters BurstParameters;
	BurstParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AAedirBloodBurst* Burst = GetWorld()->SpawnActor<AAedirBloodBurst>(AAedirBloodBurst::StaticClass(), Location, FRotator::ZeroRotator, BurstParameters))
	{
		const FVector Off = Outward.GetSafeNormal();
		const float Share = FMath::Clamp(Severity, 0.f, 1.f);
		TArray<FAedirBurstDrop> Thrown;
		Thrown.SetNum(FMath::Max(FMath::RoundToInt(FMath::Lerp(static_cast<float>(BurstDropsMin), static_cast<float>(BurstDropsMax), Share)), 0));
		for (FAedirBurstDrop& Drop : Thrown)
		{
			Drop.Velocity = Random.VRandCone(Off.IsNearlyZero() ? SprayDirection : Off, FMath::DegreesToRadians(BurstConeDegrees)) * Random.FRandRange(BurstSpeedMin, FMath::Max(BurstSpeedMin, BurstSpeedMax) * FMath::Lerp(BloodSeverityMinSpeedScale, 1.f, Share));
			Drop.Radius = Random.FRandRange(FMath::Min(BurstDropMinCm, BurstDropMaxCm), BurstDropMaxCm);
			Drop.Life = Random.FRandRange(FMath::Min(BurstMinSeconds, BurstMaxSeconds), BurstMaxSeconds);
		}
		Burst->Throw(MoveTemp(Thrown), GetDropMaterial());
	}

	const bool bCut = Type == EAedirWoundType::Cut;
	const int32 DropCount = bCut
		? Random.RandRange(FMath::Min(CutDropsMin, CutDropsMax), FMath::Max(CutDropsMin, CutDropsMax))
		: Random.RandRange(FMath::Min(PunctureDropsMin, PunctureDropsMax), FMath::Max(PunctureDropsMin, PunctureDropsMax));
	const float MinSpeed = bCut ? BloodCutSpeedMin : BloodPunctureSpeedMin;
	const float MaxSpeed = bCut ? BloodCutSpeedMax : BloodPunctureSpeedMax;
	const float SeverityScale = FMath::Lerp(BloodSeverityMinSpeedScale, 1.f, FMath::Clamp(Severity, 0.f, 1.f)) * SpraySpeedShare;
	const float ConeHalfAngleRad = FMath::DegreesToRadians(SprayConeDegrees);

	for (int32 Index = 0; Index < DropCount; ++Index)
	{
		const FVector DropDirection = Random.VRandCone(SprayDirection, ConeHalfAngleRad);
		const float Speed = static_cast<float>(Random.FRandRange(MinSpeed, MaxSpeed)) * SeverityScale;
		// Out of the wound a little first, so a drop does not start inside the body it came from.
		SpawnDrop(Location + DropDirection * BloodDropStartOutCm, DropDirection * Speed);
	}
	UE_LOG(LogAedir, Log, TEXT("[Blood] spray of %d drops at %s towards %s, severity %.2f, %d drops flying"), DropCount, *Location.ToCompactString(), *SprayDirection.ToCompactString(), Severity, Drops.Num());
}

bool UAedirBloodSubsystem::IsBloodAt(const FVector& Location, float RadiusCm)
{
	for (const TWeakObjectPtr<UDecalComponent>& Splat : Splats)
	{
		if (const UDecalComponent* Decal = Splat.Get(); Decal && FVector::DistSquared(Decal->GetComponentLocation(), Location) < FMath::Square(RadiusCm + Decal->DecalSize.Y))
		{
			return true;
		}
	}
	return false;
}

void UAedirBloodSubsystem::SpawnSplat(const FVector& Location, const FVector& Normal, float Scale)
{
	UWorld* World = GetWorld();
	if (!BloodCanSpawn(World))
	{
		return;
	}

	UMaterialInterface* Base = BloodResolveMaterial(BloodSplatSlot());
	if (!Base)
	{
		return;
	}

	PruneSplats();
	while (Splats.Num() >= MaxSplats)
	{
		if (UDecalComponent* Oldest = Splats[0].Get())
		{
			if (AActor* OldestOwner = Oldest->GetOwner())
			{
				OldestOwner->Destroy();
			}
		}

		Splats.RemoveAt(0);
	}

	FVector SurfaceNormal = Normal.GetSafeNormal();
	if (SurfaceNormal.IsNearlyZero())
	{
		SurfaceNormal = FVector::UpVector;
	}

	FRotator Rotation = FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator();
	Rotation.Roll = static_cast<float>(Random.FRandRange(0.0, 360.0));

	const float SplatScale = FMath::Max(Scale, BloodSplatMinScale);
	FVector SplatSize(BloodSplatDepth, BloodSplatRadiusPerScale * SplatScale, BloodSplatRadiusPerScale * SplatScale);

	// Landing on blood already lying on the ground it pools: down a slope it runs on downhill, on the level it spreads.
	FVector Where = Location;
	if (SurfaceNormal.Z > 0.5)
	{
		int32 Under = 0;
		for (const TWeakObjectPtr<UDecalComponent>& Splat : Splats)
		{
			if (const UDecalComponent* Lying = Splat.Get(); Lying && FVector::DistSquared(Lying->GetComponentLocation(), Location) < FMath::Square(PoolRadiusCm + Lying->DecalSize.Y * 0.5))
			{
				++Under;
			}
		}
		if (Under > 0)
		{
			const float Slope = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(SurfaceNormal.Z), -1.f, 1.f)));
			const FVector Downhill = FVector::VectorPlaneProject(FVector::DownVector, SurfaceNormal).GetSafeNormal();
			if (Slope > PoolLevelDegrees && !Downhill.IsNearlyZero())
			{
				const float Steep = FMath::Sin(FMath::DegreesToRadians(Slope)) / FMath::Max(FMath::Sin(FMath::DegreesToRadians(PoolRunSlopeDegrees)), 0.01f);
				const float Run = FMath::Min(PoolRunCmPerDrop * Under * Steep, PoolRunMaxCm) * static_cast<float>(Random.FRandRange(0.6, 1.0));
				Where += Downhill * Run;
				// Drawn out along the way it runs: the decal's Z side lies downhill.
				Rotation = FRotationMatrix::MakeFromXZ(-SurfaceNormal, Downhill).Rotator();
				SplatSize.Z *= 1.f + FMath::Min(Run / FMath::Max(SplatSize.Z * 2.f, 1.f), 2.f);
				SplatSize.Y *= 0.75f;
			}
			else
			{
				SplatSize.Y *= FMath::Min(1.f + PoolSpreadPerDrop * Under, PoolSpreadMax);
				SplatSize.Z *= FMath::Min(1.f + PoolSpreadPerDrop * Under, PoolSpreadMax);
			}
		}
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetScalarParameterValue(FName(TEXT("Fade")), 1.f);

	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, SplatSize, Where, Rotation, 0.f);
	if (!Decal)
	{
		return;
	}

	Decal->SetFadeScreenSize(0.f);
	Decal->SetFadeOut(SplatLifetime * BloodSplatFadeStartFraction, SplatLifetime * BloodSplatFadeDurationFraction, true);
	Splats.Add(Decal);
}

UMaterialInterface* UAedirBloodSubsystem::GetDropMaterial()
{
	if (!DropMaterial)
	{
		if (UMaterialInterface* Base = BloodResolveMaterial(BloodDropSlot()))
		{
			DropMaterial = UMaterialInstanceDynamic::Create(Base, this);
			DropMaterial->SetVectorParameterValue(FName(TEXT("Color")), FLinearColor(0.55f, 0.01f, 0.02f, 1.f));
		}
	}

	return DropMaterial;
}

void UAedirBloodSubsystem::PruneDrops()
{
	Drops.RemoveAll([](const TWeakObjectPtr<AAedirBloodDrop>& Drop)
	{
		return !Drop.IsValid();
	});
}

void UAedirBloodSubsystem::PruneSplats()
{
	Splats.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Splat)
	{
		return !Splat.IsValid();
	});
}
