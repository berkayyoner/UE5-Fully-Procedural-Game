#include "AedirPlayerCharacter.h"
#include "Aedir.h"
#include "Travel/AedirTravelSubsystem.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Npc/AedirTrainingDummy.h"
#include "Wounds/AedirWoundComponent.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Net/AedirSaveSubsystem.h"
#include "UI/AedirMenuSubsystem.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"

#include "Settings/AedirSettingsSubsystem.h"
#include "Engine/GameInstance.h"
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
#include "Dev/AedirConsoleCommands.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMeshActor.h"
#include "Game/AedirEntities.h"
#include "Weather/AedirPrecipitationSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Graphics/AedirFoliageSubsystem.h"
#include "UI/SAedirConsole.h"
#include "GameFramework/SpringArmComponent.h"
#include "Player/AedirCameraBoom.h"
#include "Items/AedirInteractionComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/AedirGameMode.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirItemActor.h"
#include "TimerManager.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirMenuSubsystem.h"
#include "Weather/AedirWeatherEffectsComponent.h"
#include "Weather/AedirWeatherManager.h"

namespace
{
	// A press of the thrust key shorter than this is a tap that leaves the thrust on.
	constexpr double ThrustTapSeconds = 0.25;
	constexpr float AedirGuestStoreSeconds = 60.f;
	constexpr float AedirConsolePickRangeCm = 20000.f;
	constexpr float AedirConsolePickRadiusCm = 4.f;
	constexpr float AedirPointRangeCm = 8000.f;
	constexpr float AedirPointBehindCm = 40.f;
	constexpr float AedirPointItemWidthCm = 18.f;
	constexpr float AedirPointItemWidthShare = 0.012f;
	constexpr float AedirTalkRangeCm = 300.f;
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

	CameraBoom = CreateDefaultSubobject<UAedirCameraBoom>(TEXT("CameraBoom"));
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
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UAedirSettingsSubsystem* Settings = GameInstance->GetSubsystem<UAedirSettingsSubsystem>())
		{
			Settings->ApplyWhenWorldReady();
		}
	}

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

	SendGuestCharacter();

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
			for (const TCHAR* ItemId : { TEXT("RustySword"), TEXT("IronSword"), TEXT("SteelSword"), TEXT("SteelLongsword"), TEXT("BloodySword"), TEXT("SilverShortsword"), TEXT("ElvenBattleaxe"), TEXT("Axe"), TEXT("RustyIronMace") })
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
		// Down on the ground the same keys get the body up: W off the back, S from face down, held all the way up.
		if (Balance && (Balance->IsRagdoll() || Balance->IsGettingUp()) && FMath::Abs(MoveInput.Y) > 0.5)
		{
			Balance->RequestGetUp(MoveInput.Y > 0.0);
		}
	}

	UpdateLockOn(DeltaSeconds);
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

	// A hand goes across the body to the belt, so T works the left side of the belt with the right hand and Y the right side with the left.
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHolsterPressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHolsterPressed);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AAedirPlayerCharacter::HandleTalkPressed);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AAedirPlayerCharacter::HandleShovePressed);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &AAedirPlayerCharacter::HandleKickPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AAedirPlayerCharacter::HandleThrustPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AAedirPlayerCharacter::HandleThrustReleased);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AAedirPlayerCharacter::HandleTurnWeaponPressed);
	// The key under Esc types " on a Turkish keyboard, and is known by that.
	PlayerInputComponent->BindKey(EKeys::Tilde, IE_Pressed, this, &AAedirPlayerCharacter::ToggleConsole);
	PlayerInputComponent->BindKey(EKeys::Quote, IE_Pressed, this, &AAedirPlayerCharacter::ToggleConsole);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AAedirPlayerCharacter::HandleDynamicKickPressed);
	PlayerInputComponent->BindKey(EKeys::CapsLock, IE_Pressed, this, &AAedirPlayerCharacter::HandleLockOnTogglePressed);
}

void AAedirPlayerCharacter::HandleLockOnTogglePressed()
{
	bLockOnEnabled = !bLockOnEnabled;
	LockOnTarget.Reset();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UAedirTravelSubsystem* Travel = PlayerController->GetLocalPlayer() ? PlayerController->GetLocalPlayer()->GetSubsystem<UAedirTravelSubsystem>() : nullptr)
		{
			Travel->ShowNotification(bLockOnEnabled ? NSLOCTEXT("Aedir", "LockOnOn", "Target lock on") : NSLOCTEXT("Aedir", "LockOnOff", "Target lock off"), 2.f);
		}
	}
}

AAedirCreature* AAedirPlayerCharacter::FindLockOnTarget() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !GetWorld())
	{
		return nullptr;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Look = ViewRotation.Vector();
	const float MinCos = FMath::Cos(FMath::DegreesToRadians(LockOnConeDegrees));

	// The living creature nearest to where the view points, among those close enough; a little further away counts a little.
	AAedirCreature* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		AAedirCreature* Other = *It;
		if (!Other || Other == this || (Other->Stats && Other->Stats->IsDead()))
		{
			continue;
		}
		const FVector Chest = Other->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector To = Chest - ViewLocation;
		const float Distance = static_cast<float>((Other->GetActorLocation() - GetActorLocation()).Size());
		const float Cos = static_cast<float>(FVector::DotProduct(To.GetSafeNormal(), Look));
		if (Distance > LockOnRangeCm || Cos < MinCos)
		{
			continue;
		}
		const float Score = FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f)) + Distance / LockOnRangeCm * 0.25f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

