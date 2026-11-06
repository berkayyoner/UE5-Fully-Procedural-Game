#include "Combat/AedirCombatComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirGroupSubsystem.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	constexpr float AedirCombatStatMax = 100.f;
	constexpr float AedirDefaultStat = 10.f;
	constexpr float AedirGuardBlendSpeed = 6.f;
	constexpr float AedirArmRampInSeconds = 0.1f;
	constexpr float AedirArmRampOutSeconds = 0.2f;
	constexpr float AedirMinSwingDuration = 0.05f;
	constexpr float AedirWeightSlowdownPerRatio = 0.35f;
	constexpr float AedirArmedSkillExperience = 0.05f;

	constexpr float AedirSwingWindupEnd = 0.3f;
	constexpr float AedirSwingStrikeEnd = 0.65f;
	constexpr float AedirWindupPullBack = 20.f;
	constexpr float AedirArcBulge = 15.f;
	constexpr float AedirShoulderLateral = 22.f;
	constexpr float AedirShoulderHeight = 30.f;
	constexpr float AedirGuardForward = 30.f;
	constexpr float AedirGuardHeight = 20.f;
	constexpr float AedirGuardInward = 8.f;
	constexpr float AedirPunchLateral = 14.f;
	constexpr float AedirHookOutsideLateral = 55.f;
	constexpr float AedirHookAcrossLateral = 30.f;
	constexpr float AedirChestHeight = 10.f;

	constexpr float AedirAIRetargetInterval = 0.2f;
	constexpr float AedirAIMemorySeconds = 3.f;
	constexpr float AedirAITurnSpeed = 220.f;
	constexpr float AedirAICloseRange = 90.f;

	constexpr float AedirArmDampingRatio = 0.7f;
	constexpr float AedirArmStrengthBase = 0.6f;
	constexpr float AedirArmStaminaFloor = 0.35f;
	constexpr float AedirArmInjuryStiffnessLoss = 0.7f;
	constexpr float AedirArmDazeStiffnessLoss = 0.6f;
	constexpr float AedirArmMassStrengthScale = 0.6f;
	constexpr float AedirArmSubstepSeconds = 1.f / 120.f;
	constexpr float AedirArmReachMinScale = 0.45f;
	constexpr float AedirArmReachHardLimit = 1.15f;
	constexpr float AedirArmFallbackLength = 12.f;
	constexpr float AedirArmMinSpeed = 50.f;
	constexpr float AedirArmReconcileDistance = 40.f;
	constexpr float AedirArmProxyInterpSpeed = 12.f;
	constexpr float AedirArmNetSendInterval = 1.f / 30.f;
	constexpr float AedirArmAimYawLimit = 110.f;
	constexpr float AedirArmOutwardYawLimit = 80.f;
	constexpr float AedirArmAcrossYawLimit = 0.f;
	constexpr float AedirArmConvergeYawDegrees = 9.f;
	constexpr float AedirArmHeadHeightPitchDegrees = 6.f;
	constexpr float AedirArmAimPitchMin = -70.f;
	constexpr float AedirArmAimPitchMax = 85.f;
	constexpr float AedirArmGuardRestYaw = 12.f;
	constexpr float AedirArmGuardRestPitch = 15.f;
	constexpr float AedirArmExtensionStep = 0.08f;
	constexpr int32 AedirArmorZoneCount = 4;
	constexpr int32 AedirLimbCount = 4;
	constexpr float AedirArmorMaxProtection = 0.9f;

	constexpr float AedirImpactInjuryPerDamage = 0.012f;
	constexpr float AedirImpactLimbHealPerSecond = 0.002f;
	constexpr float AedirImpactRecoilScale = 0.35f;
	constexpr float AedirImpactStaminaPerEnergy = 0.12f;
	constexpr float AedirImpactPropImpulseScale = 0.6f;

	constexpr float AedirClashCacheInterval = 0.1f;
	constexpr float AedirClashSelfReflection = -0.3f;
	constexpr float AedirClashOtherReflection = 0.1f;
	constexpr float AedirClashBalanceScale = 0.1f;
	constexpr float AedirClashEnergyShare = 0.5f;

	constexpr float AedirShoveStrikeSeconds = 0.12f;
	constexpr float AedirShoveDuration = 0.35f;
	constexpr float AedirShoveReachCm = 70.f;
	constexpr float AedirShoveConeCos = 0.5f;
	constexpr float AedirShoveRecoilBalance = 0.06f;
	constexpr float AedirShoveStiffnessScale = 2.f;
	constexpr float AedirShoveHeavyItemKg = 6.f;
	constexpr float AedirShoveArmInjuryLimit = 0.6f;
	constexpr float AedirShoveBaseJoules = 20.f;
	constexpr float AedirShoveStrengthJoules = 1.8f;
	constexpr float AedirShoveMinStaminaScale = 0.5f;

	constexpr float AedirKickDuration = 0.5f;
	constexpr float AedirKickStrikeStart = 0.18f;
	constexpr float AedirKickStrikeEnd = 0.32f;
	constexpr float AedirKickHitRadius = 14.f;
	constexpr float AedirKickMassKg = 8.f;
	constexpr float AedirKickEnergyScale = 0.8f;
	constexpr float AedirKickMaxFootSpeed = 500.f;
	constexpr float AedirKickRecoilBalance = 0.12f;
	constexpr float AedirKickMinBalance = 0.4f;
	constexpr float AedirKickPoseRiseSeconds = 0.08f;
	constexpr float AedirKickPoseFallSeconds = 0.15f;
	constexpr float AedirKickChamberTime = 0.25f;
	constexpr float AedirKickExtendTime = 0.5f;
	constexpr float AedirKickChamberBehindCm = 35.f;
	constexpr float AedirKickChamberRightCm = 20.f;
	constexpr float AedirKickExtendCm = 85.f;
	constexpr float AedirKickGroundBelowHipCm = 90.f;
	constexpr float AedirKickExtendHeightCm = 55.f;

	constexpr float AedirAIContactRange = 110.f;
	constexpr float AedirAIShoveChance = 0.12f;
	constexpr float AedirAIKickChance = 0.08f;

	const FName AedirKickFootLeftBone(TEXT("foot_l"));
	const FName AedirKickFootRightBone(TEXT("foot_r"));
	const FName AedirKickPelvisBone(TEXT("pelvis"));

	float CombatSmoothStep01(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	FVector QuadraticBezier(const FVector& A, const FVector& Control, const FVector& B, float T)
	{
		const float Inverse = 1.f - T;
		return A * (Inverse * Inverse) + Control * (2.f * Inverse * T) + B * (T * T);
	}

	bool CombatBoneNameContainsAny(const FString& BoneName, const TCHAR* const* Tokens, int32 TokenCount)
	{
		for (int32 Index = 0; Index < TokenCount; ++Index)
		{
			if (BoneName.Contains(Tokens[Index], ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

UAedirCombatComponent::UAedirCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);

	ArmorProtection.Init(0.f, AedirArmorZoneCount);
	LimbInjury.Init(0.f, AedirLimbCount);
}

void UAedirCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	Creature = Cast<AAedirCreature>(GetOwner());
}

void UAedirCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Creature)
	{
		return;
	}

	const bool bAuthority = IsAuthoritative();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		GuardAlpha[Index] = FMath::FInterpConstantTo(GuardAlpha[Index], IsGuardActive(Hand) ? 1.f : 0.f, DeltaTime, AedirGuardBlendSpeed);
		SwingCooldownRemaining[Index] = FMath::Max(0.f, SwingCooldownRemaining[Index] - DeltaTime);
	}

	ShoveCooldownRemaining = FMath::Max(0.f, ShoveCooldownRemaining - DeltaTime);
	KickCooldownRemaining = FMath::Max(0.f, KickCooldownRemaining - DeltaTime);

	UpdateSwings(DeltaTime);

	if (bAuthority || IsAutonomousProxy())
	{
		UpdateTwoHandGrip();
		UpdateShove(DeltaTime);
	}

	UpdateKick(DeltaTime);

	if (bAuthority && !Creature->IsPlayerControlled())
	{
		UpdateAI(DeltaTime);
	}

	UpdateArms(DeltaTime);

	if (bAuthority)
	{
		ClashedThisTick.Reset();
		UpdateClashes(DeltaTime);
		HealLimbInjuries(DeltaTime);

		for (int32 Index = 0; Index < 2; ++Index)
		{
			UpdateImpacts(static_cast<EAedirHand>(Index), Arms[Index], DeltaTime);
		}

		for (int32 Index = 0; Index < 2; ++Index)
		{
			PublishArmState(static_cast<EAedirHand>(Index), Arms[Index]);
		}
	}
	else if (IsAutonomousProxy())
	{
		SendArmAimToServer(DeltaTime);
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAedirArmDynamics& Arm = Arms[Index];
		Arm.PrevHandWorld = Arm.HandWorld;
		Arm.PrevTipWorld = ComputeArmTip(static_cast<EAedirHand>(Index), Arm);
	}
}

void UAedirCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirCombatComponent, bLeftGuard);
	DOREPLIFETIME(UAedirCombatComponent, bRightGuard);
	DOREPLIFETIME(UAedirCombatComponent, LeftSwing);
	DOREPLIFETIME(UAedirCombatComponent, RightSwing);
	DOREPLIFETIME(UAedirCombatComponent, LeftArmNet);
	DOREPLIFETIME(UAedirCombatComponent, RightArmNet);
	DOREPLIFETIME(UAedirCombatComponent, bTwoHandGrip);
	DOREPLIFETIME(UAedirCombatComponent, KickNet);
	DOREPLIFETIME(UAedirCombatComponent, ArmorProtection);
	DOREPLIFETIME(UAedirCombatComponent, LimbInjury);
}

void UAedirCombatComponent::SetGuard(EAedirHand Hand, bool bActive)
{
	if (!Creature)
	{
		return;
	}

	if (IsAuthoritative())
	{
		SetGuardInternal(Hand, bActive);
	}
	else if (Creature->IsLocallyControlled())
	{
		SetGuardInternal(Hand, bActive);
		ServerSetGuard(Hand, bActive);
	}
}

void UAedirCombatComponent::AddSwingInput(EAedirHand Hand, FVector2D MouseDeltaThisFrame, float DeltaTime)
{
	// The second hand of a two-hand grip is placed by the grip, so only the primary hand takes mouse input.
	if (!Creature || !Creature->IsLocallyControlled() || DeltaTime <= 0.f || !IsGuardActive(Hand) || IsSecondaryGripHand(Hand))
	{
		return;
	}

	FAedirArmDynamics& Arm = GetArm(Hand);
	if (Arm.bScriptedControl)
	{
		return;
	}

	// Mouse Y input is positive when the mouse moves up, so moving the mouse up raises the arm.
	// Outwards the arm sweeps a 90 degree arc, across the body it only reaches a little past the centre line.
	const float OutwardSign = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float OutwardYaw = FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSign + MouseDeltaThisFrame.X * OutwardSign * AimSensitivity, -AedirArmAcrossYawLimit, AedirArmOutwardYawLimit);
	Arm.AimOffsetDeg.X = OutwardYaw * OutwardSign;
	Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity, AedirArmAimPitchMin, AedirArmAimPitchMax);
}

void UAedirCombatComponent::AddExtensionInput(EAedirHand Hand, float WheelDelta)
{
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.Extension = FMath::Clamp(Arm.Extension + WheelDelta * AedirArmExtensionStep, 0.f, 1.f);
}

bool UAedirCombatComponent::StartSwing(EAedirHand Hand, EAedirSwingType Type)
{
	return TryStartSwing(Hand, Type, FVector2D::ZeroVector);
}

void UAedirCombatComponent::SetArmorProtection(EAedirBodyZone Zone, float Protection)
{
	const int32 Index = static_cast<int32>(Zone);
	if (!IsAuthoritative() || !ArmorProtection.IsValidIndex(Index))
	{
		return;
	}

	ArmorProtection[Index] = FMath::Clamp(Protection, 0.f, AedirArmorMaxProtection);
}

float UAedirCombatComponent::GetArmorProtection(EAedirBodyZone Zone) const
{
	const int32 Index = static_cast<int32>(Zone);
	return ArmorProtection.IsValidIndex(Index) ? ArmorProtection[Index] : 0.f;
}

float UAedirCombatComponent::GetLimbInjury(EAedirLimb Limb) const
{
	const int32 Index = static_cast<int32>(Limb);
	return LimbInjury.IsValidIndex(Index) ? LimbInjury[Index] : 0.f;
}

FVector UAedirCombatComponent::GetHandVelocity(EAedirHand Hand) const
{
	return GetArm(Hand).HandVelocity;
}

FVector UAedirCombatComponent::GetTipWorld(EAedirHand Hand) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	return ComputeArmTip(Hand, GetArm(Hand));
}

bool UAedirCombatComponent::IsGuardActive(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? bLeftGuard : bRightGuard;
}

bool UAedirCombatComponent::IsInFightStance() const
{
	return bLeftGuard || bRightGuard;
}

bool UAedirCombatComponent::GetGuardPose(EAedirHand Hand, float& OutAlpha) const
{
	OutAlpha = GuardAlpha[HandIndex(Hand)];
	return OutAlpha > 0.001f;
}

bool UAedirCombatComponent::GetSwingPose(EAedirHand Hand, FVector& OutHandTargetWorld, float& OutAlpha) const
{
	const FAedirArmDynamics& Arm = GetArm(Hand);
	if (IsSecondaryGripHand(Hand))
	{
		OutAlpha = 1.f;
		OutHandTargetWorld = Arm.HandWorld;
		return true;
	}

	OutAlpha = Arm.ActiveAlpha;
	if (OutAlpha <= 0.001f)
	{
		return false;
	}

	OutHandTargetWorld = Arm.HandWorld;
	return true;
}

float UAedirCombatComponent::GetFistAlpha(EAedirHand Hand) const
{
	return GetHeldItem(Hand) ? 0.f : GuardAlpha[HandIndex(Hand)];
}

bool UAedirCombatComponent::IsLeadFootRight() const
{
	return bLeadFootRight;
}

float UAedirCombatComponent::ComputeImpactEnergy(float MassKg, float SpeedCmPerSec)
{
	const float SpeedMetersPerSecond = FMath::Max(SpeedCmPerSec, 0.f) / 100.f;
	return 0.5f * FMath::Max(MassKg, 0.f) * SpeedMetersPerSecond * SpeedMetersPerSecond;
}

EAedirBodyZone UAedirCombatComponent::ClassifyBodyZone(FName BoneName)
{
	static const TCHAR* const HeadTokens[] = { TEXT("head"), TEXT("neck") };
	static const TCHAR* const TorsoTokens[] = { TEXT("spine"), TEXT("pelvis"), TEXT("clavicle") };
	static const TCHAR* const ArmTokens[] = { TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
	static const TCHAR* const LegTokens[] = { TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball") };

	const FString Bone = BoneName.ToString();
	if (CombatBoneNameContainsAny(Bone, HeadTokens, UE_ARRAY_COUNT(HeadTokens)))
	{
		return EAedirBodyZone::Head;
	}

	if (CombatBoneNameContainsAny(Bone, TorsoTokens, UE_ARRAY_COUNT(TorsoTokens)))
	{
		return EAedirBodyZone::Torso;
	}

	if (CombatBoneNameContainsAny(Bone, ArmTokens, UE_ARRAY_COUNT(ArmTokens)))
	{
		return EAedirBodyZone::Arm;
	}

	if (CombatBoneNameContainsAny(Bone, LegTokens, UE_ARRAY_COUNT(LegTokens)))
	{
		return EAedirBodyZone::Leg;
	}

	return EAedirBodyZone::Torso;
}

float UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone Zone)
{
	switch (Zone)
	{
	case EAedirBodyZone::Head:
		return 2.f;
	case EAedirBodyZone::Arm:
		return 0.6f;
	case EAedirBodyZone::Leg:
		return 0.7f;
	case EAedirBodyZone::Torso:
	default:
		return 1.f;
	}
}

float UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType Type, float AxialAlignment)
{
	switch (Type)
	{
	case EAedirDamageType::Slash:
		return 1.2f;
	case EAedirDamageType::Pierce:
		return AxialAlignment >= 0.7f ? 1.5f : 0.25f;
	case EAedirDamageType::Blunt:
	default:
		return 0.9f;
	}
}

bool UAedirCombatComponent::GetBoneLimb(FName BoneName, EAedirLimb& OutLimb)
{
	const EAedirBodyZone Zone = ClassifyBodyZone(BoneName);
	if (Zone != EAedirBodyZone::Arm && Zone != EAedirBodyZone::Leg)
	{
		return false;
	}

	const FString Bone = BoneName.ToString();
	const bool bLeft = Bone.EndsWith(TEXT("_l"), ESearchCase::IgnoreCase);
	const bool bRight = Bone.EndsWith(TEXT("_r"), ESearchCase::IgnoreCase);
	if (!bLeft && !bRight)
	{
		return false;
	}

	if (Zone == EAedirBodyZone::Arm)
	{
		OutLimb = bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm;
	}
	else
	{
		OutLimb = bLeft ? EAedirLimb::LeftLeg : EAedirLimb::RightLeg;
	}

	return true;
}

bool UAedirCombatComponent::IsAuthoritative() const
{
	return Creature && Creature->HasAuthority();
}

bool UAedirCombatComponent::IsAutonomousProxy() const
{
	return Creature && !Creature->HasAuthority() && Creature->IsLocallyControlled();
}

