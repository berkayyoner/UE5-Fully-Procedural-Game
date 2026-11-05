#include "AedirPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Items/AedirItemActor.h"
#include "Weather/AedirWeatherEffectsComponent.h"

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

	ApplyViewPitchLimits();
	SetFirstPerson(false);
}

void AAedirPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	ApplyViewPitchLimits();
}

void AAedirPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocallyControlled())
	{
		return;
	}

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const float ForwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::W) ? 1.f : 0.f;
	const float BackwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::S) ? 1.f : 0.f;
	const float RightKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::D) ? 1.f : 0.f;
	const float LeftKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::A) ? 1.f : 0.f;
	const FVector2D MoveInput(RightKeyValue - LeftKeyValue, ForwardKeyValue - BackwardKeyValue);
	MoveLocal(MoveInput.GetClampedToMaxSize(1.0));

	UpdateHandUseHold(DeltaSeconds);
	UpdateSwingInput(DeltaSeconds);
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

	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AAedirPlayerCharacter::HandleRunPressed);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AAedirPlayerCharacter::HandleRunReleased);

	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AAedirPlayerCharacter::HandleCrouchPressed);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &AAedirPlayerCharacter::HandleCrouchReleased);

	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AAedirPlayerCharacter::HandleViewTogglePressed);

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
	AddControllerYawInput(Value);
	PendingMouseDelta.X += Value;
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	AddControllerPitchInput(-Value);
	PendingMouseDelta.Y += Value;
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

