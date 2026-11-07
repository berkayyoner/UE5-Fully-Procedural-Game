#include "Weather/AedirPrecipitationSubsystem.h"

#include "Aedir.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"

namespace
{
	const TCHAR* const PrecipMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirPrecip.M_AedirPrecip");
	const TCHAR* const PrecipPlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	constexpr int32 PrecipRainCount = 2600;
	constexpr int32 PrecipSnowCount = 2200;
	constexpr float PrecipHalfBoxCm = 750.f;
	constexpr float PrecipTopCm = 550.f;
	constexpr float PrecipBottomCm = 300.f;
	constexpr float PrecipRainSpeed = 1100.f;
	constexpr float PrecipSnowSpeed = 130.f;
	constexpr float PrecipRainSideways = 40.f;
	constexpr float PrecipSnowSideways = 60.f;
	constexpr float PrecipWindCmPerShare = 500.f;
	constexpr float PrecipWindYaw = 35.f;
	constexpr float PrecipFadeSpeed = 1.2f;
	constexpr float PrecipRoofSeconds = 0.5f;
	constexpr float PrecipRoofLookCm = 3000.f;
	constexpr float PrecipPlaneCm = 100.f;
	const FVector2f PrecipRainSize(65.f, 1.1f);
	const FVector2f PrecipSnowSize(2.6f, 2.6f);
	const FLinearColor PrecipRainColor(0.55f, 0.6f, 0.68f);
	const FLinearColor PrecipSnowColor(0.85f, 0.88f, 0.92f);
	constexpr float PrecipRainStrength = 0.34f;
	constexpr float PrecipSnowStrength = 0.9f;
}

TStatId UAedirPrecipitationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAedirPrecipitationSubsystem, STATGROUP_Tickables);
}

bool UAedirPrecipitationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UAedirPrecipitationSubsystem::EnsureMeshes()
{
	if (Holder || bFailed)
	{
		return;
	}

	UWorld* World = GetWorld();
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, PrecipPlanePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, PrecipMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Plane || !Base)
	{
		bFailed = true;
		UE_LOG(LogAedir, Warning, TEXT("Rain and snow have nothing to draw with: %s"), PrecipMaterialPath);
		return;
	}

	Holder = World->SpawnActor<AActor>();
	Holder->SetRootComponent(NewObject<USceneComponent>(Holder));
	Holder->GetRootComponent()->RegisterComponent();
	const auto Make = [&](TObjectPtr<UInstancedStaticMeshComponent>& Mesh, TObjectPtr<UMaterialInstanceDynamic>& Material, FFall& Fall, int32 Count, const FLinearColor& Color)
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Material->SetScalarParameterValue(TEXT("Strength"), 0.f);
		Mesh = NewObject<UInstancedStaticMeshComponent>(Holder);
		Mesh->SetStaticMesh(Plane);
		Mesh->SetMaterial(0, Material);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetAbsolute(true, true, true);
		Mesh->SetupAttachment(Holder->GetRootComponent());
		Mesh->RegisterComponent();
		Mesh->SetVisibility(false);
		Fall.Positions.SetNumZeroed(Count);
		Fall.Velocities.SetNumZeroed(Count);
		Fall.Transforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), Count);
		Mesh->AddInstances(Fall.Transforms, false, true);
	};
	Make(RainMesh, RainMaterial, Rain, PrecipRainCount, PrecipRainColor);
	Make(SnowMesh, SnowMaterial, Snow, PrecipSnowCount, PrecipSnowColor);
}

void UAedirPrecipitationSubsystem::Spawn(FFall& Fall, int32 Index, const FVector& Center, bool bAnyHeight, const FVector& Wind, float FallSpeed, float Sideways)
{
	const float Height = bAnyHeight ? Random.FRandRange(-PrecipBottomCm, PrecipTopCm) : PrecipTopCm;
	Fall.Positions[Index] = Center + FVector(Random.FRandRange(-PrecipHalfBoxCm, PrecipHalfBoxCm), Random.FRandRange(-PrecipHalfBoxCm, PrecipHalfBoxCm), Height);
	Fall.Velocities[Index] = Wind + FVector(Random.FRandRange(-Sideways, Sideways), Random.FRandRange(-Sideways, Sideways), -FallSpeed * Random.FRandRange(0.85f, 1.15f));
}