bool UAedirCombatComponent::IsSimulatedProxy() const
{
	return Creature && !Creature->HasAuthority() && !Creature->IsLocallyControlled();
}

int32 UAedirCombatComponent::HandIndex(EAedirHand Hand)
{
	return static_cast<int32>(Hand);
}

FAedirSwingState& UAedirCombatComponent::GetSwingState(EAedirHand Hand)
{
	return Hand == EAedirHand::Left ? LeftSwing : RightSwing;
}

const FAedirSwingState& UAedirCombatComponent::GetSwingState(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? LeftSwing : RightSwing;
}

FAedirArmDynamics& UAedirCombatComponent::GetArm(EAedirHand Hand)
{
	return Arms[HandIndex(Hand)];
}

const FAedirArmDynamics& UAedirCombatComponent::GetArm(EAedirHand Hand) const
{
	return Arms[HandIndex(Hand)];
}

FAedirArmNetState& UAedirCombatComponent::GetArmNet(EAedirHand Hand)
{
	return Hand == EAedirHand::Left ? LeftArmNet : RightArmNet;
}

const FAedirArmNetState& UAedirCombatComponent::GetArmNet(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? LeftArmNet : RightArmNet;
}

AAedirItemActor* UAedirCombatComponent::GetHeldItem(EAedirHand Hand) const
{
	return Creature && Creature->Interaction ? Creature->Interaction->GetHeldItem(Hand) : nullptr;
}

EAedirLimb UAedirCombatComponent::GetArmLimb(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? EAedirLimb::LeftArm : EAedirLimb::RightArm;
}

void UAedirCombatComponent::SetGuardInternal(EAedirHand Hand, bool bActive)
{
	if (!Creature)
	{
		return;
	}

	bool& GuardFlag = Hand == EAedirHand::Left ? bLeftGuard : bRightGuard;
	const bool bWasActive = GuardFlag;
	GuardFlag = bActive;

	if (bActive && !bWasActive)
	{
		SetGuardRestAim(Hand);
	}

	// Keeps the movement fight stance flag in step on the server and on the owning client for prediction.
	Creature->SetWantsFightStance(IsInFightStance());
}

void UAedirCombatComponent::SetGuardRestAim(EAedirHand Hand)
{
	const float InwardYaw = Hand == EAedirHand::Left ? AedirArmGuardRestYaw : -AedirArmGuardRestYaw;
	GetArm(Hand).AimOffsetDeg = FVector2D(InwardYaw, AedirArmGuardRestPitch);
}

bool UAedirCombatComponent::CanStartSwing(EAedirHand Hand) const
{
	return IsGuardActive(Hand)
		&& !GetSwingState(Hand).bActive
		&& SwingCooldownRemaining[HandIndex(Hand)] <= 0.f
		&& !bShoving
		&& !IsSecondaryGripHand(Hand);
}

bool UAedirCombatComponent::IsArmActive(EAedirHand Hand) const
{
	return IsGuardActive(Hand) || GetSwingState(Hand).bActive;
}

bool UAedirCombatComponent::IsArmPosed(EAedirHand Hand) const
{
	return IsArmActive(Hand) || bShoving || IsSecondaryGripHand(Hand);
}

bool UAedirCombatComponent::IsSecondaryGripHand(EAedirHand Hand) const
{
	return bTwoHandGrip && Hand != GetGripHand();
}

float UAedirCombatComponent::ComputeWeightFactor(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float Weight = HeldItem ? HeldItem->Weight : 0.f;
	const float Strength = Creature && Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;

	return 1.f + Weight / FMath::Max(Strength, 1.f) * AedirWeightSlowdownPerRatio;
}

float UAedirCombatComponent::ComputeSwingDuration(EAedirHand Hand) const
{
	const float Athletism = Creature && Creature->Stats ? Creature->Stats->Athletism : AedirDefaultStat;
	const float AthletismAlpha = FMath::Clamp(Athletism / AedirCombatStatMax, 0.f, 1.f);

	return SwingDurationBase * ComputeWeightFactor(Hand) * FMath::Lerp(1.15f, 0.85f, AthletismAlpha);
}

float UAedirCombatComponent::GetEffectiveWeaponLength(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float Length = HeldItem ? HeldItem->GetWeaponLength() : 0.f;
	return Length > 0.f ? Length : AedirArmFallbackLength;
}

float UAedirCombatComponent::GetArmTotalMass(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	return (HeldItem ? HeldItem->Weight : 0.f) + ForearmMass;
}

bool UAedirCombatComponent::TryStartSwing(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	if (!IsAuthoritative() || !CanStartSwing(Hand))
	{
		return false;
	}

	const float Cost = SwingStaminaCost * ComputeWeightFactor(Hand);
	if (!Creature->Stats || Creature->Stats->GetStamina() < Cost)
	{
		return false;
	}

	// A two-handed grip splits the swing cost, each arm pays half.
	if (bTwoHandGrip)
	{
		Creature->Stats->ConsumeStamina(Cost * 0.5f);
		Creature->Stats->ConsumeStamina(Cost * 0.5f);
	}
	else
	{
		Creature->Stats->ConsumeStamina(Cost);
	}

	StartSwingInternal(Hand, Type, Direction);
	return true;
}

void UAedirCombatComponent::StartSwingInternal(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	FAedirSwingState& State = GetSwingState(Hand);

	State.bActive = true;
	State.Type = Type;
	State.Elapsed = 0.f;
	State.Duration = FMath::Max(ComputeSwingDuration(Hand), AedirMinSwingDuration);
	State.Direction = Direction;

	// The scripted trajectory drives the arm target; the arm dynamics then follow it with weapon inertia.
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.bScriptedControl = true;
	Arm.ScriptedTargetWorld = GetSwingWorldTarget(Hand, Type, 0.f);
}

void UAedirCombatComponent::CancelSwing(EAedirHand Hand)
{
	FAedirSwingState& State = GetSwingState(Hand);
	if (!State.bActive)
	{
		return;
	}

	State.bActive = false;
	State.Elapsed = State.Duration;
	SwingCooldownRemaining[HandIndex(Hand)] = SwingCooldown;
	GetArm(Hand).bScriptedControl = bShoving;
}

void UAedirCombatComponent::UpdateSwings(float DeltaTime)
{
	if (!IsAuthoritative())
	{
		return;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirSwingState& State = GetSwingState(Hand);
		FAedirArmDynamics& Arm = Arms[Index];

		if (!State.bActive)
		{
			// A running shove owns the scripted control of both arms.
			Arm.bScriptedControl = bShoving;
			continue;
		}

		State.Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp(State.Elapsed / State.Duration, 0.f, 1.f);
		Arm.bScriptedControl = true;
		Arm.ScriptedTargetWorld = GetSwingWorldTarget(Hand, State.Type, Alpha);

		if (State.Elapsed >= State.Duration)
		{
			State.bActive = false;
			State.Elapsed = State.Duration;
			SwingCooldownRemaining[Index] = SwingCooldown;
			Arm.bScriptedControl = false;
		}
	}
}

FVector UAedirCombatComponent::GetSwingWorldTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	return Creature->GetActorTransform().TransformPositionNoScale(GetSwingLocalTarget(Hand, Type, Alpha) * Scale);
}

FVector UAedirCombatComponent::GetSwingLocalTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const
{
	// Local space: X is forward, Y is the owner's right, Z is up. Scaled by the owner actor scale by the caller.
	const float Side = Hand == EAedirHand::Left ? -1.f : 1.f;
	const FVector Pivot(0.f, AedirShoulderLateral * Side, AedirShoulderHeight);
	const FVector Guard = Pivot + FVector(AedirGuardForward, -AedirGuardInward * Side, AedirGuardHeight);

	FVector ArcStart = Guard;
	FVector ArcEnd = Pivot + FVector(SwingReach, 0.f, -5.f);

	switch (Type)
	{
	case EAedirSwingType::Hook:
		ArcStart = FVector(20.f, AedirHookOutsideLateral * Side, AedirChestHeight);
		ArcEnd = FVector(60.f, -AedirHookAcrossLateral * Side, AedirChestHeight);
		break;
	case EAedirSwingType::Uppercut:
		ArcStart = FVector(30.f, AedirPunchLateral * Side, -25.f);
		ArcEnd = FVector(55.f, AedirPunchLateral * Side, 30.f);
		break;
	case EAedirSwingType::Overhand:
		ArcStart = FVector(0.f, AedirPunchLateral * Side, 60.f);
		ArcEnd = FVector(60.f, AedirPunchLateral * Side, -10.f);
		break;
	case EAedirSwingType::Jab:
	default:
		break;
	}

	// A quadratic bezier with its control point at the midpoint is a straight line, so jabs stay straight.
	const FVector Control = (ArcStart + ArcEnd) * 0.5f
		+ FVector(Type == EAedirSwingType::Jab ? 0.f : AedirArcBulge, 0.f, 0.f);

	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);

	if (Clamped < AedirSwingWindupEnd)
	{
		const float Eased = CombatSmoothStep01(Clamped / AedirSwingWindupEnd);
		return FMath::Lerp(Guard, ArcStart, Eased)
			+ FVector(-AedirWindupPullBack * FMath::Sin(Eased * UE_PI), 0.f, 0.f);
	}

	if (Clamped <= AedirSwingStrikeEnd)
	{
		const float Eased = CombatSmoothStep01((Clamped - AedirSwingWindupEnd) / (AedirSwingStrikeEnd - AedirSwingWindupEnd));
		return QuadraticBezier(ArcStart, Control, ArcEnd, Eased);
	}

	const float Eased = CombatSmoothStep01((Clamped - AedirSwingStrikeEnd) / (1.f - AedirSwingStrikeEnd));
	return FMath::Lerp(ArcEnd, Guard, Eased);
}

