#include "AedirCreature.h"

#include "Aedir.h"
#include "AedirGroupSubsystem.h"
#include "Combat/AedirCombatComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "Creature/AedirRaceCatalog.h"
#include "AedirStatsComponent.h"
#include "Animation/AedirGaitComponent.h"
#include "Animation/AedirProceduralAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"
#include "Surface/AedirFootstepComponent.h"
#include "Wounds/AedirWoundComponent.h"

AAedirCreature::AAedirCreature(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAedirCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationYaw = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
	}

	Stats = CreateDefaultSubobject<UAedirStatsComponent>(TEXT("Stats"));
	Balance = CreateDefaultSubobject<UAedirBalanceComponent>(TEXT("Balance"));
	Gait = CreateDefaultSubobject<UAedirGaitComponent>(TEXT("Gait"));
	Interaction = CreateDefaultSubobject<UAedirInteractionComponent>(TEXT("Interaction"));
	Combat = CreateDefaultSubobject<UAedirCombatComponent>(TEXT("Combat"));
	Footsteps = CreateDefaultSubobject<UAedirFootstepComponent>(TEXT("Footsteps"));
	Wounds = CreateDefaultSubobject<UAedirWoundComponent>(TEXT("Wounds"));

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultBodyMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (DefaultBodyMesh.Succeeded())
		{
			MeshComponent->SetSkeletalMeshAsset(DefaultBodyMesh.Object);
		}
		MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		MeshComponent->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		MeshComponent->SetAnimInstanceClass(UAedirProceduralAnimInstance::StaticClass());
		MeshComponent->bEnableUpdateRateOptimizations = false;
		MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void AAedirCreature::BeginPlay()
{
	Super::BeginPlay();

	if (Stats)
	{
		Stats->OnStrengthChanged.AddDynamic(this, &AAedirCreature::ApplySizeScale);
		ApplySizeScale(Stats->GetSizeScale());
	}
}

void AAedirCreature::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirCreature, GroupId);
	DOREPLIFETIME(AAedirCreature, CarriedWeight);
	DOREPLIFETIME(AAedirCreature, RaceId);
}

void AAedirCreature::SetGroupId_Implementation(FName NewGroupId)
{
	GroupId = NewGroupId;
	OnGroupIdChanged.Broadcast(GroupId);
}

void AAedirCreature::SetRace_Implementation(FName NewRaceId)
{
	const FAedirRaceDef* Race = UAedirRaceCatalog::Find(NewRaceId);
	if (!Race)
	{
		UE_LOG(LogAedir, Warning, TEXT("Unknown race %s"), *NewRaceId.ToString());
		return;
	}

	if (Stats)
	{
		Stats->ApplyRace(*Race);
	}

	RaceId = Race->Id;
	OnRaceChanged.Broadcast(RaceId);
}

FText AAedirCreature::GetRaceDisplayName() const
{
	const FAedirRaceDef* Race = UAedirRaceCatalog::Find(RaceId);
	return Race ? Race->DisplayName : FText::GetEmpty();
}

EAedirRelation AAedirCreature::GetRelationTo(const AAedirCreature* Other) const
{
	const UWorld* World = GetWorld();
	const UAedirGroupSubsystem* Subsystem = World ? World->GetSubsystem<UAedirGroupSubsystem>() : nullptr;
	if (!Other || !Subsystem)
	{
		return EAedirRelation::Neutral;
	}

	return Subsystem->GetRelation(GroupId, Other->GroupId);
}

bool AAedirCreature::IsEnemyOf(const AAedirCreature* Other) const
{
	return GetRelationTo(Other) == EAedirRelation::Enemy;
}

float AAedirCreature::ReceiveCreatureDamage(float Amount)
{
	if (!HasAuthority() || !Stats)
	{
		return 0.f;
	}

	return Stats->ApplyDamage(Amount);
}

float AAedirCreature::GetCarriedWeight() const
{
	return CarriedWeight;
}

void AAedirCreature::SetCarriedWeight(float NewCarriedWeight)
{
	if (!HasAuthority())
	{
		return;
	}

	CarriedWeight = FMath::Max(0.f, NewCarriedWeight);
}

float AAedirCreature::GetCarryCapacity() const
{
	return Stats ? Stats->GetCarryCapacity() : 0.f;
}

void AAedirCreature::MoveLocal(FVector2D Input)
{
	const FRotator BaseRotation = Controller ? GetControlRotation() : GetActorRotation();
	const FRotationMatrix YawMatrix(FRotator(0.f, BaseRotation.Yaw, 0.f));

	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), static_cast<float>(Input.Y));
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), static_cast<float>(Input.X));
}

void AAedirCreature::SetWantsToRun(bool bWantsToRun)
{
	if (UAedirCharacterMovementComponent* Movement = Cast<UAedirCharacterMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetWantsToRun(bWantsToRun);
	}
}

void AAedirCreature::SetWantsToCrouch(bool bWantsToCrouch)
{
	if (bWantsToCrouch)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}

void AAedirCreature::StartJump()
{
	if (bIsCrouched)
	{
		UnCrouch();
		return;
	}

	Jump();
}

void AAedirCreature::StopJump()
{
	StopJumping();
}

void AAedirCreature::SetWantsFightStance(bool bWantsFightStance)
{
	if (UAedirCharacterMovementComponent* Movement = Cast<UAedirCharacterMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetWantsFightStance(bWantsFightStance);
	}
}

bool AAedirCreature::IsRunning() const
{
	const UAedirCharacterMovementComponent* Movement = Cast<UAedirCharacterMovementComponent>(GetCharacterMovement());
	return Movement && Movement->IsRunningNow();
}

bool AAedirCreature::IsInFightStance() const
{
	const UAedirCharacterMovementComponent* Movement = Cast<UAedirCharacterMovementComponent>(GetCharacterMovement());
	return Movement && Movement->bWantsFightStance;
}

void AAedirCreature::ApplySizeScale(float NewSizeScale)
{
	SetActorScale3D(FVector(NewSizeScale));
}

void AAedirCreature::OnRep_GroupId()
{
	OnGroupIdChanged.Broadcast(GroupId);
}

void AAedirCreature::OnRep_RaceId()
{
	OnRaceChanged.Broadcast(RaceId);
}
