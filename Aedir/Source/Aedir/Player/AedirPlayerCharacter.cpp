#include "AedirPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Dev/AedirDevTools.h"
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
	CameraBoom->CameraLagSpeed = 18.f;

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

void AAedirPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

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
		FTimerHandle StartupTimer;
		GetWorldTimerManager().SetTimer(StartupTimer, FTimerDelegate::CreateWeakLambda(this, [this, StartupCommands]()
		{
			TArray<FString> Commands;
			StartupCommands.ParseIntoArray(Commands, TEXT(";"), true);
			if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
			{
				for (const FString& Command : Commands)
				{
					PlayerController->ConsoleCommand(Command.TrimStartAndEnd());
				}
			}
		}), 2.0f, false);
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
		const FVector2D MoveInput(RightKeyValue - LeftKeyValue, ForwardKeyValue - BackwardKeyValue);
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

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAedirPlayerCharacter::HandleJumpPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AAedirPlayerCharacter::HandleJumpReleased);

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftGuardPressed);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleLeftGuardReleased);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightGuardPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleRightGuardReleased);

	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHandPressed);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &AAedirPlayerCharacter::HandleLeftHandReleased);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHandPressed);
	PlayerInputComponent->BindKey(EKeys::E, IE_Released, this, &AAedirPlayerCharacter::HandleRightHandReleased);

	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHolsterPressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHolsterPressed);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AAedirPlayerCharacter::HandleShovePressed);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &AAedirPlayerCharacter::HandleKickPressed);
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
	// While a guard is held the mouse moves the arm instead of the camera.
	if (!Combat || !Combat->IsInFightStance())
	{
		AddControllerYawInput(Value);
	}

	PendingMouseDelta.X += Value;
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	if (!Combat || !Combat->IsInFightStance())
	{
		AddControllerPitchInput(-Value);
	}

	PendingMouseDelta.Y += Value;
}

void AAedirPlayerCharacter::HandleMouseWheel(float Value)
{
	if (!Combat || Value == 0.f)
	{
		return;
	}

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (Combat->IsGuardActive(Hand))
		{
			Combat->AddExtensionInput(Hand, Value);
		}
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

void AAedirPlayerCharacter::HandleKickPressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
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


