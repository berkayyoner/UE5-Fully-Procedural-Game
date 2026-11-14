#include "AedirCreature.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodySetup.h"

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
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"
#include "Surface/AedirFootstepComponent.h"
#include "Wounds/AedirWoundComponent.h"

static TAutoConsoleVariable<int32> CVarAedirSelfCollision(TEXT("aedir.SelfCollision"), 1, TEXT("Whether a body stands against its own weapon and its arms against its trunk (1), or passes through them (0)."));

namespace
{
	const TCHAR* const AedirManBodyPath = TEXT("/Game/Aedir/Characters/MetaHuman/Build/MHC_AedirMan/Body/SKM_MHC_AedirMan_BodyMesh.SKM_MHC_AedirMan_BodyMesh");
	const TCHAR* const AedirManFacePath = TEXT("/Game/Aedir/Characters/MetaHuman/Build/MHC_AedirMan/Face/SKM_MHC_AedirMan_FaceMesh.SKM_MHC_AedirMan_FaceMesh");
}

namespace
{
	// The bit the hands of every body and what they hold share, so two hands can hold one grip.
	constexpr uint8 AedirHandMaskBit = 128;
	// The feet pass each other too: in a quick turn or a narrow step one foot brushes past the other, and catching there would
	// trip the walk. The legs above them stay solid.
	constexpr uint8 AedirFootMaskBit = 64;
}

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

	// The skin takes its own wounds and the blood running from them. What lies on the ground reaches only a few centimetres
	// up from it, so at most the soles standing in it take some.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetReceivesDecals(true);
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

	// Everyone is a plain bald man in underwear made with MetaHuman, so tests read more clearly than with the mannequin; the
	// mannequin stays for a build without him.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ManBody(AedirManBodyPath);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ManFace(AedirManFacePath);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultBodyMesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (ManBody.Succeeded())
		{
			MeshComponent->SetSkeletalMeshAsset(ManBody.Object);
		}
		else if (DefaultBodyMesh.Succeeded())
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
	Face = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Face"));
	Face->SetupAttachment(GetMesh());
	Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Face->SetGenerateOverlapEvents(false);
	Face->bUseAttachParentBound = true;
	if (ManFace.Succeeded() && ManBody.Succeeded())
	{
		Face->SetSkeletalMeshAsset(ManFace.Object);
	}
}

void AAedirCreature::UseManBody()
{
	USkeletalMesh* Body = LoadObject<USkeletalMesh>(nullptr, AedirManBodyPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Body && GetMesh() && GetMesh()->GetSkeletalMeshAsset() != Body)
	{
		GetMesh()->SetSkeletalMeshAsset(Body);
	}
	if (Face && Body)
	{
		if (!Face->GetSkeletalMeshAsset())
		{
			Face->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, AedirManFacePath, nullptr, LOAD_NoWarn | LOAD_Quiet));
		}
		Face->SetLeaderPoseComponent(GetMesh());
	}
}

void AAedirCreature::ApplyBodyMask()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}
	if (CVarAedirSelfCollision.GetValueOnGameThread() == 0)
	{
		Body->BodyInstance.SetMaskFilter(BodyMaskBit);
		for (FBodyInstance* Part : Body->Bodies)
		{
			if (Part)
			{
				Part->SetMaskFilter(BodyMaskBit);
			}
		}
		return;
	}
	// Nothing passes through a body, not even what it holds itself: the trunk, the arms and the legs stand against its own
	// weapon as against anyone's, and the arms against the trunk, as the physics asset pairs them. Only the hands pass each
	// other, so two hands can close on one grip, and the feet each other.
	Body->BodyInstance.SetMaskFilter(0);
	for (FBodyInstance* Part : Body->Bodies)
	{
		if (Part && Part->BodySetup.IsValid())
		{
			const FString Bone = Part->BodySetup->BoneName.ToString();
			// The hands carry the body's own bit as well: what this body holds passes through its own hands, which close on it,
			// and strikes everyone else's.
			Part->SetMaskFilter(Bone.StartsWith(TEXT("hand")) ? (AedirHandMaskBit | BodyMaskBit) : Bone.StartsWith(TEXT("foot")) || Bone.StartsWith(TEXT("ball")) ? AedirFootMaskBit : 0);
		}
	}
}

