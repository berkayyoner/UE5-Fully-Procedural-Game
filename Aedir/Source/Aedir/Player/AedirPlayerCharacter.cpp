#include "AedirPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

AAedirPlayerCharacter::AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(GetCapsuleComponent());
	FollowCamera->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	FollowCamera->bUsePawnControlRotation = true;
}

void AAedirPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	ApplyViewPitchLimits();
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

	const FVector2D MoveInput(RightKeyValue - LeftKeyValue, ForwardKeyValue - BackwardKeyValue);
	MoveLocal(MoveInput.GetClampedToMaxSize(1.0));
}

void AAedirPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	PlayerInputComponent->BindAxisKey(EKeys::W, this, &AAedirPlayerCharacter::HandleForwardKey);
	PlayerInputComponent->BindAxisKey(EKeys::S, this, &AAedirPlayerCharacter::HandleBackwardKey);
	PlayerInputComponent->BindAxisKey(EKeys::D, this, &AAedirPlayerCharacter::HandleRightKey);
	PlayerInputComponent->BindAxisKey(EKeys::A, this, &AAedirPlayerCharacter::HandleLeftKey);

	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AAedirPlayerCharacter::HandleMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AAedirPlayerCharacter::HandleMouseY);

	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AAedirPlayerCharacter::HandleRunPressed);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AAedirPlayerCharacter::HandleRunReleased);

	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AAedirPlayerCharacter::HandleCrouchPressed);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &AAedirPlayerCharacter::HandleCrouchReleased);

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAedirPlayerCharacter::HandleJumpPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AAedirPlayerCharacter::HandleJumpReleased);

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleFightButtonPressed);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleFightButtonReleased);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleFightButtonPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleFightButtonReleased);
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

void AAedirPlayerCharacter::HandleForwardKey(float Value)
{
	ForwardKeyValue = Value;
}

void AAedirPlayerCharacter::HandleBackwardKey(float Value)
{
	BackwardKeyValue = Value;
}

void AAedirPlayerCharacter::HandleRightKey(float Value)
{
	RightKeyValue = Value;
}

void AAedirPlayerCharacter::HandleLeftKey(float Value)
{
	LeftKeyValue = Value;
}

void AAedirPlayerCharacter::HandleMouseX(float Value)
{
	AddControllerYawInput(Value);
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	AddControllerPitchInput(-Value);
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

void AAedirPlayerCharacter::HandleFightButtonPressed()
{
	++HeldFightButtons;
	SetWantsFightStance(true);
}

void AAedirPlayerCharacter::HandleFightButtonReleased()
{
	HeldFightButtons = FMath::Max(0, HeldFightButtons - 1);
	SetWantsFightStance(HeldFightButtons > 0);
}