void UAedirCombatComponent::UpdateArms(float DeltaTime)
{
	const bool bProxy = IsSimulatedProxy();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirArmDynamics& Arm = Arms[Index];

		Arm.ShoulderWorld = ComputeArmShoulder(Hand);

		if (!Arm.bInitialized)
		{
			Arm.HandWorld = ComputeArmTarget(Hand, Arm);
			Arm.PrevHandWorld = Arm.HandWorld;
			Arm.PrevTipWorld = ComputeArmTip(Hand, Arm);
			Arm.bInitialized = true;
		}

		UpdateArmActivity(Hand, Arm, DeltaTime);

		if (bProxy)
		{
			ApplyRemoteArmState(Hand, Arm, DeltaTime);
			continue;
		}

		IntegrateArm(Hand, Arm, DeltaTime);

		if (IsAutonomousProxy())
		{
			const FVector ServerHand = GetArmNet(Hand).HandWorld;
			if (!ServerHand.IsNearlyZero() && FVector::Dist(ServerHand, Arm.HandWorld) > AedirArmReconcileDistance)
			{
				Arm.HandWorld = ServerHand;
				Arm.HandVelocity = GetArmNet(Hand).HandVelocity;
			}
		}
	}
}

FVector UAedirCombatComponent::ComputeArmShoulder(EAedirHand Hand) const
{
	const float Side = Hand == EAedirHand::Left ? -1.f : 1.f;
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const FVector Local(0.f, AedirShoulderLateral * Side * Scale, AedirShoulderHeight * Scale);
	return Creature->GetActorTransform().TransformPositionNoScale(Local);
}

FVector UAedirCombatComponent::ComputeArmTarget(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	if (Arm.bScriptedControl)
	{
		return Arm.ScriptedTargetWorld;
	}

	if (IsSecondaryGripHand(Hand))
	{
		return GetGripPointWorld();
	}

	// Centred fists converge slightly inwards and up to head height, so they meet the target instead of running parallel past it.
	const float OutwardSignForAim = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float CentreShare = 1.f - FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSignForAim / AedirArmOutwardYawLimit, 0.f, 1.f);
	const double Yaw = Creature->GetActorRotation().Yaw + Arm.AimOffsetDeg.X - OutwardSignForAim * AedirArmConvergeYawDegrees * CentreShare;
	// Height is relative to where the camera looks, so a punch aimed at the screen centre hits what the crosshair shows.
	const double CameraPitch = FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch);
	const FVector AimDirection = FRotator(CameraPitch + Arm.AimOffsetDeg.Y + AedirArmHeadHeightPitchDegrees * CentreShare, Yaw, 0.0).Vector();
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Extension = FMath::Clamp(Arm.Extension, 0.f, 1.f);
	// Pulled outwards the elbow breaks and the hand travels an arc beside the body; centred the arm reaches straight ahead.
	const float OutwardSign = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float OutwardAlpha = FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSign / AedirArmOutwardYawLimit, 0.f, 1.f);
	const float ElbowBendScale = FMath::Lerp(1.f, 0.5f, OutwardAlpha);
	const float Reach = ElbowBendScale * FMath::Lerp(AedirArmReachMinScale, 1.f, Extension) * (SwingReach + GetEffectiveWeaponLength(Hand)) * Scale;

	return Arm.ShoulderWorld + AimDirection * Reach;
}

FVector UAedirCombatComponent::ComputeArmTip(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	FVector Direction = (Arm.HandWorld - Arm.ShoulderWorld).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Creature ? Creature->GetActorForwardVector() : FVector::ForwardVector;
	}

	const float Scale = Creature ? static_cast<float>(Creature->GetActorScale3D().X) : 1.f;
	return Arm.HandWorld + Direction * (GetEffectiveWeaponLength(Hand) * Scale);
}

void UAedirCombatComponent::IntegrateArm(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
	const float StaminaFraction = Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f;
	const float ArmInjury = GetLimbInjury(GetArmLimb(Hand));
	// A two-handed grip shares the weapon load between both arms, so the stiffness sees only part of the weapon mass.
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float WeaponWeight = HeldItem ? HeldItem->Weight : 0.f;
	const float StiffnessMass = WeaponWeight * (bTwoHandGrip ? 1.f - GripWeaponMassShare : 1.f) + ForearmMass;
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);

	const float StrengthFactor = AedirArmStrengthBase + Strength / AedirCombatStatMax;
	const float StaminaFactor = FMath::Lerp(AedirArmStaminaFloor, 1.f, FMath::Clamp(StaminaFraction, 0.f, 1.f));
	const float InjuryFactor = 1.f - AedirArmInjuryStiffnessLoss * ArmInjury;
	const float DazeFactor = Creature->Balance ? Creature->Balance->GetDazeFactor() : 0.f;
	const float DazeFactorStiffness = 1.f - AedirArmDazeStiffnessLoss * DazeFactor;
	const float MassRatio = StiffnessMass / FMath::Max(Strength * AedirArmMassStrengthScale, 1.f);
	const float GripScale = (bTwoHandGrip && !IsSecondaryGripHand(Hand)) ? GripPrimaryStiffnessScale : 1.f;
	const float ShoveScale = bShoving ? AedirShoveStiffnessScale : 1.f;
	const float Stiffness = ArmStiffnessBase * StrengthFactor * StaminaFactor * InjuryFactor * DazeFactorStiffness * GripScale * ShoveScale / (1.f + MassRatio);
	const float Damping = 2.f * FMath::Sqrt(Stiffness) * AedirArmDampingRatio;
	const float MaxAcceleration = MaxArmAcceleration * StrengthFactor * StaminaFactor * InjuryFactor;
	const float MaxRadius = AedirArmReachHardLimit * (SwingReach + GetEffectiveWeaponLength(Hand)) * Scale;

	const FVector Target = ComputeArmTarget(Hand, Arm);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaTime / AedirArmSubstepSeconds));
	const float StepSeconds = DeltaTime / Steps;

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FVector Acceleration = Stiffness * (Target - Arm.HandWorld) - Damping * Arm.HandVelocity;
		const float AccelerationSize = static_cast<float>(Acceleration.Size());
		if (AccelerationSize > MaxAcceleration)
		{
			Acceleration *= MaxAcceleration / AccelerationSize;
		}

		Arm.HandVelocity += Acceleration * StepSeconds;
		Arm.HandWorld += Arm.HandVelocity * StepSeconds;

		const FVector FromShoulder = Arm.HandWorld - Arm.ShoulderWorld;
		const float Distance = static_cast<float>(FromShoulder.Size());
		if (Distance > MaxRadius && Distance > KINDA_SMALL_NUMBER)
		{
			const FVector Outward = FromShoulder / Distance;
			Arm.HandWorld = Arm.ShoulderWorld + Outward * MaxRadius;

			const float OutwardSpeed = static_cast<float>(FVector::DotProduct(Arm.HandVelocity, Outward));
			if (OutwardSpeed > 0.f)
			{
				Arm.HandVelocity -= Outward * OutwardSpeed;
			}
		}
	}
}

void UAedirCombatComponent::ApplyRemoteArmState(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const FAedirArmNetState& NetState = GetArmNet(Hand);
	const FVector ServerHand = NetState.HandWorld;
	if (ServerHand.IsNearlyZero())
	{
		return;
	}

	Arm.HandWorld = FMath::VInterpTo(Arm.HandWorld, ServerHand, DeltaTime, AedirArmProxyInterpSpeed);
	Arm.HandVelocity = NetState.HandVelocity;
}

void UAedirCombatComponent::PublishArmState(EAedirHand Hand, const FAedirArmDynamics& Arm)
{
	FAedirArmNetState& NetState = GetArmNet(Hand);
	NetState.HandWorld = Arm.HandWorld;
	NetState.HandVelocity = Arm.HandVelocity;
	NetState.Active = IsArmPosed(Hand) ? 1 : 0;
}

