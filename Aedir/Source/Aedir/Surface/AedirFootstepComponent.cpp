#include "Surface/AedirFootstepComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Wounds/AedirBloodSubsystem.h"

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
	constexpr float AedirStepBloodRadiusCm = 12.f;
	constexpr float AedirStepBloodReachCm = 30.f;
	constexpr float AedirStepCarryShare = 0.78f;
	constexpr float AedirStepDirtStrength = 0.8f;
	constexpr float AedirStepHalfWidthCm = 6.f;
	constexpr float AedirStepHalfLengthCm = 14.f;
	constexpr float AedirStepForwardCm = 8.f;
	constexpr float AedirStepLifeSeconds = 45.f;
	constexpr float AedirStepDustSpeed = 380.f;
	constexpr float AedirStepDustScale = 0.18f;
	constexpr float AedirStepDustSeconds = 0.25f;
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
		// A foot that has been up comes down once, however slowly it settles.
		if (Lift[FootIndex] > AedirPlantLiftHigh)
		{
			bFootRaised[FootIndex] = true;
		}
		const bool bPlanted = bLanded || (bFootRaised[FootIndex] && Lift[FootIndex] <= AedirPlantLiftLow);
		if (bPlanted)
		{
			bFootRaised[FootIndex] = false;
		}
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

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	LeaveStepMarks(FootIndex, Point, Normal, Speed2D, Volume, SurfaceWetness, bHit ? Hit.PhysMaterial.Get() : nullptr);

	if (!Material)
	{
		return;
	}

	const bool bWetFootstep = SurfaceWetness > AedirWetSoundThreshold;
	const bool bLeaveDecal = SurfaceWetness > AedirDecalWetSurfaceThreshold || Foot > AedirDecalFootWetThreshold;
	PlayFootEffects(Material, Point, Normal, Speed2D, Volume, bWetFootstep, bLeaveDecal);
}

void UAedirFootstepComponent::LeaveStepMarks(int32 FootIndex, const FVector& Point, const FVector& Normal, float Speed2D, float Volume, float SurfaceWetness, const UPhysicalMaterial* Physical)
{
	UWorld* World = GetWorld();
	const int32 Foot = FMath::Clamp(FootIndex, 0, 1);

	// Stepping in it wets the sole, and each step leaves less of it.
	UAedirBloodSubsystem* Blood = World->GetSubsystem<UAedirBloodSubsystem>();
	if (Blood && Blood->IsBloodAt(Point, AedirStepBloodRadiusCm + AedirStepBloodReachCm))
	{
		BloodOnFoot[Foot] = 1.f;
	}
	if (SurfaceWetness > AedirDecalWetSurfaceThreshold)
	{
		WaterOnFoot[Foot] = 1.f;
	}

	FLinearColor Color(0.06f, 0.045f, 0.03f);
	float Strength = AedirStepDirtStrength;
	float Roughness = 0.95f;
	if (BloodOnFoot[Foot] > 0.02f)
	{
		Color = FLinearColor(0.2f, 0.006f, 0.006f);
		Strength = FMath::Lerp(0.2f, 0.95f, BloodOnFoot[Foot]);
		Roughness = 0.35f;
		BloodOnFoot[Foot] *= AedirStepCarryShare;
	}
	else if (WaterOnFoot[Foot] > 0.02f)
	{
		Color = FLinearColor(0.02f, 0.02f, 0.022f);
		Strength = FMath::Lerp(0.15f, 0.6f, WaterOnFoot[Foot]);
		Roughness = 0.12f;
		WaterOnFoot[Foot] *= AedirStepCarryShare;
	}

	// The plant is told from the foot in the air, so the print finds its ground for itself.
	FVector PrintPoint = Point;
	FVector PrintNormal = Normal;
	FHitResult GroundHit;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(AedirStepPrint), false, CreatureOwner);
	if (World->LineTraceSingleByChannel(GroundHit, Point + FVector(0.f, 0.f, 60.f), Point - FVector(0.f, 0.f, 120.f), ECC_Visibility, GroundParams))
	{
		PrintPoint = GroundHit.ImpactPoint;
		PrintNormal = GroundHit.ImpactNormal;
	}

	static TWeakObjectPtr<UMaterialInterface> StepMaterial;
	if (!StepMaterial.IsValid())
	{
		StepMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aedir/Materials/M_Footprint.M_Footprint"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (UMaterialInterface* Base = StepMaterial.Get())
	{
		const FVector Forward = FVector::VectorPlaneProject(CreatureOwner->GetActorForwardVector(), PrintNormal).GetSafeNormal();
		const float Scale = static_cast<float>(CreatureOwner->GetActorScale3D().Z);
		if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(this, Base, FVector(14.f, AedirStepHalfWidthCm, AedirStepHalfLengthCm) * Scale, PrintPoint + Forward * (AedirStepForwardCm * Scale), FRotationMatrix::MakeFromXZ(-PrintNormal, Forward).Rotator(), AedirStepLifeSeconds))
		{
			Decal->SetFadeScreenSize(0.f);
			if (UMaterialInstanceDynamic* Print = Decal->CreateDynamicMaterialInstance())
			{
				Print->SetVectorParameterValue(TEXT("Color"), Color);
				Print->SetScalarParameterValue(TEXT("Strength"), Strength);
				Print->SetScalarParameterValue(TEXT("Roughness"), Roughness);
				// The sole is drawn as a left one; the right foot gets it mirrored.
				Print->SetScalarParameterValue(TEXT("Mirror"), Foot == 1 ? 1.f : 0.f);
			}
		}
	}

	// What the ground sounds like, when the surface has no set of its own.
	const EPhysicalSurface Surface = Physical ? Physical->SurfaceType.GetValue() : SurfaceType_Default;
	const UAedirPhysicalMaterial* Own = Cast<UAedirPhysicalMaterial>(Physical);
	if (!Own || Own->FootstepSound.IsNull())
	{
		struct FStepSounds { const TCHAR* Family; int32 Count; };
		static const FStepSounds Sets[] = { { TEXT("Dirt"), 6 }, { TEXT("Grass"), 13 }, { TEXT("Cobbles"), 7 }, { TEXT("Wood"), 9 } };
		const FStepSounds& Set = Sets[FMath::Clamp(static_cast<int32>(Surface), 0, 3)];
		const FString Name = FString::Printf(TEXT("sfx_Foley_Fts_%s_%02d"), Set.Family, FMath::RandRange(1, Set.Count));
		if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/_Content/SFX/Footsteps/wav/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, Point, Volume * UAedirSettingsSubsystem::GetValues().FootstepVolume);
		}
	}

	// A run kicks up the dry ground.
	if (Speed2D > AedirStepDustSpeed && SurfaceWetness < AedirDecalWetSurfaceThreshold)
	{
		static TWeakObjectPtr<UNiagaraSystem> Dust;
		if (!Dust.IsValid())
		{
			Dust = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_WellSmoke.NS_WellSmoke"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (UNiagaraSystem* System = Dust.Get())
		{
			if (UNiagaraComponent* Puff = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Point, FRotator::ZeroRotator, FVector(AedirStepDustScale)))
			{
				FTimerHandle StopPuff;
				World->GetTimerManager().SetTimer(StopPuff, FTimerDelegate::CreateWeakLambda(Puff, [Puff]() { Puff->Deactivate(); }), AedirStepDustSeconds, false);
			}
		}
	}
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
