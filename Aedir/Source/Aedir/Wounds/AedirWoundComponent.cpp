#include "Wounds/AedirWoundComponent.h"

#include "Aedir.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "UObject/SoftObjectPath.h"
#include "Wounds/AedirBloodSubsystem.h"

namespace
{
	constexpr float WoundTickIntervalSeconds = 0.25f;
	constexpr int32 WoundMaxCount = 24;
	constexpr float WoundMergeDistance = 8.f;
	constexpr float WoundArmorSeverityFactor = 0.7f;
	constexpr float WoundBluntReferenceEnergy = 90.f;
	constexpr float WoundSlashReferenceEnergy = 70.f;
	constexpr float WoundPierceReferenceEnergy = 60.f;
	constexpr float WoundBluntMinEnergy = 6.f;
	constexpr float WoundBluntMaxArmor = 0.85f;
	constexpr float WoundSlashMinEnergy = 10.f;
	constexpr float WoundSlashMaxArmor = 0.6f;
	constexpr float WoundPierceMinEnergy = 8.f;
	constexpr float WoundPierceMaxArmor = 0.7f;
	constexpr float WoundDecalDepth = 6.f;
	constexpr float WoundDripMinInterval = 0.15f;
	constexpr float WoundDripMaxInterval = 0.6f;
	constexpr float WoundDripMinRate = 0.2f;
	constexpr float WoundDripMaxRate = 2.5f;
	constexpr float WoundDripGravityVelocity = -20.f;
	constexpr float WoundDripCarryScale = 0.3f;

	struct FWoundMaterialSlot
	{
		explicit FWoundMaterialSlot(const TCHAR* Path)
			: Soft(FSoftObjectPath(FString(Path)))
		{
		}

		TSoftObjectPtr<UMaterialInterface> Soft;
		TWeakObjectPtr<UMaterialInterface> Material;
		bool bMissing = false;
	};

	// Loads the material once; a missing asset is logged a single time and the decal is skipped from then on.
	UMaterialInterface* WoundResolveMaterial(FWoundMaterialSlot& Slot)
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
		UE_LOG(LogAedir, Warning, TEXT("Wound material %s is missing, wound decals are skipped"), *Slot.Soft.ToSoftObjectPath().ToString());
		return nullptr;
	}

	FWoundMaterialSlot& WoundBruiseSlot()
	{
		static FWoundMaterialSlot Slot(TEXT("/Game/Aedir/Materials/M_Wound_Bruise.M_Wound_Bruise"));
		return Slot;
	}

	FWoundMaterialSlot& WoundCutSlot()
	{
		static FWoundMaterialSlot Slot(TEXT("/Game/Aedir/Materials/M_Wound_Cut.M_Wound_Cut"));
		return Slot;
	}

	FWoundMaterialSlot& WoundSlotForType(EAedirWoundType Type)
	{
		return Type == EAedirWoundType::Bruise ? WoundBruiseSlot() : WoundCutSlot();
	}

	float WoundReferenceEnergy(EAedirDamageType DamageType)
	{
		switch (DamageType)
		{
		case EAedirDamageType::Slash:
			return WoundSlashReferenceEnergy;
		case EAedirDamageType::Pierce:
			return WoundPierceReferenceEnergy;
		case EAedirDamageType::Blunt:
		default:
			return WoundBluntReferenceEnergy;
		}
	}

	float WoundDecalRadius(EAedirWoundType Type, float Severity)
	{
		const float Alpha = FMath::Clamp(Severity, 0.f, 1.f);
		switch (Type)
		{
		case EAedirWoundType::Bruise:
			return FMath::Lerp(6.f, 16.f, Alpha);
		case EAedirWoundType::Cut:
			return FMath::Lerp(4.f, 10.f, Alpha);
		case EAedirWoundType::Puncture:
		default:
			return 2.5f;
		}
	}

	// Bone that a wound is attached to; NAME_None or an unknown bone falls back to the mesh root.
	FName WoundResolveBone(const USkeletalMeshComponent* Mesh, FName Bone)
	{
		return (!Bone.IsNone() && Mesh->DoesSocketExist(Bone)) ? Bone : NAME_None;
	}

	FTransform WoundBoneTransform(const USkeletalMeshComponent* Mesh, FName Bone)
	{
		const FName ResolvedBone = WoundResolveBone(Mesh, Bone);
		return ResolvedBone.IsNone() ? Mesh->GetComponentTransform() : Mesh->GetSocketTransform(ResolvedBone, RTS_World);
	}
}

UAedirWoundComponent::UAedirWoundComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = WoundTickIntervalSeconds;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void UAedirWoundComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsAuthoritative())
	{
		UpdateBleeding(DeltaTime);
	}

	if (!IsDedicatedServer())
	{
		EnsureWoundVisuals();
		UpdateWoundVisuals(DeltaTime);
	}
}

void UAedirWoundComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirWoundComponent, ActiveWounds);
}

EAedirWoundType UAedirWoundComponent::ClassifyWound(EAedirDamageType DamageType)
{
	switch (DamageType)
	{
	case EAedirDamageType::Slash:
		return EAedirWoundType::Cut;
	case EAedirDamageType::Pierce:
		return EAedirWoundType::Puncture;
	case EAedirDamageType::Blunt:
	default:
		return EAedirWoundType::Bruise;
	}
}

bool UAedirWoundComponent::ShouldCreateWound(EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection)
{
	switch (DamageType)
	{
	case EAedirDamageType::Slash:
		return EnergyJoules >= WoundSlashMinEnergy && ArmorProtection < WoundSlashMaxArmor;
	case EAedirDamageType::Pierce:
		return EnergyJoules >= WoundPierceMinEnergy && ArmorProtection < WoundPierceMaxArmor;
	case EAedirDamageType::Blunt:
	default:
		return EnergyJoules >= WoundBluntMinEnergy && ArmorProtection < WoundBluntMaxArmor;
	}
}

float UAedirWoundComponent::ComputeSeverity(EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection)
{
	const float EnergyAlpha = FMath::Clamp(EnergyJoules / WoundReferenceEnergy(DamageType), 0.f, 1.f);
	const float Armor = FMath::Clamp(ArmorProtection, 0.f, 1.f);

	return FMath::Clamp(EnergyAlpha * (1.f - WoundArmorSeverityFactor * Armor), 0.f, 1.f);
}

float UAedirWoundComponent::GetClotSeconds(EAedirWoundType Type, float Severity)
{
	const float Alpha = FMath::Clamp(Severity, 0.f, 1.f);
	switch (Type)
	{
	case EAedirWoundType::Cut:
		return FMath::Lerp(20.f, 80.f, Alpha);
	case EAedirWoundType::Puncture:
		return FMath::Lerp(35.f, 120.f, Alpha);
	case EAedirWoundType::Bruise:
	default:
		return 0.f;
	}
}

float UAedirWoundComponent::GetHealSeconds(EAedirWoundType Type, float Severity)
{
	const float Alpha = FMath::Clamp(Severity, 0.f, 1.f);
	switch (Type)
	{
	case EAedirWoundType::Cut:
		return FMath::Lerp(240.f, 720.f, Alpha);
	case EAedirWoundType::Puncture:
		return FMath::Lerp(300.f, 840.f, Alpha);
	case EAedirWoundType::Bruise:
	default:
		return FMath::Lerp(120.f, 420.f, Alpha);
	}
}

float UAedirWoundComponent::ComputeBleedRate(EAedirWoundType Type, float Severity, float AgeSeconds)
{
	if (Type == EAedirWoundType::Bruise)
	{
		return 0.f;
	}

	const float ClotSeconds = GetClotSeconds(Type, Severity);
	const float Age = FMath::Max(AgeSeconds, 0.f);
	if (ClotSeconds <= 0.f || Age >= ClotSeconds)
	{
		return 0.f;
	}

	const float BaseRate = (Type == EAedirWoundType::Puncture ? 1.1f : 0.8f) * FMath::Clamp(Severity, 0.f, 1.f);
	return BaseRate * (1.f - Age / ClotSeconds);
}

float UAedirWoundComponent::ComputeAge01(EAedirWoundType Type, float Severity, float AgeSeconds)
{
	const float HealSeconds = GetHealSeconds(Type, Severity);
	if (HealSeconds <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}

	return FMath::Clamp(FMath::Max(AgeSeconds, 0.f) / HealSeconds, 0.f, 1.f);
}

