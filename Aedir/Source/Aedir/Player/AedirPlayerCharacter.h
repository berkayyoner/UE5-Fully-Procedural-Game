#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "Items/AedirItemTypes.h"
#include "AedirPlayerCharacter.generated.h"

class UAedirWeatherEffectsComponent;
class UCameraComponent;

UCLASS(Blueprintable)
class AEDIR_API AAedirPlayerCharacter : public AAedirCreature
{
	GENERATED_BODY()

public:
	AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Player")
	TObjectPtr<UCameraComponent> FollowCamera;

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

	void HandleForwardKey(float Value);
	void HandleBackwardKey(float Value);
	void HandleRightKey(float Value);
	void HandleLeftKey(float Value);

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

	float ForwardKeyValue = 0.f;
	float BackwardKeyValue = 0.f;
	float RightKeyValue = 0.f;
	float LeftKeyValue = 0.f;
	FVector2D PendingMouseDelta = FVector2D::ZeroVector;

	bool bHandKeyTracked[2] = { false, false };
	bool bHandUseTriggered[2] = { false, false };
	float HandKeyHeldSeconds[2] = { 0.f, 0.f };
};