uint8 AAedirCreature::GetHeldMaskBit() const
{
	return BodyMaskBit;
}

void AAedirCreature::BeginPlay()
{
	UseManBody();
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
	// Bits 64 and 128 are the feet's and the hands'; the others share the five between.
	BodyMaskBit = IsPlayerControlled() ? 1 : static_cast<uint8>(1 << (1 + NextCreatureBit++ % 5));
	ApplyBodyMask();

	ApplyAppearance();
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

bool AAedirCreature::IsFriendOf(const AAedirCreature* Other) const
{
	return Other && Other != this && (Other->GroupId == GroupId || GetRelationTo(Other) == EAedirRelation::Ally);
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

	// A goal taken from a body thrown about by physics can be broken for a frame; a broken direction would break the probes.
	if (Goal.ContainsNaN() || From.ContainsNaN() || FVector::DistSquared2D(Goal, From) < 1.f)
	{
		return;
	}
	const float GoalYaw = static_cast<float>((Goal - From).Rotation().Yaw) + (WalkDetourSeconds > 0.f ? WalkDetourYaw : 0.f);
	if (!FMath::IsFinite(GoalYaw))
	{
		return;
	}
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

static TAutoConsoleVariable<int32> CVarAedirBodySizeVariation(TEXT("aedir.BodySizeVariation"), 1, TEXT("Every body a little taller or shorter by its name (1) or all of one height (0)."));

static TAutoConsoleVariable<int32> CVarAedirBodySizeScale(TEXT("aedir.BodySizeScale"), 1, TEXT("Bodies sized by their strength and race (1) or all of one size (0)."));

void AAedirCreature::ApplySizeScale(float NewSizeScale)
{
	if (CVarAedirBodySizeScale.GetValueOnGameThread() == 0)
	{
		SetActorScale3D(FVector::OneVector);
		return;
	}
	SetActorScale3D(FVector(NewSizeScale * AppearanceHeight));
}

void AAedirCreature::ApplyAppearance()
{
	// Drawn from the body's own name, so the same person looks the same every time: a little taller or shorter, a little
	// heavier or slighter in build than strength alone makes them, and a skin of its own.
	static const FLinearColor Skins[] = {
		FLinearColor(0.93f, 0.76f, 0.64f), FLinearColor(0.86f, 0.66f, 0.52f), FLinearColor(0.76f, 0.56f, 0.42f), FLinearColor(0.62f, 0.44f, 0.32f),
		FLinearColor(0.47f, 0.32f, 0.23f), FLinearColor(0.34f, 0.23f, 0.17f), FLinearColor(0.96f, 0.82f, 0.72f), FLinearColor(0.80f, 0.62f, 0.50f) };
	FRandomStream Random(static_cast<int32>(GetTypeHash(GetName())));
	AppearanceHeight = Random.FRandRange(0.95f, 1.05f);
	if (CVarAedirBodySizeVariation.GetValueOnGameThread() == 0)
	{
		AppearanceHeight = 1.f;
	}
	AppearanceBulk = Random.FRandRange(0.92f, 1.08f);
	SkinTint = Skins[Random.RandHelper(UE_ARRAY_COUNT(Skins))] * Random.FRandRange(0.94f, 1.06f);
	SkinTint.A = 1.f;
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Skin = Body->CreateDynamicMaterialInstance(Slot))
			{
				Skin->SetVectorParameterValue(TEXT("Tint"), SkinTint);
			}
		}
	}
}

void AAedirCreature::OnRep_GroupId()
{
	OnGroupIdChanged.Broadcast(GroupId);
}

void AAedirCreature::OnRep_RaceId()
{
	OnRaceChanged.Broadcast(RaceId);
}
