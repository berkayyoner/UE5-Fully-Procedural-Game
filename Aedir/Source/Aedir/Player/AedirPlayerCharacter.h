#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "Net/AedirSaveGame.h"
#include "Npc/AedirNpc.h"
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
	float ThirdPersonArmLength = 165.f;

	// The mouse wheel moves the camera between these distances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomMinCm = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomMaxCm = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomStepCm = 22.f;

	float CameraZoomTargetCm = 165.f;

	// Height of the camera boom pivot above the capsule centre while standing.
	float CameraBoomBaseZ = 60.f;

	// Drops a few catalog items in front of the player at the start, in non-shipping builds, so grab and inventory can be tested.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dev")
	bool bSpawnTestItemsOnStart = true;

	// How quickly the camera pivot follows the body, how much of the pelvis height motion it takes over, and the ragdoll follow settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraBodyFollowSpeed = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraPelvisInfluence = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollFollowSpeed = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollRaiseCm = 45.f;

	FVector CameraRelative = FVector::ZeroVector;
	FVector CameraPelvisBaseline = FVector::ZeroVector;
	bool bCameraStateInitialized = false;

	// Share of the mouse movement that still turns the camera while a guard is held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightCameraMouseScale = 0.015f;

	// Blend of the subtle fight-stance camera drift, 0 outside the stance.
	float FightCameraAlpha = 0.f;
	FRotator FightCameraSway = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector ThirdPersonShoulderOffset = FVector(0.f, 18.f, 8.f);

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Server only. Applies the chosen race and posts the starter quest.
	void HandleTalkPressed();
	void SendGuestCharacter();
	void StoreGuestCharacterNow();
	FTimerHandle GuestStoreTimer;

	UFUNCTION(Server, Reliable)
	void ServerBringCharacter(const FAedirCreatureSave& Character);

	UFUNCTION(Server, Reliable)
	void ServerNpcAction(AAedirNpc* Npc, EAedirNpcAction Action);

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

	// SetStat Strength 80: change a stat and watch how the body reacts.
	UFUNCTION(Exec)
	void SetStat(const FString& StatName, float Value);

	UFUNCTION(Exec)
	void ShowStats();

	// Push 0.5: shove the balance forward, 1 and above knocks the body down.
	UFUNCTION(Exec)
	void Push(float Strength);

	UFUNCTION(Exec)
	void Knockdown();

	// Diagnostics: logs gait, balance and reach state for the given number of seconds.
	UFUNCTION(Exec)
	void GaitInfo(float Seconds, float Interval);

	UFUNCTION(Exec)
	void AutoMove(float Forward, float Right, int32 Run, float TurnDegrees);

	UFUNCTION(Exec)
	void ShotCam(const FString& CameraArgs);

	UFUNCTION(Exec)
	void PlaceItem(const FString& ItemArgs);

	UFUNCTION(Exec)
	void PoseArm(const FString& PoseArgs);

	UFUNCTION(Exec)
	void GripInfo();

	UFUNCTION(Exec)
	void DropHand(int32 RightHand);

	UFUNCTION(Exec)
	void HandWatch(int32 RightHand, float Seconds);

	UFUNCTION(Exec)
	void MouseFlick(int32 RightHand, float DeltaX, float DeltaY, float Seconds);

	UFUNCTION(Exec)
	void SwingTrace(int32 RightHand, float Seconds);

	UFUNCTION(Exec)
	void TalkTest(const FString& Topic);

	UFUNCTION(Exec)
	void NeedsInfo();

	UFUNCTION(Exec)
	void DecalTest();

	UFUNCTION(Exec)
	void SaveNow(const FString& Slot);

	UFUNCTION(Exec)
	void GuestSaveNow();

	// Parts a limb of the nearest person, or cuts the training pell, as a heavy blade would.
	UFUNCTION(Exec)
	void CutTest(const FString& Bone);

	UFUNCTION(Exec)
	void DummyCutTest(const FString& PartName);

	UFUNCTION(Exec)
	void AssetCheck(const FString& Paths);

	// The living creature in the middle of the view, if any.
	AAedirCreature* FindLookTarget(float MaxDistance) const;
	void RequestNpcAction(AAedirNpc* Npc, EAedirNpcAction Action);

	// The follower waiting to be shown where to go, and what the player is pointing out to them: a spot, and the item lying there if any.
	AAedirNpc* GetPointingNpc() const { return PointingNpc.Get(); }
	bool FindPointTarget(FVector& OutWhere, AAedirItemActor*& OutItem) const;

	UFUNCTION(Exec)
	void PointTest(float Yaw, float Pitch);

	UFUNCTION(Exec)
	void PointItemTest(const FString& Id);

	UFUNCTION(Exec)
	void AimOnlyItem(const FString& Id);

	// Looks at a person and presses the talk key, or only looks with Press 0.
	UFUNCTION(Exec)
	void LookNpcTest(const FString& Name, int32 Press);

	UFUNCTION(Exec)
	void GearInfo();

	// NpcDropTest Marra Mace 500: lays a catalog item that far in front of a person.
	UFUNCTION(Exec)
	void NpcDropTest(const FString& Name, const FString& Id, float Forward);

	UFUNCTION(Exec)
	void NpcRemoveTest(const FString& Name);

	UFUNCTION(Exec)
	void NpcDisarmTest(const FString& Name);

	UFUNCTION(Exec)
	void NpcBringTest(const FString& Name, float Forward);

	UFUNCTION(Exec)
	void PassHand();

	UFUNCTION(Exec)
	void KickNow(float HoldShare);

	UFUNCTION(Exec)
	void HandKey(int32 RightHand);

	UFUNCTION(Exec)
	void PlaceBlock(float Forward, float Right, float Up, float SizeCm);

	UFUNCTION(Exec)
	void DrawSlot(int32 RightHand, int32 Slot);

	UFUNCTION(Exec)
	void HolsterTest(int32 RightHand);

	// Fixed movement input for diagnostics, on top of the keyboard.
	void SetDevAutoMove(const FVector2D& Input) { DevAutoMove = Input; }

	UFUNCTION(Exec)
	void ReachTest(float DelaySeconds, int32 BothHands);

	UFUNCTION(Exec)
	void CrouchTest();

	// FallTest 300: lift the body by that many cm and let it fall.
	UFUNCTION(Exec)
	void FallTest(float HeightCm);

	// Opens or closes the lower left console. Bound to the key under Esc.
	void ToggleConsole();

	// The thing clicked while the console is open.
	AActor* GetConsoleSelection() const { return SelectedEntity.Get(); }

	// Con player.additem Bread 2: a line typed into the console, without the console.
	UFUNCTION(Exec)
	void Con(const FString& Line);

	// ConPick 0.5 0.5: a click at that share of the screen, as with the console open.
	UFUNCTION(Exec)
	void ConPick(float ShareX, float ShareY);

	UFUNCTION(Exec)
	void ConOpen();

	// MeshSheet /Game/Pack/Meshes: stands every model of a folder in a row with its name over it and logs its size, for choosing from a pack.
	UFUNCTION(Exec)
	void MeshSheet(const FString& Folder);

	// FxTest /Game/Pack/NS_Thing.NS_Thing 0.5: plays an effect in front of the chest.
	UFUNCTION(Exec)
	void FxTest(const FString& Path, float Scale);

	UFUNCTION(Exec)
	void FoliageInfo();

	UFUNCTION(Exec)
	void TalkOpenTest(const FString& Name);

	// MouthTest 1 1: holds the grab key of the right hand down, 0 lets go. T and Y and the inventory click also have test commands.
	UFUNCTION(Exec)
	void MouthTest(int32 RightHand, int32 Hold);

	UFUNCTION(Exec)
	void PackDrawTest(int32 RightHand);

	UFUNCTION(Exec)
	void NeedInfo();

	bool IsDebugBodyEnabled() const { return bDebugBody; }
	void SetDebugBodyEnabled(bool bEnabled) { bDebugBody = bEnabled; }

