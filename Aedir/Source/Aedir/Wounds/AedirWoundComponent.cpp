#include "Wounds/AedirWoundComponent.h"

#include "Creature/AedirCreature.h"
#include "Combat/AedirCombatComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "Wounds/AedirSever.h"

#include "Aedir.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "ReferenceSkeleton.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Net/UnrealNetwork.h"
#include "UObject/SoftObjectPath.h"
#include "Wounds/AedirBloodSubsystem.h"

namespace
{
	constexpr float AedirWoundPulseSeconds = 0.8f;
	// How much more than its cutting energy a single blow needs to take off a limb that has not been hurt yet.
	// A cut counts towards taking a limb off from this share of what parts it; the limb gives once its cuts add up to this
	// share, or at once to a single blow of this share; an injured limb needs this share of it.
	constexpr float AedirSeverGoodCutShare = 0.6f;
	constexpr float AedirSeverWearShare = 2.2f;
	constexpr float AedirSeverOneBlowShare = 3.f;
	constexpr float AedirSeverInjuredShare = 0.7f;
	constexpr int32 AedirWoundPulseDrops = 5;
	// A game day passes in 72 real minutes; a bruise is gone in a week of them.
	constexpr float AedirGameDaySeconds = 72.f * 60.f;
	constexpr float AedirBruiseHealDays = 7.f;
	constexpr double AedirWoundBandageSeconds = 60.0;
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

	FWoundMaterialSlot& WoundRunSlot()
	{
		static FWoundMaterialSlot Slot(TEXT("/Game/Aedir/Materials/M_BloodRun.M_BloodRun"));
		return Slot;
	}

	// Blood runs from an open wound down the skin: this wide, this long for the worst wound, at this share of its length a
	// second for a wound bleeding at a point of health a second, and it dries over a minute once the wound has closed.
	constexpr float WoundRunHalfWidthCm = 3.2f;
	constexpr float WoundRunHalfLengthMinCm = 7.f;
	constexpr float WoundRunHalfLengthPerSeverityCm = 11.f;
	constexpr float WoundRunShareBase = 0.03f;
	constexpr float WoundRunSharePerBleed = 0.05f;
	constexpr float WoundRunDrySeconds = 60.f;
	constexpr float WoundRunDepthCm = 10.f;
	constexpr float WoundRunOpenShare = 0.15f;
	constexpr float WoundRunOpenPerSeverity = 0.35f;
	// The material that draws blood on the skin, and how many wounds it holds at once.
	const TCHAR* const WoundSkinBloodPath = TEXT("/Game/Aedir/Materials/M_AedirBloodSkin.M_AedirBloodSkin");
	constexpr int32 WoundSkinBloodSlots = 8;
	TAutoConsoleVariable<int32> CVarAedirSkinBloodLog(TEXT("aedir.SkinBloodLog"), 0, TEXT("Log where each wound's blood is drawn on the skin (1)."));

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
			return FMath::Lerp(2.5f, 7.f, Alpha);
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

	UpdateArterialBleeding(DeltaTime);

	if (!IsDedicatedServer())
	{
		EnsureWoundVisuals();
		UpdateWoundVisuals(DeltaTime);
		if (!ActiveWounds.IsEmpty() || SkinBlood)
		{
			UpdateSkinBlood();
		}
	}
}

void UAedirWoundComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirWoundComponent, ActiveWounds);
	DOREPLIFETIME(UAedirWoundComponent, BrokenLimbs);
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
		return AedirBruiseHealDays * AedirGameDaySeconds;
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

