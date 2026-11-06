#include "AedirPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Animation/AedirGaitComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Dev/AedirDevTools.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UI/SAedirConsole.h"
#include "GameFramework/SpringArmComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/AedirGameMode.h"
#include "Items/AedirItemActor.h"
#include "TimerManager.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirMenuSubsystem.h"
#include "Weather/AedirWeatherEffectsComponent.h"

namespace
{
	constexpr float PlayerCharacterCreationDelaySeconds = 0.8f;
	constexpr float PlayerTutorialTurnDegrees = 15.f;

	FName PlayerTutorialQuestId()
	{
		return FName(TEXT("Awakening"));
	}

	const FAedirQuest* PlayerFindQuest(const UAedirJournalSubsystem* Journal, FName QuestId)
	{
		return Journal->GetQuests().FindByPredicate([QuestId](const FAedirQuest& Quest)
		{
			return Quest.Id == QuestId;
		});
	}

	FAedirQuest PlayerBuildAwakeningQuest()
	{
		FAedirQuest Quest;
		Quest.Id = PlayerTutorialQuestId();
		Quest.Title = FText::FromString(TEXT("Awakening"));
		Quest.Description = FText::FromString(TEXT("You come to your senses on the training grounds of Aedir. Learn how to move, fight and take what you need."));

		const TCHAR* const ObjectiveTexts[] =
		{
			TEXT("Look around with the mouse"),
			TEXT("Pick up an item (Q or E)"),
			TEXT("Raise your fists (hold left or right mouse button)"),
			TEXT("Open your menu (Tab)")
		};
		for (const TCHAR* ObjectiveText : ObjectiveTexts)
		{
			FAedirQuestObjective Objective;
			Objective.Text = FText::FromString(ObjectiveText);
			Quest.Objectives.Add(Objective);
		}

		return Quest;
	}
}

AAedirPlayerCharacter::AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	CameraBoom->TargetArmLength = ThirdPersonArmLength;
	CameraBoom->SocketOffset = ThirdPersonShoulderOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 14.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FirstPersonEyeOffset);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetAutoActivate(false);

	WeatherEffects = CreateDefaultSubobject<UAedirWeatherEffectsComponent>(TEXT("WeatherEffects"));
}

void AAedirPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseConsole();
	Super::EndPlay(EndPlayReason);
}

void AAedirPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// The camera settings of the code win over values stored on the boom in older blueprints.
	CameraZoomTargetCm = FMath::Clamp(ThirdPersonArmLength, CameraZoomMinCm, CameraZoomMaxCm);
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = CameraZoomTargetCm;
		CameraBoom->SocketOffset = ThirdPersonShoulderOffset;
	}

	if (HasAuthority() && GroupId.IsNone())
	{
		GroupId = FName(TEXT("Heroes"));
		OnGroupIdChanged.Broadcast(GroupId);
	}

	if (HasAuthority())
	{
		EnsureStarterQuest();
	}

	ApplyViewPitchLimits();
	SetFirstPerson(false);

	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(CharacterCreationTimer, this, &AAedirPlayerCharacter::MaybeOpenCharacterCreation, PlayerCharacterCreationDelaySeconds, false);
	}

