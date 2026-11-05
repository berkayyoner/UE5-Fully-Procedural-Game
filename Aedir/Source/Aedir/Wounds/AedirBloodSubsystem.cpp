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
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	constexpr float BloodDropScale = 0.012f;
	constexpr float BloodDropLifeSpan = 4.f;
	constexpr float BloodDropLinearDamping = 0.05f;
	constexpr float BloodSprayConeHalfAngleDeg = 25.f;
	constexpr int32 BloodCutDropMin = 6;
	constexpr int32 BloodCutDropMax = 14;
	constexpr int32 BloodPunctureDropMin = 3;
	constexpr int32 BloodPunctureDropMax = 6;
	constexpr float BloodCutSpeedMin = 150.f;
	constexpr float BloodCutSpeedMax = 450.f;
	constexpr float BloodPunctureSpeedMin = 60.f;
	constexpr float BloodPunctureSpeedMax = 180.f;
	constexpr float BloodSeverityMinSpeedScale = 0.6f;
	constexpr float BloodSplatDepth = 6.f;
	constexpr float BloodSplatRadiusPerScale = 8.f;
	constexpr float BloodSplatMinScale = 0.05f;
	constexpr float BloodSplatFadeStartFraction = 0.6f;
	constexpr float BloodSplatFadeDurationFraction = 0.4f;
	constexpr float BloodDropHitSplatScale = 1.f;

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
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
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
}

void AAedirBloodDrop::OnDropHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bSplatted)
	{
		return;
	}

	bSplatted = true;

	if (UWorld* World = GetWorld())
	{
		if (UAedirBloodSubsystem* Blood = World->GetSubsystem<UAedirBloodSubsystem>())
		{
			Blood->SpawnSplat(Hit.ImpactPoint, Hit.ImpactNormal, BloodDropHitSplatScale);
		}
	}

	Destroy();
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

void UAedirBloodSubsystem::SpawnSpray(const FVector& Location, const FVector& Direction, float Severity, EAedirWoundType Type)
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

	const bool bCut = Type == EAedirWoundType::Cut;
	const int32 DropCount = bCut
		? Random.RandRange(BloodCutDropMin, BloodCutDropMax)
		: Random.RandRange(BloodPunctureDropMin, BloodPunctureDropMax);
	const float MinSpeed = bCut ? BloodCutSpeedMin : BloodPunctureSpeedMin;
	const float MaxSpeed = bCut ? BloodCutSpeedMax : BloodPunctureSpeedMax;
	const float SeverityScale = FMath::Lerp(BloodSeverityMinSpeedScale, 1.f, FMath::Clamp(Severity, 0.f, 1.f));
	const float ConeHalfAngleRad = FMath::DegreesToRadians(BloodSprayConeHalfAngleDeg);

	for (int32 Index = 0; Index < DropCount; ++Index)
	{
		const FVector DropDirection = Random.VRandCone(SprayDirection, ConeHalfAngleRad);
		const float Speed = static_cast<float>(Random.FRandRange(MinSpeed, MaxSpeed)) * SeverityScale;
		SpawnDrop(Location, DropDirection * Speed);
	}
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
	const FVector SplatSize(BloodSplatDepth, BloodSplatRadiusPerScale * SplatScale, BloodSplatRadiusPerScale * SplatScale);

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetScalarParameterValue(FName(TEXT("Fade")), 1.f);

	UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(World, Material, SplatSize, Location, Rotation, 0.f);
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
			DropMaterial->SetVectorParameterValue(FName(TEXT("Color")), FLinearColor(0.35f, 0.f, 0.01f, 1.f));
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