bool UAedirWoundComponent::TrySever(FName HitBone, const FVector& PlanePoint, const FVector& PlaneNormal, const FVector& Push, float EnergyJoules, float ArmorProtection)
{
	const FName Bone = AedirSever::FindSeverBone(HitBone);
	if (Bone.IsNone() || SeveredBones.Contains(Bone) || !IsAuthoritative())
	{
		return false;
	}

	// As in a real fight, a limb is not taken off by any blade that touches it. A cut has to be a good one, a fair share of
	// what parts that joint, and the limb has to have taken enough such cuts before it gives: every edge that bites wears it,
	// a glancing touch does not count. Only a blow far beyond what parts it, a heavy axe swung with the whole body, takes it
	// at once. An injured limb gives sooner; armour turns most of an edge.
	const AAedirCreature* Owner = Cast<AAedirCreature>(GetOwner());
	EAedirLimb Limb;
	const float Injury = Owner && Owner->Combat && UAedirCombatComponent::GetBoneLimb(Bone, Limb) ? Owner->Combat->GetLimbInjury(Limb) : 0.f;
	const float Through = FMath::Max(1.f - ArmorProtection, 0.1f);
	const float Needed = AedirSever::GetSeverJoules(Bone) * FMath::Lerp(1.f, AedirSeverInjuredShare, FMath::Clamp(Injury, 0.f, 1.f)) / Through;
	if (EnergyJoules < Needed * AedirSeverGoodCutShare)
	{
		return false;
	}
	float& Wear = CutWear.FindOrAdd(Bone);
	Wear += EnergyJoules;
	UE_LOG(LogAedir, Log, TEXT("%s: %s cut with %.0f J, worn %.0f of %.0f"), *GetNameSafe(GetOwner()), *Bone.ToString(), EnergyJoules, Wear, Needed * AedirSeverWearShare);
	if (Wear < Needed * AedirSeverWearShare && EnergyJoules < Needed * AedirSeverOneBlowShare)
	{
		return false;
	}

	SeveredBones.Add(Bone);
	MulticastSever(Bone, PlanePoint, PlaneNormal, Push);

	AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	if (!Creature)
	{
		return true;
	}

	const FString Name = Bone.ToString();
	SeverBleedPerSecond += Name.StartsWith(TEXT("neck")) ? 100.f : (Name.StartsWith(TEXT("thigh")) || Name.StartsWith(TEXT("upperarm")) ? 5.f : 2.5f);
	if (Name.StartsWith(TEXT("neck")) && Creature->Stats)
	{
		Creature->Stats->Die();
	}
	else if (Name.Contains(TEXT("arm")) || Name.StartsWith(TEXT("hand")))
	{
		// What that hand held falls with it.
		if (Creature->Interaction)
		{
			Creature->Interaction->DropHeld(Name.EndsWith(TEXT("_l")) ? EAedirHand::Left : EAedirHand::Right);
		}
	}
	else if (Creature->Balance)
	{
		// Nobody stands on what is no longer there.
		Creature->Balance->ApplyBalanceImpact(Push.GetSafeNormal(), 2.f);
		Creature->Balance->SetLegLost();
	}
	UE_LOG(LogAedir, Log, TEXT("%s lost %s to a %.0f J cut"), *Creature->GetName(), *Bone.ToString(), EnergyJoules);
	return true;
}

bool UAedirWoundComponent::BreakLimb(EAedirLimb Limb)
{
	AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	if (!IsAuthoritative() || !Creature || IsLimbBroken(Limb))
	{
		return false;
	}
	BrokenLimbs |= 1 << static_cast<int32>(Limb);
	const bool bArm = Limb == EAedirLimb::LeftArm || Limb == EAedirLimb::RightArm;
	if (bArm && Creature->Interaction)
	{
		// A broken arm cannot hold anything.
		Creature->Interaction->DropHeld(Limb == EAedirLimb::LeftArm ? EAedirHand::Left : EAedirHand::Right);
	}
	else if (!bArm && Creature->Balance)
	{
		// A broken leg buckles under the body and will not carry it up again.
		Creature->Balance->ApplyBalanceImpact(FVector::DownVector, 1.f);
		Creature->Balance->SetLegLost();
	}
	static const TCHAR* const Names[] = { TEXT("left arm"), TEXT("right arm"), TEXT("left leg"), TEXT("right leg") };
	UE_LOG(LogAedir, Log, TEXT("[Bone] %s's %s breaks."), *Creature->GetName(), Names[static_cast<int32>(Limb)]);
	return true;
}

void UAedirWoundComponent::MulticastSever_Implementation(FName Bone, FVector PlanePoint, FVector PlaneNormal, FVector Push)
{
	SeveredBones.Add(Bone);
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && Character->GetNetMode() != NM_DedicatedServer)
	{
		AedirSever::CutLimb(Character->GetMesh(), Bone, PlanePoint, PlaneNormal, Push);
	}
}

void UAedirWoundComponent::UpdateArterialBleeding(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	UAedirStatsComponent* Stats = Owner ? Owner->FindComponentByClass<UAedirStatsComponent>() : nullptr;
	const bool bDead = Stats && Stats->IsDead();
	if (Stats && !bDead && SeverBleedPerSecond > 0.f && IsAuthoritative())
	{
		Stats->ApplyDamage(SeverBleedPerSecond * DeltaTime);
	}

	const ACharacter* Character = Cast<ACharacter>(Owner);
	UWorld* World = GetWorld();
	UAedirBloodSubsystem* Blood = World ? World->GetSubsystem<UAedirBloodSubsystem>() : nullptr;
	const float Total = GetTotalBleedRate() + SeverBleedPerSecond;
	if (!Character || !Blood || Total <= 0.f || IsDedicatedServer())
	{
		return;
	}

	// A heart still beating throws it out in beats from each parted limb.
	PulseTimer -= DeltaTime;
	if (!bDead && PulseTimer <= 0.f && !SeveredBones.IsEmpty())
	{
		PulseTimer = AedirWoundPulseSeconds;
		for (const FName& Bone : SeveredBones)
		{
			const FName Parent = Character->GetMesh()->GetParentBone(Bone);
			const FVector From = Character->GetMesh()->GetBoneLocation(Bone);
			const FVector Out = (From - Character->GetMesh()->GetBoneLocation(Parent)).GetSafeNormal();
			for (int32 Drop = 0; Drop < AedirWoundPulseDrops; ++Drop)
			{
				Blood->SpawnDrop(From, Out * FMath::FRandRange(110.f, 260.f) + FMath::VRand() * 35.f);
			}
		}
	}

}