#if !UE_BUILD_SHIPPING
	// Test helper: "-AedirStartup=\"SpawnEnemy 1 1;SpawnItem Longsword hold\"" runs console commands once after the local player spawned.
	static bool bStartupCommandsRan = false;
	FString StartupCommands;
	if (!bStartupCommandsRan && IsLocallyControlled() && FParse::Value(FCommandLine::Get(), TEXT("AedirStartup="), StartupCommands))
	{
		bStartupCommandsRan = true;

		// "Wait 3" between the commands delays everything after it, so a whole test can be scripted from the command line.
		TArray<FString> Commands;
		StartupCommands.ParseIntoArray(Commands, TEXT(";"), true);
		float Delay = 2.f;
		for (const FString& RawCommand : Commands)
		{
			const FString Command = RawCommand.TrimStartAndEnd();
			if (Command.StartsWith(TEXT("Wait "), ESearchCase::IgnoreCase))
			{
				Delay += FCString::Atof(*Command.RightChop(5));
				continue;
			}

			FTimerHandle StartupTimer;
			GetWorldTimerManager().SetTimer(StartupTimer, FTimerDelegate::CreateWeakLambda(this, [this, Command]()
			{
				if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
				{
					PlayerController->ConsoleCommand(Command);
				}
			}), Delay, false);
		}
	}
	else if (!bStartupCommandsRan && bSpawnTestItemsOnStart && IsLocallyControlled() && HasAuthority())
	{
		// Grab and inventory tests need something to pick up, so a few catalog items drop in front of the player.
		FTimerHandle TestItemTimer;
		GetWorldTimerManager().SetTimer(TestItemTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			for (const TCHAR* ItemId : { TEXT("Sword"), TEXT("Longsword"), TEXT("Mace"), TEXT("Boulder") })
			{
				UAedirDevTools::Execute(this, TEXT("SpawnItem"), ItemId);
			}
		}), 1.5f, false);
	}
#endif
}

void AAedirPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	ApplyViewPitchLimits();

	// BeginPlay usually runs before possession, so the local journal is filled here.
	if (IsLocallyControlled())
	{
		EnsureStarterQuest();
	}
}

void AAedirPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocallyControlled())
	{
		return;
	}

	if (!IsMenuOpen() && !IsCharacterCreationOpen())
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const float ForwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::W) ? 1.f : 0.f;
		const float BackwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::S) ? 1.f : 0.f;
		const float RightKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::D) ? 1.f : 0.f;
		const float LeftKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::A) ? 1.f : 0.f;
		const FVector2D MoveInput = FVector2D(RightKeyValue - LeftKeyValue, ForwardKeyValue - BackwardKeyValue) + DevAutoMove;
		MoveLocal(MoveInput.GetClampedToMaxSize(1.0));
	}

	UpdateTutorialObjectives();
	UpdateHandUseHold(DeltaSeconds);
	UpdateSwingInput(DeltaSeconds);

	if (bDebugBody)
	{
		DrawDebugBody();
	}
}

void AAedirPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}


	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AAedirPlayerCharacter::HandleMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AAedirPlayerCharacter::HandleMouseY);
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AAedirPlayerCharacter::HandleMouseWheel);

	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AAedirPlayerCharacter::HandleRunPressed);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AAedirPlayerCharacter::HandleRunReleased);

	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AAedirPlayerCharacter::HandleCrouchPressed);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &AAedirPlayerCharacter::HandleCrouchReleased);

	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AAedirPlayerCharacter::HandleViewTogglePressed);

	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AAedirPlayerCharacter::HandleMenuTogglePressed);
	PlayerInputComponent->BindKey(EKeys::J, IE_Pressed, this, &AAedirPlayerCharacter::HandleJournalPressed);
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAedirPlayerCharacter::HandleMapPressed);
	PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &AAedirPlayerCharacter::HandleSkillsPressed);
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAedirPlayerCharacter::HandlePausePressed);

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAedirPlayerCharacter::HandleJumpPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AAedirPlayerCharacter::HandleJumpReleased);

	// The left mouse button works the right hand and the right button the left hand.
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightGuardPressed);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleRightGuardReleased);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftGuardPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleLeftGuardReleased);

	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHandPressed);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &AAedirPlayerCharacter::HandleLeftHandReleased);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHandPressed);
	PlayerInputComponent->BindKey(EKeys::E, IE_Released, this, &AAedirPlayerCharacter::HandleRightHandReleased);

	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHolsterPressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHolsterPressed);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AAedirPlayerCharacter::HandleShovePressed);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &AAedirPlayerCharacter::HandleKickPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AAedirPlayerCharacter::HandleThrustPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AAedirPlayerCharacter::HandleThrustReleased);
	PlayerInputComponent->BindKey(EKeys::Tilde, IE_Pressed, this, &AAedirPlayerCharacter::ToggleConsole);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AAedirPlayerCharacter::HandleDynamicKickPressed);
}

