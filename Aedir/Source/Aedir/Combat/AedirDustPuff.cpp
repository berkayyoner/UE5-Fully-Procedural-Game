#include "Combat/AedirDustPuff.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* const DustPuffPlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const DustPuffMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirPrecip.M_AedirPrecip");
	constexpr float DustPuffPlaneCm = 100.f;
}

AAedirDustPuff::AAedirDustPuff()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorEnableCollision(false);
	Puffs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Puffs"));
	Puffs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Puffs->SetCastShadow(false);
	Puffs->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Puffs);
}

void AAedirDustPuff::Spawn(UWorld* World, const FVector& Point, const FVector& Normal, float Strength)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AAedirDustPuff* Puff = World->SpawnActor<AAedirDustPuff>(AAedirDustPuff::StaticClass(), FTransform(Point), Params))
	{
		Puff->Begin(Normal.GetSafeNormal(), FMath::Clamp(Strength, 0.f, 1.f));
	}
}

void AAedirDustPuff::Begin(const FVector& Normal, float Strength)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, DustPuffPlanePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, DustPuffMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Plane || !Base)
	{
		Destroy();
		return;
	}
	Amount = FMath::Lerp(0.5f, 1.f, Strength);
	Material = UMaterialInstanceDynamic::Create(Base, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Material->SetScalarParameterValue(TEXT("Strength"), Opacity * Amount);
	Puffs->SetStaticMesh(Plane);
	Puffs->SetMaterial(0, Material);
	// Thrown back off the skin the way the blow came, spread wide, a little rising.
	const int32 Count = FMath::Max(1, FMath::RoundToInt(Specks * Amount));
	TArray<FTransform> Start;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Spread = FMath::VRandCone(Normal, FMath::DegreesToRadians(70.f));
		Speeds.Add((Spread + FVector(0.f, 0.f, 0.25f)) * ThrowSpeed * Amount * FMath::FRandRange(0.4f, 1.f));
		Places.Add(Normal * 3.f + FMath::VRand() * 2.f);
		Sizes.Add(FMath::FRandRange(0.7f, 1.3f));
		Start.Add(FTransform(FQuat::Identity, Places.Last(), FVector(0.f)));
	}
	Puffs->AddInstances(Start, false, false);
}

void AAedirDustPuff::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float Life = FMath::Max(LifeSeconds * FMath::Lerp(0.7f, 1.f, Amount), 0.05f);
	if (Age >= Life || !Material)
	{
		Destroy();
		return;
	}
	const float T = Age / Life;
	// Thick at once, thinning to nothing.
	Material->SetScalarParameterValue(TEXT("Strength"), Opacity * Amount * FMath::Square(1.f - T));
	FVector Eye = GetActorLocation() + FVector(100.f, 0.f, 0.f);
	if (const APlayerController* Viewer = GetWorld()->GetFirstPlayerController(); Viewer && Viewer->PlayerCameraManager)
	{
		Eye = Viewer->PlayerCameraManager->GetCameraLocation();
	}
	const FVector Middle = GetActorLocation();
	const FQuat Facing = FRotationMatrix::MakeFromZ((Eye - Middle).GetSafeNormal()).ToQuat();
	const float Grow = FMath::Lerp(StartSizeCm, EndSizeCm * FMath::Lerp(0.6f, 1.f, Amount), 1.f - FMath::Square(1.f - T));
	TArray<FTransform> Now;
	for (int32 Index = 0; Index < Places.Num(); ++Index)
	{
		Speeds[Index] *= FMath::Exp(-Drag * DeltaSeconds);
		Places[Index] += Speeds[Index] * DeltaSeconds;
		const float Size = Grow * Sizes[Index] / DustPuffPlaneCm;
		Now.Add(FTransform(Facing, Places[Index], FVector(Size, Size, 1.f)));
	}
	Puffs->BatchUpdateInstancesTransforms(0, Now, false, true, true);
}
