#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AedirCharacterMovementComponent.generated.h"

class FNetworkPredictionData_Client;
class UAedirStatsComponent;

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UAedirCharacterMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float WalkSpeed = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Movement")
	float RunSpeedMultiplier = 1.75f;

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
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

private:
	UAedirStatsComponent* GetOwnerStats() const;
	float GetCarryRatio() const;
	float GetDirectionalSpeedMultiplier() const;
};