bool UAedirWoundComponent::AddWound(FName Bone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection, const FVector& SprayDirection)
{
	if (!IsAuthoritative() || !ShouldCreateWound(DamageType, EnergyJoules, ArmorProtection))
	{
		return false;
	}

	USkeletalMeshComponent* Mesh = GetBodyMesh();
	if (!Mesh)
	{
		return false;
	}

	const FName ResolvedBone = WoundResolveBone(Mesh, Bone);
	const FTransform Reference = WoundBoneTransform(Mesh, ResolvedBone);
	const FVector LocalOffset = Reference.InverseTransformPosition(WorldLocation);
	FVector LocalNormal = Reference.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();
	if (LocalNormal.IsNearlyZero())
	{
		LocalNormal = FVector::UpVector;
	}

	const EAedirWoundType Type = ClassifyWound(DamageType);
	const float Severity = ComputeSeverity(DamageType, EnergyJoules, ArmorProtection);
	const uint8 AddedSeverity255 = static_cast<uint8>(FMath::RoundToInt(Severity * 255.f));
	const float ServerTime = static_cast<float>(GetWoundServerTime());

	FAedirWound* Existing = ActiveWounds.FindByPredicate([ResolvedBone, Type, &LocalOffset](const FAedirWound& Wound)
	{
		return Wound.Bone == ResolvedBone && Wound.Type == Type && FVector::Dist(Wound.Offset, LocalOffset) <= WoundMergeDistance;
	});

	if (Existing)
	{
		Existing->Severity255 = static_cast<uint8>(FMath::Min(255, static_cast<int32>(Existing->Severity255) + static_cast<int32>(AddedSeverity255)));
		Existing->StartServerTime = ServerTime;
	}
	else
	{
		const uint8 NewId = AllocateWoundId();
		if (ActiveWounds.Num() >= WoundMaxCount)
		{
			ActiveWounds.RemoveAt(0);
		}

		FAedirWound& NewWound = ActiveWounds.AddDefaulted_GetRef();
		NewWound.Id = NewId;
		NewWound.Bone = ResolvedBone;
		NewWound.Offset = LocalOffset;
		NewWound.Normal = LocalNormal;
		NewWound.Type = Type;
		NewWound.Severity255 = AddedSeverity255;
		NewWound.StartServerTime = ServerTime;
	}

	if (!IsDedicatedServer())
	{
		EnsureWoundVisuals();
	}

	FVector Direction = SprayDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = WorldNormal.GetSafeNormal();
	}

	OnWounded.Broadcast(Type, ResolvedBone, Severity);
	MulticastWoundSpray(WorldLocation, Direction, Severity, Type);
	return true;
}

int32 UAedirWoundComponent::GetWoundCount() const
{
	return ActiveWounds.Num();
}

float UAedirWoundComponent::GetTotalBleedRate() const
{
	return ComputeTotalBleedRate(GetWoundServerTime());
}

bool UAedirWoundComponent::IsBleeding() const
{
	return GetTotalBleedRate() > 0.f;
}

bool UAedirWoundComponent::IsAuthoritative() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

bool UAedirWoundComponent::IsDedicatedServer() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_DedicatedServer;
}

USkeletalMeshComponent* UAedirWoundComponent::GetBodyMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

double UAedirWoundComponent::GetWoundServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		const double ServerTime = GameState->GetServerWorldTimeSeconds();
		if (ServerTime > 0.0)
		{
			return ServerTime;
		}
	}

	return World->GetTimeSeconds();
}

float UAedirWoundComponent::ComputeTotalBleedRate(double Now) const
{
	float Total = 0.f;
	for (const FAedirWound& Wound : ActiveWounds)
	{
		Total += ComputeBleedRate(Wound.Type, Wound.GetSeverity(), static_cast<float>(Now - Wound.StartServerTime));
	}

	return Total;
}

uint8 UAedirWoundComponent::AllocateWoundId()
{
	for (int32 Attempt = 0; Attempt < 256; ++Attempt)
	{
		const uint8 Candidate = NextWoundId++;
		if (!ActiveWounds.ContainsByPredicate([Candidate](const FAedirWound& Wound) { return Wound.Id == Candidate; }))
		{
			return Candidate;
		}
	}

	return NextWoundId;
}

void UAedirWoundComponent::UpdateBleeding(float DeltaTime)
{
	const double Now = GetWoundServerTime();

	for (int32 Index = ActiveWounds.Num() - 1; Index >= 0; --Index)
	{
		const FAedirWound& Wound = ActiveWounds[Index];
		const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));
		if (Age >= GetHealSeconds(Wound.Type, Wound.GetSeverity()))
		{
			ActiveWounds.RemoveAt(Index);
		}
	}

	const float BleedRate = ComputeTotalBleedRate(Now);
	const AActor* Owner = GetOwner();
	if (BleedRate <= 0.f || !Owner)
	{
		return;
	}

	UAedirStatsComponent* Stats = Owner->FindComponentByClass<UAedirStatsComponent>();
	if (Stats && !Stats->IsDead())
	{
		Stats->ApplyDamage(BleedRate * DeltaTime);
	}
}