void AAedirPlayerCharacter::UpdateLockOn(float DeltaSeconds)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const bool bGuard = Combat && Combat->IsInFightStance();
	const bool bDown = Balance && (Balance->IsRagdoll() || Balance->IsGettingUp());
	if (!bLockOnEnabled || !bGuard || bDown || !PlayerController || IsMenuOpen())
	{
		LockOnTarget.Reset();
		return;
	}

	AAedirCreature* Target = LockOnTarget.Get();
	const bool bLost = !Target || (Target->Stats && Target->Stats->IsDead()) || FVector::Dist(Target->GetActorLocation(), GetActorLocation()) > LockOnRangeCm * 1.4f;
	if (bLost)
	{
		Target = FindLockOnTarget();
		LockOnTarget = Target;
		UE_CLOG(Target != nullptr, LogAedir, Log, TEXT("[LockOn] The view locks on %s."), *GetNameSafe(Target));
	}
	if (!Target)
	{
		// With nobody to look at, the guard still brings the view down over the shoulder.
		FRotator Control = PlayerController->GetControlRotation();
		const float Pitch = FMath::FInterpTo(static_cast<float>(FRotator::NormalizeAxis(Control.Pitch)), FightViewPitch, DeltaSeconds, FightViewPitchSpeed);
		PlayerController->SetControlRotation(FRotator(Pitch, Control.Yaw, Control.Roll));
		return;
	}

	// The view turns to the target's chest, or where its body lies, and keeps it there while the guard is held.
	FVector Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	if (const USkeletalMeshComponent* TargetMesh = Target->GetMesh(); TargetMesh && TargetMesh->GetBoneIndex(TEXT("spine_03")) != INDEX_NONE)
	{
		Aim = TargetMesh->GetBoneLocation(TEXT("spine_03"));
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FRotator Want = (Aim - ViewLocation).Rotation();
	FRotator Control = PlayerController->GetControlRotation();
	// Never looking up at the fight from below its own shoulder: at least as steeply down as the fight view.
	const float WantPitch = FMath::Min(static_cast<float>(FRotator::NormalizeAxis(Want.Pitch)), FightViewPitch);
	const FRotator Turned = FMath::RInterpTo(Control, FRotator(FMath::Clamp(WantPitch, MinViewPitch, MaxViewPitch), Want.Yaw, Control.Roll), DeltaSeconds, LockOnTurnSpeed);
	PlayerController->SetControlRotation(FRotator(Turned.Pitch, Turned.Yaw, Control.Roll));
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
	const FAedirGameplaySettings& Settings = UAedirSettingsSubsystem::GetValues();
	const float CameraValue = Value * Settings.MouseSensitivity;
	AddControllerYawInput(bFighting ? CameraValue * FightCameraMouseScale : CameraValue);
	if (!FMath::IsNearlyZero(Value))
	{
		LastLookInputSeconds = 0.f;
	}

	PendingMouseDelta.X += Value * Settings.WeaponSensitivity;
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	const bool bFighting = Combat && Combat->IsInFightStance();
	const FAedirGameplaySettings& Settings = UAedirSettingsSubsystem::GetValues();
	const float CameraValue = (Settings.bInvertMouseY ? Value : -Value) * Settings.MouseSensitivity;
	AddControllerPitchInput(bFighting ? CameraValue * FightCameraMouseScale : CameraValue);
	if (!FMath::IsNearlyZero(Value))
	{
		LastLookInputSeconds = 0.f;
	}

	PendingMouseDelta.Y += Value * Settings.WeaponSensitivity;
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
	// Letting go of the guard goes back to swinging.
	EndLatchedThrust();
}

void AAedirPlayerCharacter::UpdateSwingInput(float DeltaSeconds)
{
	if (CameraBoom)
	{
		// The fight stance draws the view in close and up over the shoulder; out of it the view goes back to where it was set.
		const float FightAlpha = FMath::SmoothStep(0.f, 1.f, FightCameraAlpha);
		const float WantedLength = FMath::Lerp(CameraZoomTargetCm, FightArmLengthCm, FightAlpha);
		CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, WantedLength, DeltaSeconds, 10.f);
		CameraBoom->SocketOffset = FMath::Lerp(ThirdPersonShoulderOffset, FightShoulderOffset, FightAlpha);
	}

	LastLookInputSeconds += DeltaSeconds;
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
				// Down on the ground the view comes round behind the body, looking along it from its back, unless the mouse
				// is turning it.
				const FVector HeadWorld = BodyMesh->GetBoneLocation(TEXT("head"), EBoneSpaces::WorldSpace);
				const FVector Along = (HeadWorld - PelvisWorld).GetSafeNormal2D();
				if (Balance->IsRagdoll() && !Along.IsNearlyZero() && GetController() && LastLookInputSeconds > CameraRagdollFollowAfterSeconds)
				{
					const FRotator Current = GetControlRotation();
					const FRotator Wanted(CameraRagdollPitch, Along.Rotation().Yaw, 0.f);
					GetController()->SetControlRotation(FMath::RInterpTo(Current, Wanted, DeltaSeconds, CameraRagdollTurnSpeed));
				}
			}
			else
			{
				// The pivot follows the pelvis height with a slight delay, so steps, jumps and landings reach the camera.
				// Horizontally too: sway, lean and step shifts of the pelvis move the pivot a little.
				const FVector PelvisLocal = GetActorTransform().InverseTransformPositionNoScale(PelvisWorld);
				CameraPelvisBaseline = bCameraStateInitialized ? FMath::VInterpTo(CameraPelvisBaseline, PelvisLocal, DeltaSeconds, 0.6f) : PelvisLocal;
				TargetRelative += (PelvisLocal - CameraPelvisBaseline) * CameraPelvisInfluence * UAedirSettingsSubsystem::GetValues().CameraBodyMotion;
			}
		}

		CameraRelative = bCameraStateInitialized ? FMath::VInterpTo(CameraRelative, TargetRelative, DeltaSeconds, FollowSpeed) : TargetRelative;
		bCameraStateInitialized = true;
		CameraBoom->SetRelativeLocation(CameraRelative);
	}

	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(UAedirSettingsSubsystem::GetValues().FieldOfView);

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

void AAedirPlayerCharacter::SendGuestCharacter()
{
	UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr;
	if (!Saves || !Saves->IsGuest() || HasAuthority())
	{
		return;
	}

	// Controllers arrive a moment after the body on a guest's machine.
	FTimerHandle BringTimer;
	GetWorldTimerManager().SetTimer(BringTimer, FTimerDelegate::CreateWeakLambda(this, [this, Saves]()
	{
		if (!IsLocallyControlled())
		{
			return;
		}
		FAedirCreatureSave Character;
		bool bFound = false;
		Saves->GetCharacterOfSlot(Saves->GetCurrentSlot(), Character, bFound);
		if (bFound)
		{
			ServerBringCharacter(Character);
		}
		GetWorldTimerManager().SetTimer(GuestStoreTimer, this, &AAedirPlayerCharacter::StoreGuestCharacterNow, AedirGuestStoreSeconds, true);
	}), 1.f, false);
}

void AAedirPlayerCharacter::StoreGuestCharacterNow()
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr)
	{
		Saves->StoreGuestCharacter(Saves->GetCurrentSlot(), Cast<APlayerController>(GetController()));
	}
}

void AAedirPlayerCharacter::ServerBringCharacter_Implementation(const FAedirCreatureSave& Character)
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr)
	{
		Saves->ApplyCharacter(this, Character);
	}
}