void UAedirPrecipitationSubsystem::Step(FFall& Fall, UInstancedStaticMeshComponent* Mesh, UMaterialInstanceDynamic* Material, float Wanted, float DeltaTime, const FVector& Eye, const FVector& Wind, float FallSpeed, float Sideways, const FVector2f& Size, bool bAlongFall)
{
	Fall.Fade = FMath::FInterpConstantTo(Fall.Fade, bUnderRoof ? 0.f : Wanted, DeltaTime, PrecipFadeSpeed);
	const int32 Count = FMath::RoundToInt(Fall.Positions.Num() * FMath::Clamp(Fall.Fade, 0.f, 1.f));
	const bool bVisible = Count > 0;
	if (Mesh->IsVisible() != bVisible)
	{
		Mesh->SetVisibility(bVisible);
	}
	if (!bVisible && Fall.Active == 0)
	{
		return;
	}

	// Newly needed drops start anywhere in the box, so that a change of weather does not arrive as one sheet from above.
	for (int32 Index = Fall.Active; Index < Count; ++Index)
	{
		Spawn(Fall, Index, Eye, true, Wind, FallSpeed, Sideways);
	}
	for (int32 Index = Count; Index < Fall.Active; ++Index)
	{
		Fall.Transforms[Index].SetScale3D(FVector(0.f));
	}
	const int32 Touched = FMath::Max(Count, Fall.Active);
	Fall.Active = Count;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector& Where = Fall.Positions[Index];
		Where += Fall.Velocities[Index] * DeltaTime;
		const FVector Off = Where - Eye;
		if (Off.Z < -PrecipBottomCm || FMath::Abs(Off.X) > PrecipHalfBoxCm || FMath::Abs(Off.Y) > PrecipHalfBoxCm)
		{
			Spawn(Fall, Index, Eye, false, Wind, FallSpeed, Sideways);
		}
		const FVector ToEye = (Eye - Where).GetSafeNormal();
		const FVector Along = bAlongFall ? Fall.Velocities[Index].GetSafeNormal() : FVector::UpVector;
		Fall.Transforms[Index] = FTransform(FRotationMatrix::MakeFromXZ(Along, ToEye).ToQuat(), Where, FVector(Size.X / PrecipPlaneCm, Size.Y / PrecipPlaneCm, 1.f));
	}
	if (Touched > 0)
	{
		Mesh->BatchUpdateInstancesTransforms(0, TArrayView<const FTransform>(Fall.Transforms.GetData(), Touched), true, true, true);
	}
}

void UAedirPrecipitationSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || DeltaTime <= 0.f)
	{
		return;
	}
	const UAedirWeatherSubsystem* Weather = World->GetSubsystem<UAedirWeatherSubsystem>();
	const AAedirWeatherManager* Manager = Weather ? Weather->GetManager() : nullptr;
	const APlayerController* Viewer = World->GetFirstPlayerController();
	if (!Manager || !Viewer || !Viewer->PlayerCameraManager)
	{
		return;
	}
	const float WantedRain = FMath::Clamp(Manager->GetRainIntensity(), 0.f, 1.f);
	const float WantedSnow = FMath::Clamp(Manager->GetSnowIntensity(), 0.f, 1.f);
	if (WantedRain <= 0.f && WantedSnow <= 0.f && !Holder)
	{
		return;
	}
	EnsureMeshes();
	if (!Holder)
	{
		return;
	}

	const FVector Eye = Viewer->PlayerCameraManager->GetCameraLocation();
	RoofTimer -= DeltaTime;
	if (RoofTimer <= 0.f)
	{
		RoofTimer = PrecipRoofSeconds;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirPrecipRoof), false, Viewer->GetPawn());
		bUnderRoof = World->LineTraceTestByChannel(Eye, Eye + FVector(0.f, 0.f, PrecipRoofLookCm), ECC_Visibility, Params);
	}

	const FVector Wind = FRotator(0.f, PrecipWindYaw, 0.f).Vector() * (Manager->GetWindShare() * PrecipWindCmPerShare);
	Step(Rain, RainMesh, RainMaterial, WantedRain, DeltaTime, Eye, Wind, PrecipRainSpeed, PrecipRainSideways, PrecipRainSize, true);
	Step(Snow, SnowMesh, SnowMaterial, WantedSnow, DeltaTime, Eye, Wind * 0.6f, PrecipSnowSpeed, PrecipSnowSideways, PrecipSnowSize, false);
	RainMaterial->SetScalarParameterValue(TEXT("Strength"), PrecipRainStrength);
	SnowMaterial->SetScalarParameterValue(TEXT("Strength"), PrecipSnowStrength);
}
