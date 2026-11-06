#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AedirCharacterMovementComponent.generated.h"

class FNetworkPredictionData_Client;
class UAedirFootstepComponent;
class UAedirStatsComponent;

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UAedirCharacterMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float WalkSpeed = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float RunSpeedMultiplier = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float CrouchSpeedMultiplier = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float ForwardSpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float SideSpeedMultiplier = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float BackSpeedMultiplier = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float FightStanceSpeedMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float MinStaminaToRun = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float RunStaminaPerSecond = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float JumpStaminaCost = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float MinStaminaToJump = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float MaxCarrySlowdown = 0.45f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aedir|Movement")
	bool bWantsToRun = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aedir|Movement")
	bool bWantsFightStance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float RotationRateMoving = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float BodyLagYawMoving = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float MaxBodyHeadYawWhileMoving = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float StandingLookYawLimit = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float TurnInPlaceRate = 130.f;

	// Largest change of the body turn rate in degrees per second squared while moving.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float BodyTurnAcceleration = 900.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aedir|Movement")
	bool bTurningInPlace = false;

	float BodyYawRate = 0.f;

	// Speed left on the steepest walkable slope, going up and going down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float SlopeUpSpeedScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float SlopeDownSpeedScale = 0.72f;

	// Set by the gait while the feet find stairs.
	void SetStairSpeedScale(float Scale) { StairSpeedScale = Scale; }

	// Seconds the body crouches down before the legs push off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float JumpWindupSeconds = 0.5f;

	// Starts the crouch before a jump; the jump itself fires when the wind-up ends. Returns false when a jump cannot start now.
	bool BeginJumpWindup();

	// 0 when no jump is winding up, rising to 1 at the push-off.
	float GetJumpWindupAlpha() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void SetWantsToRun(bool bNewWantsToRun);

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void SetWantsFightStance(bool bNewWantsFightStance);

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	bool CanRunNow() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	bool IsRunningNow() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	bool IsTurningInPlace() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	float GetYawDeltaToControl() const;

private:
	UAedirStatsComponent* GetOwnerStats() const;
	UAedirFootstepComponent* GetOwnerFootsteps() const;
	float GetCarryRatio() const;
	float GetDirectionalSpeedMultiplier() const;
	void SetBodyYaw(float NewYaw);
	void UpdateSurfaceFriction();

	float StairSpeedScale = 1.f;
	float JumpWindupRemaining = 0.f;
	bool bJumpWindupFired = false;
	float BaseGroundFriction = 8.f;
	float BaseBrakingDecelerationWalking = 1400.f;
	float BaseMaxAcceleration = 1400.f;
};