AAedirCreature* AAedirPlayerCharacter::FindLookTarget(float MaxDistance) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		return nullptr;
	}

	FVector Eye;
	FRotator View;
	PlayerController->GetPlayerViewPoint(Eye, View);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirLookTarget), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	// The camera sits behind the shoulder, so the distance counts from the body.
	const float CameraOffset = static_cast<float>(FVector::Dist(Eye, GetActorLocation()));
	if (!World->SweepSingleByObjectType(Hit, Eye, Eye + View.Vector() * (MaxDistance + CameraOffset), FQuat::Identity, Objects, FCollisionShape::MakeSphere(22.f), Params))
	{
		return nullptr;
	}

	AAedirCreature* Target = Cast<AAedirCreature>(Hit.GetActor());
	return Target && Target->Stats && !Target->Stats->IsDead() ? Target : nullptr;
}

void AAedirPlayerCharacter::HandleTalkPressed()
{
	if (IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// A follower is waiting to be shown where to go: this press points it out.
	if (AAedirNpc* Pointing = PointingNpc.Get())
	{
		PointingNpc.Reset();
		FVector Where;
		AAedirItemActor* Item = nullptr;
		if (FindPointTarget(Where, Item))
		{
			ServerSendNpc(Pointing, Where, Item);
		}
		return;
	}

	AAedirNpc* Npc = Cast<AAedirNpc>(FindLookTarget(AedirTalkRangeCm));
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* Local = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UAedirMenuSubsystem* Menus = Local ? Local->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
	if (Npc && Menus && Npc->CanTalkTo(this))
	{
		Menus->OpenDialogue(Npc);
	}
}

bool AAedirPlayerCharacter::FindPointTarget(FVector& OutWhere, AAedirItemActor*& OutItem) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		return false;
	}

	FVector Eye;
	FRotator View;
	PlayerController->GetPlayerViewPoint(Eye, View);
	const FVector Direction = View.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirPointTarget), false, this);
	Params.AddIgnoredActor(PointingNpc.Get());
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Direction * AedirPointRangeCm, ECC_Visibility, Params);
	const double Limit = bHit ? Hit.Distance + AedirPointBehindCm : AedirPointRangeCm;

	// The item closest to the line of sight, and not behind whatever the line ends on. Big objects are nothing to go and fetch.
	OutItem = nullptr;
	double BestOff = UE_BIG_NUMBER;
	for (TActorIterator<AAedirItemActor> It(World); It; ++It)
	{
		const FVector ToItem = It->GetBoundsCenterWorld() - Eye;
		const double Along = FVector::DotProduct(ToItem, Direction);
		const double Off = (ToItem - Direction * Along).Size();
		if (Along > 0.0 && Along <= Limit && Off < AedirPointItemWidthCm + Along * AedirPointItemWidthShare && Off < BestOff && AAedirItemActor::IsPickable(*It) && It->ItemClass != EAedirItemClass::Object)
		{
			OutItem = *It;
			BestOff = Off;
		}
	}
	if (OutItem)
	{
		OutWhere = OutItem->GetBoundsCenterWorld();
		return true;
	}

	OutWhere = Hit.ImpactPoint;
	return bHit;
}

void AAedirPlayerCharacter::ServerSendNpc_Implementation(AAedirNpc* Npc, const FVector& Where, AAedirItemActor* Item)
{
	if (!Npc)
	{
		return;
	}
	if (Item)
	{
		Npc->OrderFetch(this, Item);
	}
	else
	{
		Npc->OrderGoTo(this, Where);
	}
}

void AAedirPlayerCharacter::RequestNpcAction(AAedirNpc* Npc, EAedirNpcAction Action)
{
	// Showing the way happens on this screen, the order goes out once a place is picked.
	if (Action == EAedirNpcAction::GoThere)
	{
		PointingNpc = Npc;
		return;
	}

	if (HasAuthority())
	{
		if (Npc)
		{
			Npc->PerformAction(this, Action);
		}
	}
	else
	{
		ServerNpcAction(Npc, Action);
	}
}

void AAedirPlayerCharacter::ServerNpcAction_Implementation(AAedirNpc* Npc, EAedirNpcAction Action)
{
	if (Npc && FVector::Dist(Npc->GetActorLocation(), GetActorLocation()) < AedirTalkRangeCm * 2.f)
	{
		Npc->PerformAction(this, Action);
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
		bThrustWasOn = bThrustLatched;
		ThrustPressedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Combat->SetThrustMode(true);
	}
}

void AAedirPlayerCharacter::HandleThrustReleased()
{
	if (!Combat)
	{
		return;
	}
	// A short tap turns the thrust on and leaves it, or off if it was on; a held key lets it go with the key.
	const bool bTap = GetWorld() && GetWorld()->GetTimeSeconds() - ThrustPressedAt < ThrustTapSeconds;
	bThrustLatched = bTap && !bThrustWasOn;
	Combat->SetThrustMode(bThrustLatched);
}

void AAedirPlayerCharacter::EndLatchedThrust()
{
	if (bThrustLatched && Combat)
	{
		bThrustLatched = false;
		Combat->SetThrustMode(false);
	}
}

void AAedirPlayerCharacter::HandleTurnWeaponPressed()
{
	if (!Interaction || !Combat)
	{
		return;
	}
	AAedirItemActor* Item = Interaction->GetHeldItem(EAedirHand::Right);
	if (!Item)
	{
		Item = Interaction->GetHeldItem(EAedirHand::Left);
	}
	if (Item)
	{
		const AAedirItemActor::EGripTurn Turn = Item->TurnInHand(this, Combat->IsHalfSwording());
		static const TCHAR* const Names[] = { TEXT("as made"), TEXT("turned on its haft"), TEXT("reversed"), TEXT("by the blade") };
		UE_LOG(LogAedir, Log, TEXT("[Grip] %s held %s"), *Item->GetName(), Names[static_cast<int32>(Turn)]);
	}
}

