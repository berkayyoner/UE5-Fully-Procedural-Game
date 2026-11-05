#include "Combat/AedirCombatComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirGroupSubsystem.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"

namespace
{
	constexpr float AedirCombatStatMax = 100.f;
	constexpr float AedirDefaultStat = 10.f;
	constexpr float AedirMouseVelocityWindow = 0.1f;
	constexpr float AedirGuardBlendSpeed = 6.f;
	constexpr float AedirSwingRampInSeconds = 0.08f;
	constexpr float AedirSwingRampOutSeconds = 0.15f;
	constexpr float AedirMinSwingDuration = 0.05f;
	constexpr float AedirWeightSlowdownPerRatio = 0.35f;
	constexpr float AedirMinSpeedFactor = 0.2f;
	constexpr float AedirMaxSpeedFactor = 1.5f;
	constexpr float AedirArmedSkillExperience = 0.05f;
	constexpr float AedirPhysicsPropImpulse = 800.f;

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

	float SmoothStep01(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	FVector QuadraticBezier(const FVector& A, const FVector& Control, const FVector& B, float T)
	{
		const float Inverse = 1.f - T;
		return A * (Inverse * Inverse) + Control * (2.f * Inverse * T) + B * (T * T);
	}

	// Direction is in view space: X is mouse right, Y is mouse up.
	EAedirSwingType DetermineSwingType(EAedirHand Hand, const FVector2D& Direction)
	{
		if (FMath::Abs(Direction.Y) >= FMath::Abs(Direction.X))
		{
			return Direction.Y > 0.f ? EAedirSwingType::Uppercut : EAedirSwingType::Overhand;
		}

		const float Inward = Hand == EAedirHand::Left ? Direction.X : -Direction.X;
		return Inward > 0.f ? EAedirSwingType::Hook : EAedirSwingType::Jab;
	}
}

UAedirCombatComponent::UAedirCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
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

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		const bool bSwinging = GetSwingState(Hand).bActive;

		GuardAlpha[Index] = FMath::FInterpConstantTo(GuardAlpha[Index], IsGuardActive(Hand) ? 1.f : 0.f, DeltaTime, AedirGuardBlendSpeed);
		SwingAlpha[Index] = FMath::FInterpConstantTo(
			SwingAlpha[Index],
			bSwinging ? 1.f : 0.f,
			DeltaTime,
			bSwinging ? 1.f / AedirSwingRampInSeconds : 1.f / AedirSwingRampOutSeconds);
	}

	UpdateSwings(DeltaTime);

	if (IsAuthoritative() && !Creature->IsPlayerControlled())
	{
		UpdateAI(DeltaTime);
	}
}

void UAedirCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirCombatComponent, bLeftGuard);
	DOREPLIFETIME(UAedirCombatComponent, bRightGuard);
	DOREPLIFETIME(UAedirCombatComponent, LeftSwing);
	DOREPLIFETIME(UAedirCombatComponent, RightSwing);
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
	if (!Creature || !Creature->IsLocallyControlled() || DeltaTime <= 0.f)
	{
		return;
	}

	const int32 Index = HandIndex(Hand);

	// Raw MouseY is positive when the mouse moves down, so it is flipped into a Y-up swing space.
	const FVector2D ScreenDelta(MouseDeltaThisFrame.X, -MouseDeltaThisFrame.Y);
	const float Decay = FMath::Exp(-DeltaTime / AedirMouseVelocityWindow);
	MouseVelocity[Index] = MouseVelocity[Index] * Decay + (ScreenDelta / DeltaTime) * (1.f - Decay);

	if (!CanStartSwing(Hand) || MouseVelocity[Index].Size() < MinSwingMouseSpeed)
	{
		return;
	}

	const FVector2D Direction = MouseVelocity[Index].GetSafeNormal();
	const EAedirSwingType Type = DetermineSwingType(Hand, Direction);
	MouseVelocity[Index] = FVector2D::ZeroVector;

	if (IsAuthoritative())
	{
		TryStartSwing(Hand, Type, Direction);
		return;
	}

	StartSwingInternal(Hand, Type, Direction);
	ServerStartSwing(Hand, Type, Direction);
}