void AAedirPlayerCharacter::ApplyViewPitchLimits()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	PlayerController->PlayerCameraManager->ViewPitchMin = MinViewPitch;
	PlayerController->PlayerCameraManager->ViewPitchMax = MaxViewPitch;
}

void AAedirPlayerCharacter::HandleMouseX(float Value)
{
	// While a guard is held the mouse mostly moves the arm; the camera still follows a few percent of it.
	const bool bFighting = Combat && Combat->IsInFightStance();
	AddControllerYawInput(bFighting ? Value * FightCameraMouseScale : Value);

	PendingMouseDelta.X += Value;
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	const bool bFighting = Combat && Combat->IsInFightStance();
	AddControllerPitchInput(bFighting ? -Value * FightCameraMouseScale : -Value);

	PendingMouseDelta.Y += Value;
}

void AAedirPlayerCharacter::HandleMouseWheel(float Value)
{
	if (!Combat || Value == 0.f)
	{
		return;
	}

	// A raised fist takes the wheel to set its reach; everything else zooms the camera.
	bool bFistRaised = false;
	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (Combat->IsGuardActive(Hand) && !(Interaction && Interaction->GetHeldItem(Hand)))
		{
			Combat->AddExtensionInput(Hand, Value);
			bFistRaised = true;
		}
	}

	if (!bFistRaised)
	{
		CameraZoomTargetCm = FMath::Clamp(CameraZoomTargetCm - Value * CameraZoomStepCm, CameraZoomMinCm, CameraZoomMaxCm);
	}
}

void AAedirPlayerCharacter::HandleRunPressed()
{
	SetWantsToRun(true);
}

void AAedirPlayerCharacter::HandleRunReleased()
{
	SetWantsToRun(false);
}

void AAedirPlayerCharacter::HandleCrouchPressed()
{
	SetWantsToCrouch(true);
}

void AAedirPlayerCharacter::HandleCrouchReleased()
{
	SetWantsToCrouch(false);
}

void AAedirPlayerCharacter::HandleJumpPressed()
{
	StartJump();
}

void AAedirPlayerCharacter::HandleJumpReleased()
{
	StopJump();
}

void AAedirPlayerCharacter::HandleLeftGuardPressed()
{
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Left, true);
	}
}

void AAedirPlayerCharacter::HandleLeftGuardReleased()
{
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Left, false);
	}
}

void AAedirPlayerCharacter::HandleRightGuardPressed()
{
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Right, true);
	}
}

void AAedirPlayerCharacter::HandleRightGuardReleased()
{
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Right, false);
	}
}