void UAedirCombatComponent::SendArmAimToServer(float DeltaTime)
{
	ArmAimSendTimer -= DeltaTime;
	if (ArmAimSendTimer > 0.f)
	{
		return;
	}

	ArmAimSendTimer = AedirArmNetSendInterval;

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!IsGuardActive(Hand))
		{
			continue;
		}

		const FAedirArmDynamics& Arm = Arms[Index];
		ServerUpdateArmAim(Hand, Arm.AimOffsetDeg, Arm.Extension);
	}
}

void UAedirCombatComponent::UpdateArmActivity(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const bool bActive = IsSimulatedProxy() ? GetArmNet(Hand).Active != 0 : IsArmPosed(Hand);
	Arm.ActiveAlpha = FMath::FInterpConstantTo(Arm.ActiveAlpha, bActive ? 1.f : 0.f, DeltaTime, bActive ? 1.f / AedirArmRampInSeconds : 1.f / AedirArmRampOutSeconds);
}

void UAedirCombatComponent::UpdateClashes(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClashCandidateTimer -= DeltaTime;
	if (ClashCandidateTimer <= 0.f)
	{
		ClashCandidateTimer = AedirClashCacheInterval;
		RefreshClashCandidates();
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!IsArmActive(Hand))
		{
			continue;
		}

		FAedirArmDynamics& Arm = Arms[Index];
		const FVector Start = Arm.HandWorld;
		const FVector End = ComputeArmTip(Hand, Arm);

		for (const TWeakObjectPtr<AAedirCreature>& Candidate : ClashCandidates)
		{
			AAedirCreature* Other = Candidate.Get();
			if (!Other || !Other->Combat || (Other->Stats && Other->Stats->IsDead()))
			{
				continue;
			}

			UAedirCombatComponent* OtherCombat = Other->Combat;
			for (int32 OtherIndex = 0; OtherIndex < 2; ++OtherIndex)
			{
				const EAedirHand OtherHand = static_cast<EAedirHand>(OtherIndex);
				if (!OtherCombat->IsArmActive(OtherHand))
				{
					continue;
				}

				const FAedirArmDynamics& OtherArm = OtherCombat->Arms[OtherIndex];

				FVector ClosestSelf = FVector::ZeroVector;
				FVector ClosestOther = FVector::ZeroVector;
				FMath::SegmentDistToSegment(Start, End, OtherArm.HandWorld, OtherCombat->ComputeArmTip(OtherHand, OtherArm), ClosestSelf, ClosestOther);

				const FVector Offset = ClosestSelf - ClosestOther;
				const float CombinedSpeed = static_cast<float>(Arm.HandVelocity.Size() + OtherArm.HandVelocity.Size());
				if (Offset.Size() >= ClashRadius || CombinedSpeed <= ClashMinCombinedSpeed)
				{
					continue;
				}

				ClashedThisTick.AddUnique(TWeakObjectPtr<AActor>(Other));

				if (IsClashCooldownElapsed(Other, Now))
				{
					FVector Normal = Offset.GetSafeNormal();
					if (Normal.IsNearlyZero())
					{
						Normal = FVector::UpVector;
					}

					ResolveClash(Hand, Arm, Other, OtherArm.HandVelocity, Normal, CombinedSpeed, Now);
				}
			}
		}
	}
}

void UAedirCombatComponent::RefreshClashCandidates()
{
	ClashCandidates.Reset();

	UWorld* World = GetWorld();
	if (!World || !Creature)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatClash), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Creature->GetActorLocation(), FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(ClashSearchRadius), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (Candidate && Candidate != Creature)
		{
			ClashCandidates.Add(Candidate);
		}
	}
}

bool UAedirCombatComponent::IsClashCooldownElapsed(const AAedirCreature* Other, double Now) const
{
	for (const FAedirTimedContact& Record : ClashRecords)
	{
		if (Record.Target.Get() == Other && Now - Record.Time < ClashCooldown)
		{
			return false;
		}
	}

	return true;
}

void UAedirCombatComponent::ResolveClash(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Other, const FVector& OtherVelocity, const FVector& Normal, float CombinedSpeed, double Now)
{
	const float MassEff = GetArmTotalMass(Hand);

	Arm.HandVelocity = Arm.HandVelocity * AedirClashSelfReflection + OtherVelocity * AedirClashOtherReflection;

	const float ReducedEnergy = ComputeImpactEnergy(MassEff, CombinedSpeed);
	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(AedirClashEnergyShare * ReducedEnergy * AedirImpactStaminaPerEnergy);
	}

	if (Creature->Balance)
	{
		Creature->Balance->ApplyImpulseImpact(Normal * (MassEff * CombinedSpeed * AedirClashBalanceScale));
	}

	ClashRecords.RemoveAll([this, Now](const FAedirTimedContact& Record)
	{
		return !Record.Target.IsValid() || Now - Record.Time >= ClashCooldown;
	});

	FAedirTimedContact* Record = ClashRecords.FindByPredicate([Other](const FAedirTimedContact& Existing)
	{
		return Existing.Target.Get() == Other;
	});

	if (Record)
	{
		Record->Time = Now;
	}
	else
	{
		FAedirTimedContact NewRecord;
		NewRecord.Target = Other;
		NewRecord.Time = Now;
		ClashRecords.Add(NewRecord);
	}

	OnClash.Broadcast(Other);
}

void UAedirCombatComponent::UpdateImpacts(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || DeltaTime <= KINDA_SMALL_NUMBER || !IsArmActive(Hand))
	{
		return;
	}

	const FVector Tip = ComputeArmTip(Hand, Arm);
	const FVector TipVelocity = (Tip - Arm.PrevTipWorld) / DeltaTime;
	const float Speed = static_cast<float>(FMath::Max(Arm.HandVelocity.Size(), TipVelocity.Size()));
	if (Speed <= AedirArmMinSpeed)
	{
		return;
	}

	const FVector WeaponDirection = (Arm.HandWorld - Arm.ShoulderWorld).GetSafeNormal();
	const float AxialAlignment = static_cast<float>(FMath::Abs(FVector::DotProduct(TipVelocity.GetSafeNormal(), WeaponDirection)));
	const float MassEff = GetArmTotalMass(Hand);
	const double Now = World->GetTimeSeconds();

	ProcessImpactSweep(Hand, Arm, Arm.PrevHandWorld, Arm.HandWorld, TipVelocity, MassEff, AxialAlignment, Now);
	ProcessImpactSweep(Hand, Arm, Arm.PrevTipWorld, Tip, TipVelocity, MassEff, AxialAlignment, Now);
	ProcessImpactSweep(Hand, Arm, (Arm.PrevHandWorld + Arm.PrevTipWorld) * 0.5, (Arm.HandWorld + Tip) * 0.5, TipVelocity, MassEff, AxialAlignment, Now);
}

bool UAedirCombatComponent::IsImpactOnCooldown(EAedirHand Hand, const AActor* Victim, double Now) const
{
	for (const FAedirTimedContact& Record : ImpactRecords[HandIndex(Hand)])
	{
		if (Record.Target.Get() == Victim && Now - Record.Time < ImpactCooldown)
		{
			return true;
		}
	}

	return false;
}

void UAedirCombatComponent::RegisterImpact(EAedirHand Hand, AActor* Victim, double Now)
{
	TArray<FAedirTimedContact>& Records = ImpactRecords[HandIndex(Hand)];
	Records.RemoveAll([this, Now](const FAedirTimedContact& Record)
	{
		return !Record.Target.IsValid() || Now - Record.Time >= ImpactCooldown;
	});

	FAedirTimedContact* Existing = Records.FindByPredicate([Victim](const FAedirTimedContact& Record)
	{
		return Record.Target.Get() == Victim;
	});

	if (Existing)
	{
		Existing->Time = Now;
		return;
	}

	FAedirTimedContact NewRecord;
	NewRecord.Target = Victim;
	NewRecord.Time = Now;
	Records.Add(NewRecord);
}

