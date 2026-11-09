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
#include "Graphics/AedirFoliageSubsystem.h"
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
	constexpr float AedirStepDirtStrength = 0.5f;
	// How deep a print is projected: just the ground's own unevenness, so it does not climb plants and stones beside it.
	constexpr float AedirStepPrintDepthCm = 4.f;
	constexpr float AedirStepHalfWidthCm = 6.f;
	constexpr float AedirStepHalfLengthCm = 14.f;
	constexpr float AedirStepForwardCm = 8.f;
	constexpr float AedirStepLifeSeconds = 45.f;
	constexpr float AedirStepRunSpeed = 380.f;
	constexpr float AedirStepSnowShare = 0.3f;
	constexpr float AedirStepSoundShare = 0.35f;
	constexpr float AedirStepGrassRadiusCm = 18.f;
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
			bStepIsLanding = bLanded;
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

	StepGround = bHit ? Hit.GetComponent() : nullptr;
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

	FLinearColor Color(0.11f, 0.085f, 0.06f);
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
		if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(this, Base, FVector(AedirStepPrintDepthCm, AedirStepHalfWidthCm, AedirStepHalfLengthCm) * Scale, PrintPoint + Forward * (AedirStepForwardCm * Scale), FRotationMatrix::MakeFromXZ(-PrintNormal, Forward).Rotator(), AedirStepLifeSeconds))
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

	// What the ground sounds like, when the surface has no set of its own: boots on dirt, stone or wood, crunching in snow,
	// sloshing when wet; a walk, a run or a landing.
	const EPhysicalSurface Surface = Physical ? Physical->SurfaceType.GetValue() : SurfaceType_Default;
	const UAedirPhysicalMaterial* Own = Cast<UAedirPhysicalMaterial>(Physical);
	const UAedirSurfaceSubsystem* Ground = GetSurfaceSubsystem();
	const float Snow = Ground ? Ground->GetSnow(Point) : 0.f;
	const bool bSnow = Snow > AedirStepSnowShare;
	const bool bWet = !bSnow && SurfaceWetness > AedirWetSoundThreshold;
	if (!Own || Own->FootstepSound.IsNull())
	{
		static const TCHAR* const Families[] = { TEXT("Dirt/Footstep_Dirt_Boots"), TEXT("Leaves/Footstep_FootstepLeaves_Boots"), TEXT("Concrete/Footstep_Concrete_Boots"), TEXT("Wood/Footstep_Wood_Boots") };
		const FString Family = bSnow ? TEXT("Snow/Footstep_Snow") : bWet ? TEXT("Slush/Footstep_Slush_Boots") : Surface != SurfaceType_Default ? Families[FMath::Clamp(static_cast<int32>(Surface), 0, 3)] : GuessStepFamily(Point);
		const TCHAR* Gait = bStepIsLanding ? TEXT("Land") : Speed2D > AedirStepRunSpeed ? TEXT("Run") : TEXT("Walk");
		if (USoundBase* Sound = FindStepSound(Family, Gait))
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, Point, Volume * AedirStepSoundShare * UAedirSettingsSubsystem::GetValues().FootstepVolume);
		}
	}

	// The ground the foot came down on gives a little: dust and bits of dirt, a spray of snow or of water. Faster steps throw more.
	const TCHAR* SystemName = bSnow ? TEXT("PSN_SnowLight_Surface") : bWet ? TEXT("PSN_WaterLight_Surface") : Surface == SurfaceType1 ? TEXT("PSN_Grass_Surface") : TEXT("PSN_Dirt_Surface");
	if (UNiagaraSystem* System = FindStepEffect(SystemName))
	{
		// Every step stirs a little, more the faster it is; a landing most.
		const float Pace = FMath::Clamp(Speed2D / AedirStepRunSpeed, 0.f, 1.5f);
		const float Scale = FMath::Lerp(StepEffectWalkScale, StepEffectRunScale, FMath::Square(FMath::Min(Pace, 1.f))) * (bStepIsLanding ? 1.6f : 1.f);
		const float Chance = FMath::Lerp(StepEffectWalkChance, StepEffectRunChance, FMath::Clamp(Pace, 0.f, 1.f));
		if (bStepIsLanding || FMath::FRand() < Chance)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Point, FRotationMatrix::MakeFromZ(Normal).Rotator(), FVector(Scale));
		}
	}
}