void AAedirPlayerCharacter::UpdateSwingInput(float DeltaSeconds)
{
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, CameraZoomTargetCm, DeltaSeconds, 10.f);
	}

	if (CameraBoom && Gait && GetCharacterMovement())
	{
		// The capsule changes size at once when crouching; the boom pivot is eased so the camera sinks with the pelvis.
		const float Drop = FMath::Max(GetDefaultHalfHeight() - GetCharacterMovement()->GetCrouchedHalfHeight(), 0.f);
		const float CapsuleDrop = bIsCrouched ? Drop : 0.f;
		FVector TargetRelative(0.f, 0.f, CameraBoomBaseZ + CapsuleDrop - Drop * Gait->GetCrouchAlpha());
		float FollowSpeed = CameraBodyFollowSpeed;

		if (USkeletalMeshComponent* BodyMesh = GetMesh())
		{
			const FName PelvisName = Balance ? Balance->PelvisBoneName : FName(TEXT("pelvis"));
			const FVector PelvisWorld = BodyMesh->GetBoneLocation(PelvisName, EBoneSpaces::WorldSpace);
			if (Balance && Balance->IsRagdoll())
			{
				// A fallen or dead body keeps the camera: the pivot follows the pelvis wherever the ragdoll goes.
				TargetRelative = GetActorTransform().InverseTransformPositionNoScale(PelvisWorld + FVector(0.f, 0.f, CameraRagdollRaiseCm));
				FollowSpeed = CameraRagdollFollowSpeed;
			}
			else
			{
				// The pivot follows the pelvis height with a slight delay, so steps, jumps and landings reach the camera.
				// Horizontally too: sway, lean and step shifts of the pelvis move the pivot a little.
				const FVector PelvisLocal = GetActorTransform().InverseTransformPositionNoScale(PelvisWorld);
				CameraPelvisBaseline = bCameraStateInitialized ? FMath::VInterpTo(CameraPelvisBaseline, PelvisLocal, DeltaSeconds, 0.6f) : PelvisLocal;
				TargetRelative += (PelvisLocal - CameraPelvisBaseline) * CameraPelvisInfluence;
			}
		}

		CameraRelative = bCameraStateInitialized ? FMath::VInterpTo(CameraRelative, TargetRelative, DeltaSeconds, FollowSpeed) : TargetRelative;
		bCameraStateInitialized = true;
		CameraBoom->SetRelativeLocation(CameraRelative);
	}

	if (FollowCamera)
	{
		// The fight camera is locked in aim but never dead still: breathing drift plus a lean into the mouse movement.
		const bool bFighting = Combat && Combat->IsInFightStance();
		FightCameraAlpha = FMath::FInterpTo(FightCameraAlpha, bFighting ? 1.f : 0.f, DeltaSeconds, 4.f);
		const float Time = GetWorld()->GetTimeSeconds();
		const FVector2D MouseLean = FVector2D(PendingMouseDelta.X, PendingMouseDelta.Y).GetClampedToMaxSize(30.f) * 0.03f;
		const FRotator Sway(
			(FMath::Sin(Time * 1.3f) * 0.12f - MouseLean.Y * 0.4f) * FightCameraAlpha,
			(FMath::Sin(Time * 0.9f + 1.f) * 0.15f + MouseLean.X * 0.2f) * FightCameraAlpha,
			(FMath::Sin(Time * 1.1f + 2.f) * 0.15f + MouseLean.X * 0.4f) * FightCameraAlpha);
		FightCameraSway = FMath::RInterpTo(FightCameraSway, Sway, DeltaSeconds, 6.f);
		FollowCamera->SetRelativeRotation(FightCameraSway);
	}

	if (Combat)
	{
		for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
		{
			if (Combat->IsGuardActive(Hand))
			{
				Combat->AddSwingInput(Hand, PendingMouseDelta, DeltaSeconds);
			}
		}
	}

	PendingMouseDelta = FVector2D::ZeroVector;
}

void AAedirPlayerCharacter::HandleLeftHandPressed()
{
	HandleHandPressed(EAedirHand::Left);
}

void AAedirPlayerCharacter::HandleLeftHandReleased()
{
	HandleHandReleased(EAedirHand::Left);
}

void AAedirPlayerCharacter::HandleRightHandPressed()
{
	HandleHandPressed(EAedirHand::Right);
}

void AAedirPlayerCharacter::HandleRightHandReleased()
{
	HandleHandReleased(EAedirHand::Right);
}

void AAedirPlayerCharacter::HandleLeftHolsterPressed()
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::HandleRightHolsterPressed()
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(EAedirHand::Right);
	}
}

void AAedirPlayerCharacter::HandleShovePressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	Combat->StartShove();
}

void AAedirPlayerCharacter::HandleThrustPressed()
{
	if (Combat)
	{
		Combat->SetThrustMode(true);
	}
}

void AAedirPlayerCharacter::HandleThrustReleased()
{
	if (Combat)
	{
		Combat->SetThrustMode(false);
	}
}

void AAedirPlayerCharacter::HandleDynamicKickPressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// The kick stays where the body stands: the leg lifts slowly, the knee comes up to the belly and the foot snaps out.
	Combat->StartKick();
}