void UAedirCombatComponent::ProcessImpactSweep(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& From, const FVector& To, const FVector& TipVelocity, float MassEff, float AxialAlignment, double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AAedirItemActor* HeldItem = GetHeldItem(Hand);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatImpact), false);
	Params.AddIgnoredActor(Creature);
	if (HeldItem)
	{
		Params.AddIgnoredActor(HeldItem);
	}

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(HitRadius), Params);

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Creature || HitActor == HeldItem || IsImpactOnCooldown(Hand, HitActor, Now))
		{
			continue;
		}

		if (AAedirCreature* Victim = Cast<AAedirCreature>(HitActor))
		{
			if (ClashedThisTick.Contains(TWeakObjectPtr<AActor>(Victim)))
			{
				continue;
			}

			const float RelativeSpeed = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(TipVelocity - Victim->GetVelocity(), -Hit.ImpactNormal)));
			if (RelativeSpeed < MinImpactSpeed)
			{
				continue;
			}

			RegisterImpact(Hand, Victim, Now);
			ApplyCreatureImpact(Hand, Arm, Victim, Hit, TipVelocity, RelativeSpeed, MassEff, AxialAlignment);
			continue;
		}

		UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (!HitComponent || !HitComponent->IsSimulatingPhysics())
		{
			continue;
		}

		const float RelativeSpeed = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(TipVelocity - HitComponent->GetComponentVelocity(), -Hit.ImpactNormal)));
		if (RelativeSpeed < MinImpactSpeed)
		{
			continue;
		}

		RegisterImpact(Hand, HitActor, Now);
		ApplyPhysicsImpact(Arm, Hit, HitComponent, RelativeSpeed, MassEff);
	}
}

void UAedirCombatComponent::ApplyCreatureImpact(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Victim, const FHitResult& Hit, const FVector& TipVelocity, float RelativeSpeed, float MassEff, float AxialAlignment)
{
	AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const FName Bone = Hit.BoneName;
	const EAedirBodyZone Zone = ClassifyBodyZone(Bone);
	const EAedirDamageType DamageType = HeldItem ? HeldItem->GetDamageType() : EAedirDamageType::Blunt;
	const float FlatDamage = HeldItem ? HeldItem->GetDamage() : FistDamage;
	const float Energy = ComputeImpactEnergy(MassEff, RelativeSpeed);
	const float VictimZoneProtection = Victim->Combat ? Victim->Combat->GetArmorProtection(Zone) : 0.f;
	const float ArmorFactor = 1.f - VictimZoneProtection;

	const float Damage = Energy * ImpactDamageScale * ComputeDamageTypeFactor(DamageType, AxialAlignment) * GetZoneDamageMultiplier(Zone) * ArmorFactor + FlatDamage;
	const float AppliedDamage = Victim->ReceiveCreatureDamage(Damage);

	EAedirLimb Limb = EAedirLimb::LeftArm;
	if (Victim->Combat && GetBoneLimb(Bone, Limb))
	{
		Victim->Combat->ApplyLimbInjury(Limb, Damage * AedirImpactInjuryPerDamage);
	}

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Bone = Bone;
		Impact.Zone = Zone;
		Impact.EnergyJoules = Energy;
		Impact.Direction = TipVelocity.GetSafeNormal();
		Impact.Location = Hit.ImpactPoint;
		Impact.MassKg = MassEff;
		Impact.RelativeSpeed = RelativeSpeed;
		Victim->Balance->ApplyHitReaction(Impact);
	}

	if (Victim->Wounds)
	{
		const FVector TipMotion = ComputeArmTip(Hand, Arm) - Arm.PrevTipWorld;
		Victim->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, DamageType, Energy, VictimZoneProtection, TipMotion);
	}

	Arm.HandVelocity *= AedirImpactRecoilScale;

	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(Energy * AedirImpactStaminaPerEnergy);
		if (HeldItem)
		{
			Creature->Stats->AddSkillExperience(EAedirSkill::SingleHandWeapon, AedirArmedSkillExperience);
		}
	}

	OnImpact.Broadcast(Victim, Bone, Energy, AppliedDamage);
	OnHitDealt.Broadcast(Victim, AppliedDamage);
}

void UAedirCombatComponent::ApplyPhysicsImpact(FAedirArmDynamics& Arm, const FHitResult& Hit, UPrimitiveComponent* HitComponent, float RelativeSpeed, float MassEff)
{
	const float Energy = ComputeImpactEnergy(MassEff, RelativeSpeed);
	HitComponent->AddImpulseAtLocation(-Hit.ImpactNormal * (MassEff * RelativeSpeed * AedirImpactPropImpulseScale), Hit.ImpactPoint);

	Arm.HandVelocity *= AedirImpactRecoilScale;

	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(Energy * AedirImpactStaminaPerEnergy);
	}

	OnImpact.Broadcast(Hit.GetActor(), Hit.BoneName, Energy, 0.f);
}

void UAedirCombatComponent::ApplyLimbInjury(EAedirLimb Limb, float Amount)
{
	const int32 Index = static_cast<int32>(Limb);
	if (!IsAuthoritative() || !LimbInjury.IsValidIndex(Index))
	{
		return;
	}

	LimbInjury[Index] = FMath::Clamp(LimbInjury[Index] + Amount, 0.f, 1.f);
}

void UAedirCombatComponent::HealLimbInjuries(float DeltaTime)
{
	for (float& Injury : LimbInjury)
	{
		Injury = FMath::Max(0.f, Injury - AedirImpactLimbHealPerSecond * DeltaTime);
	}
}

void UAedirCombatComponent::UpdateAI(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AIRetargetTimer -= DeltaTime;
	if (AIRetargetTimer <= 0.f)
	{
		AIRetargetTimer = AedirAIRetargetInterval;
		UpdateAITarget(World->GetTimeSeconds());
	}

	if (AITarget && (!IsValid(AITarget) || (AITarget->Stats && AITarget->Stats->IsDead())))
	{
		AITarget = nullptr;
	}

	if (!AITarget)
	{
		CancelSwing(EAedirHand::Left);
		CancelSwing(EAedirHand::Right);

		if (IsGuardActive(EAedirHand::Left))
		{
			SetGuardInternal(EAedirHand::Left, false);
		}

		if (IsGuardActive(EAedirHand::Right))
		{
			SetGuardInternal(EAedirHand::Right, false);
		}

		bAIApproaching = false;
		AIAttackTimer = 0.f;
		return;
	}

	if (!IsGuardActive(EAedirHand::Left))
	{
		SetGuardInternal(EAedirHand::Left, true);
	}

	if (!IsGuardActive(EAedirHand::Right))
	{
		SetGuardInternal(EAedirHand::Right, true);
	}

	const FVector OwnerLocation = Creature->GetActorLocation();
	const FVector TargetLocation = AITarget->GetActorLocation();
	const float Distance = static_cast<float>(FVector::Dist(OwnerLocation, TargetLocation));

	if (AController* Controller = Creature->GetController())
	{
		const FRotator Desired = (TargetLocation - OwnerLocation).Rotation();
		Controller->SetControlRotation(FMath::RInterpConstantTo(Controller->GetControlRotation(), Desired, DeltaTime, AedirAITurnSpeed));
	}

	if (Distance > AIAttackRange)
	{
		bAIApproaching = true;
	}

	if (bAIApproaching && Distance <= AIApproachSpeedStopDistance)
	{
		bAIApproaching = false;
	}

	if (bAIApproaching)
	{
		Creature->MoveLocal(FVector2D(0.f, 1.f));
	}

	if (Distance > AIAttackRange)
	{
		AIAttackTimer = 0.f;
		return;
	}

	AIAttackTimer += DeltaTime;
	if (AIAttackTimer < AIAttackInterval)
	{
		return;
	}

	AIAttackTimer = 0.f;

	if (Distance <= AedirAIContactRange && !GetSwingState(EAedirHand::Left).bActive && !GetSwingState(EAedirHand::Right).bActive)
	{
		if (FMath::FRand() < AedirAIShoveChance)
		{
			StartShove();
		}

		if (FMath::FRand() < AedirAIKickChance)
		{
			StartKick();
		}
	}

	const EAedirSwingType Type = Distance < AedirAICloseRange
		? (FMath::RandBool() ? EAedirSwingType::Hook : EAedirSwingType::Uppercut)
		: EAedirSwingType::Jab;

	const EAedirHand FirstHand = bAINextLeft ? EAedirHand::Left : EAedirHand::Right;
	const EAedirHand SecondHand = bAINextLeft ? EAedirHand::Right : EAedirHand::Left;

	if (StartSwing(FirstHand, Type))
	{
		bAINextLeft = false;
	}
	else if (StartSwing(SecondHand, Type))
	{
		bAINextLeft = true;
	}
}

void UAedirCombatComponent::UpdateAITarget(double Now)
{
	UWorld* World = GetWorld();
	UAedirGroupSubsystem* Groups = World ? World->GetSubsystem<UAedirGroupSubsystem>() : nullptr;
	if (!Groups)
	{
		return;
	}

	const FVector OwnerLocation = Creature->GetActorLocation();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatSight), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, OwnerLocation, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(AISightRange), Params);

	AAedirCreature* BestTarget = nullptr;
	double BestDistanceSquared = 0.0;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (!Candidate || (Candidate->Stats && Candidate->Stats->IsDead()))
		{
			continue;
		}

		if (Groups->GetRelation(Creature->GroupId, Candidate->GroupId) != EAedirRelation::Enemy || !CanSeeTarget(Candidate))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(OwnerLocation, Candidate->GetActorLocation());
		if (!BestTarget || DistanceSquared < BestDistanceSquared)
		{
			BestTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestTarget)
	{
		AITarget = BestTarget;
		AILastSeenTime = Now;
		return;
	}

	if (AITarget && (!IsValid(AITarget) || (AITarget->Stats && AITarget->Stats->IsDead()) || Now - AILastSeenTime > AedirAIMemorySeconds))
	{
		AITarget = nullptr;
	}
}

