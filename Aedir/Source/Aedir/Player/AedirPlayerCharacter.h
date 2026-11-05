#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "Engine/TimerHandle.h"
#include "Items/AedirItemTypes.h"
#include "AedirPlayerCharacter.generated.h"

class UAedirWeatherEffectsComponent;
class UCameraComponent;
class USpringArmComponent;
class UAedirMenuSubsystem;

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

	// Blend of the subtle fight-stance camera drift, 0 outside the stance.
	float FightCameraAlpha = 0.f;
	FRotator FightCameraSway = FRotator::ZeroRotator;

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

	// Server only. Applies the chosen race and posts the starter quest.
	UFUNCTION(Server, Reliable)
	void ServerChooseRace(FName ChosenRaceId);

	// Opens the race picker on the owning client when the game mode asks for it.
	void MaybeOpenCharacterCreation();

	// Console commands. Commands that change the world are sent to the server in co-op; ListItems, DebugBody, BloodTest and DevHelp run locally.
	UFUNCTION(Exec)
	void SpawnItem(const FString& ItemId, const FString& Mode);

	UFUNCTION(Exec)
	void ListItems();

	UFUNCTION(Exec)
	void SpawnEnemy(int32 Count, int32 Armed);

	UFUNCTION(Exec)
	void KillAll();

	UFUNCTION(Exec)
	void SetWeatherType(const FString& WeatherName);

	UFUNCTION(Exec)
	void Heal();

	UFUNCTION(Exec)
	void Hurt(float Amount);

	UFUNCTION(Exec)
	void God();

	UFUNCTION(Exec)
	void GiveSkill(const FString& SkillName, float Amount);

	UFUNCTION(Exec)
	void CharacterCreation();

	// Ids are case-insensitive: Human, Elf, DarkElf, Dwarf.
	UFUNCTION(Exec)
	void ChooseRace(const FString& RaceName);

	UFUNCTION(Exec)
	void Wound(const FString& WoundType, const FString& Bone);

	UFUNCTION(Exec)
	void BloodTest();

	UFUNCTION(Exec)
	void DebugBody();

	UFUNCTION(Exec)
	void DevHelp();

	bool IsDebugBodyEnabled() const { return bDebugBody; }
	void SetDebugBodyEnabled(bool bEnabled) { bDebugBody = bEnabled; }

private:
	void ApplyViewPitchLimits();
	void HandleViewTogglePressed();

	void HandleMenuTogglePressed();
	void HandleJournalPressed();
	void HandleMapPressed();
	void HandleSkillsPressed();
	void OpenMenuCategory(int32 Category);
	UAedirMenuSubsystem* GetMenuSubsystem();
	bool IsMenuOpen();
	bool IsCharacterCreationOpen();

	void EnsureStarterQuest();
	void UpdateTutorialObjectives();

	FTimerHandle CharacterCreationTimer;
	bool bTutorialRotationCaptured = false;
	FRotator TutorialStartRotation = FRotator::ZeroRotator;

	bool bFirstPerson = false;


	void HandleMouseX(float Value);
	void HandleMouseY(float Value);
	void HandleMouseWheel(float Value);

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
	void HandleShovePressed();
	void HandleKickPressed();

	void HandleHandPressed(EAedirHand Hand);
	void HandleHandReleased(EAedirHand Hand);
	void UpdateHandUseHold(float DeltaSeconds);
	void UpdateSwingInput(float DeltaSeconds);

	FVector2D PendingMouseDelta = FVector2D::ZeroVector;

	bool bHandKeyTracked[2] = { false, false };
	bool bHandUseTriggered[2] = { false, false };
	float HandKeyHeldSeconds[2] = { 0.f, 0.f };

	void RunDevCommand(const FString& Command, const FString& Args, bool bRunOnServer);
	void DrawDebugBody();

	UFUNCTION(Server, Reliable)
	void ServerDevCommand(const FString& Command, const FString& Args);

	bool bDebugBody = false;
};
