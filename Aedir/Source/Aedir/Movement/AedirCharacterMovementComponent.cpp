#include "AedirCharacterMovementComponent.h"

#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Character.h"
#include "Physics/AedirBalanceComponent.h"
#include "Surface/AedirFootstepComponent.h"

namespace
{
	constexpr float AedirLegInjurySpeedPenalty = 0.45f;
	constexpr float AedirDazeSpeedPenalty = 0.3f;
	constexpr float AedirStaggerSpeedMultiplier = 0.8f;
	constexpr float AedirForwardArcDegrees = 45.f;
	constexpr float AedirSideArcDegrees = 90.f;
	constexpr float AedirBackArcDegrees = 135.f;
	constexpr double AedirRunMinSpeedSquared = 100.0;
	constexpr float AedirBodyMovingSpeed = 20.f;
	constexpr float AedirTurnInPlaceFinishYaw = 5.f;

	float SmoothStep01(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}
}

class FSavedMove_Aedir : public FSavedMove_Character
{
public:
	bool bSavedWantsToRun = false;
	bool bSavedWantsFightStance = false;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();

		bSavedWantsToRun = false;
		bSavedWantsFightStance = false;
	}

	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);

		if (const UAedirCharacterMovementComponent* Movement = C ? Cast<UAedirCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
		{
			bSavedWantsToRun = Movement->bWantsToRun;
			bSavedWantsFightStance = Movement->bWantsFightStance;
		}
	}

	virtual void PrepMoveFor(ACharacter* C) override
	{
		FSavedMove_Character::PrepMoveFor(C);

		if (UAedirCharacterMovementComponent* Movement = C ? Cast<UAedirCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr)
		{
			Movement->bWantsToRun = bSavedWantsToRun;
			Movement->bWantsFightStance = bSavedWantsFightStance;
		}
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = FSavedMove_Character::GetCompressedFlags();

		if (bSavedWantsToRun)
		{
			Result |= FLAG_Custom_0;
		}

		if (bSavedWantsFightStance)
		{
			Result |= FLAG_Custom_1;
		}

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSavedMove_Aedir* NewAedirMove = static_cast<const FSavedMove_Aedir*>(NewMove.Get());
		if (!NewAedirMove
			|| bSavedWantsToRun != NewAedirMove->bSavedWantsToRun
			|| bSavedWantsFightStance != NewAedirMove->bSavedWantsFightStance)
		{
			return false;
		}

		return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}
};

class FNetworkPredictionData_Client_Aedir : public FNetworkPredictionData_Client_Character
{
public:
	FNetworkPredictionData_Client_Aedir(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return MakeShared<FSavedMove_Aedir>();
	}
};

UAedirCharacterMovementComponent::UAedirCharacterMovementComponent()
{
	bCanWalkOffLedgesWhenCrouching = true;
	GetNavAgentPropertiesRef().bCanCrouch = true;
	SetCrouchedHalfHeight(55.f);
	JumpZVelocity = 420.f;
	AirControl = 0.25f;
	// Soft acceleration and braking give the body weight: a walk takes about half a second to reach speed.
	// Ground friction steers the velocity towards the input direction, so it stays high; the soft stop comes from the separate braking friction.
	bUseSeparateBrakingFriction = true;
	BrakingFrictionFactor = 1.f;
	BrakingFriction = 1.2f;
	BaseGroundFriction = 8.f;
	BaseBrakingDecelerationWalking = 160.f;
	BaseMaxAcceleration = 240.f;

	GroundFriction = BaseGroundFriction;
	BrakingDecelerationWalking = BaseBrakingDecelerationWalking;
	MaxAcceleration = BaseMaxAcceleration;
}

void UAedirCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UpdateSurfaceFriction();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (JumpWindupRemaining > 0.f)
	{
		JumpWindupRemaining -= DeltaTime;
		if (JumpWindupRemaining <= 0.f && GetCharacterOwner())
		{
			JumpWindupRemaining = 0.f;
			GetCharacterOwner()->Jump();
		}
	}

	const ACharacter* Character = GetCharacterOwner();
	UAedirStatsComponent* Stats = GetOwnerStats();
	if (!bWantsToRun || !Stats || !Character || !Character->HasAuthority())
	{
		return;
	}

	if (IsCrouching() || bWantsFightStance || !IsMovingOnGround() || Velocity.SizeSquared2D() <= AedirRunMinSpeedSquared)
	{
		return;
	}

	if (!Stats->ConsumeStamina(RunStaminaPerSecond * DeltaTime))
	{
		bWantsToRun = false;
	}
}

bool UAedirCharacterMovementComponent::BeginJumpWindup()
{
	const UAedirStatsComponent* Stats = GetOwnerStats();
	if (JumpWindupRemaining > 0.f || !IsMovingOnGround() || (Stats && Stats->GetStamina() < MinStaminaToJump))
	{
		return false;
	}

	JumpWindupRemaining = FMath::Max(JumpWindupSeconds, KINDA_SMALL_NUMBER);
	return true;
}