bool UAedirCombatComponent::CanSeeTarget(const AAedirCreature* Target) const
{
	UWorld* World = GetWorld();
	if (!World || !Creature || !Target)
	{
		return false;
	}

	const FVector EyeLocation = Creature->GetPawnViewLocation();
	const FVector TargetLocation = Target->GetPawnViewLocation();
	const FVector ToTarget = (TargetLocation - EyeLocation).GetSafeNormal();

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(AISightHalfAngleDeg));
	if (FVector::DotProduct(Creature->GetActorForwardVector(), ToTarget) < CosHalfAngle)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatVisibility), false);
	Params.AddIgnoredActor(Creature);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, EyeLocation, TargetLocation, ECC_Visibility, Params);
}

bool UAedirCombatComponent::IsTwoHandGrip() const
{
	return bTwoHandGrip;
}

EAedirHand UAedirCombatComponent::GetGripHand() const
{
	const bool bLeftHeld = GetHeldItem(EAedirHand::Left) != nullptr;
	const bool bRightHeld = GetHeldItem(EAedirHand::Right) != nullptr;
	if (bLeftHeld && bRightHeld)
	{
		// A two-handed item occupies both hands. Its own held hand is the primary one.
		const AAedirItemActor* SharedItem = GetHeldItem(EAedirHand::Left);
		return SharedItem ? SharedItem->HeldHand : EAedirHand::Left;
	}

	return bLeftHeld ? EAedirHand::Left : EAedirHand::Right;
}

bool UAedirCombatComponent::EvaluateTwoHandGrip() const
{
	const AAedirItemActor* LeftItem = GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* RightItem = GetHeldItem(EAedirHand::Right);
	if (!LeftItem && !RightItem)
	{
		return false;
	}

	// A two-handed item is held by both hands as the same item, which does not count as holding a second item.
	if (LeftItem && RightItem && LeftItem != RightItem)
	{
		return false;
	}

	const AAedirItemActor* Weapon = LeftItem ? LeftItem : RightItem;
	return Weapon->bTwoHanded || (bLeftGuard && bRightGuard);
}

void UAedirCombatComponent::UpdateTwoHandGrip()
{
	bTwoHandGrip = EvaluateTwoHandGrip();
}

FVector UAedirCombatComponent::GetGripPointWorld() const
{
	const FAedirArmDynamics& PrimaryArm = GetArm(GetGripHand());
	FVector WeaponDirection = (PrimaryArm.HandWorld - PrimaryArm.ShoulderWorld).GetSafeNormal();
	if (WeaponDirection.IsNearlyZero())
	{
		WeaponDirection = Creature ? Creature->GetActorForwardVector() : FVector::ForwardVector;
	}

	return PrimaryArm.HandWorld + WeaponDirection * GripSpacingCm;
}

float UAedirCombatComponent::ComputeShoveEnergy(float Strength, float StaminaFraction)
{
	const float StaminaScale = FMath::Lerp(AedirShoveMinStaminaScale, 1.f, FMath::Clamp(StaminaFraction, 0.f, 1.f));
	return (AedirShoveBaseJoules + FMath::Max(Strength, 0.f) * AedirShoveStrengthJoules) * StaminaScale;
}

FVector UAedirCombatComponent::EvaluateKickArc(const FVector& Start, const FVector& Hip, const FVector& Forward, float T01)
{
	FVector Ahead = Forward.GetSafeNormal2D();
	if (Ahead.IsNearlyZero())
	{
		Ahead = FVector::ForwardVector;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	const FVector Chamber = Hip - Ahead * AedirKickChamberBehindCm + Right * AedirKickChamberRightCm;
	const FVector Extension = Hip + Ahead * AedirKickExtendCm + FVector(0.f, 0.f, AedirKickExtendHeightCm - AedirKickGroundBelowHipCm);
	const float T = FMath::Clamp(T01, 0.f, 1.f);

	if (T < AedirKickChamberTime)
	{
		return FMath::Lerp(Start, Chamber, CombatSmoothStep01(T / AedirKickChamberTime));
	}

	if (T < AedirKickExtendTime)
	{
		return FMath::Lerp(Chamber, Extension, CombatSmoothStep01((T - AedirKickChamberTime) / (AedirKickExtendTime - AedirKickChamberTime)));
	}

	return FMath::Lerp(Extension, Start, CombatSmoothStep01((T - AedirKickExtendTime) / (1.f - AedirKickExtendTime)));
}

bool UAedirCombatComponent::IsKicking() const
{
	return KickNet.bActive;
}

bool UAedirCombatComponent::GetKickPose(bool& bOutLeftLeg, FVector& OutFootTargetWorld, float& OutAlpha) const
{
	bOutLeftLeg = KickNet.bLeftLeg;
	OutAlpha = FMath::Clamp(KickPoseAlpha, 0.f, 1.f);
	OutFootTargetWorld = FVector::ZeroVector;
	if (!Creature || OutAlpha <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float T01 = FMath::Clamp(KickNet.Elapsed / AedirKickDuration, 0.f, 1.f);
	OutFootTargetWorld = EvaluateKickArc(KickNet.StartWorld, GetPelvisWorld(), Creature->GetActorForwardVector(), T01);
	return true;
}

bool UAedirCombatComponent::StartShove()
{
	if (!Creature)
	{
		return false;
	}

	if (IsAuthoritative())
	{
		return TryStartShove();
	}

	if (!Creature->IsLocallyControlled() || !CanStartShove())
	{
		return false;
	}

	BeginShove();
	ServerStartShove();
	return true;
}

bool UAedirCombatComponent::StartKick()
{
	if (!Creature)
	{
		return false;
	}

	if (IsAuthoritative())
	{
		return TryStartKick();
	}

	if (!Creature->IsLocallyControlled() || !CanStartKick())
	{
		return false;
	}

	BeginKick();
	ServerStartKick();
	return true;
}

bool UAedirCombatComponent::CanStartShove() const
{
	if (!Creature || !Creature->Stats || bShoving || ShoveCooldownRemaining > 0.f)
	{
		return false;
	}

	// A shove needs both arms free: a running swing on either hand blocks it.
	if (Creature->Stats->GetStamina() < ShoveStaminaCost || GetSwingState(EAedirHand::Left).bActive || GetSwingState(EAedirHand::Right).bActive)
	{
		return false;
	}

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		const AAedirItemActor* HeldItem = GetHeldItem(Hand);
		if ((HeldItem && HeldItem->Weight > AedirShoveHeavyItemKg) || GetLimbInjury(GetArmLimb(Hand)) > AedirShoveArmInjuryLimit)
		{
			return false;
		}
	}

	return true;
}

bool UAedirCombatComponent::TryStartShove()
{
	if (!IsAuthoritative() || !CanStartShove() || !Creature->Stats->ConsumeStamina(ShoveStaminaCost))
	{
		return false;
	}

	BeginShove();
	return true;
}

void UAedirCombatComponent::BeginShove()
{
	bShoving = true;
	ShoveElapsed = 0.f;
	bShoveStruck = false;
	ShoveCooldownRemaining = ShoveCooldown;

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		FAedirArmDynamics& Arm = GetArm(Hand);
		Arm.bScriptedControl = true;
		Arm.ScriptedTargetWorld = GetShoveWorldTarget(Hand, 0.f);
	}
}

void UAedirCombatComponent::EndShove()
{
	bShoving = false;
	ShoveElapsed = 0.f;

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (!GetSwingState(Hand).bActive)
		{
			GetArm(Hand).bScriptedControl = false;
		}
	}
}

void UAedirCombatComponent::UpdateShove(float DeltaTime)
{
	if (!bShoving || !Creature)
	{
		return;
	}

	ShoveElapsed += DeltaTime;

	if (!bShoveStruck && ShoveElapsed >= AedirShoveStrikeSeconds)
	{
		bShoveStruck = true;
		if (IsAuthoritative())
		{
			ResolveShoveStrike();
		}
	}

	if (ShoveElapsed >= AedirShoveDuration)
	{
		EndShove();
		return;
	}

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		GetArm(Hand).ScriptedTargetWorld = GetShoveWorldTarget(Hand, ShoveElapsed);
	}
}