FString UAedirFootstepComponent::GuessStepFamily(const FVector& Point) const
{
	// Grass underfoot rustles whatever lies beneath it.
	const UAedirFoliageSubsystem* Foliage = GetWorld() ? GetWorld()->GetSubsystem<UAedirFoliageSubsystem>() : nullptr;
	if (Foliage && Foliage->IsGrassAt(Point, AedirStepGrassRadiusCm))
	{
		return TEXT("Leaves/Footstep_FootstepLeaves_Boots");
	}

	// A surface without a type of its own is told by what its material is called.
	const UPrimitiveComponent* Ground = StepGround.Get();
	const UMaterialInterface* Material = Ground ? Ground->GetMaterial(0) : nullptr;
	const FString Name = Material ? Material->GetName() : FString();
	struct FFamilyWord { const TCHAR* Word; const TCHAR* Family; };
	static const FFamilyWord Words[] =
	{
		{ TEXT("Wood"), TEXT("Wood/Footstep_Wood_Boots") }, { TEXT("Plank"), TEXT("Wood/Footstep_Wood_Boots") },
		{ TEXT("Gravel"), TEXT("Gravel/Footstep_Gravel_Boots") }, { TEXT("Sand"), TEXT("Sand/Footstep_Sand_Boots") },
		{ TEXT("Metal"), TEXT("Metal/Footstep_Metal_Boots") }, { TEXT("Grass"), TEXT("Leaves/Footstep_FootstepLeaves_Boots") },
		{ TEXT("Moss"), TEXT("Leaves/Footstep_FootstepLeaves_Boots") }, { TEXT("Leaves"), TEXT("Leaves/Footstep_FootstepLeaves_Boots") },
		{ TEXT("Stone"), TEXT("Concrete/Footstep_Concrete_Boots") }, { TEXT("Rock"), TEXT("Concrete/Footstep_Concrete_Boots") },
		{ TEXT("Cobble"), TEXT("Concrete/Footstep_Concrete_Boots") }, { TEXT("Concrete"), TEXT("Concrete/Footstep_Concrete_Boots") },
		{ TEXT("Tile"), TEXT("Concrete/Footstep_Concrete_Boots") }, { TEXT("Grid"), TEXT("Concrete/Footstep_Concrete_Boots") },
		{ TEXT("Floor"), TEXT("Concrete/Footstep_Concrete_Boots") }, { TEXT("Brick"), TEXT("Concrete/Footstep_Concrete_Boots") },
	};
	for (const FFamilyWord& Entry : Words)
	{
		if (Name.Contains(Entry.Word))
		{
			return Entry.Family;
		}
	}
	return TEXT("Dirt/Footstep_Dirt_Boots");
}

USoundBase* UAedirFootstepComponent::FindStepSound(const FString& Family, const TCHAR* Gait)
{
	// The cues are numbered by how many takes they shuffle, so the first number that exists is the one.
	const FString Key = Family + Gait;
	static TMap<FString, TWeakObjectPtr<USoundBase>> Found;
	if (const TWeakObjectPtr<USoundBase>* Known = Found.Find(Key); Known && Known->IsValid())
	{
		return Known->Get();
	}
	const FString Base = FPaths::GetCleanFilename(Family);
	for (int32 Takes = 1; Takes <= 8; ++Takes)
	{
		const FString Name = FString::Printf(TEXT("%s_%s_%d_Cue"), *Base, Gait, Takes);
		if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Essential_Foosteps_SK/CUE/%s/%s.%s"), *FPaths::GetPath(Family), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Found.Add(Key, Sound);
			return Sound;
		}
	}
	return nullptr;
}

UNiagaraSystem* UAedirFootstepComponent::FindStepEffect(const TCHAR* Name)
{
	static TMap<FString, TWeakObjectPtr<UNiagaraSystem>> Found;
	if (const TWeakObjectPtr<UNiagaraSystem>* Known = Found.Find(Name); Known && Known->IsValid())
	{
		return Known->Get();
	}
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("/Game/A_Surface_Footstep/Niagara_FX/ParticleSystems/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	Found.Add(Name, System);
	return System;
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