void UAedirWoundComponent::EnsureWoundVisuals()
{
	for (int32 Index = Visuals.Num() - 1; Index >= 0; --Index)
	{
		const uint8 VisualId = Visuals[Index].Id;
		const bool bStillWounded = ActiveWounds.ContainsByPredicate([VisualId](const FAedirWound& Wound) { return Wound.Id == VisualId; });
		if (!bStillWounded)
		{
			if (Visuals[Index].Decal)
			{
				Visuals[Index].Decal->DestroyComponent();
			}

			Visuals.RemoveAt(Index);
		}
	}

	USkeletalMeshComponent* Mesh = GetBodyMesh();

	for (const FAedirWound& Wound : ActiveWounds)
	{
		FAedirWoundVisual* Visual = FindVisual(Wound.Id);
		if (!Visual)
		{
			FAedirWoundVisual& NewVisual = Visuals.AddDefaulted_GetRef();
			NewVisual.Id = Wound.Id;
			NewVisual.Random = FRandomStream(static_cast<int32>(Wound.Id) + 1);
			Visual = &NewVisual;
		}

		const float Radius = WoundDecalRadius(Wound.Type, Wound.GetSeverity());
		if (!Visual->Decal)
		{
			UMaterialInterface* Parent = Mesh ? WoundResolveMaterial(WoundSlotForType(Wound.Type)) : nullptr;
			if (Parent)
			{
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
				const float Roll = static_cast<float>(Visual->Random.FRandRange(0.0, 360.0));
				Visual->Material = Material;
				Visual->Decal = CreateWoundDecal(Mesh, Wound, Material, Radius, Roll);
				Visual->DecalRadius = Radius;
			}
		}
		else if (!FMath::IsNearlyEqual(Visual->DecalRadius, Radius))
		{
			Visual->Decal->DecalSize = FVector(WoundDecalDepth, Radius, Radius);
			Visual->Decal->MarkRenderStateDirty();
			Visual->DecalRadius = Radius;
		}
	}
}

void UAedirWoundComponent::UpdateWoundVisuals(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	UWorld* World = GetWorld();
	UAedirBloodSubsystem* Blood = World ? World->GetSubsystem<UAedirBloodSubsystem>() : nullptr;
	if (!Owner || !Mesh)
	{
		return;
	}

	const double Now = GetWoundServerTime();

	for (const FAedirWound& Wound : ActiveWounds)
	{
		FAedirWoundVisual* Visual = FindVisual(Wound.Id);
		if (!Visual)
		{
			continue;
		}

		const float Severity = Wound.GetSeverity();
		const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));

		if (Visual->Material)
		{
			Visual->Material->SetScalarParameterValue(FName(TEXT("Severity")), Severity);
			Visual->Material->SetScalarParameterValue(FName(TEXT("Age01")), ComputeAge01(Wound.Type, Severity, Age));
		}

		const float Rate = ComputeBleedRate(Wound.Type, Severity, Age);
		if (Rate <= 0.f || Wound.Type == EAedirWoundType::Bruise || !Blood)
		{
			continue;
		}

		Visual->DripTimer -= DeltaTime;
		if (Visual->DripTimer > 0.f)
		{
			continue;
		}

		const float IntervalSeconds = static_cast<float>(Visual->Random.FRandRange(WoundDripMinInterval, WoundDripMaxInterval));
		Visual->DripTimer = IntervalSeconds / FMath::Clamp(Rate, WoundDripMinRate, WoundDripMaxRate);

		const FTransform Reference = WoundBoneTransform(Mesh, Wound.Bone);
		const FVector Location = Reference.TransformPosition(Wound.Offset);
		const FVector Velocity = FVector(0.f, 0.f, WoundDripGravityVelocity) + Owner->GetVelocity() * WoundDripCarryScale;
		Blood->SpawnDrop(Location, Velocity);
	}
}

FAedirWoundVisual* UAedirWoundComponent::FindVisual(uint8 Id)
{
	return Visuals.FindByPredicate([Id](const FAedirWoundVisual& Visual) { return Visual.Id == Id; });
}

UDecalComponent* UAedirWoundComponent::CreateWoundDecal(USkeletalMeshComponent* Mesh, const FAedirWound& Wound, UMaterialInstanceDynamic* Material, float Radius, float Roll)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Mesh || !Material)
	{
		return nullptr;
	}

	UDecalComponent* Decal = NewObject<UDecalComponent>(Owner, NAME_None, RF_Transient);
	Owner->AddInstanceComponent(Decal);
	Decal->SetDecalMaterial(Material);
	Decal->DecalSize = FVector(WoundDecalDepth, Radius, Radius);
	Decal->RegisterComponent();
	Decal->AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, Wound.Bone);

	const FVector LocalNormal = Wound.Normal;
	FRotator Rotation = FRotationMatrix::MakeFromX(-LocalNormal).Rotator();
	Rotation.Roll = Roll;
	Decal->SetRelativeLocationAndRotation(Wound.Offset, Rotation);

	return Decal;
}

void UAedirWoundComponent::OnRep_Wounds()
{
	if (!IsDedicatedServer())
	{
		EnsureWoundVisuals();
	}
}

void UAedirWoundComponent::MulticastWoundSpray_Implementation(FVector Location, FVector Direction, float Severity, EAedirWoundType Type)
{
	if (IsDedicatedServer())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (UAedirBloodSubsystem* Blood = World ? World->GetSubsystem<UAedirBloodSubsystem>() : nullptr)
	{
		Blood->SpawnSpray(Location, Direction, Severity, Type);
	}
}
