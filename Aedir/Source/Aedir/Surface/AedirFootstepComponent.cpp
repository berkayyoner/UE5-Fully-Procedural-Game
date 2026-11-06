#include "Surface/AedirFootstepComponent.h"

#include "Settings/AedirSettingsSubsystem.h"
#include "Animation/AedirGaitComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Surface/AedirPhysicalMaterial.h"
#include "Surface/AedirSurfaceSubsystem.h"

namespace
{
	constexpr float AedirFootGroundOffsetCm = 10.f;
	constexpr float AedirSurfaceQueryInterval = 0.1f;
	constexpr float AedirPlantLiftHigh = 0.15f;
	constexpr float AedirPlantLiftLow = 0.05f;
	constexpr float AedirLandingVolumeMultiplier = 2.f;
	constexpr float AedirWetnessDepositRadiusCm = 25.f;
	constexpr float AedirWetnessDepositFraction = 0.8f;
	constexpr float AedirFootWetnessCarryFactor = 0.65f;
	constexpr float AedirFootWetnessThreshold = 0.1f;
	constexpr float AedirSurfaceWetFootThreshold = 0.3f;
	constexpr float AedirSnowWetnessFactor = 0.6f;
	constexpr float AedirWetSoundThreshold = 0.4f;
	constexpr float AedirDecalWetSurfaceThreshold = 0.2f;
	constexpr float AedirDecalFootWetThreshold = 0.15f;
	constexpr float AedirDecalLifeSpanSeconds = 20.f;
	constexpr float AedirFootstepStrengthDivisor = 200.f;
	constexpr float AedirFootstepSpeedDivisor = 600.f;
}

UAedirFootstepComponent::UAedirFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAedirFootstepComponent::BeginPlay()
{
	Super::BeginPlay();

	CreatureOwner = Cast<AAedirCreature>(GetOwner());
}

void UAedirFootstepComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CreatureOwner)
	{
		return;
	}

	SurfaceQueryTimer += DeltaTime;
	if (SurfaceQueryTimer >= AedirSurfaceQueryInterval)
	{
		SurfaceQueryTimer = 0.f;
		UpdateSurfaceQuery();
	}

	const UAedirGaitComponent* Gait = CreatureOwner->Gait.Get();
	const UCharacterMovementComponent* Movement = CreatureOwner->GetCharacterMovement();
	if (!Gait || !Movement)
	{
		return;
	}

	const bool bLanded = bWasFalling && Movement->IsMovingOnGround();
	bWasFalling = Movement->IsFalling();

	const FAedirGaitState& GaitState = Gait->GetGaitState();
	const float Lift[2] = { GaitState.LeftFootLift, GaitState.RightFootLift };

	if (!GaitState.bProceduralActive)
	{
		PreviousLift[0] = Lift[0];
		PreviousLift[1] = Lift[1];
		return;
	}

	const FVector FootWorld[2] = { GaitState.LeftFootWorld, GaitState.RightFootWorld };
	const float Speed2D = Movement->Velocity.Size2D();

	for (int32 FootIndex = 0; FootIndex < 2; ++FootIndex)
	{
		const bool bPlanted = bLanded || (PreviousLift[FootIndex] > AedirPlantLiftHigh && Lift[FootIndex] <= AedirPlantLiftLow);
		if (bPlanted)
		{
			ProcessFootPlant(FootIndex, FootWorld[FootIndex], Speed2D, bLanded ? AedirLandingVolumeMultiplier : 1.f);
		}

		PreviousLift[FootIndex] = Lift[FootIndex];
	}
}

float UAedirFootstepComponent::GetSurfaceSlipperiness() const
{
	return CachedSlipperiness;
}

float UAedirFootstepComponent::GetSurfaceSpeedMultiplier() const
{
	return CachedSpeedMultiplier;
}

void UAedirFootstepComponent::ProcessFootPlant(int32 FootIndex, const FVector& FootWorld, float Speed2D, float VolumeMultiplier)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector GroundPoint = FootWorld - FVector(0.f, 0.f, AedirFootGroundOffsetCm);

	FHitResult Hit;
	const bool bHit = UAedirSurfaceSubsystem::QuerySurface(World, GroundPoint, CreatureOwner, Hit);
	const FVector Point = bHit ? Hit.ImpactPoint : GroundPoint;
	const FVector Normal = bHit ? Hit.ImpactNormal : FVector::UpVector;
	const UAedirPhysicalMaterial* Material = bHit ? UAedirSurfaceSubsystem::GetAedirMaterial(Hit) : nullptr;

	UAedirSurfaceSubsystem* Subsystem = GetSurfaceSubsystem();
	const bool bAllowsWetness = !Material || Material->bAllowsWetness;

	const float CellWetness = Subsystem ? Subsystem->GetWetness(Point) : 0.f;
	const float Snow = Subsystem ? Subsystem->GetSnow(Point) : 0.f;
	const float SurfaceWetness = bAllowsWetness ? FMath::Max(CellWetness, Snow * AedirSnowWetnessFactor) : 0.f;

	float& Foot = FootWetness[FootIndex];
	if (Subsystem && bAllowsWetness)
	{
		if (SurfaceWetness > AedirSurfaceWetFootThreshold)
		{
			Foot = FMath::Max(Foot, SurfaceWetness);
		}
		else if (Foot > AedirFootWetnessThreshold)
		{
			const float DryRate = Material ? Material->WetnessDryRatePerSecond : DefaultWetnessDryRatePerSecond;
			Subsystem->AddWetnessInRadius(Point, AedirWetnessDepositRadiusCm, Foot * AedirWetnessDepositFraction, DryRate);
			Foot *= AedirFootWetnessCarryFactor;
		}
	}

	const float Strength = CreatureOwner->Stats ? CreatureOwner->Stats->Strength : 0.f;
	const float Volume = FMath::Clamp(0.25f + Speed2D / AedirFootstepSpeedDivisor, 0.2f, 1.3f) * (1.f + Strength / AedirFootstepStrengthDivisor) * VolumeMultiplier;

	OnFootstep.Broadcast(Point, Material, Volume);

	if (!Material || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const bool bWetFootstep = SurfaceWetness > AedirWetSoundThreshold;
	const bool bLeaveDecal = SurfaceWetness > AedirDecalWetSurfaceThreshold || Foot > AedirDecalFootWetThreshold;
	PlayFootEffects(Material, Point, Normal, Speed2D, Volume, bWetFootstep, bLeaveDecal);
}