void AAedirPlayerCharacter::TurnWeapon()
{
	HandleTurnWeaponPressed();
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

	// With one weapon and a free hand X passes it over.
	if (Interaction && !Combat->IsTwoHandGrip() && Interaction->PassToOtherHand())
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

	if (Interaction && Interaction->IsMouthHold(Hand))
	{
		Interaction->SetMouthHold(Hand, false);
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
		if (HeldItem && HeldItem->IsConsumable())
		{
			// It goes to the mouth for as long as the key stays down.
			Interaction->SetMouthHold(Hand, true);
			bHandUseTriggered[Index] = true;
		}
		else if (HeldItem && HeldItem->IsUsable())
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
	if (PointingNpc.IsValid())
	{
		PointingNpc.Reset();
		return;
	}

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

void AAedirPlayerCharacter::BoneAxes(const FString& Bones)
{
	RunDevCommand(TEXT("BoneAxes"), Bones, false);
}

void AAedirPlayerCharacter::CloudFlash()
{
	RunDevCommand(TEXT("CloudFlash"), FString(), false);
}

void AAedirPlayerCharacter::Holster(const FString& HandArgs)
{
	RunDevCommand(TEXT("Holster"), HandArgs, false);
}

void AAedirPlayerCharacter::WaistInfo()
{
	RunDevCommand(TEXT("WaistInfo"), FString(), false);
}

void AAedirPlayerCharacter::LayDecal(const FString& Args)
{
	RunDevCommand(TEXT("LayDecal"), Args, false);
}

void AAedirPlayerCharacter::ArmourInfo()
{
	RunDevCommand(TEXT("ArmourInfo"), FString(), false);
}

void AAedirPlayerCharacter::ClearItems()
{
	RunDevCommand(TEXT("ClearItems"), FString(), false);
}

void AAedirPlayerCharacter::GaitInfo(float Seconds, float Interval)
{
	RunDevCommand(TEXT("GaitInfo"), Interval > 0.f ? FString::Printf(TEXT("%f %f"), Seconds, Interval) : FString::Printf(TEXT("%f"), Seconds), false);
}

void AAedirPlayerCharacter::AutoMove(float Forward, float Right, int32 Run, float TurnDegrees)
{
	RunDevCommand(TEXT("AutoMove"), FString::Printf(TEXT("%f %f %d %f"), Forward, Right, Run, TurnDegrees), false);
}

namespace
{
	// A blow on the chest from straight ahead, or on the back from straight behind, as a weapon of that mass and speed would land it.
	void StrikeCreature(AAedirCreature* Victim, float EnergyJoules, float MassKg, float SpeedCmPerSec, bool bFromBehind)
	{
		if (!Victim || !Victim->Balance || !Victim->GetMesh())
		{
			return;
		}
		const FVector Toward = Victim->GetActorForwardVector() * (bFromBehind ? -1.f : 1.f);
		FAedirImpactInfo Impact;
		Impact.Bone = TEXT("spine_03");
		Impact.Zone = EAedirBodyZone::Torso;
		Impact.EnergyJoules = EnergyJoules;
		Impact.Direction = -Toward;
		Impact.Location = Victim->GetMesh()->GetBoneLocation(TEXT("spine_03")) + Toward * 15.f;
		Impact.MassKg = MassKg;
		Impact.RelativeSpeed = SpeedCmPerSec;
		Victim->Balance->ApplyHitReaction(Impact);
		UE_LOG(LogTemp, Display, TEXT("[Dev] %s struck with %.0f J from %s, balance %.2f"), *Victim->GetName(), EnergyJoules, bFromBehind ? TEXT("behind") : TEXT("the front"), Victim->Balance->GetBalance());
	}
}

void AAedirPlayerCharacter::StrikeSelf(float EnergyJoules, float MassKg, float SpeedCmPerSec, int32 FromBehind)
{
	StrikeCreature(this, EnergyJoules, MassKg, SpeedCmPerSec, FromBehind != 0);
}

void AAedirPlayerCharacter::StrikeNearest(float EnergyJoules, int32 FromBehind)
{
	AAedirCreature* Nearest = nullptr;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (*It != this && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	StrikeCreature(Nearest, EnergyJoules, 4.f, 900.f, FromBehind != 0);
}

void AAedirPlayerCharacter::WarpTo(float XMetres, float YMetres, float Yaw)
{
	const FVector Spot(XMetres * 100.f, YMetres * 100.f, 0.f);
	// The ground itself, under whatever mist or tree stands over it.
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirTeleport), false, this);
	GetWorld()->LineTraceMultiByObjectType(Hits, Spot + FVector(0.f, 0.f, 100000.f), Spot - FVector(0.f, 0.f, 100000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	const FHitResult* Ground = Hits.FindByPredicate([](const FHitResult& One) { return One.GetActor() && One.GetActor()->IsA<ALandscapeProxy>(); });
	if (Ground)
	{
		const FHitResult& Hit = *Ground;
		SetActorLocationAndRotation(Hit.ImpactPoint + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Mind = GetController())
		{
			Mind->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		}
	}
}

void AAedirPlayerCharacter::ShotCam(const FString& CameraArgs)
{
	RunDevCommand(TEXT("ShotCam"), CameraArgs, false);
}

void AAedirPlayerCharacter::LightningNow()
{
	for (TActorIterator<AAedirWeatherManager> It(GetWorld()); It; ++It)
	{
		It->StrikeLightningNow();
	}
}

void AAedirPlayerCharacter::ShapeInfo()
{
	RunDevCommand(TEXT("ShapeInfo"), FString(), false);
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

void AAedirPlayerCharacter::KickNow(float HoldShare)
{
	if (Combat)
	{
		Combat->DevHoldKick(HoldShare);
		Combat->StartKick();
	}
}

void AAedirPlayerCharacter::HandKey(int32 RightHand)
{
	HandleHandPressed(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
}

void AAedirPlayerCharacter::PlaceBlock(float Forward, float Right, float Up, float SizeCm)
{
	// A fixed block in front of the body, for trying what a held item does against something that does not give way.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (AStaticMeshActor* Block = Cube ? GetWorld()->SpawnActor<AStaticMeshActor>(GetActorTransform().TransformPositionNoScale(FVector(Forward, Right, Up)), GetActorRotation()) : nullptr)
	{
		Block->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->SetActorScale3D(FVector(SizeCm / 100.f));
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	}
}

void AAedirPlayerCharacter::HandWatch(int32 RightHand, float Seconds)
{
	// How much the hand turns against the body over a stretch of time, and how fast at the worst moment.
	const FName HandBone = RightHand != 0 ? TEXT("hand_r") : TEXT("hand_l");
	struct FWatch
	{
		FTimerHandle Timer;
		FQuat Last = FQuat::Identity;
		bool bStarted = false;
		float Elapsed = 0.f;
		float PeakAt = 0.f;
		float Total = 0.f;
		float Peak = 0.f;
	};
	TSharedRef<FWatch> Watch = MakeShared<FWatch>();
	const float Step = 1.f / 60.f;
	GetWorldTimerManager().SetTimer(Watch->Timer, FTimerDelegate::CreateWeakLambda(this, [this, Watch, HandBone, Seconds, Step]()
	{
		const FQuat Now = GetActorQuat().Inverse() * GetMesh()->GetBoneQuaternion(HandBone);
		Watch->Elapsed += Step;
		if (Watch->bStarted)
		{
			const float Degrees = FMath::RadiansToDegrees(static_cast<float>(Watch->Last.AngularDistance(Now)));
			Watch->Total += Degrees;
			if (Degrees > Watch->Peak)
			{
				Watch->Peak = Degrees;
				Watch->PeakAt = Watch->Elapsed;
			}
		}
		Watch->Last = Now;
		Watch->bStarted = true;
		if (Watch->Elapsed >= Seconds)
		{
			UE_LOG(LogTemp, Display, TEXT("HandWatch total %.0f deg, worst step %.1f deg at %.2f s, spine twist %.1f"), Watch->Total, Watch->Peak, Watch->PeakAt, Combat ? Combat->GetSwingTwistDegrees() : 0.f);
			GetWorldTimerManager().ClearTimer(Watch->Timer);
		}
	}), Step, true);
}

void AAedirPlayerCharacter::MouseFlick(int32 RightHand, float DeltaX, float DeltaY, float Seconds)
{
	// Feeds an arm the same input a moving mouse gives it, for a look at throws and swings without one.
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	const float Step = 1.f / 60.f;
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	TSharedRef<float> Left = MakeShared<float>(Seconds);
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Timer, Left, Hand, DeltaX, DeltaY, Step]()
	{
		if (Combat)
		{
			Combat->AddSwingInput(Hand, FVector2D(DeltaX, DeltaY), Step);
		}
		*Left -= Step;
		if (*Left <= 0.f)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), Step, true);
}

void AAedirPlayerCharacter::SwingTrace(int32 RightHand, float Seconds)
{
	// One log line per frame with where the hand, the blade and the feet really are against the body, for a look at how evenly they move.
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	const FName HandBone = RightHand != 0 ? TEXT("hand_r") : TEXT("hand_l");
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	TSharedRef<float> Elapsed = MakeShared<float>(0.f);
	// The item in that hand at the start is followed wherever it goes, onto the belt and back.
	const TWeakObjectPtr<AAedirItemActor> Watched = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
	FTimerManagerTimerParameters Params;
	Params.bLoop = true;
	Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Timer, Elapsed, Hand, HandBone, Seconds, Watched]()
	{
		const float Delta = GetWorld()->GetDeltaSeconds();
		*Elapsed += Delta;
		const FTransform& Body = GetActorTransform();
		const USkeletalMeshComponent* BodyMesh = GetMesh();
		const FVector HandLocal = Body.InverseTransformPositionNoScale(BodyMesh->GetBoneLocation(HandBone));
		const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
		const FVector Blade = Item ? Body.InverseTransformVectorNoScale(Item->GetActorForwardVector()) : FVector::ZeroVector;
		const FVector ItemLocal = Watched.IsValid() ? Body.InverseTransformPositionNoScale(Watched->GetActorLocation()) : FVector::ZeroVector;
		const FVector2D Pose = Combat ? Combat->DevGetPoseAim(Hand) : FVector2D::ZeroVector;
		const auto FootLine = [&](const TCHAR* Foot, const TCHAR* Ball) { return Body.InverseTransformVectorNoScale(BodyMesh->GetBoneLocation(Ball) - BodyMesh->GetBoneLocation(Foot)).GetSafeNormal(); };
		const FVector FootL = FootLine(TEXT("foot_l"), TEXT("ball_l"));
		const FVector FootR = FootLine(TEXT("foot_r"), TEXT("ball_r"));
		FVector Meant = FVector::ZeroVector;
		float MeantAlpha = 0.f;
		if (Combat && Combat->GetWeaponAim(Hand, Meant, MeantAlpha))
		{
			Meant = Body.InverseTransformVectorNoScale(Meant);
		}
		UE_LOG(LogTemp, Display, TEXT("[Swing] %.3f %.4f %.1f %.1f %.2f %.2f %.2f %.4f %.4f %.4f %.3f %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f %.2f %.2f %.2f"), *Elapsed, Delta, Pose.X, Pose.Y, HandLocal.X, HandLocal.Y, HandLocal.Z, Blade.X, Blade.Y, Blade.Z, FootL.X, FootL.Y, FootL.Z, FootR.X, FootR.Y, FootR.Z, Meant.X, Meant.Y, Meant.Z, ItemLocal.X, ItemLocal.Y, ItemLocal.Z);
		if (*Elapsed >= Seconds)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), 0.001f, Params);
}

void AAedirPlayerCharacter::PointTest(float Yaw, float Pitch)
{
	if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FRotator(Pitch, GetActorRotation().Yaw + Yaw, 0.f));
	}
	FTimerHandle PressTimer;
	GetWorldTimerManager().SetTimer(PressTimer, this, &AAedirPlayerCharacter::HandleTalkPressed, 0.3f, false);
}

