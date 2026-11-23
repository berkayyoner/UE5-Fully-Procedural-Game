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
#include "Engine/Texture2D.h"
#include "RenderUtils.h"
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
#include "Items/AedirItemActor.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	constexpr float AedirWoundPulseSeconds = 0.8f;
	// How much more than its cutting energy a single blow needs to take off a limb that has not been hurt yet.
	// A cut counts towards taking a limb off from this share of what parts it; the limb gives once its cuts add up to this
	// share, or at once to a single blow of this share; an injured limb needs this share of it.
	constexpr float AedirSeverGoodCutShare = 0.85f;
	constexpr float AedirSeverWearShare = 3.5f;
	constexpr float AedirSeverOneBlowShare = 4.f;
	constexpr float AedirSeverInjuredShare = 0.7f;
	constexpr int32 AedirWoundPulseDrops = 5;
	// A game day passes in 72 real minutes; a bruise is gone in a week of them.
	constexpr float AedirGameDaySeconds = 72.f * 60.f;
	constexpr float AedirBruiseHealDays = 7.f;
	constexpr double AedirWoundBandageSeconds = 60.0;
	constexpr float WoundTickIntervalSeconds = 0.25f;
	// Wound ids are one byte, so no more than this can be told apart.
	constexpr int32 WoundMaxCount = 250;
	constexpr float WoundMergeDistance = 8.f;
	constexpr float WoundArmorSeverityFactor = 0.7f;
	constexpr float WoundBluntReferenceEnergy = 90.f;
	constexpr float WoundSlashReferenceEnergy = 150.f;
	constexpr float WoundPierceReferenceEnergy = 130.f;
	constexpr float WoundBluntMinEnergy = 6.f;
	constexpr float WoundBluntMaxArmor = 0.85f;
	constexpr float WoundSlashMinEnergy = 20.f;
	constexpr float WoundSlashMaxArmor = 0.6f;
	constexpr float WoundPierceMinEnergy = 18.f;
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
	// The skin material's data texture is this wide: one texel for each wound it can draw.
	constexpr int32 WoundSkinBloodSlots = 256;
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

	float WoundDecalRadius(EAedirWoundType Type, float Severity, float BruiseMinCm, float BruiseMaxCm, const FVector2D& CutMinMaxCm, float PunctureCm)
	{
		const float Alpha = FMath::Clamp(Severity, 0.f, 1.f);
		switch (Type)
		{
		case EAedirWoundType::Bruise:
			return FMath::Lerp(BruiseMinCm, BruiseMaxCm, Alpha);
		case EAedirWoundType::Cut:
			return FMath::Lerp(static_cast<float>(CutMinMaxCm.X), static_cast<float>(CutMinMaxCm.Y), Alpha);
		case EAedirWoundType::Puncture:
		default:
			return PunctureCm;
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

	// Where a bone stands in the bind pose, the body's own frame in which the skin is drawn before it moves.
	FTransform WoundBindPose(const FReferenceSkeleton& Skeleton, FName Bone)
	{
		FTransform Pose = FTransform::Identity;
		for (int32 Index = Skeleton.FindBoneIndex(Bone); Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
		{
			Pose = Pose * Skeleton.GetRefBonePose()[Index];
		}
		return Pose;
	}

	// The skin's blood data: two rows (where, how) and the streams' rows under them: the wound's normal and how many
	// streams, then a row for each of at most four streams.
	constexpr int32 WoundSkinBloodStreamRow = 2;
	constexpr int32 WoundSkinBloodRows = 3 + 4;
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

bool UAedirWoundComponent::AddWound(FName Bone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection, const FVector& SprayDirection, float SpeedCmPerSec)
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
	if (DamageType != EAedirDamageType::Blunt)
	{
		OpenWound(ResolvedBone, WorldLocation, WorldNormal, ClassifyWound(DamageType), ComputeSeverity(DamageType, EnergyJoules, ArmorProtection), true, SprayDirection);
		return true;
	}

	// A blunt blow wears the place, by how fast it came; the face wears soonest. A bruise comes up once it has taken enough,
	// and with more the skin splits and bleeds.
	const float Fast = SpeedCmPerSec < 0.f ? 1.f : FMath::SmoothStep(BluntSlowSpeed, FMath::Max(BluntFastSpeed, BluntSlowSpeed + 1.f), SpeedCmPerSec);
	const bool bFace = ResolvedBone.ToString().StartsWith(TEXT("head"));
	const float Added = EnergyJoules * Fast * (1.f - WoundArmorSeverityFactor * FMath::Clamp(ArmorProtection, 0.f, 1.f)) * (bFace ? FaceBluntShare : 1.f);
	const double Now = GetWoundServerTime();
	FAedirBluntWear& Wear = BluntWear.FindOrAdd(ResolvedBone);
	if (Wear.Time > 0.0 && BluntWearHalfLifeSeconds > KINDA_SMALL_NUMBER)
	{
		Wear.Joules *= FMath::Pow(0.5f, static_cast<float>(Now - Wear.Time) / BluntWearHalfLifeSeconds);
	}
	Wear.Time = Now;
	Wear.Joules += Added;
	UE_LOG(LogAedir, Log, TEXT("%s: %s takes %.0f J of a blunt blow at %.0f cm/s (%.0f counted), worn %.0f, bruises at %.0f, splits at %.0f"), *GetNameSafe(GetOwner()), *ResolvedBone.ToString(),
		EnergyJoules, SpeedCmPerSec, Added, Wear.Joules, BluntBruiseJoules, BluntSplitJoules);
	if (Added <= 0.f || Wear.Joules < BluntBruiseJoules)
	{
		return false;
	}
	OpenWound(ResolvedBone, WorldLocation, WorldNormal, EAedirWoundType::Bruise, FMath::Clamp(Wear.Joules / FMath::Max(BluntSplitJoules, 1.f), 0.f, 1.f), false, SprayDirection);
	if (Wear.Joules >= BluntSplitJoules)
	{
		const float Over = (Wear.Joules - BluntSplitJoules) / FMath::Max(BluntSplitJoules, 1.f);
		OpenWound(ResolvedBone, WorldLocation, WorldNormal, EAedirWoundType::Cut, FMath::Clamp(0.15f + 0.5f * Over, 0.15f, 0.7f), false, SprayDirection);
	}
	return true;
}

float UAedirWoundComponent::GetBluntWear(FName Bone) const
{
	const FAedirBluntWear* Wear = BluntWear.Find(Bone);
	if (!Wear)
	{
		return 0.f;
	}
	return BluntWearHalfLifeSeconds > KINDA_SMALL_NUMBER ? Wear->Joules * FMath::Pow(0.5f, static_cast<float>(GetWoundServerTime() - Wear->Time) / BluntWearHalfLifeSeconds) : Wear->Joules;
}

void UAedirWoundComponent::OpenWound(FName ResolvedBone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirWoundType Type, float Severity, bool bAdd, const FVector& SprayDirection)
{
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	if (!Mesh)
	{
		return;
	}
	const FTransform Reference = WoundBoneTransform(Mesh, ResolvedBone);
	const FVector LocalOffset = Reference.InverseTransformPosition(WorldLocation);
	FVector LocalNormal = Reference.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();
	if (LocalNormal.IsNearlyZero())
	{
		LocalNormal = FVector::UpVector;
	}

	const uint8 AddedSeverity255 = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Severity, 0.f, 1.f) * 255.f));
	const float ServerTime = static_cast<float>(GetWoundServerTime());

	FAedirWound* Existing = ActiveWounds.FindByPredicate([ResolvedBone, Type, &LocalOffset](const FAedirWound& Wound)
	{
		return Wound.Bone == ResolvedBone && Wound.Type == Type && FVector::Dist(Wound.Offset, LocalOffset) <= WoundMergeDistance;
	});

	if (Existing)
	{
		Existing->Severity255 = bAdd ? static_cast<uint8>(FMath::Min(255, static_cast<int32>(Existing->Severity255) + static_cast<int32>(AddedSeverity255))) : FMath::Max(Existing->Severity255, AddedSeverity255);
		Existing->StartServerTime = ServerTime;
	}
	else
	{
		const uint8 NewId = AllocateWoundId();
		while (ActiveWounds.Num() >= FMath::Clamp(MaxWounds, 1, WoundMaxCount))
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
		// A broken leg buckles under the body, which goes down, but it can still get up on it: only a leg cut off is gone.
		Creature->Balance->BuckleLeg();
	}
	static const TCHAR* const Names[] = { TEXT("left arm"), TEXT("right arm"), TEXT("left leg"), TEXT("right leg") };
	UE_LOG(LogAedir, Log, TEXT("[Bone] %s's %s breaks."), *Creature->GetName(), Names[static_cast<int32>(Limb)]);
	return true;
}