void UAedirFootstepComponent::PlayFootEffects(const UAedirPhysicalMaterial* Material, const FVector& Location, const FVector& Normal, float Speed2D, float Volume, bool bWetFootstep, bool bLeaveDecal)
{
	if (!Material)
	{
		return;
	}

	const bool bUseWetSound = bWetFootstep && !Material->WetFootstepSound.IsNull();
	const FSoftObjectPath& SoundPath = bUseWetSound ? Material->WetFootstepSound.ToSoftObjectPath() : Material->FootstepSound.ToSoftObjectPath();
	if (USoundBase* Sound = Cast<USoundBase>(ResolveAsset(SoundPath)))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, Volume * UAedirSettingsSubsystem::GetValues().FootstepVolume);
	}

	if (UNiagaraSystem* ParticleSystem = Cast<UNiagaraSystem>(ResolveAsset(Material->FootstepParticle.ToSoftObjectPath())))
	{
		const FRotator EffectRotation = FRotationMatrix::MakeFromZ(Normal).Rotator();
		if (UNiagaraComponent* Effect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ParticleSystem, Location, EffectRotation, FVector(1.f), true, false))
		{
			Effect->SetFloatParameter(TEXT("Speed"), Speed2D);
			Effect->Activate(true);
		}
	}

	if (bLeaveDecal && CreatureOwner && !Material->FootprintDecalMaterial.IsNull())
	{
		if (UMaterialInterface* DecalMaterial = Cast<UMaterialInterface>(ResolveAsset(Material->FootprintDecalMaterial.ToSoftObjectPath())))
		{
			const FVector Forward = FVector::VectorPlaneProject(CreatureOwner->GetActorForwardVector(), Normal).GetSafeNormal();
			const FRotator DecalRotation = FRotationMatrix::MakeFromXZ(-Normal, Forward).Rotator();
			const float CreatureScale = CreatureOwner->GetActorScale3D().X;

			if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(this, DecalMaterial, FVector(8.f, 12.f, 12.f) * CreatureScale, Location, DecalRotation, AedirDecalLifeSpanSeconds))
			{
				Decal->SetFadeScreenSize(0.f);
			}
		}
	}
}

void UAedirFootstepComponent::UpdateSurfaceQuery()
{
	CachedSlipperiness = 0.f;
	CachedSpeedMultiplier = 1.f;

	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = CreatureOwner ? CreatureOwner->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return;
	}

	const FVector FeetLocation = CreatureOwner->GetActorLocation() - FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight());

	FHitResult Hit;
	if (!UAedirSurfaceSubsystem::QuerySurface(World, FeetLocation, CreatureOwner, Hit))
	{
		return;
	}

	const UAedirPhysicalMaterial* Material = UAedirSurfaceSubsystem::GetAedirMaterial(Hit);
	if (!Material)
	{
		return;
	}

	const UAedirSurfaceSubsystem* Subsystem = GetSurfaceSubsystem();
	const float Wetness = (Subsystem && Material->bAllowsWetness) ? Subsystem->GetWetness(Hit.ImpactPoint) : 0.f;
	const float Snow = Subsystem ? Subsystem->GetSnow(Hit.ImpactPoint) : 0.f;

	CachedSlipperiness = FMath::Clamp(Material->Slipperiness + Wetness * Material->WetSlipperinessBonus + Snow * Material->SnowSlipperinessBonus, 0.f, 1.f);
	CachedSpeedMultiplier = Material->SpeedMultiplier;
}

UAedirSurfaceSubsystem* UAedirFootstepComponent::GetSurfaceSubsystem() const
{
	UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UAedirSurfaceSubsystem>() : nullptr;
}

UObject* UAedirFootstepComponent::ResolveAsset(const FSoftObjectPath& Path)
{
	if (Path.IsNull())
	{
		return nullptr;
	}

	if (const TObjectPtr<UObject>* Cached = LoadedAssets.Find(Path))
	{
		return Cached->Get();
	}

	UObject* Asset = Path.TryLoad();
	LoadedAssets.Add(Path, Asset);
	return Asset;
}