private:
	void ApplyViewPitchLimits();
	void HandleViewTogglePressed();

	void HandleMenuTogglePressed();
	void HandleJournalPressed();
	void HandleMapPressed();
	void HandleSkillsPressed();
	void HandlePausePressed();
	void OpenMenuCategory(int32 Category);
	UAedirMenuSubsystem* GetMenuSubsystem();
	bool IsMenuOpen();
	bool IsCharacterCreationOpen();

	void EnsureStarterQuest();
	void UpdateTutorialObjectives();

	TSharedPtr<class SAedirConsole> ConsoleWidget;
	FDelegateHandle ConsoleReportHandle;
	void CloseConsole();

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
	TWeakObjectPtr<AAedirNpc> PointingNpc;
	void AimForTest(TWeakObjectPtr<AActor> Target, bool bPress);

	TWeakObjectPtr<AActor> SelectedEntity;
	bool bConsolePaused = false;
	void HandleConsolePick(const FVector2D& ViewportPixel);
	void RefreshConsoleSelection();
	void RunConsoleLine(const FString& Line);

	UFUNCTION(Server, Reliable)
	void ServerConsoleLine(const FString& Line);
	bool bPressThenShot = false;

	UFUNCTION(Server, Reliable)
	void ServerSendNpc(AAedirNpc* Npc, const FVector& Where, AAedirItemActor* Item);
	void HandleKickPressed();
	void HandleThrustPressed();
	void HandleThrustReleased();
	void HandleDynamicKickPressed();

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

	FVector2D DevAutoMove = FVector2D::ZeroVector;
	bool bDebugBody = false;
};