bool UAedirCombatComponent::StartSwing(EAedirHand Hand, EAedirSwingType Type)
{
	return TryStartSwing(Hand, Type, FVector2D::ZeroVector);
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
	OutAlpha = SwingAlpha[HandIndex(Hand)];
	if (OutAlpha <= 0.001f)
	{
		return false;
	}

	const FAedirSwingState& State = GetSwingState(Hand);
	const float Alpha = State.bActive && State.Duration > 0.f ? FMath::Clamp(State.Elapsed / State.Duration, 0.f, 1.f) : 1.f;
	OutHandTargetWorld = GetSwingWorldTarget(Hand, State.Type, Alpha);
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

bool UAedirCombatComponent::IsAuthoritative() const
{
	return Creature && Creature->HasAuthority();
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

AAedirItemActor* UAedirCombatComponent::GetHeldItem(EAedirHand Hand) const
{
	return Creature && Creature->Interaction ? Creature->Interaction->GetHeldItem(Hand) : nullptr;
}

void UAedirCombatComponent::SetGuardInternal(EAedirHand Hand, bool bActive)
{
	if (!Creature)
	{
		return;
	}

	bool& GuardFlag = Hand == EAedirHand::Left ? bLeftGuard : bRightGuard;
	GuardFlag = bActive;
	MouseVelocity[HandIndex(Hand)] = FVector2D::ZeroVector;

	// Keeps the movement fight stance flag in step on the server and on the owning client for prediction.
	Creature->SetWantsFightStance(IsInFightStance());
}

bool UAedirCombatComponent::CanStartSwing(EAedirHand Hand) const
{
	return IsGuardActive(Hand)
		&& !GetSwingState(Hand).bActive
		&& SwingCooldownRemaining[HandIndex(Hand)] <= 0.f;
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

bool UAedirCombatComponent::TryStartSwing(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	if (!IsAuthoritative() || !CanStartSwing(Hand))
	{
		return false;
	}

	if (!Creature->Stats || !Creature->Stats->ConsumeStamina(SwingStaminaCost * ComputeWeightFactor(Hand)))
	{
		return false;
	}

	StartSwingInternal(Hand, Type, Direction);
	return true;
}

void UAedirCombatComponent::StartSwingInternal(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	const int32 Index = HandIndex(Hand);
	FAedirSwingState& State = GetSwingState(Hand);

	State.bActive = true;
	State.Type = Type;
	State.Elapsed = 0.f;
	State.Duration = FMath::Max(ComputeSwingDuration(Hand), AedirMinSwingDuration);
	State.Direction = Direction;

	bSwingHitRegistered[Index] = false;
	PreviousHandTarget[Index] = GetSwingWorldTarget(Hand, Type, 0.f);
}

void UAedirCombatComponent::UpdateSwings(float DeltaTime)
{
	const bool bAuthority = IsAuthoritative();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirSwingState& State = GetSwingState(Hand);

		SwingCooldownRemaining[Index] = FMath::Max(0.f, SwingCooldownRemaining[Index] - DeltaTime);

		if (!State.bActive)
		{
			PreviousHandTarget[Index] = GetSwingWorldTarget(Hand, State.Type, 1.f);
			continue;
		}

		State.Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp(State.Elapsed / State.Duration, 0.f, 1.f);
		const FVector CurrentTarget = GetSwingWorldTarget(Hand, State.Type, Alpha);

		if (bAuthority && Alpha >= AedirSwingWindupEnd && Alpha <= AedirSwingStrikeEnd)
		{
			ProcessStrikeHit(Hand, State, PreviousHandTarget[Index], CurrentTarget);
		}

		PreviousHandTarget[Index] = CurrentTarget;

		if (State.Elapsed >= State.Duration)
		{
			State.bActive = false;
			State.Elapsed = State.Duration;
			SwingCooldownRemaining[Index] = SwingCooldown;
		}
	}
}

void UAedirCombatComponent::ProcessStrikeHit(EAedirHand Hand, const FAedirSwingState& State, const FVector& From, const FVector& To)
{
	const int32 Index = HandIndex(Hand);
	if (bSwingHitRegistered[Index])
	{
		return;
	}

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

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatStrike), false);
	Params.AddIgnoredActor(Creature);
	if (HeldItem)
	{
		Params.AddIgnoredActor(HeldItem);
	}

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(HitRadius), Params);

	FVector Direction = (To - From).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Creature->GetActorForwardVector();
	}

	for (const FHitResult& Hit : Hits)
	{
		if (AAedirCreature* Victim = Cast<AAedirCreature>(Hit.GetActor()))
		{
			if (Victim == Creature)
			{
				continue;
			}

			bSwingHitRegistered[Index] = true;
			ApplyCreatureHit(Hand, State, Victim, Direction);
			return;
		}

		UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (HitComponent && HitComponent->IsSimulatingPhysics())
		{
			const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
			bSwingHitRegistered[Index] = true;
			HitComponent->AddImpulseAtLocation(Direction * (AedirPhysicsPropImpulse * Strength / AedirDefaultStat), Hit.ImpactPoint);
			return;
		}
	}
}