void UAedirWoundComponent::StopBleedingSome()
{
	SeverBleedPerSecond *= 0.25f;
	// As far along as a wound that has had a minute to clot.
	for (FAedirWound& Wound : ActiveWounds)
	{
		Wound.StartServerTime -= AedirWoundBandageSeconds;
	}
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
		// A bruise fades away; the blood of a cut or a hole dries on the skin and stays.
		const FAedirWound& Wound = ActiveWounds[Index];
		const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));
		if (Wound.Type == EAedirWoundType::Bruise && Age >= GetHealSeconds(Wound.Type, Wound.GetSeverity()))
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
			if (Visuals[Index].Run)
			{
				Visuals[Index].Run->DestroyComponent();
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
		// Wounds and bruises are drawn on the skin itself.
		if (UsesSkinBlood())
		{
			continue;
		}
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
		// An open wound has blood running from it down the skin.
		if (!Visual->Run && Mesh && Wound.Type != EAedirWoundType::Bruise)
		{
			if (UMaterialInterface* RunParent = WoundResolveMaterial(WoundRunSlot()))
			{
				Visual->RunMaterial = UMaterialInstanceDynamic::Create(RunParent, this);
				Visual->RunMaterial->SetScalarParameterValue(TEXT("Seed"), static_cast<float>(Visual->Random.FRandRange(0.0, 10.0)));
				// The cut itself lets out a little at once, more the worse it is; bleeding carries it on from there.
				Visual->RunLength = WoundRunOpenShare + WoundRunOpenPerSeverity * FMath::Clamp(Wound.GetSeverity(), 0.f, 1.f);
				Visual->RunMaterial->SetScalarParameterValue(TEXT("Length01"), Visual->RunLength);
				Visual->Run = CreateRunDecal(Mesh, Wound, Visual->RunMaterial);
			}
		}
		if (Visual->Decal && !FMath::IsNearlyEqual(Visual->DecalRadius, Radius))
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
		// The run creeps further down while the wound bleeds, quicker the more it bleeds, and dries once it has stopped.
		if (Wound.Type != EAedirWoundType::Bruise)
		{
			// The cut itself lets out a little at once, more the worse it is; bleeding carries it on from there.
			Visual->RunLength = FMath::Max(Visual->RunLength, WoundRunOpenShare + WoundRunOpenPerSeverity * FMath::Clamp(Severity, 0.f, 1.f));
			if (Rate > 0.f)
			{
				Visual->RunLength = FMath::Min(Visual->RunLength + DeltaTime * (WoundRunShareBase + Rate * WoundRunSharePerBleed), 1.f);
				Visual->RunWet = 1.f;
			}
			else
			{
				Visual->RunWet = FMath::Max(Visual->RunWet - DeltaTime / WoundRunDrySeconds, 0.f);
			}
		}
		if (Visual->RunMaterial)
		{
			Visual->RunMaterial->SetScalarParameterValue(TEXT("Length01"), Visual->RunLength);
			Visual->RunMaterial->SetScalarParameterValue(TEXT("Wet"), Visual->RunWet);
		}
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

bool UAedirWoundComponent::UsesSkinBlood()
{
	if (!bSkinBloodTried)
	{
		bSkinBloodTried = true;
		USkeletalMeshComponent* Mesh = GetBodyMesh();
		UMaterialInterface* Parent = Mesh ? LoadObject<UMaterialInterface>(nullptr, WoundSkinBloodPath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		UE_CLOG(Mesh && !Parent, LogAedir, Warning, TEXT("[Wounds] %s is missing: open wounds are laid on as before"), WoundSkinBloodPath);
		if (Parent)
		{
			SkinBlood = UMaterialInstanceDynamic::Create(Parent, this);
			Mesh->SetOverlayMaterial(SkinBlood);
		}
	}
	return SkinBlood != nullptr;
}

void UAedirWoundComponent::UpdateSkinBlood()
{
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	if (!Mesh || !UsesSkinBlood() || !Mesh->GetSkinnedAsset())
	{
		return;
	}
	// Where each bone stands in the bind pose, the body's own frame in which the skin is drawn before it moves.
	const FReferenceSkeleton& Skeleton = Mesh->GetSkinnedAsset()->GetRefSkeleton();
	auto BindPose = [&Skeleton](FName Bone)
	{
		FTransform Pose = FTransform::Identity;
		for (int32 Index = Skeleton.FindBoneIndex(Bone); Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
		{
			Pose = Pose * Skeleton.GetRefBonePose()[Index];
		}
		return Pose;
	};

	// The worst wounds and bruises, as many as the material holds.
	TArray<const FAedirWound*, TInlineAllocator<16>> Open;
	for (const FAedirWound& Wound : ActiveWounds)
	{
		Open.Add(&Wound);
	}
	Open.Sort([](const FAedirWound& A, const FAedirWound& B) { return A.GetSeverity() > B.GetSeverity(); });
	const double Now = GetWoundServerTime();
	for (int32 Slot = 0; Slot < WoundSkinBloodSlots; ++Slot)
	{
		FLinearColor Where(0.f, 0.f, 0.f, 0.f);
		FLinearColor How(0.f, 0.f, 0.f, 0.f);
		if (Open.IsValidIndex(Slot))
		{
			const FAedirWound& Wound = *Open[Slot];
			const float Severity = Wound.GetSeverity();
			const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));
			const FVector Point = BindPose(WoundResolveBone(Mesh, Wound.Bone)).TransformPosition(Wound.Offset);
			const FAedirWoundVisual* Visual = FindVisual(Wound.Id);
			// The run creeps down as the wound bleeds, as far as the old laid-on run went.
			const float RunCm = (Visual ? Visual->RunLength : 0.f) * 2.f * (WoundRunHalfLengthMinCm + WoundRunHalfLengthPerSeverityCm * FMath::Clamp(Severity, 0.f, 1.f));
			// A bruise shrinks slowly to nothing over its week as it turns from purple to yellow.
			const float Healed = Wound.Type == EAedirWoundType::Bruise ? FMath::SmoothStep(0.f, 1.f, ComputeAge01(Wound.Type, Severity, Age)) : 0.f;
			Where = FLinearColor(Point.X, Point.Y, Point.Z, WoundDecalRadius(Wound.Type, Severity) * (1.f - Healed));
			UE_CLOG(CVarAedirSkinBloodLog.GetValueOnGameThread() != 0, LogAedir, Log, TEXT("[Wounds] skin blood %d on %s at %s, radius %.1f, run %.1f cm"), Slot, *Wound.Bone.ToString(), *Point.ToCompactString(), Where.A, RunCm);
			// A bruise has no run: the material knows it by a run length below zero.
			const bool bBruise = Wound.Type == EAedirWoundType::Bruise;
			How = FLinearColor(FMath::Max(Severity, 0.2f), ComputeAge01(Wound.Type, Severity, Age), bBruise ? -1.f : RunCm, static_cast<float>(Wound.Id % 17));
		}
		SkinBlood->SetVectorParameterValue(FName(*FString::Printf(TEXT("W%d"), Slot)), Where);
		SkinBlood->SetVectorParameterValue(FName(*FString::Printf(TEXT("D%d"), Slot)), How);
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

UDecalComponent* UAedirWoundComponent::CreateRunDecal(USkeletalMeshComponent* Mesh, const FAedirWound& Wound, UMaterialInstanceDynamic* Material)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Mesh || !Material)
	{
		return nullptr;
	}
	// Laid on the skin with its top at the wound and its length the way down the skin goes as the body stands when struck.
	const FTransform Reference = WoundBoneTransform(Mesh, Wound.Bone);
	const FVector Normal = Wound.Normal.GetSafeNormal();
	FVector Down = FVector::VectorPlaneProject(Reference.InverseTransformVectorNoScale(-FVector::UpVector), Normal).GetSafeNormal();
	if (Down.IsNearlyZero())
	{
		Down = FVector::VectorPlaneProject(Reference.InverseTransformVectorNoScale(-Owner->GetActorForwardVector()), Normal).GetSafeNormal();
	}
	const float HalfLength = WoundRunHalfLengthMinCm + WoundRunHalfLengthPerSeverityCm * FMath::Clamp(Wound.GetSeverity(), 0.f, 1.f);

	UDecalComponent* Decal = NewObject<UDecalComponent>(Owner, NAME_None, RF_Transient);
	Owner->AddInstanceComponent(Decal);
	Decal->SetDecalMaterial(Material);
	Decal->DecalSize = FVector(WoundRunDepthCm, WoundRunHalfWidthCm, HalfLength);
	Decal->SetFadeScreenSize(0.f);
	Decal->RegisterComponent();
	Decal->AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, Wound.Bone);
	// The top of the decal's picture lies towards its own -Z: turned so that end is at the wound and the runs go down.
	Decal->SetRelativeLocationAndRotation(Wound.Offset + Down * HalfLength, FRotationMatrix::MakeFromXZ(-Normal, Down).Rotator());
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