FVector UAedirCombatComponent::GetShoveWorldTarget(EAedirHand Hand, float Elapsed) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	// Both hands thrust to the same point in front of the chest, then draw back to the guard.
	const FVector Guard = GetSwingLocalTarget(Hand, EAedirSwingType::Jab, 0.f);
	const FVector Thrust(AedirShoveReachCm, 0.f, AedirChestHeight);

	float Alpha = 0.f;
	if (Elapsed < AedirShoveStrikeSeconds)
	{
		Alpha = CombatSmoothStep01(Elapsed / AedirShoveStrikeSeconds);
	}
	else
	{
		Alpha = 1.f - CombatSmoothStep01((Elapsed - AedirShoveStrikeSeconds) / (AedirShoveDuration - AedirShoveStrikeSeconds));
	}

	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	return Creature->GetActorTransform().TransformPositionNoScale(FMath::Lerp(Guard, Thrust, Alpha) * Scale);
}

void UAedirCombatComponent::ResolveShoveStrike()
{
	UWorld* World = GetWorld();
	if (!World || !Creature)
	{
		return;
	}

	const FVector OwnerLocation = Creature->GetActorLocation();
	const FVector Forward = Creature->GetActorForwardVector().GetSafeNormal2D();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatShove), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, OwnerLocation, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(ShoveRange), Params);

	AAedirCreature* Victim = nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (!Candidate || Candidate == Creature || (Candidate->Stats && Candidate->Stats->IsDead()))
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - OwnerLocation;
		if (ToCandidate.Size() > ShoveRange || FVector::DotProduct(Forward, ToCandidate.GetSafeNormal2D()) <= AedirShoveConeCos)
		{
			continue;
		}

		Victim = Candidate;
		break;
	}

	if (!Victim)
	{
		return;
	}

	const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
	const float StaminaFraction = Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f;

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Zone = EAedirBodyZone::Torso;
		Impact.EnergyJoules = ComputeShoveEnergy(Strength, StaminaFraction);
		Impact.Direction = Forward;
		Impact.Location = Victim->GetActorLocation();
		Victim->Balance->ApplyHitReaction(Impact);
	}

	// The shove pushes back on the attacker. A shove does not wound.
	if (Creature->Balance)
	{
		Creature->Balance->ApplyBalanceImpact(-Forward, AedirShoveRecoilBalance);
	}

	OnShove.Broadcast(Victim);
}

bool UAedirCombatComponent::CanStartKick() const
{
	if (!Creature || !Creature->Stats || KickNet.bActive || KickCooldownRemaining > 0.f)
	{
		return false;
	}

	if (Creature->Stats->GetStamina() < KickStaminaCost)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Creature->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}

	if (Creature->Balance && (Creature->Balance->IsRagdoll() || Creature->Balance->GetBalance() < AedirKickMinBalance))
	{
		return false;
	}

	return true;
}

bool UAedirCombatComponent::TryStartKick()
{
	if (!IsAuthoritative() || !CanStartKick() || !Creature->Stats->ConsumeStamina(KickStaminaCost))
	{
		return false;
	}

	BeginKick();

	// Standing on one leg costs balance.
	if (Creature->Balance)
	{
		Creature->Balance->ApplyBalanceImpact(-Creature->GetActorForwardVector(), AedirKickRecoilBalance);
	}

	return true;
}

void UAedirCombatComponent::BeginKick()
{
	const bool bLeftLeg = IsLeadFootRight();
	KickNet.bActive = true;
	KickNet.bLeftLeg = bLeftLeg;
	KickNet.Elapsed = 0.f;
	KickNet.StartWorld = GetFootBoneWorld(bLeftLeg);
	KickPrevFootWorld = KickNet.StartWorld;
	bKickStruck = false;
	KickCooldownRemaining = KickCooldown;
}

void UAedirCombatComponent::UpdateKick(float DeltaTime)
{
	KickPoseAlpha = FMath::FInterpConstantTo(KickPoseAlpha, KickNet.bActive ? 1.f : 0.f, DeltaTime,
		KickNet.bActive ? 1.f / AedirKickPoseRiseSeconds : 1.f / AedirKickPoseFallSeconds);

	// A knockdown ends the kick at once.
	if (IsAuthoritative() && KickNet.bActive && Creature->Balance && Creature->Balance->IsRagdoll())
	{
		KickNet.bActive = false;
	}

	if (!KickNet.bActive || !Creature)
	{
		return;
	}

	KickNet.Elapsed = FMath::Min(KickNet.Elapsed + DeltaTime, AedirKickDuration);

	if (IsAuthoritative())
	{
		const FVector FootWorld = GetFootBoneWorld(KickNet.bLeftLeg);
		if (!bKickStruck && KickNet.Elapsed >= AedirKickStrikeStart && KickNet.Elapsed <= AedirKickStrikeEnd)
		{
			ResolveKickSweep(KickPrevFootWorld, FootWorld, DeltaTime);
		}

		KickPrevFootWorld = FootWorld;

		if (KickNet.Elapsed >= AedirKickDuration)
		{
			KickNet.bActive = false;
		}
	}
}

void UAedirCombatComponent::ResolveKickSweep(const FVector& From, const FVector& To, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !Creature || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatKick), false);
	Params.AddIgnoredActor(Creature);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(AedirKickHitRadius), Params);

	const FVector FootMotion = To - From;
	const float FootSpeed = FMath::Min(static_cast<float>(FootMotion.Size() / DeltaTime), AedirKickMaxFootSpeed);

	for (const FHitResult& Hit : Hits)
	{
		AAedirCreature* Victim = Cast<AAedirCreature>(Hit.GetActor());
		if (!Victim || Victim == Creature)
		{
			continue;
		}

		ApplyKickImpact(Victim, Hit, FootMotion, FootSpeed);
		bKickStruck = true;
		return;
	}
}

void UAedirCombatComponent::ApplyKickImpact(AAedirCreature* Victim, const FHitResult& Hit, const FVector& FootMotion, float FootSpeed)
{
	const FName Bone = Hit.BoneName;
	EAedirBodyZone Zone = ClassifyBodyZone(Bone);
	if (Zone != EAedirBodyZone::Leg)
	{
		Zone = EAedirBodyZone::Torso;
	}

	const float Energy = ComputeImpactEnergy(AedirKickMassKg, FootSpeed) * AedirKickEnergyScale;
	const float VictimZoneProtection = Victim->Combat ? Victim->Combat->GetArmorProtection(Zone) : 0.f;
	const float Damage = Energy * ImpactDamageScale * ComputeDamageTypeFactor(EAedirDamageType::Blunt, 0.f) * GetZoneDamageMultiplier(Zone) * (1.f - VictimZoneProtection);
	Victim->ReceiveCreatureDamage(Damage);

	EAedirLimb Limb = EAedirLimb::LeftArm;
	if (Victim->Combat && GetBoneLimb(Bone, Limb))
	{
		Victim->Combat->ApplyLimbInjury(Limb, Damage * AedirImpactInjuryPerDamage);
	}

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Bone = Bone;
		Impact.Zone = Zone;
		Impact.EnergyJoules = Energy;
		Impact.Direction = FootMotion.GetSafeNormal();
		Impact.Location = Hit.ImpactPoint;
		Impact.MassKg = AedirKickMassKg;
		Impact.RelativeSpeed = FootSpeed;
		Victim->Balance->ApplyHitReaction(Impact);
	}

	if (Victim->Wounds)
	{
		Victim->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, EAedirDamageType::Blunt, Energy, VictimZoneProtection, FootMotion);
	}

	OnKick.Broadcast(Victim);
}

FVector UAedirCombatComponent::GetFootBoneWorld(bool bLeftLeg) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	if (!Mesh)
	{
		return Creature->GetActorLocation();
	}

	return Mesh->GetBoneLocation(bLeftLeg ? AedirKickFootLeftBone : AedirKickFootRightBone);
}

FVector UAedirCombatComponent::GetPelvisWorld() const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	return Mesh ? Mesh->GetBoneLocation(AedirKickPelvisBone) : Creature->GetActorLocation();
}

void UAedirCombatComponent::ServerStartShove_Implementation()
{
	TryStartShove();
}

void UAedirCombatComponent::ServerStartKick_Implementation()
{
	TryStartKick();
}

void UAedirCombatComponent::ServerSetGuard_Implementation(EAedirHand Hand, bool bActive)
{
	SetGuardInternal(Hand, bActive);
}

void UAedirCombatComponent::ServerUpdateArmAim_Implementation(EAedirHand Hand, FVector2D Aim, float Extension)
{
	if (!IsGuardActive(Hand) || IsSecondaryGripHand(Hand))
	{
		return;
	}

	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.AimOffsetDeg = FVector2D(
		FMath::Clamp(Aim.X, -AedirArmAimYawLimit, AedirArmAimYawLimit),
		FMath::Clamp(Aim.Y, AedirArmAimPitchMin, AedirArmAimPitchMax));
	Arm.Extension = FMath::Clamp(Extension, 0.f, 1.f);
}