void AAedirPlayerCharacter::AimForTest(TWeakObjectPtr<AActor> Target, bool bPress)
{
	// The camera swings round the body as the view turns, so the aim is taken a few times over.
	const bool bPressShot = bPressThenShot;
	TSharedRef<int32> Left = MakeShared<int32>(6);
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Target, bPress, bPressShot, Left, Timer]()
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const AAedirItemActor* Item = Cast<AAedirItemActor>(Target.Get());
		if (PlayerController && Target.IsValid())
		{
			FVector Eye;
			FRotator View;
			PlayerController->GetPlayerViewPoint(Eye, View);
			GetController()->SetControlRotation(((Item ? Item->GetBoundsCenterWorld() : Target->GetActorLocation()) - Eye).Rotation());
		}
		if (--*Left <= 0)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
			if (bPress)
			{
				HandleTalkPressed();
				if (bPressShot)
				{
					GetController<APlayerController>()->ConsoleCommand(TEXT("shot showui"));
				}
			}
		}
	}), 0.15f, true);
}

void AAedirPlayerCharacter::PointItemTest(const FString& Id)
{
	for (TActorIterator<AAedirCatalogItem> It(GetWorld()); It; ++It)
	{
		if (It->CatalogId == FName(*Id) && AAedirItemActor::IsPickable(*It))
		{
			AimForTest(*It, true);
			return;
		}
	}
}

void AAedirPlayerCharacter::AimOnlyItem(const FString& Id)
{
	for (TActorIterator<AAedirCatalogItem> It(GetWorld()); It; ++It)
	{
		if (It->CatalogId == FName(*Id) && AAedirItemActor::IsPickable(*It))
		{
			AimForTest(*It, false);
			return;
		}
	}
}

void AAedirPlayerCharacter::LookNpcTest(const FString& Name, int32 Press)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			bPressThenShot = Press == 2;
			AimForTest(*It, Press != 0);
			return;
		}
	}
}

void AAedirPlayerCharacter::NpcDropTest(const FString& Name, const FString& Id, float Forward)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			FHitResult Hit;
			const FVector Over = It->GetActorLocation() + It->GetActorForwardVector() * Forward;
			const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit, Over + FVector(0.0, 0.0, 300.0), Over - FVector(0.0, 0.0, 1000.0), ECC_Visibility);
			AAedirCatalogItem::SpawnCatalogItem(GetWorld(), FName(*Id), FTransform((bGround ? Hit.ImpactPoint : Over) + FVector(0.0, 0.0, 30.0)));
			return;
		}
	}
}