void UAedirCombatComponent::ApplyCreatureHit(EAedirHand Hand, const FAedirSwingState& State, AAedirCreature* Victim, const FVector& Direction)
{
	AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float BaseDamage = HeldItem ? HeldItem->GetDamage() : FistDamage;
	const float Strength = Creature->Stats ? Creature->Stats->Strength : 0.f;
	const float SpeedFactor = FMath::Clamp(SwingDurationBase / State.Duration, AedirMinSpeedFactor, AedirMaxSpeedFactor);
	const float Damage = (BaseDamage + Strength * StrengthDamageScale) * SpeedFactor;
	const float AppliedDamage = Victim->ReceiveCreatureDamage(Damage);

	if (Victim->Balance)
	{
		const float DamageRatio = Damage / FMath::Max(FistDamage, KINDA_SMALL_NUMBER);
		Victim->Balance->ApplyImpulseImpact(Direction * (BalanceImpactScale * DamageRatio));
	}

	if (HeldItem && Creature->Stats)
	{
		Creature->Stats->AddSkillExperience(EAedirSkill::SingleHandWeapon, AedirArmedSkillExperience);
	}

	OnHitDealt.Broadcast(Victim, AppliedDamage);
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
		const float Eased = SmoothStep01(Clamped / AedirSwingWindupEnd);
		return FMath::Lerp(Guard, ArcStart, Eased)
			+ FVector(-AedirWindupPullBack * FMath::Sin(Eased * UE_PI), 0.f, 0.f);
	}

	if (Clamped <= AedirSwingStrikeEnd)
	{
		const float Eased = SmoothStep01((Clamped - AedirSwingWindupEnd) / (AedirSwingStrikeEnd - AedirSwingWindupEnd));
		return QuadraticBezier(ArcStart, Control, ArcEnd, Eased);
	}

	const float Eased = SmoothStep01((Clamped - AedirSwingStrikeEnd) / (1.f - AedirSwingStrikeEnd));
	return FMath::Lerp(ArcEnd, Guard, Eased);
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

	if (!AITarget)
	{
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

void UAedirCombatComponent::ServerSetGuard_Implementation(EAedirHand Hand, bool bActive)
{
	SetGuardInternal(Hand, bActive);
}

void UAedirCombatComponent::ServerStartSwing_Implementation(EAedirHand Hand, EAedirSwingType Type, FVector2D Direction)
{
	TryStartSwing(Hand, Type, Direction);
}
