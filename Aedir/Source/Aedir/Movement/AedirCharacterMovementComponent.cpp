#include "AedirCharacterMovementComponent.h"

#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Character.h"

namespace
{
	constexpr float AedirForwardArcDegrees = 45.f;
	constexpr float AedirSideArcDegrees = 90.f;
	constexpr float AedirBackArcDegrees = 135.f;
	constexpr double AedirRunMinSpeedSquared = 100.0;

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
	GroundFriction = 8.f;
	BrakingDecelerationWalking = 1400.f;
	MaxAcceleration = 1400.f;
}

void UAedirCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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

UAedirStatsComponent* UAedirCharacterMovementComponent::GetOwnerStats() const
{
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetCharacterOwner());
	return Creature ? Creature->Stats.Get() : nullptr;
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