void AAedirPlayerCharacter::NpcRemoveTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->Destroy();
		}
	}
}

void AAedirPlayerCharacter::NpcDisarmTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->RestoreGear(TArray<FName>());
		}
	}
}

void AAedirPlayerCharacter::NpcBringTest(const FString& Name, float Forward)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->TeleportTo(GetActorLocation() + GetActorForwardVector() * Forward, GetActorRotation() + FRotator(0.f, 180.f, 0.f), false, false);
		}
	}
}

void AAedirPlayerCharacter::GearInfo()
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (!It->Interaction)
		{
			continue;
		}
		const auto NameOf = [](const AAedirItemActor* Item) { return Item ? Item->ItemName.ToString() : FString(TEXT("-")); };
		FString Belt;
		for (const AAedirItemActor* Item : It->Interaction->GetWaistItemsCopy())
		{
			if (Item)
			{
				Belt += Item->ItemName.ToString() + TEXT(" ");
			}
		}
		FString Worn;
		for (const TObjectPtr<AAedirItemActor>& Item : It->Interaction->WornItems)
		{
			Worn += NameOf(Item.Get()) + TEXT(" ");
		}
		for (TActorIterator<AAedirCatalogItem> Loose(GetWorld()); Loose; ++Loose)
		{
			if (AAedirItemActor::IsPickable(*Loose) && FVector::Dist(Loose->GetActorLocation(), It->GetActorLocation()) < 2000.0)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirGearInfo), false, *It);
				Params.AddIgnoredActor(*Loose);
				GetWorld()->LineTraceSingleByChannel(Hit, It->GetPawnViewLocation(), Loose->GetBoundsCenterWorld(), ECC_Visibility, Params);
				Belt += FString::Printf(TEXT("(near %s %.0f at %s blocked by %s) "), *Loose->CatalogId.ToString(), FVector::Dist(Loose->GetActorLocation(), It->GetActorLocation()), *Loose->GetBoundsCenterWorld().ToCompactString(), *GetNameSafe(Hit.GetActor()));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[Gear] %s order %d at %s: right %s left %s belt [%s] worn [%s] target %d"), *It->NpcName.ToString(), static_cast<int32>(It->Order), *It->GetActorLocation().ToCompactString(), *NameOf(It->Interaction->GetHeldItem(EAedirHand::Right)), *NameOf(It->Interaction->GetHeldItem(EAedirHand::Left)), *Belt, *Worn, It->Combat && It->Combat->HasAITarget() ? 1 : 0);
	}
}

void AAedirPlayerCharacter::TalkTest(const FString& Topic)
{
	// Walks up to the nearest person, opens the talk and says what was asked for, without a keyboard.
	AAedirNpc* Nearest = nullptr;
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->Role_Npc != EAedirNpcRole::Enemy && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())) && (!Topic.StartsWith(TEXT("Follow")) && !Topic.StartsWith(TEXT("Stay")) && !Topic.StartsWith(TEXT("part")) && !Topic.StartsWith(TEXT("Go")) || It->Role_Npc == EAedirNpcRole::Follower))
		{
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		return;
	}
	TArray<FAedirDialogueTopic> Topics;
	Nearest->BuildTopics(this, Topics);
	FString List;
	for (const FAedirDialogueTopic& One : Topics)
	{
		List += One.Label.ToString() + TEXT(" | ");
		if (!Topic.IsEmpty() && One.Label.ToString().Contains(Topic))
		{
			UE_LOG(LogTemp, Display, TEXT("[Talk] %s says: %s"), *Nearest->NpcName.ToString(), *One.Response.ToString());
			RequestNpcAction(Nearest, One.Action);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[Talk] %s (%s), order %d, %s: %s"), *Nearest->NpcName.ToString(), *Nearest->GetStandingText(UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(GetController()))).ToString(), static_cast<int32>(Nearest->Order), *Nearest->GetActorLocation().ToCompactString(), *List);
}

void AAedirPlayerCharacter::AssetCheck(const FString& Paths)
{
	TArray<FString> List;
	Paths.ParseIntoArray(List, TEXT(","));
	for (const FString& Path : List)
	{
		const UObject* Object = LoadObject<UObject>(nullptr, *Path);
		const UStaticMesh* Static = Cast<UStaticMesh>(Object);
		const UMaterialInterface* Material = Static ? Static->GetMaterial(0) : Cast<UMaterialInterface>(Object);
		const UMaterial* Root = Material ? Material->GetMaterial() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("[Asset] %s: %s, material %s, root %s"), *Path, Object ? *Object->GetClass()->GetName() : TEXT("MISSING"), *GetNameSafe(Material), *GetPathNameSafe(Root));
		if (const UMaterialInterface* Master = Cast<UMaterialInterface>(Object))
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Master->GetAllTextureParameterInfo(Infos, Ids);
			FString Names;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				Names += Info.Name.ToString() + TEXT(" ");
			}
			UE_LOG(LogTemp, Display, TEXT("[Asset] params: %s domain %d"), *Names, static_cast<int32>(Master->GetMaterial()->MaterialDomain));
		}
		if (const UTexture* Texture = Cast<UTexture>(Object))
		{
			UE_LOG(LogTemp, Display, TEXT("[Asset] texture virtual %d srgb %d compression %d lodgroup %d"), Texture->VirtualTextureStreaming ? 1 : 0, Texture->SRGB ? 1 : 0, static_cast<int32>(Texture->CompressionSettings), static_cast<int32>(Texture->LODGroup));
		}
	}
}

void AAedirPlayerCharacter::CutTest(const FString& Bone)
{
	AAedirNpc* Nearest = nullptr;
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation()))
		{
			Nearest = *It;
		}
	}
	if (Nearest && Nearest->Wounds)
	{
		const FVector Where = Nearest->GetMesh()->GetBoneLocation(FName(*Bone));
		const bool bCut = Nearest->Wounds->TrySever(FName(*Bone), Where, FVector(0.3, 0.2, 1.0).GetSafeNormal(), Nearest->GetActorRightVector() * 500.0, 500.f, 0.f);
		UE_LOG(LogTemp, Display, TEXT("[Cut] %s %s: %d, %s stands at %s"), *Nearest->NpcName.ToString(), *Bone, bCut ? 1 : 0, *Nearest->NpcName.ToString(), *Nearest->GetActorLocation().ToCompactString());
	}
}

