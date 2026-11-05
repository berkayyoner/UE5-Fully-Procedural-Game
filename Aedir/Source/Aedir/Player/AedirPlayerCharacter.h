#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "Items/AedirItemTypes.h"
#include "AedirPlayerCharacter.generated.h"

class UAedirWeatherEffectsComponent;
class UCameraComponent;
class USpringArmComponent;

UCLASS(Blueprintable)
class AEDIR_API AAedirPlayerCharacter : public AAedirCreature
{
	GENERATED_BODY()

public:
	AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float ThirdPersonArmLength = 280.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector ThirdPersonShoulderOffset = FVector(0.f, 45.f, 25.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector FirstPersonEyeOffset = FVector(14.f, 0.f, 62.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FName HeadBoneToHideInFirstPerson = FName(TEXT("head"));

	UFUNCTION(BlueprintCallable, Category="Aedir|Camera")
	void SetFirstPerson(bool bNewFirstPerson);

	UFUNCTION(BlueprintCallable, Category="Aedir|Camera")
	void ToggleView();

	UFUNCTION(BlueprintPure, Category="Aedir|Camera")
	bool IsFirstPerson() const { return bFirstPerson; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Weather")
	TObjectPtr<UAedirWeatherEffectsComponent> WeatherEffects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Player")
	float MinViewPitch = -80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Player")
	float MaxViewPitch = 80.f;

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void ApplyViewPitchLimits();
	void HandleViewTogglePressed();

	bool bFirstPerson = false;


	void HandleMouseX(float Value);
	void HandleMouseY(float Value);

	void HandleRunPressed();
	void HandleRunReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandleJumpPressed();
	void HandleJumpReleased();
	void HandleLeftGuardPressed();
	void HandleLeftGuardReleased();
	void HandleRightGuardPressed();
	void HandleRightGuardReleased();

	void HandleLeftHandPressed();
	void HandleLeftHandReleased();
	void HandleRightHandPressed();
	void HandleRightHandReleased();
	void HandleLeftHolsterPressed();
	void HandleRightHolsterPressed();

	void HandleHandPressed(EAedirHand Hand);
	void HandleHandReleased(EAedirHand Hand);
	void UpdateHandUseHold(float DeltaSeconds);
	void UpdateSwingInput(float DeltaSeconds);

	FVector2D PendingMouseDelta = FVector2D::ZeroVector;

	bool bHandKeyTracked[2] = { false, false };
	bool bHandUseTriggered[2] = { false, false };
	float HandKeyHeldSeconds[2] = { 0.f, 0.f };
};