void AAedirPlayerCharacter::HandleKickPressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// With a two-handed weapon in both hands X swaps the grip: the upper hand and the lower hand change places.
	if (Interaction && Combat->IsTwoHandGrip() && Interaction->GetHeldItem(EAedirHand::Left) && Interaction->GetHeldItem(EAedirHand::Left) == Interaction->GetHeldItem(EAedirHand::Right))
	{
		Interaction->SwapGripOrder();
		return;
	}

	Combat->StartKick();
}

void AAedirPlayerCharacter::HandleHandPressed(EAedirHand Hand)
{
	if (!Interaction)
	{
		return;
	}

	const int32 Index = static_cast<int32>(Hand);
	switch (Interaction->GetHandState(Hand))
	{
	case EAedirHandState::Idle:
		Interaction->StartReach(Hand);
		break;
	case EAedirHandState::Holding:
		bHandKeyTracked[Index] = true;
		bHandUseTriggered[Index] = false;
		HandKeyHeldSeconds[Index] = 0.f;
		break;
	default:
		break;
	}
}

void AAedirPlayerCharacter::HandleHandReleased(EAedirHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	if (!bHandKeyTracked[Index])
	{
		return;
	}

	const bool bUseTriggered = bHandUseTriggered[Index];
	const bool bLongPress = Interaction && HandKeyHeldSeconds[Index] >= Interaction->UseHoldTime;

	bHandKeyTracked[Index] = false;
	bHandUseTriggered[Index] = false;
	HandKeyHeldSeconds[Index] = 0.f;

	if (!Interaction || bUseTriggered || Interaction->GetHandState(Hand) != EAedirHandState::Holding)
	{
		return;
	}

	const AAedirItemActor* HeldItem = Interaction->GetHeldItem(Hand);
	if (bLongPress && HeldItem && HeldItem->IsUsable())
	{
		Interaction->UseHeld(Hand);
	}
	else
	{
		Interaction->DropHeld(Hand);
	}
}

void AAedirPlayerCharacter::UpdateHandUseHold(float DeltaSeconds)
{
	if (!Interaction)
	{
		return;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!bHandKeyTracked[Index])
		{
			continue;
		}

		HandKeyHeldSeconds[Index] += DeltaSeconds;
		if (bHandUseTriggered[Index] || HandKeyHeldSeconds[Index] < Interaction->UseHoldTime)
		{
			continue;
		}

		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (Interaction->GetHandState(Hand) != EAedirHandState::Holding)
		{
			continue;
		}

		const AAedirItemActor* HeldItem = Interaction->GetHeldItem(Hand);
		if (HeldItem && HeldItem->IsUsable())
		{
			Interaction->UseHeld(Hand);
			bHandUseTriggered[Index] = true;
		}
	}
}

void AAedirPlayerCharacter::SetFirstPerson(bool bNewFirstPerson)
{
	bFirstPerson = bNewFirstPerson;

	if (FollowCamera)
	{
		FollowCamera->SetActive(!bFirstPerson);
	}
	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetActive(bFirstPerson);
	}

	if (USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		if (bFirstPerson)
		{
			BodyMesh->HideBoneByName(HeadBoneToHideInFirstPerson, PBO_None);
		}
		else
		{
			BodyMesh->UnHideBoneByName(HeadBoneToHideInFirstPerson);
		}
	}
}

void AAedirPlayerCharacter::ToggleView()
{
	SetFirstPerson(!bFirstPerson);
}

void AAedirPlayerCharacter::HandleViewTogglePressed()
{
	ToggleView();
}

void AAedirPlayerCharacter::HandleMenuTogglePressed()
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->ToggleMenu();
	}
}

void AAedirPlayerCharacter::HandleJournalPressed()
{
	OpenMenuCategory(4);
}

void AAedirPlayerCharacter::HandleMapPressed()
{
	OpenMenuCategory(3);
}

void AAedirPlayerCharacter::HandleSkillsPressed()
{
	OpenMenuCategory(2);
}

void AAedirPlayerCharacter::HandlePausePressed()
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->TogglePauseMenu();
	}
}

void AAedirPlayerCharacter::OpenMenuCategory(int32 Category)
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->OpenMenu(Category);
	}
}