void UAedirWoundComponent::MulticastSever_Implementation(FName Bone, FVector PlanePoint, FVector PlaneNormal, FVector Push)
{
	SeveredBones.Add(Bone);
	SetComponentTickInterval(FMath::Min(PrimaryComponentTick.TickInterval, StumpTickSeconds));
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && Character->GetNetMode() != NM_DedicatedServer)
	{
		USkeletalMeshComponent* Body = Character->GetMesh();
		const FVector Joint = Body->GetBoneLocation(Bone);
		const FVector TowardBody = (Body->GetBoneLocation(Body->GetParentBone(Bone)) - Joint).GetSafeNormal();
		if (AActor* Piece = AedirSever::CutLimb(Body, Bone, PlanePoint, PlaneNormal, Push))
		{
			// The piece runs from its cut end, which faces back where the body was.
			FAedirCutPiece& Cut = CutPieces.AddDefaulted_GetRef();
			Cut.Piece = Piece;
			Cut.LocalEnd = Piece->GetActorTransform().InverseTransformPosition(Joint);
			Cut.LocalOut = Piece->GetActorTransform().InverseTransformVectorNoScale(TowardBody);
			// The wounds and marks it already had go with it: their blood, bruises and runs stay where they were on the skin.
			int32 Carried = 0;
			for (const FAedirWound& Wound : ActiveWounds)
			{
				if (Wound.Bone != Bone && !Body->BoneIsChildOf(Wound.Bone, Bone))
				{
					continue;
				}
				if (FAedirWoundVisual* Visual = FindVisual(Wound.Id))
				{
					for (UDecalComponent* Mark : { Visual->Decal.Get(), Visual->Run.Get() })
					{
						if (Mark && Piece->GetRootComponent())
						{
							Mark->AttachToComponent(Piece->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
							++Carried;
						}
					}
				}
			}
			UE_CLOG(Carried > 0, LogAedir, Log, TEXT("[Sever] %s's cut-off %s takes %d of its marks with it"), *GetNameSafe(GetOwner()), *Bone.ToString(), Carried);
		}
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
	if (!Character || !Blood || IsDedicatedServer())
	{
		return;
	}
	DeadSeconds = bDead ? DeadSeconds + DeltaTime : 0.f;
	const auto Running = [this, Blood](const FVector& From, const FVector& Out, float Count)
	{
		for (int32 Drop = 0; Drop < static_cast<int32>(Count); ++Drop)
		{
			Blood->SpawnDrop(From + FMath::VRand() * 1.5f, Out * FMath::FRandRange(FMath::Min(StumpDropSpeedMin, StumpDropSpeedMax), FMath::Max(StumpDropSpeedMin, StumpDropSpeedMax)) + FMath::VRand() * StumpDropScatter);
		}
	};
	// Every stump runs all the time, thinning out once the heart has stopped.
	const float StumpShare = bDead ? FMath::Clamp(1.f - DeadSeconds / FMath::Max(StumpDeadBleedSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f) : 1.f;
	if (!SeveredBones.IsEmpty() && StumpShare > 0.f)
	{
		StumpDebt += StumpDropsPerSecond * StumpShare * DeltaTime;
		const float Whole = FMath::FloorToFloat(StumpDebt);
		StumpDebt -= Whole;
		for (const FName& Bone : SeveredBones)
		{
			const FVector From = Character->GetMesh()->GetBoneLocation(Bone);
			Running(From, (From - Character->GetMesh()->GetBoneLocation(Character->GetMesh()->GetParentBone(Bone))).GetSafeNormal(), Whole);
		}
	}
	// And so does every piece cut off, from its cut end.
	for (int32 Index = CutPieces.Num() - 1; Index >= 0; --Index)
	{
		FAedirCutPiece& Cut = CutPieces[Index];
		const AActor* Piece = Cut.Piece.Get();
		Cut.Seconds += DeltaTime;
		if (!Piece || Cut.Seconds > PieceBleedSeconds)
		{
			CutPieces.RemoveAt(Index);
			continue;
		}
		Cut.Debt += PieceDropsPerSecond * (1.f - Cut.Seconds / FMath::Max(PieceBleedSeconds, KINDA_SMALL_NUMBER)) * DeltaTime;
		const float Whole = FMath::FloorToFloat(Cut.Debt);
		Cut.Debt -= Whole;
		Running(Piece->GetActorTransform().TransformPosition(Cut.LocalEnd), Piece->GetActorTransform().TransformVectorNoScale(Cut.LocalOut), Whole);
	}
	const float Total = GetTotalBleedRate() + SeverBleedPerSecond;
	if (Total <= 0.f)
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

void UAedirWoundComponent::HealAll()
{
	if (!IsAuthoritative())
	{
		return;
	}
	ActiveWounds.Reset();
	BrokenLimbs = 0;
	SeverBleedPerSecond = 0.f;
	BluntWear.Reset();
	CutWear.Reset();
	OnRep_Wounds();
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

namespace
{
	// Where a wound lies in the bind pose of the body, the frame its skin and the clothes on it are drawn in.
	FVector WoundBindPoint(const USkeletalMeshComponent* Mesh, const FAedirWound& Wound)
	{
		const USkinnedAsset* Asset = Mesh ? Mesh->GetSkinnedAsset() : nullptr;
		if (!Asset)
		{
			return FVector::ZeroVector;
		}
		const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
		FTransform Pose = FTransform::Identity;
		for (int32 Index = Skeleton.FindBoneIndex(WoundResolveBone(Mesh, Wound.Bone)); Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
		{
			Pose = Pose * Skeleton.GetRefBonePose()[Index];
		}
		return Pose.TransformPosition(Wound.Offset);
	}

	// Whether a piece worn covers a bone: the bone itself or one of the two above it is among those it covers.
	bool WoundCovered(const USkeletalMeshComponent* Mesh, const AAedirItemActor* Item, FName Bone)
	{
		const USkinnedAsset* Asset = Mesh ? Mesh->GetSkinnedAsset() : nullptr;
		if (!Asset || !Item)
		{
			return false;
		}
		const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
		int32 Index = Skeleton.FindBoneIndex(Bone);
		for (int32 Up = 0; Up < 3 && Index != INDEX_NONE; ++Up, Index = Skeleton.GetParentIndex(Index))
		{
			if (Item->ArmorBones.Contains(Skeleton.GetBoneName(Index)))
			{
				return true;
			}
		}
		return false;
	}
	constexpr float WoundSoakIntervalSeconds = 0.25f;
}

void UAedirWoundComponent::SoakClothes(float DeltaTime)
{
	SoakTimer += DeltaTime;
	if (SoakTimer < WoundSoakIntervalSeconds)
	{
		return;
	}
	const float Step = SoakTimer;
	SoakTimer = 0.f;
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	const UWorld* World = GetWorld();
	if (!Creature || !Mesh || !World)
	{
		return;
	}
	// In water the blood comes off the body and out of the clothes.
	const UCharacterMovementComponent* Movement = Creature->GetCharacterMovement();
	if (Movement && Movement->IsInWater())
	{
		WashBlood(WaterWashPerSecond * Step);
	}
	const TArray<AAedirItemActor*> Worn = Creature->Interaction ? Creature->Interaction->GetWornItemsCopy() : TArray<AAedirItemActor*>();
	const double Now = GetWoundServerTime();
	const float WorldNow = static_cast<float>(World->GetTimeSeconds());
	for (FAedirWound& Wound : ActiveWounds)
	{
		if (Wound.Type == EAedirWoundType::Bruise)
		{
			continue;
		}
		const float Severity = Wound.GetSeverity();
		const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));
		const float Rate = ComputeBleedRate(Wound.Type, Severity, Age);
		// Fresh blood comes back over what was washed off.
		if (Rate > 0.f)
		{
			Wound.Washed255 = 0;
		}
		if (Rate <= 0.f && Age > 1.f)
		{
			continue;
		}
		const FVector Where = WoundBindPoint(Mesh, Wound);
		const float Radius = WoundDecalRadius(Wound.Type, Severity, BruiseRadiusMinCm, BruiseRadiusMaxCm, CutBloodRadiusMinMaxCm, PunctureBloodRadiusCm) * ClothStainRadiusShare;
		for (AAedirItemActor* Item : Worn)
		{
			if (Item && Item->TakesBlood() && WoundCovered(Mesh, Item, WoundResolveBone(Mesh, Wound.Bone)))
			{
				const FAedirBloodStain* Had = Item->Stains.FindByPredicate([&Wound](const FAedirBloodStain& S) { return S.WoundId == Wound.Id; });
				const float Grown = (Had ? Had->Amount : 0.f) + Step * (0.15f + Rate);
				Item->SoakBlood(Where, Radius, FMath::Min(Grown, FMath::Max(Severity, 0.25f) * ClothSoakShare), Wound.Id, WorldNow);
			}
		}
	}
}

void UAedirWoundComponent::WashBlood(float Share)
{
	if (!IsAuthoritative() || Share <= 0.f)
	{
		return;
	}
	for (FAedirWound& Wound : ActiveWounds)
	{
		if (Wound.Type != EAedirWoundType::Bruise)
		{
			Wound.Washed255 = static_cast<uint8>(FMath::Clamp(static_cast<float>(Wound.Washed255) + Share * 255.f, 0.f, 255.f));
		}
	}
	if (const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner()); Creature && Creature->Interaction)
	{
		for (AAedirItemActor* Item : Creature->Interaction->GetWornItemsCopy())
		{
			if (Item)
			{
				Item->WashBlood(Share);
			}
		}
	}
}

void UAedirWoundComponent::UpdateBleeding(float DeltaTime)
{
	SoakClothes(DeltaTime);
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

		const float Radius = WoundDecalRadius(Wound.Type, Wound.GetSeverity(), BruiseRadiusMinCm, BruiseRadiusMaxCm, CutBloodRadiusMinMaxCm, PunctureBloodRadiusCm);
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
			const float Opened = WoundRunOpenShare + WoundRunOpenPerSeverity * FMath::Clamp(Severity, 0.f, 1.f);
			float Shed = FMath::Max(Opened - Visual->RunLength, 0.f);
			Visual->RunLength = FMath::Max(Visual->RunLength, Opened);
			if (Rate > 0.f)
			{
				Shed += DeltaTime * (WoundRunShareBase + Rate * WoundRunSharePerBleed);
				Visual->RunLength = FMath::Min(Visual->RunLength + DeltaTime * (WoundRunShareBase + Rate * WoundRunSharePerBleed), 1.f);
				Visual->RunWet = 1.f;
			}
			else
			{
				Visual->RunWet = FMath::Max(Visual->RunWet - DeltaTime / WoundRunDrySeconds, 0.f);
			}
			UpdateBloodStreams(*Visual, Wound, Shed, DeltaTime);
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

void UAedirWoundComponent::UpdateBloodStreams(FAedirWoundVisual& Visual, const FAedirWound& Wound, float Shed, float DeltaTime)
{
	USkeletalMeshComponent* Mesh = GetBodyMesh();
	if (Shed <= 0.f || !Mesh || !Mesh->GetSkinnedAsset())
	{
		return;
	}
	// The way down the skin at the wound as the body lies now, put into the bind pose: the world's down brought into the
	// bone, the bone as it stands in the bind pose, and what of it goes into the skin taken off.
	const FName Bone = WoundResolveBone(Mesh, Wound.Bone);
	const FTransform Bind = WoundBindPose(Mesh->GetSkinnedAsset()->GetRefSkeleton(), Bone);
	const FVector Normal = Bind.TransformVectorNoScale(Wound.Normal).GetSafeNormal();
	const FVector Down = Bind.TransformVectorNoScale(WoundBoneTransform(Mesh, Wound.Bone).InverseTransformVectorNoScale(-FVector::UpVector));
	const FVector Slope = FVector::VectorPlaneProject(Down, Normal);
	if (Slope.Size() < BloodStreamLeastSlope)
	{
		return;
	}
	const FVector3f Way(Slope.GetSafeNormal());
	FVector4f* Running = Visual.Streams.IsEmpty() ? nullptr : &Visual.Streams.Last();
	const float Turned = Running ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3f::DotProduct(FVector3f(Running->X, Running->Y, Running->Z), Way), -1.f, 1.f))) : 180.f;
	if (Turned > BloodStreamTurnDegrees)
	{
		const bool bHeld = FVector3f::DotProduct(Visual.NewWay, Way) > FMath::Cos(FMath::DegreesToRadians(BloodStreamTurnDegrees));
		Visual.NewWaySeconds = bHeld ? Visual.NewWaySeconds + DeltaTime : 0.f;
		Visual.NewWay = Way;
		Visual.NewWayShed += Shed;
		// The first blood takes the way there is at once.
		if (Running && Visual.NewWaySeconds < BloodStreamSettleSeconds)
		{
			return;
		}
		Shed = Visual.NewWayShed;
		if (Visual.Streams.Num() >= FMath::Clamp(BloodStreamsMax, 1, 4))
		{
			int32 Shortest = 0;
			for (int32 Index = 1; Index < Visual.Streams.Num(); ++Index)
			{
				Shortest = Visual.Streams[Index].W < Visual.Streams[Shortest].W ? Index : Shortest;
			}
			Visual.Streams.RemoveAt(Shortest);
		}
		Running = &Visual.Streams.Add_GetRef(FVector4f(Way.X, Way.Y, Way.Z, 0.f));
		UE_CLOG(CVarAedirSkinBloodLog.GetValueOnGameThread() != 0, LogAedir, Log, TEXT("[Wounds] blood from the wound on %s runs a new way (turned %.0f degrees), %d streams"), *Wound.Bone.ToString(), Turned, Visual.Streams.Num());
	}
	else
	{
		const FVector3f Bent = FMath::Lerp(FVector3f(Running->X, Running->Y, Running->Z), Way, FMath::Min(DeltaTime * BloodStreamFollowPerSecond, 1.f)).GetSafeNormal();
		Running->X = Bent.X;
		Running->Y = Bent.Y;
		Running->Z = Bent.Z;
	}
	Visual.NewWaySeconds = 0.f;
	Visual.NewWayShed = 0.f;
	Running->W = FMath::Min(Running->W + Shed, 1.f);
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
			// The face is a model of its own over the head, moved by the same bones: it shows the same blood and bruises.
			if (const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner()); Creature && Creature->Face)
			{
				Creature->Face->SetOverlayMaterial(SkinBlood);
			}
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
	const FReferenceSkeleton& Skeleton = Mesh->GetSkinnedAsset()->GetRefSkeleton();

	// Every wound and bruise, one texel each in the material's data texture.
	if (!SkinBloodData)
	{
		SkinBloodData = UTexture2D::CreateTransient(WoundSkinBloodSlots, WoundSkinBloodRows, PF_A32B32G32R32F);
		if (!SkinBloodData)
		{
			return;
		}
		SkinBloodData->SRGB = false;
		SkinBloodData->Filter = TF_Nearest;
		SkinBloodData->CompressionSettings = TC_HDR;
		SkinBloodData->NeverStream = true;
		SkinBloodData->UpdateResource();
		SkinBlood->SetTextureParameterValue(TEXT("Data"), SkinBloodData);
	}
	FLinearColor* Texels = new FLinearColor[WoundSkinBloodSlots * WoundSkinBloodRows];
	FMemory::Memzero(Texels, sizeof(FLinearColor) * WoundSkinBloodSlots * WoundSkinBloodRows);
	const int32 Count = FMath::Min(ActiveWounds.Num(), WoundSkinBloodSlots);
	const double Now = GetWoundServerTime();
	for (int32 Slot = 0; Slot < WoundSkinBloodSlots; ++Slot)
	{
		FLinearColor Where(0.f, 0.f, 0.f, 0.f);
		FLinearColor How(0.f, 0.f, 0.f, 0.f);
		if (Slot < Count)
		{
			const FAedirWound& Wound = ActiveWounds[Slot];
			const float Severity = Wound.GetSeverity();
			const float Age = static_cast<float>(FMath::Max(0.0, Now - Wound.StartServerTime));
			const FTransform Bind = WoundBindPose(Skeleton, WoundResolveBone(Mesh, Wound.Bone));
			const FVector Point = Bind.TransformPosition(Wound.Offset);
			const FAedirWoundVisual* Visual = FindVisual(Wound.Id);
			// The runs creep down as the wound bleeds, each as far as the old laid-on run went, the way the skin went down
			// while it ran; the longest tells the material how far from the wound to look for them.
			const float FullRunCm = 2.f * (WoundRunHalfLengthMinCm + WoundRunHalfLengthPerSeverityCm * FMath::Clamp(Severity, 0.f, 1.f));
			float RunCm = 0.f;
			const int32 StreamCount = Visual && Wound.Type != EAedirWoundType::Bruise ? FMath::Min(Visual->Streams.Num(), WoundSkinBloodRows - WoundSkinBloodStreamRow - 1) : 0;
			const FVector Normal = Bind.TransformVectorNoScale(Wound.Normal).GetSafeNormal();
			Texels[WoundSkinBloodStreamRow * WoundSkinBloodSlots + Slot] = FLinearColor(Normal.X, Normal.Y, Normal.Z, static_cast<float>(StreamCount));
			for (int32 Stream = 0; Stream < StreamCount; ++Stream)
			{
				const FVector4f& Flow = Visual->Streams[Stream];
				RunCm = FMath::Max(RunCm, Flow.W * FullRunCm);
				Texels[(WoundSkinBloodStreamRow + 1 + Stream) * WoundSkinBloodSlots + Slot] = FLinearColor(Flow.X, Flow.Y, Flow.Z, Flow.W * FullRunCm);
			}
			// A bruise shrinks slowly to nothing over its week as it turns from purple to yellow.
			const float Healed = Wound.Type == EAedirWoundType::Bruise ? FMath::SmoothStep(0.f, 1.f, ComputeAge01(Wound.Type, Severity, Age)) : 0.f;
			Where = FLinearColor(Point.X, Point.Y, Point.Z, WoundDecalRadius(Wound.Type, Severity, BruiseRadiusMinCm, BruiseRadiusMaxCm, CutBloodRadiusMinMaxCm, PunctureBloodRadiusCm) * (1.f - Healed));
			UE_CLOG(CVarAedirSkinBloodLog.GetValueOnGameThread() != 0, LogAedir, Log, TEXT("[Wounds] skin blood %d on %s at %s, radius %.1f, run %.1f cm"), Slot, *Wound.Bone.ToString(), *Point.ToCompactString(), Where.A, RunCm);
			// A bruise has no run: the material knows it by a run length below zero.
			const bool bBruise = Wound.Type == EAedirWoundType::Bruise;
			// A bruise turns by its healing; shed blood by how long it has had to dry, fresh while it still runs.
			float Look = ComputeAge01(Wound.Type, Severity, Age);
			if (!bBruise)
			{
				const float DrySeconds = FMath::Max(BloodDryGameHours, 0.01f) * AedirGameDaySeconds / 24.f;
				Look = FMath::Clamp(Age / DrySeconds, 0.f, 1.f);
				if (ComputeBleedRate(Wound.Type, Severity, Age) > 0.f)
				{
					Look = FMath::Min(Look, BloodBleedingDryShare);
				}
			}
			// Washed off, the blood round it is gone, as much as was washed.
			const float Left = bBruise ? 1.f : 1.f - static_cast<float>(Wound.Washed255) / 255.f;
			How = FLinearColor(FMath::Max(Severity, 0.2f) * Left, Look, bBruise ? -1.f : RunCm, static_cast<float>(Wound.Id % 17));
		}
		Texels[Slot] = Where;
		Texels[WoundSkinBloodSlots + Slot] = How;
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, WoundSkinBloodSlots, WoundSkinBloodRows);
	SkinBloodData->UpdateTextureRegions(0, 1, Region, WoundSkinBloodSlots * sizeof(FLinearColor), sizeof(FLinearColor), reinterpret_cast<uint8*>(Texels),
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			delete[] reinterpret_cast<FLinearColor*>(Data);
			delete Regions;
		});
	SkinBlood->SetScalarParameterValue(TEXT("Count"), static_cast<float>(Count));
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
