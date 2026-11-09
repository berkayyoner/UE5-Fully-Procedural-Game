#include "AedirCreature.h"

#include "Creature/AedirWayFinder.h"

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

namespace
{
	constexpr float AedirWalkProbeCm = 110.f;
	constexpr float AedirWayGoalMovedCm = 300.f;
	constexpr float AedirWayCheckSeconds = 0.5f;
	constexpr float AedirWayLookAheadCm = 1500.f;
	constexpr float AedirWayGroundUpCm = 300.f;
	constexpr float AedirWayGroundDownCm = 600.f;
	constexpr int32 AedirWayCellsPerFrame = 30;
	constexpr float AedirWayPointReachedCm = 70.f;
	constexpr float AedirWalkCheckSeconds = 0.8f;
	constexpr float AedirWalkStuckCm = 25.f;
	constexpr float AedirWalkDetourSeconds = 1.4f;
	constexpr float AedirWalkMaxDropCm = 200.f;
}

AAedirCreature::AAedirCreature(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAedirCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bUseControllerRotationYaw = false;

	// Footprints and blood on the ground land on the ground, not on the legs standing over it.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetReceivesDecals(false);
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
		// No higher than a leg lifts in a step: anything taller, the body would be left hanging on as the capsule went up.
		Movement->MaxStepHeight = 35.f;
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

void AAedirCreature::ApplyBodyMask()
{
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->BodyInstance.SetMaskFilter(BodyMaskBit);
		for (FBodyInstance* Part : Body->Bodies)
		{
			if (Part)
			{
				Part->SetMaskFilter(BodyMaskBit);
			}
		}
	}
}

void AAedirCreature::BeginPlay()
{
	Super::BeginPlay();

	// The body is solid to every other body, held weapon, corpse and loose thing: it is a physics body of its own, and only the
	// capsules, which carry the bodies about, pass through it.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetCollisionObjectType(ECC_PhysicsBody);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// The player's body has a bit of its own; the others share seven, so a weapon can tell its own body from anyone else's.
	static uint8 NextCreatureBit = 0;
	BodyMaskBit = IsPlayerControlled() ? 1 : static_cast<uint8>(1 << (1 + NextCreatureBit++ % 7));
	ApplyBodyMask();

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

void AAedirCreature::TurnBodyTo(float Yaw, float DeltaSeconds, float TurnDegreesPerSecond)
{
	SetActorRotation(FMath::RInterpConstantTo(FRotator(0.f, GetActorRotation().Yaw, 0.f), FRotator(0.f, Yaw, 0.f), DeltaSeconds, TurnDegreesPerSecond));
}

FVector AAedirCreature::SteerAlongWay(const FVector& Goal, float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);

	if (FVector::DistSquared2D(Goal, WayGoal) > FMath::Square(AedirWayGoalMovedCm))
	{
		WayPoints.Reset();
		WaySearch.Reset();
		WayGoal = Goal;
		WayCheckTimer = 0.f;
	}

	WayCheckTimer -= DeltaSeconds;
	if (WayCheckTimer <= 0.f)
	{
		WayCheckTimer = AedirWayCheckSeconds;
		// Far off, only the stretch ahead counts: it is looked at again when it gets there.
		FVector Target = Goal;
		const float Far = static_cast<float>(FVector::Dist2D(Feet, Goal));
		if (Far > AedirWayLookAheadCm)
		{
			Target = Feet + (Goal - Feet).GetSafeNormal2D() * AedirWayLookAheadCm;
		}
		FVector TargetGround;
		const bool bGround = FAedirWayFinder::FindGround(World, Target, AedirWayGroundUpCm, AedirWayGroundDownCm, this, TargetGround);
		if (bGround && FAedirWayFinder::CanWalkStraight(World, Feet, TargetGround, this))
		{
			WayPoints.Reset();
			WaySearch.Reset();
			return Goal;
		}
		if (WayPoints.IsEmpty() && !WaySearch.IsValid())
		{
			FVector GoalGround = Goal;
			FAedirWayFinder::FindGround(World, Goal, AedirWayGroundUpCm, AedirWayGroundDownCm, this, GoalGround);
			WaySearch = MakeShared<FAedirWayFinder>();
			WaySearch->Begin(World, Feet, GoalGround, this);
		}
	}

	if (WaySearch.IsValid() && WaySearch->Step(AedirWayCellsPerFrame))
	{
		WayPoints = WaySearch->GetPoints();
		WaySearch.Reset();
	}

	while (!WayPoints.IsEmpty() && FVector::DistSquared2D(WayPoints[0], Feet) < FMath::Square(AedirWayPointReachedCm))
	{
		WayPoints.RemoveAt(0);
	}
	return WayPoints.IsEmpty() ? Goal : WayPoints[0] + FVector(0.f, 0.f, HalfHeight);
}

void AAedirCreature::WalkTowards(const FVector& FinalGoal, float DeltaSeconds, float TurnDegreesPerSecond)
{
	const FVector Goal = SteerAlongWay(FinalGoal, DeltaSeconds);
	const FVector From = GetActorLocation();
	WalkCheckTimer += DeltaSeconds;
	if (WalkCheckTimer >= AedirWalkCheckSeconds)
	{
		if (FVector::DistSquared2D(From, WalkCheckLocation) < AedirWalkStuckCm * AedirWalkStuckCm && WalkDetourSeconds <= 0.f)
		{
			WalkDetourSeconds = AedirWalkDetourSeconds;
			WalkDetourYaw = FMath::RandBool() ? 90.f : -90.f;
			// The way it was on does not work out after all.
			WayPoints.Reset();
			WayCheckTimer = AedirWalkDetourSeconds;
		}
		WalkCheckLocation = From;
		WalkCheckTimer = 0.f;
	}
	WalkDetourSeconds = FMath::Max(WalkDetourSeconds - DeltaSeconds, 0.f);

	const float GoalYaw = static_cast<float>((Goal - From).Rotation().Yaw) + (WalkDetourSeconds > 0.f ? WalkDetourYaw : 0.f);
	const float FeetZ = static_cast<float>(From.Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWalkProbe), false, this);
	UWorld* World = GetWorld();

	// The first way that is near the wanted one and has ground under it and nothing in front of it.
	float WalkYaw = GoalYaw;
	for (const float Offset : { 0.f, 35.f, -35.f, 70.f, -70.f, 110.f, -110.f, 150.f, -150.f })
	{
		const FVector Way = FRotator(0.f, GoalYaw + Offset, 0.f).Vector();
		const FVector Ahead = From + Way * AedirWalkProbeCm;
		FHitResult Hit;
		const bool bWall = World->LineTraceSingleByChannel(Hit, From, Ahead, ECC_Visibility, Params);
		const bool bGround = World->LineTraceSingleByChannel(Hit, Ahead, FVector(Ahead.X, Ahead.Y, FeetZ - AedirWalkMaxDropCm), ECC_Visibility, Params);
		if (!bWall && bGround)
		{
			WalkYaw = GoalYaw + Offset;
			AddMovementInput(Way, 1.f);
			break;
		}
	}
	TurnBodyTo(WalkYaw, DeltaSeconds, TurnDegreesPerSecond);
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

	// A jump starts with a short crouch; the movement component fires the jump once it is over.
	if (UAedirCharacterMovementComponent* AedirMovement = Cast<UAedirCharacterMovementComponent>(GetCharacterMovement()))
	{
		if (AedirMovement->BeginJumpWindup())
		{
			return;
		}
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