float UAedirCharacterMovementComponent::GetJumpWindupAlpha() const
{
	if (JumpWindupRemaining <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(1.f - JumpWindupRemaining / FMath::Max(JumpWindupSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f);
}

void UAedirCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	const ACharacter* Character = GetCharacterOwner();
	if (!Character || !Character->GetController() || !UpdatedComponent || !HasValidData())
	{
		return;
	}

	const float CurrentYaw = UpdatedComponent->GetComponentRotation().Yaw;
	const float DesiredYaw = Character->GetControlRotation().Yaw;
	const float YawDelta = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);

	const bool bMovingBody = (IsMovingOnGround() && Velocity.Size2D() > AedirBodyMovingSpeed) || IsFalling();

	if (bMovingBody)
	{
		bTurningInPlace = false;

		const float Sign = YawDelta < 0.f ? -1.f : 1.f;
		float Step = 0.f;
		if (FMath::Abs(YawDelta) > BodyLagYawMoving)
		{
			const float Excess = YawDelta - Sign * BodyLagYawMoving;
			// FixedTurn returns an angle wrapped to 0..360, which turned small left steps into large right ones.
			Step = FMath::Clamp(Excess, -RotationRateMoving * DeltaTime, RotationRateMoving * DeltaTime);
		}

		if (FMath::Abs(YawDelta - Step) > MaxBodyHeadYawWhileMoving)
		{
			Step = YawDelta - Sign * MaxBodyHeadYawWhileMoving;
		}

		// The turn eases in and out: the yaw rate may only change at a limited angular acceleration.
		const float TargetRate = DeltaTime > KINDA_SMALL_NUMBER ? Step / DeltaTime : 0.f;
		BodyYawRate = FMath::FInterpConstantTo(BodyYawRate, TargetRate, DeltaTime, BodyTurnAcceleration);
		const float SmoothedStep = FMath::Abs(BodyYawRate * DeltaTime) > FMath::Abs(YawDelta) ? YawDelta : BodyYawRate * DeltaTime;
		SetBodyYaw(CurrentYaw + SmoothedStep);
		return;
	}

	BodyYawRate = 0.f;

	if (!bTurningInPlace && FMath::Abs(YawDelta) > StandingLookYawLimit)
	{
		bTurningInPlace = true;
	}

	if (!bTurningInPlace)
	{
		return;
	}

	const float Step = FMath::Clamp(YawDelta, -TurnInPlaceRate * DeltaTime, TurnInPlaceRate * DeltaTime);
	SetBodyYaw(CurrentYaw + Step);

	if (FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentYaw + Step, DesiredYaw)) < AedirTurnInPlaceFinishYaw)
	{
		bTurningInPlace = false;
	}
}

float UAedirCharacterMovementComponent::GetMaxSpeed() const
{
	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking && MovementMode != MOVE_Falling)
	{
		return Super::GetMaxSpeed();
	}

	float Speed = WalkSpeed;

	if (IsCrouching())
	{
		Speed *= CrouchSpeedMultiplier;
	}
	else if (IsRunningNow())
	{
		Speed *= RunSpeedMultiplier;
	}

	if (bWantsFightStance)
	{
		Speed *= FightStanceSpeedMultiplier;
	}

	Speed *= GetDirectionalSpeedMultiplier();

	if (const UAedirStatsComponent* Stats = GetOwnerStats())
	{
		Speed *= Stats->GetSpeedMultiplier();
	}

	Speed *= 1.f - MaxCarrySlowdown * GetCarryRatio();

	if (const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner()); Creature && Creature->Combat)
	{
		const float LegInjury = FMath::Max(
			Creature->Combat->GetLimbInjury(EAedirLimb::LeftLeg),
			Creature->Combat->GetLimbInjury(EAedirLimb::RightLeg));
		Speed *= 1.f - AedirLegInjurySpeedPenalty * LegInjury;
	}

	if (const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner()); Creature && Creature->Balance)
	{
		Speed *= 1.f - AedirDazeSpeedPenalty * Creature->Balance->GetDazeFactor();

		if (Creature->Balance->IsStaggering())
		{
			Speed *= AedirStaggerSpeedMultiplier;
		}
	}

	if (const UAedirFootstepComponent* Footsteps = GetOwnerFootsteps())
	{
		Speed *= Footsteps->GetSurfaceSpeedMultiplier();
		Speed *= FMath::Lerp(1.f, 0.85f, Footsteps->GetSurfaceSlipperiness());
	}

	return Speed;
}

bool UAedirCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	UAedirStatsComponent* Stats = GetOwnerStats();
	if (Stats && Stats->GetStamina() < MinStaminaToJump)
	{
		return false;
	}

	const float BaseJumpZVelocity = JumpZVelocity;
	const float JumpHeightMultiplier = Stats ? Stats->GetJumpHeightMultiplier() : 1.f;
	const float JumpCarryMultiplier = 1.f - 0.5f * MaxCarrySlowdown * GetCarryRatio();

	JumpZVelocity = BaseJumpZVelocity * JumpHeightMultiplier * JumpCarryMultiplier;
	const bool bJumped = Super::DoJump(bReplayingMoves, DeltaTime);
	JumpZVelocity = BaseJumpZVelocity;

	const ACharacter* Character = GetCharacterOwner();
	if (bJumped && Stats && Character && Character->HasAuthority())
	{
		Stats->ConsumeStamina(JumpStaminaCost);
	}

	return bJumped;
}

void UAedirCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsToRun = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsFightStance = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UAedirCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UAedirCharacterMovementComponent* MutableThis = const_cast<UAedirCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Aedir(*this);
	}

	return ClientPredictionData;
}

void UAedirCharacterMovementComponent::SetWantsToRun(bool bNewWantsToRun)
{
	bWantsToRun = bNewWantsToRun;
}

void UAedirCharacterMovementComponent::SetWantsFightStance(bool bNewWantsFightStance)
{
	bWantsFightStance = bNewWantsFightStance;
}

bool UAedirCharacterMovementComponent::CanRunNow() const
{
	const UAedirStatsComponent* Stats = GetOwnerStats();
	return !Stats || Stats->GetStamina() >= MinStaminaToRun;
}

bool UAedirCharacterMovementComponent::IsRunningNow() const
{
	return bWantsToRun && !bWantsFightStance && !IsCrouching() && CanRunNow();
}

bool UAedirCharacterMovementComponent::IsTurningInPlace() const
{
	return bTurningInPlace;
}

float UAedirCharacterMovementComponent::GetYawDeltaToControl() const
{
	const ACharacter* Character = GetCharacterOwner();
	if (!Character || !Character->GetController() || !UpdatedComponent)
	{
		return 0.f;
	}

	return FMath::FindDeltaAngleDegrees(UpdatedComponent->GetComponentRotation().Yaw, Character->GetControlRotation().Yaw);
}

void UAedirCharacterMovementComponent::SetBodyYaw(float NewYaw)
{
	FRotator NewRotation = UpdatedComponent->GetComponentRotation();
	NewRotation.Yaw = NewYaw;
	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false);
}

UAedirStatsComponent* UAedirCharacterMovementComponent::GetOwnerStats() const
{
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner());
	return Creature ? Creature->Stats.Get() : nullptr;
}

UAedirFootstepComponent* UAedirCharacterMovementComponent::GetOwnerFootsteps() const
{
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner());
	return Creature ? Creature->Footsteps.Get() : nullptr;
}

void UAedirCharacterMovementComponent::UpdateSurfaceFriction()
{
	const UAedirFootstepComponent* Footsteps = GetOwnerFootsteps();
	const float Slipperiness = Footsteps ? Footsteps->GetSurfaceSlipperiness() : 0.f;

	GroundFriction = FMath::Lerp(BaseGroundFriction, 1.2f, Slipperiness);
	BrakingDecelerationWalking = FMath::Lerp(BaseBrakingDecelerationWalking, 40.f, Slipperiness);
	BrakingFriction = FMath::Lerp(1.2f, 0.15f, Slipperiness);
	MaxAcceleration = FMath::Lerp(BaseMaxAcceleration, 110.f, Slipperiness);
}

float UAedirCharacterMovementComponent::GetCarryRatio() const
{
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner());
	if (!Creature)
	{
		return 0.f;
	}

	const float Capacity = Creature->GetCarryCapacity();
	return Capacity > 0.f ? FMath::Clamp(Creature->GetCarriedWeight() / Capacity, 0.f, 1.f) : 0.f;
}

float UAedirCharacterMovementComponent::GetDirectionalSpeedMultiplier() const
{
	const ACharacter* Character = GetCharacterOwner();
	if (!Character)
	{
		return 1.f;
	}

	FVector MoveDirection = GetCurrentAcceleration();
	MoveDirection.Z = 0.0;
	if (MoveDirection.IsNearlyZero())
	{
		MoveDirection = Velocity;
		MoveDirection.Z = 0.0;
	}

	if (!MoveDirection.Normalize())
	{
		return 1.f;
	}

	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	const double CosAngle = FMath::Clamp(FVector::DotProduct(MoveDirection, Forward), -1.0, 1.0);
	const float AngleDegrees = static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(CosAngle)));

	if (AngleDegrees <= AedirForwardArcDegrees)
	{
		return ForwardSpeedMultiplier;
	}

	if (AngleDegrees <= AedirSideArcDegrees)
	{
		const float Alpha = SmoothStep01((AngleDegrees - AedirForwardArcDegrees) / (AedirSideArcDegrees - AedirForwardArcDegrees));
		return FMath::Lerp(ForwardSpeedMultiplier, SideSpeedMultiplier, Alpha);
	}

	if (AngleDegrees <= AedirBackArcDegrees)
	{
		const float Alpha = SmoothStep01((AngleDegrees - AedirSideArcDegrees) / (AedirBackArcDegrees - AedirSideArcDegrees));
		return FMath::Lerp(SideSpeedMultiplier, BackSpeedMultiplier, Alpha);
	}

	return BackSpeedMultiplier;
}
