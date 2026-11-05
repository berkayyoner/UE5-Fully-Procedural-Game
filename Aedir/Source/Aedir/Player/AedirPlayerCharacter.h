#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "AedirPlayerCharacter.generated.h"

class UCameraComponent;

UCLASS(Blueprintable)
class AEDIR_API AAedirPlayerCharacter : public AAedirCreature
{
	GENERATED_BODY()

public:
	AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Player")
	TObjectPtr<UCameraComponent> FollowCamera;

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
	void HandleFightButtonPressed();
	void HandleFightButtonReleased();

	float ForwardKeyValue = 0.f;
	float BackwardKeyValue = 0.f;
	float RightKeyValue = 0.f;
	float LeftKeyValue = 0.f;
	int32 HeldFightButtons = 0;
};