UAedirMenuSubsystem* AAedirPlayerCharacter::GetMenuSubsystem()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
}

bool AAedirPlayerCharacter::IsMenuOpen()
{
	const UAedirMenuSubsystem* Menu = GetMenuSubsystem();
	return Menu && Menu->IsMenuOpen();
}

bool AAedirPlayerCharacter::IsCharacterCreationOpen()
{
	const UAedirMenuSubsystem* Menu = GetMenuSubsystem();
	return Menu && Menu->IsCharacterCreationOpen();
}

void AAedirPlayerCharacter::MaybeOpenCharacterCreation()
{
	const AAedirGameMode* GameModeDefaults = GetDefault<AAedirGameMode>();
	if (GameModeDefaults && GameModeDefaults->bShowCharacterCreationOnStart && IsLocallyControlled())
	{
		CharacterCreation();
	}
}

void AAedirPlayerCharacter::ServerChooseRace_Implementation(FName ChosenRaceId)
{
	AAedirCreature::SetRace(ChosenRaceId);
	EnsureStarterQuest();
}

void AAedirPlayerCharacter::CharacterCreation()
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->OpenCharacterCreation();
	}
}

void AAedirPlayerCharacter::EnsureStarterQuest()
{
	UWorld* World = GetWorld();
	UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	if (!Journal || PlayerFindQuest(Journal, PlayerTutorialQuestId()))
	{
		return;
	}

	Journal->AddQuest(PlayerBuildAwakeningQuest());
}

void AAedirPlayerCharacter::UpdateTutorialObjectives()
{
	UWorld* World = GetWorld();
	UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	const FAedirQuest* Quest = Journal ? PlayerFindQuest(Journal, PlayerTutorialQuestId()) : nullptr;
	if (!Quest || !Quest->bActive || Quest->bCompleted)
	{
		return;
	}

	if (!bTutorialRotationCaptured)
	{
		TutorialStartRotation = GetControlRotation();
		bTutorialRotationCaptured = true;
	}

	const FRotator Turn = (GetControlRotation() - TutorialStartRotation).GetNormalized();
	const bool bLookedAround = FMath::Abs(Turn.Yaw) > PlayerTutorialTurnDegrees || FMath::Abs(Turn.Pitch) > PlayerTutorialTurnDegrees;
	const bool bHoldingItem = Interaction && (IsValid(Interaction->GetHeldItem(EAedirHand::Left)) || IsValid(Interaction->GetHeldItem(EAedirHand::Right)));
	const bool bInFightStance = Combat && Combat->IsInFightStance();
	const bool bMenuOpened = IsMenuOpen();

	// Indices match the objective order in PlayerBuildAwakeningQuest.
	const bool bObjectiveMet[] = { bLookedAround, bHoldingItem, bInFightStance, bMenuOpened };
	const FName QuestId = PlayerTutorialQuestId();
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(bObjectiveMet)); ++Index)
	{
		if (bObjectiveMet[Index] && Quest->Objectives.IsValidIndex(Index) && !Quest->Objectives[Index].bDone)
		{
			Journal->CompleteObjective(QuestId, Index);
		}
	}
}


void AAedirPlayerCharacter::SpawnItem(const FString& ItemId, const FString& Mode)
{
	RunDevCommand(TEXT("SpawnItem"), FString::Printf(TEXT("%s %s"), *ItemId, *Mode), true);
}

void AAedirPlayerCharacter::ListItems()
{
	RunDevCommand(TEXT("ListItems"), FString(), false);
}

void AAedirPlayerCharacter::SpawnEnemy(int32 Count, int32 Armed)
{
	RunDevCommand(TEXT("SpawnEnemy"), FString::Printf(TEXT("%d %d"), Count, Armed), true);
}

void AAedirPlayerCharacter::KillAll()
{
	RunDevCommand(TEXT("KillAll"), FString(), true);
}

void AAedirPlayerCharacter::SetWeatherType(const FString& WeatherName)
{
	RunDevCommand(TEXT("SetWeatherType"), WeatherName, true);
}