void AAedirPlayerCharacter::DummyCutTest(const FString& PartName)
{
	for (TActorIterator<AAedirTrainingDummy> It(GetWorld()); It; ++It)
	{
		TArray<UPrimitiveComponent*> Parts;
		It->GetComponents<UPrimitiveComponent>(Parts);
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part->GetName() == PartName)
			{
				It->ReceiveStrike(Part, Part->Bounds.Origin, FVector(0.0, 500.0, 0.0), FVector(1.0, 0.0, 0.2).GetSafeNormal(), 200.f, true);
				UE_LOG(LogTemp, Display, TEXT("[Cut] dummy part %s at %s"), *PartName, *Part->Bounds.Origin.ToCompactString());
			}
		}
	}
}

void AAedirPlayerCharacter::SaveNow(const FString& Slot)
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Save] %s: %d"), *Slot, Saves->SaveWorld(Slot) ? 1 : 0);
	}
}

void AAedirPlayerCharacter::GuestSaveNow()
{
	StoreGuestCharacterNow();
	UAedirSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>();
	UE_LOG(LogTemp, Display, TEXT("[Save] guest %d into %s, strength %.1f"), Saves && Saves->IsGuest() ? 1 : 0, Saves ? *Saves->GetCurrentSlot() : TEXT(""), Stats ? Stats->Strength : 0.f);
}

void AAedirPlayerCharacter::DecalTest()
{
	FHitResult Hit;
	const FVector From = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirDecalTest), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, 300.0), ECC_Visibility, Params))
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aedir/Materials/M_Footprint.M_Footprint"));
		UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(this, Base, FVector(30.f, 50.f, 90.f), Hit.ImpactPoint + GetActorForwardVector() * 120.f, FRotationMatrix::MakeFromXZ(-Hit.ImpactNormal, GetActorForwardVector()).Rotator(), 60.f);
		UE_LOG(LogTemp, Display, TEXT("[Decal] on %s receives %d, decal %d material %s"), *GetNameSafe(Hit.GetComponent()), Hit.GetComponent() && Hit.GetComponent()->bReceivesDecals ? 1 : 0, Decal ? 1 : 0, *GetNameSafe(Base));
		if (UMaterialInstanceDynamic* Print = Decal ? Decal->CreateDynamicMaterialInstance() : nullptr)
		{
			Print->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.f, 0.f));
			Print->SetScalarParameterValue(TEXT("Strength"), 1.f);
		}
	}
}

void AAedirPlayerCharacter::NeedsInfo()
{
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (It->Stats)
		{
			const AAedirNpc* Npc = Cast<AAedirNpc>(*It);
			UE_LOG(LogTemp, Display, TEXT("[Needs] %s at %s yaw %.0f: health %.0f hunger %.1f thirst %.1f target %d"), Npc ? *Npc->NpcName.ToString() : *It->GetName(), *It->GetActorLocation().ToCompactString(), It->GetActorRotation().Yaw, It->Stats->GetHealth(), It->Stats->GetHunger(), It->Stats->GetThirst(), It->Combat && It->Combat->HasAITarget() ? 1 : 0);
		}
	}
}

void AAedirPlayerCharacter::PassHand()
{
	if (Interaction)
	{
		Interaction->PassToOtherHand();
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
		.OnCommand(FAedirConsoleCommand::CreateUObject(this, &AAedirPlayerCharacter::RunConsoleLine))
		.OnPick(FAedirConsolePick::CreateUObject(this, &AAedirPlayerCharacter::HandleConsolePick))
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
	RefreshConsoleSelection();

	// Alone in the world, everything waits. With others there it goes on.
	bConsolePaused = GetWorld()->GetNetMode() == NM_Standalone && PlayerController->SetPause(true);
}

void AAedirPlayerCharacter::RunConsoleLine(const FString& Line)
{
	if (AedirConsoleCommands::IsCommand(Line))
	{
		const AActor* Selected = SelectedEntity.Get();
		const FString Full = AedirConsoleCommands::TakesReference(Line) && Selected ? FString::Printf(TEXT("%s %08X"), *Line.TrimEnd(), AedirEntities::RefIdOfActor(Selected)) : Line;
		if (HasAuthority())
		{
			AedirConsoleCommands::Run(this, Full);
		}
		else
		{
			ServerConsoleLine(Full);
		}
		if (Selected && !IsValid(SelectedEntity.Get()))
		{
			SelectedEntity.Reset();
		}
		RefreshConsoleSelection();
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		const FString Output = PlayerController->ConsoleCommand(Line, false);
		if (!Output.IsEmpty() && ConsoleWidget.IsValid())
		{
			ConsoleWidget->AddLogLine(Output);
		}
	}
}

void AAedirPlayerCharacter::ServerConsoleLine_Implementation(const FString& Line)
{
	AedirConsoleCommands::Run(this, Line);
}

void AAedirPlayerCharacter::HandleConsolePick(const FVector2D& ViewportPixel)
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector Origin;
	FVector Direction;
	AActor* Picked = nullptr;
	if (PlayerController && PlayerController->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, Origin, Direction))
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirConsolePick), true, this);
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, Origin, Origin + Direction * AedirConsolePickRangeCm, FQuat::Identity, Objects, FCollisionShape::MakeSphere(AedirConsolePickRadiusCm), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Actor = Hit.GetActor();
			if (AedirEntities::RefIdOfActor(Actor) != 0)
			{
				Picked = Actor;
				break;
			}
			// A wall in the way ends the search.
			if (Hit.bBlockingHit && Hit.Component.IsValid() && Hit.Component->GetCollisionObjectType() == ECC_WorldStatic)
			{
				break;
			}
		}
	}

	// A second click anywhere lets go of what was picked, on something else it takes that instead.
	SelectedEntity = Picked != SelectedEntity.Get() ? Picked : nullptr;
	RefreshConsoleSelection();
}

void AAedirPlayerCharacter::RefreshConsoleSelection()
{
	if (ConsoleWidget.IsValid())
	{
		const AActor* Selected = SelectedEntity.Get();
		const FName Base = AedirEntities::BaseNameOfActor(Selected);
		ConsoleWidget->SetSelection(Selected ? FString::Printf(TEXT("%s    Ref %08X    Base %08X (%s)"), *AedirEntities::DisplayNameOfActor(Selected), AedirEntities::RefIdOfActor(Selected), AedirEntities::BaseIdOfName(Base), *Base.ToString()) : FString());
	}
}

void AAedirPlayerCharacter::Con(const FString& Line)
{
	RunConsoleLine(Line);
}

void AAedirPlayerCharacter::TalkOpenTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const ULocalPlayer* Local = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
		UAedirMenuSubsystem* Menus = Local ? Local->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
		if (Menus && It->NpcName.ToString().Contains(Name))
		{
			Menus->OpenDialogue(*It);
			return;
		}
	}
}