void AAedirPlayerCharacter::Heal()
{
	RunDevCommand(TEXT("Heal"), FString(), true);
}

void AAedirPlayerCharacter::Hurt(float Amount)
{
	RunDevCommand(TEXT("Hurt"), FString::Printf(TEXT("%f"), Amount), true);
}

void AAedirPlayerCharacter::God()
{
	RunDevCommand(TEXT("God"), FString(), true);
}

void AAedirPlayerCharacter::GiveSkill(const FString& SkillName, float Amount)
{
	const FString Args = SkillName.IsEmpty() ? FString() : FString::Printf(TEXT("%s %f"), *SkillName, Amount);
	RunDevCommand(TEXT("GiveSkill"), Args, true);
}

void AAedirPlayerCharacter::ChooseRace(const FString& RaceName)
{
	RunDevCommand(TEXT("SetRace"), RaceName, true);
}

void AAedirPlayerCharacter::Wound(const FString& WoundType, const FString& Bone)
{
	RunDevCommand(TEXT("Wound"), FString::Printf(TEXT("%s %s"), *WoundType, *Bone), true);
}

void AAedirPlayerCharacter::BloodTest()
{
	RunDevCommand(TEXT("BloodTest"), FString(), false);
}

void AAedirPlayerCharacter::DebugBody()
{
	RunDevCommand(TEXT("DebugBody"), FString(), false);
}

void AAedirPlayerCharacter::DevHelp()
{
	RunDevCommand(TEXT("DevHelp"), FString(), false);
}

void AAedirPlayerCharacter::SetStat(const FString& StatName, float Value)
{
	RunDevCommand(TEXT("SetStat"), FString::Printf(TEXT("%s %f"), *StatName, Value), true);
}

void AAedirPlayerCharacter::ShowStats()
{
	RunDevCommand(TEXT("Stats"), FString(), false);
}

void AAedirPlayerCharacter::Push(float Strength)
{
	RunDevCommand(TEXT("Push"), FString::Printf(TEXT("%f"), Strength), true);
}

void AAedirPlayerCharacter::Knockdown()
{
	RunDevCommand(TEXT("Knockdown"), FString(), true);
}

void AAedirPlayerCharacter::GaitInfo(float Seconds, float Interval)
{
	RunDevCommand(TEXT("GaitInfo"), Interval > 0.f ? FString::Printf(TEXT("%f %f"), Seconds, Interval) : FString::Printf(TEXT("%f"), Seconds), false);
}

void AAedirPlayerCharacter::AutoMove(float Forward, float Right, int32 Run, float TurnDegrees)
{
	RunDevCommand(TEXT("AutoMove"), FString::Printf(TEXT("%f %f %d %f"), Forward, Right, Run, TurnDegrees), false);
}

void AAedirPlayerCharacter::ShotCam(const FString& CameraArgs)
{
	RunDevCommand(TEXT("ShotCam"), CameraArgs, false);
}

void AAedirPlayerCharacter::HolsterTest(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::PoseArm(const FString& PoseArgs)
{
	RunDevCommand(TEXT("PoseArm"), PoseArgs, true);
}

void AAedirPlayerCharacter::GripInfo()
{
	RunDevCommand(TEXT("GripInfo"), FString(), false);
}

void AAedirPlayerCharacter::DropHand(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->DropHeld(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::DrawSlot(int32 RightHand, int32 Slot)
{
	if (Interaction)
	{
		Interaction->DrawWaistSlot(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left, Slot);
	}
}

void AAedirPlayerCharacter::PlaceItem(const FString& ItemArgs)
{
	RunDevCommand(TEXT("PlaceItem"), ItemArgs, true);
}

void AAedirPlayerCharacter::ReachTest(float DelaySeconds, int32 BothHands)
{
	RunDevCommand(TEXT("ReachTest"), FString::Printf(TEXT("%f %d"), DelaySeconds, BothHands), true);
}

void AAedirPlayerCharacter::CrouchTest()
{
	RunDevCommand(TEXT("CrouchTest"), FString(), false);
}

void AAedirPlayerCharacter::FallTest(float HeightCm)
{
	RunDevCommand(TEXT("FallTest"), FString::Printf(TEXT("%f"), HeightCm), true);
}

void AAedirPlayerCharacter::ToggleConsole()
{
	if (ConsoleWidget.IsValid())
	{
		CloseConsole();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !IsLocallyControlled() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	ConsoleWidget = SNew(SAedirConsole)
		.OnCommand(FAedirConsoleCommand::CreateLambda([this](const FString& Command)
		{
			if (APlayerController* Controller = Cast<APlayerController>(GetController()))
			{
				const FString Output = Controller->ConsoleCommand(Command, false);
				if (!Output.IsEmpty())
				{
					ConsoleWidget->AddLogLine(Output);
				}
			}
		}))
		.OnClose(FSimpleDelegate::CreateUObject(this, &AAedirPlayerCharacter::CloseConsole));
	GEngine->GameViewport->AddViewportWidgetContent(ConsoleWidget.ToSharedRef(), 90);

	ConsoleReportHandle = UAedirDevTools::OnReported().AddLambda([this](const FString& Message)
	{
		if (ConsoleWidget.IsValid())
		{
			ConsoleWidget->AddLogLine(Message, FLinearColor(0.6f, 1.f, 0.7f, 1.f));
		}
	});

	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(ConsoleWidget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
	ConsoleWidget->FocusInput();
}

void AAedirPlayerCharacter::CloseConsole()
{
	if (!ConsoleWidget.IsValid())
	{
		return;
	}

	UAedirDevTools::OnReported().Remove(ConsoleReportHandle);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ConsoleWidget.ToSharedRef());
	}
	ConsoleWidget.Reset();

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->SetShowMouseCursor(false);
		PlayerController->SetInputMode(FInputModeGameOnly());
	}
}

void AAedirPlayerCharacter::RunDevCommand(const FString& Command, const FString& Args, bool bRunOnServer)
{
	if (bRunOnServer && !HasAuthority())
	{
		ServerDevCommand(Command, Args);
		return;
	}

	UAedirDevTools::Execute(this, Command, Args);
}

void AAedirPlayerCharacter::ServerDevCommand_Implementation(const FString& Command, const FString& Args)
{
	UAedirDevTools::Execute(this, Command, Args);
}

void AAedirPlayerCharacter::DrawDebugBody()
{
	const UWorld* World = GetWorld();
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!World || !BodyMesh || !Balance || !Combat || !Interaction)
	{
		return;
	}

	const FVector CenterOfMass = Balance->GetCenterOfMassWorld();
	DrawDebugSphere(World, CenterOfMass, 7.f, 12, FColor::Yellow, false, 0.f);

	const FVector HeadLocation = GetActorLocation() + FVector(0.f, 0.f, 110.f);
	DrawDebugString(World, HeadLocation, FString::Printf(TEXT("Margin %.1f cm  Balance %.2f"), Balance->GetSupportMargin(), Balance->GetBalance()), nullptr, FColor::White, 0.f, true);

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		const FColor HandColor = Hand == EAedirHand::Left ? FColor::Cyan : FColor::Green;
		const FName HandBone = Hand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
		const FVector HandLocation = BodyMesh->GetBoneLocation(HandBone);
		DrawDebugSphere(World, HandLocation, 4.f, 8, HandColor, false, 0.f);

		FVector SwingTarget = FVector::ZeroVector;
		float SwingAlpha = 0.f;
		if (Combat->GetSwingPose(Hand, SwingTarget, SwingAlpha))
		{
			DrawDebugSphere(World, SwingTarget, 4.f, 8, FColor::Magenta, false, 0.f);
		}

		const FVector Tip = Combat->GetTipWorld(Hand);
		DrawDebugLine(World, HandLocation, Tip, HandColor, false, 0.f, 0, 1.f);
		DrawDebugSphere(World, Tip, 3.f, 8, FColor::Red, false, 0.f);
	}
}