void AAedirPlayerCharacter::LandInfo()
{
	// Which material each piece of the land draws with, by its class.
	int32 Shown = 0;
	for (TActorIterator<AActor> It(GetWorld()); It && Shown < 6; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("Landscape")))
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Parts;
		It->GetComponents<UPrimitiveComponent>(Parts);
		for (UPrimitiveComponent* Part : Parts)
		{
			UMaterialInterface* Material = Part->GetMaterial(0);
			UE_LOG(LogTemp, Display, TEXT("[Land] %s %s %s material %s parent %s"), *It->GetClass()->GetName(), *Part->GetClass()->GetName(), *Part->GetName(), *GetNameSafe(Material), *GetNameSafe(Material ? Material->GetMaterial() : nullptr));
			TArray<UMaterialInterface*> Used;
			Part->GetUsedMaterials(Used);
			for (UMaterialInterface* Each : Used)
			{
				const FMaterialResource* Resource = Each ? Each->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
				const UMaterialInstance* Instance = Cast<UMaterialInstance>(Each);
				UE_LOG(LogTemp, Display, TEXT("[Land]   uses %s (%s) finished %d errors %d permutation %d landscape %d shadermap %d"), *GetNameSafe(Each), Each ? *Each->GetClass()->GetName() : TEXT("-"), Resource ? Resource->IsCompilationFinished() : -1, Resource ? Resource->GetCompileErrors().Num() : -1,
					Instance ? (int32)Instance->bHasStaticPermutationResource : -1, Resource ? (int32)Resource->IsUsedWithLandscape() : -1, Resource ? (int32)(Resource->GetGameThreadShaderMap() != nullptr) : -1);
				if (Resource)
				{
					for (const FString& Error : Resource->GetCompileErrors())
					{
						UE_LOG(LogTemp, Display, TEXT("[Land]     %s"), *Error.Left(300));
					}
				}
			}
			if (++Shown >= 6)
			{
				break;
			}
		}
	}
}

void AAedirPlayerCharacter::FoliageInfo()
{
	if (const UAedirFoliageSubsystem* Foliage = GetWorld()->GetSubsystem<UAedirFoliageSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Foliage] %d planted, %d crushed"), Foliage->GetInstanceCount(), Foliage->GetCrushedCount());
	}
}

void AAedirPlayerCharacter::FxTest(const FString& Path, float Scale)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	UE_LOG(LogTemp, Display, TEXT("[Fx] %s: %d"), *Path, System ? 1 : 0);
	if (System)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, GetActorLocation() + GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 40.f), GetActorForwardVector().Rotation(), FVector(Scale));
	}
}

void AAedirPlayerCharacter::MeshSheet(const FString& Folder)
{
	TArray<FAssetData> Assets;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({ Folder }, true);
	Registry.GetAssetsByPath(FName(*Folder), Assets, true);
	FVector Where = GetActorLocation() + GetActorForwardVector() * 300.f;
	const FVector Step = GetActorRightVector() * 45.f;
	Where -= Step * (Assets.Num() / 2);
	for (const FAssetData& Asset : Assets)
	{
		UStaticMesh* Model = Cast<UStaticMesh>(Asset.GetAsset());
		if (!Model)
		{
			continue;
		}
		const FBox Box = Model->GetBoundingBox();
		AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Where - Box.GetCenter(), FRotator::ZeroRotator);
		Actor->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Model);
		UE_LOG(LogTemp, Display, TEXT("[Model] %s size %s center %s tris %d material %s"), *Model->GetName(), *Box.GetSize().ToCompactString(), *Box.GetCenter().ToCompactString(), Model->GetNumTriangles(0), *GetNameSafe(Model->GetMaterial(0)));
		Where += Step;
	}
}

void AAedirPlayerCharacter::LookTo(float Yaw, float Pitch)
{
	if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FRotator(Pitch, GetActorRotation().Yaw + Yaw, 0.f));
	}
}

void AAedirPlayerCharacter::GuardKey(int32 RightHand, int32 Down)
{
	if (Combat)
	{
		Combat->SetGuard(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left, Down != 0);
	}
}

void AAedirPlayerCharacter::PrecipInfo()
{
	if (const UAedirPrecipitationSubsystem* Precip = GetWorld()->GetSubsystem<UAedirPrecipitationSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Precip] rain %d snow %d"), Precip->GetActiveRain(), Precip->GetActiveSnow());
	}
}

void AAedirPlayerCharacter::ConOpen()
{
	ToggleConsole();
}

void AAedirPlayerCharacter::ConPick(float ShareX, float ShareY)
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->GetViewportSize(SizeX, SizeY);
	}
	HandleConsolePick(FVector2D(SizeX * ShareX, SizeY * ShareY));
	const AActor* Selected = SelectedEntity.Get();
	UE_LOG(LogTemp, Display, TEXT("[Console] selected %s ref %08X"), *AedirEntities::DisplayNameOfActor(Selected), AedirEntities::RefIdOfActor(Selected));
}

void AAedirPlayerCharacter::MouthTest(int32 RightHand, int32 Hold)
{
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	if (Hold != 0)
	{
		HandleHandPressed(Hand);
	}
	else
	{
		HandleHandReleased(Hand);
	}
}

void AAedirPlayerCharacter::PackDrawTest(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->DrawFromBackpack(0, RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::NeedInfo()
{
	if (Stats && Interaction)
	{
		const AAedirItemActor* Right = Interaction->GetHeldItem(EAedirHand::Right);
		const AAedirItemActor* Left = Interaction->GetHeldItem(EAedirHand::Left);
		const USkeletalMeshComponent* BodyMesh = GetMesh();
		const FVector Head = BodyMesh->GetBoneLocation(FName(TEXT("head")));
		UE_LOG(LogTemp, Display, TEXT("[Need] hunger %.1f thirst %.1f stamina %.1f/%.1f regen %.2f athletics %.0f pack %d right %s left %s item to head %.1f up %.2f"), Stats->GetHunger(), Stats->GetThirst(), Stats->GetStamina(), Stats->GetMaxStamina(), Stats->GetStaminaRegenRate(), Stats->Athletism, Interaction->GetBackpackItemsCopy().Num(), Right ? *Right->ItemName.ToString() : TEXT("-"), Left ? *Left->ItemName.ToString() : TEXT("-"), Right ? FVector::Dist(Right->GetBoundsCenterWorld(), Head) : 0.f, Right ? Right->GetActorForwardVector().Z : 0.f);
	}
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
		if (bConsolePaused)
		{
			PlayerController->SetPause(false);
		}
	}
	bConsolePaused = false;
	SelectedEntity.Reset();
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


