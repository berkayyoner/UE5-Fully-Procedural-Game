#include "AedirBalanceComponent.h"

#include "AnimationRuntime.h"
#include "Animation/AedirGaitComponent.h"
#include "Engine/SkeletalMesh.h"

#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#include "Combat/AedirCombatComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Wounds/AedirWoundComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Aedir.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Physics/AedirBodyControlComponent.h"
#include "Player/AedirPlayerCharacter.h"
#include "Audio/AedirSfxSubsystem.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

namespace
{
	constexpr float AedirPhysicsRescaleStep = 0.005f;
	constexpr float AedirLegInjuryRecoveryPenalty = 0.5f;
	constexpr float AedirLeanInterpSpeed = 8.f;
	constexpr float AedirSafeLandingSpeed = 900.f;
	constexpr float AedirUntrainedSafeLandingSpeed = 520.f;
	constexpr float AedirCrashLandingShare = 1.2f;
	constexpr float AedirAthletismRecoveryScale = 100.f;
	constexpr float AedirAthletismDamageScale = 200.f;
	constexpr float AedirWalkableNormalZ = 0.5f;
	constexpr float AedirWallImpactMaxLoss = 0.8f;
	constexpr float AedirObjectImpactBalanceMultiplier = 0.14f;
	constexpr float AedirObjectImpactMinSpeed = 120.f;
	constexpr float ObjectImpactMinHeightCm = 50.f;
	constexpr float AedirObjectImpactDamagePerScore = 0.002f;
	constexpr float AedirWallImpactDamagePerSpeed = 0.01f;
	// One shove from running into another body lasts this long.
	constexpr double BumpRepeatSeconds = 0.6;
	// Hips further than this from where they belong were not knocked there: the body was moved.
	constexpr float HipsTeleportCm = 250.f;
	// The same for a fully physical body, whose hips the pose sets exactly: no stride or blow carries them this far
	// from it while it stands.
	constexpr float BodyTeleportCm = 90.f;
	// A get-up key counts this long after it was last held.
	constexpr double AedirGetUpRequestHoldSeconds = 0.3;
	// A foot lifted less than this share of its step counts as standing on the ground.
	constexpr float AedirPlantedFootLift = 0.12f;
	// A frame longer than the physics steps through (six substeps of a sixtieth of a second).
	constexpr float AedirBodyHitchSeconds = 0.1f;
	// The mass an arm moves by itself, and the strength at which a held load weighs what it weighs.
	constexpr float AedirArmKilos = 4.f;
	constexpr float AedirArmLoadStrengthRef = 10.f;
	// How much further the hips may stray from the pose while getting up before it costs balance.
	constexpr float AedirGetUpDriftFreeScale = 2.f;
	// How long a body that was just carried over is left to arrive.
	constexpr float AedirBodyTeleportGraceSeconds = 0.15f;
	// Balance lost each second a player getting up lets go of the key: the body sinks back down within half a second.
	constexpr float AedirGetUpLetGoLossPerSecond = 2.5f;
	// How much harder a blow throws a body that is getting up than one standing.
	constexpr float AedirGetUpBlowScale = 2.f;
	// A living joint's room against the limp body's limits in the physics asset: scaled, then widened, never past the cap.
	constexpr float AedirLivingJointScale = 1.5f;
	constexpr float AedirLivingJointExtraDegrees = 30.f;
	constexpr float AedirLivingLockedExtraDegrees = 15.f;
	constexpr float AedirLivingJointMaxDegrees = 150.f;
	constexpr float AedirLivingWristDegrees = 140.f;
	constexpr float AedirLivingForearmSwingDegrees = 85.f;
	constexpr float AedirLivingForearmTwistDegrees = 150.f;
	// Toppled over, parts of the body below this share of its height are pushed back against the blow.
	constexpr float AedirToppleFootHoldShare = 0.3f;
	// Seconds a body that starts to get up takes to tense its limbs.
	constexpr float AedirGetUpTenseSeconds = 0.12f;

	constexpr float HitReactionAthleticsCap = 100.f;
	constexpr float HitReactionAthleticsLossScale = 0.5f;
	constexpr float HitReactionStaminaLossScale = 0.5f;
	constexpr float HitReactionDefaultAthletism = 10.f;
	constexpr float HitReactionStaggerThreshold = 0.15f;
	constexpr float HitReactionStaggerMinSeconds = 0.5f;
	constexpr float HitReactionStaggerMaxSeconds = 1.4f;
	constexpr float HitReactionStaggerSpeedPerJoule = 2.f;
	constexpr float HitReactionStaggerSpeedMax = 300.f;
	// A blow slackens the whole body for a moment, by how hard it pushed the part it landed on: struck, a body gives to the
	// blow instead of springing straight back into its stance.
	constexpr float HitFlinchFullImpulse = 1600.f;
	constexpr float HitFlinchMaxSlack = 0.35f;
	constexpr float HitFlinchSeconds = 0.45f;
	constexpr float HitFlinchMotorInterval = 0.05f;
	constexpr float HitReactionStaggerRecoveryScale = 0.7f;
	constexpr float HitReactionDazeEnergyRef = 60.f;
	constexpr float HitReactionDazeMaxSeconds = 2.5f;
	constexpr float HitReactionDazeRecoveryScale = 0.5f;
	constexpr float AedirGroggyBelow = 0.65f;
	// A head turned this far from where it sits on the neck, for this long, has gone round the wrong way.
	constexpr float AedirHeadTwistedDegrees = 100.f;
	constexpr float AedirHeadTwistedSeconds = 0.4f;
	// Put back on the capsule, the mesh keeps its place relative to it but its size in the world: let go, its relative
	// scale is its world one, and taken as relative it would be scaled by the capsule's once more for a moment, which
	// resizes every moving part of the body twice and breaks the physics on them.
	const FAttachmentTransformRules AedirMeshOnCapsuleRules(EAttachmentRule::KeepRelative, EAttachmentRule::KeepRelative, EAttachmentRule::KeepWorld, false);
	// What is left of the muscles of a broken arm.
	constexpr float AedirBrokenArmShare = 0.05f;
	constexpr float AedirGroggyMaxDaze = 0.85f;
	constexpr float HitReactionKnockdownExtraSeconds = 0.8f;
	// The blow moves the part it lands on, not the whole body through the air.
	constexpr float GrippedArmMotorShare = 0.3f;
	constexpr float BalanceArmMuscleShare = 0.5f;
	constexpr float BalanceTrunkMuscleShare = 1.f;
	// A standing body is never quite still: it drifts about its balance, more when tired or hurt.
	constexpr float BalanceSwayDegrees = 1.1f;
	constexpr float BalanceSwayTiredDegrees = 2.2f;
	constexpr float BalanceSwayHurtDegrees = 2.5f;
	constexpr float BalanceSwayRate = 0.32f;
	TAutoConsoleVariable<int32> CVarAedirActivePelvis(TEXT("aedir.ActivePelvis"), 1, TEXT("Hold the physical hips up by force (1) or move them with the pose (0)."));
	TAutoConsoleVariable<float> CVarAedirArmDamping(TEXT("aedir.ArmDamping"), 1.f, TEXT("Scale of the arm muscles' damping."));
	TAutoConsoleVariable<int32> CVarAedirBodyControl(TEXT("aedir.BodyControl"), -1, TEXT("The whole body on the engine's physics control: 1 on, 0 off (older motors), -1 as each body is set. Read when a body is made."));
	TAutoConsoleVariable<int32> CVarAedirPhysicalHips(TEXT("aedir.PhysicalHips"), -1, TEXT("Hips tied to the capsule by a motorised joint, limbs held by their joints: 1 on, 0 off, -1 as each body is set. Read when a body is made."));
	TAutoConsoleVariable<int32> CVarAedirHipVelocityTarget(TEXT("aedir.HipVelocityTarget"), 1, TEXT("The hip joint damps towards the capsule's own speed (1) or towards standing still against it (0)."));
	constexpr float HitReactionBoneImpulseScale = 0.1f;
	constexpr float HitReactionBoneImpulseMax = 2500.f;
	// Part of a blow goes on into the trunk, which turns away from it; the one who takes it feels it in the eyes as well.
	constexpr float HitReactionTrunkShare = 0.45f;
	constexpr float HitReactionHeadChestShare = 1.2f;
	constexpr float HitReactionHeadShare = 2.5f;
	constexpr float HitReactionViewDegreesPerJoule = 0.04f;
	constexpr float HitReactionViewMaxDegrees = 2.5f;
	// How much of its usual recovery a stumbling body manages while it tries to get its feet back under it.
	constexpr float StumbleCatchShare = 0.75f;
	constexpr float StumbleCatchTiredShare = 0.35f;
	constexpr float StumbleStaggerSeconds = 0.35f;
	// Below this balance a body steps to catch itself; every step that lands wins back this much of it, less when tired.
	constexpr float AedirStepCatchBelowBalance = 0.7f;
	// How much of its strength a living body keeps while it is down.
	constexpr float AedirLivingFallMotorShare = 0.22f;
	constexpr float AedirStepCatchGain = 0.16f;
	constexpr float AedirStepCatchTiredShare = 0.4f;
	constexpr float ComBalanceSampleIntervalSeconds = 0.1f;
	constexpr float ComBalanceGroundToleranceCm = 12.f;
	constexpr float ComBalanceTraceUpCm = 20.f;
	constexpr float ComBalanceTraceDownCm = 40.f;
	constexpr float ComBalanceHeelOffsetCm = 6.f;
	constexpr float ComBalanceToeOffsetCm = 6.f;
	constexpr float ComBalanceFootHalfWidthCm = 4.5f;
	constexpr float ComBalanceFullLossMarginCm = 12.f;
	constexpr float ComBalanceMaxLossPerSecond = 0.9f;
	constexpr float ComBalanceRecoveryMarginCm = -8.f;
	constexpr float ComBalanceSteadyBalance = 0.9f;
	constexpr float ComBalanceStaticAllowanceCm = 6.f;
	constexpr float ComBalanceBendingLowerCm = 5.f;
	constexpr float ComBalanceMovingSpeedRef = 300.f;
	constexpr float ComBalanceMovingAllowanceCm = 30.f;
	constexpr float ComBalanceReflexRangeCm = 220.f;
	constexpr float ComBalanceReflexFallSpeed = 350.f;
	constexpr float ComBalanceReflexRagdollSeconds = 0.4f;
	constexpr float ComBalanceReflexForwardCm = 25.f;
	constexpr float ComBalanceReflexInterpSpeed = 10.f;
	constexpr float ComBalanceReflexFallDamageReduction = 0.25f;

	// Cross product of (A - Origin) and (B - Origin). Positive when B is counter-clockwise from A around Origin.
	double ComBalanceCross(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D OriginToA = A - Origin;
		const FVector2D OriginToB = B - Origin;
		return OriginToA.X * OriginToB.Y - OriginToA.Y * OriginToB.X;
	}

	double ComBalanceDistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = Edge.SizeSquared();
		if (LengthSquared <= UE_SMALL_NUMBER)
		{
			return (Point - A).Size();
		}

		const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSquared, 0.0, 1.0);
		return (Point - (A + Edge * T)).Size();
	}

	float HitReactionZoneEnergyRef(EAedirBodyZone Zone)
	{
		switch (Zone)
		{
		case EAedirBodyZone::Head: return 90.f;
		case EAedirBodyZone::Torso: return 230.f;
		case EAedirBodyZone::Arm: return 400.f;
		case EAedirBodyZone::Leg: return 300.f;
		default: checkNoEntry(); return 60.f;
		}
	}
}

UAedirBalanceComponent::UAedirBalanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UAedirBalanceComponent::BeginPlay()
{
	Super::BeginPlay();

	CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	BodyMesh = CharacterOwner->GetMesh();
	BodyCapsule = CharacterOwner->GetCapsuleComponent();
	if (!BodyMesh || !BodyCapsule)
	{
		return;
	}

	BodyMesh->SetAllBodiesBelowSimulatePhysics(PelvisBoneName, true, true);
	BodyMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	MeshRestRelative = BodyMesh->GetRelativeTransform();
	MeshRelativeAtFall = MeshRestRelative;
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyMesh->SetNotifyRigidBodyCollision(true);
	ApplyBodyContacts(false);
	ApplyJointLimits(false);
	ApplyLegPhysics(false);
	BodyMesh->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnMeshHit);

	BodyCapsule->SetNotifyRigidBodyCollision(true);
	BodyCapsule->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnCapsuleHit);

	PelvisRestHeight = static_cast<float>(BodyMesh->GetBoneLocation(PelvisBoneName, EBoneSpaces::ComponentSpace).Z);

	if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}

	FTimerHandle DiagnosticsTimer;
	GetWorld()->GetTimerManager().SetTimer(DiagnosticsTimer, [this]()
	{
		if (!BodyMesh)
		{
			return;
		}
		int32 SimulatingBodies = 0;
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				++SimulatingBodies;
			}
		}
		UE_LOG(LogAedir, Log, TEXT("Balance diagnostics: physics asset %s, %d bodies, %d simulating, pelvis body %s."),
			BodyMesh->GetPhysicsAsset() ? *BodyMesh->GetPhysicsAsset()->GetName() : TEXT("none"), BodyMesh->Bodies.Num(), SimulatingBodies,
			BodyMesh->GetBodyInstance(PelvisBoneName) ? TEXT("found") : TEXT("missing"));
	}, 1.5f, false);
}

void UAedirBalanceComponent::KeepHeadOnNeck(float DeltaTime)
{
	// A blow or a fall can throw the head past where a neck turns, and from there its muscles pull it on round the short way,
	// backwards, where the joint holds it: a head standing that far from its neck for a moment is set back on it, as it
	// sits in the pose, at rest.
	FBodyInstance* Head = BodyMesh->GetBodyInstance(TEXT("head"));
	const FName NeckName = BodyMesh->GetParentBone(TEXT("head"));
	if (bIsRagdoll || IsGettingUp() || !Head || !Head->IsInstanceSimulatingPhysics() || NeckName.IsNone())
	{
		HeadTwistedSeconds = 0.f;
		return;
	}
	const FQuat NeckWorld = BodyMesh->GetBoneQuaternion(NeckName);
	const FQuat HeadWorld = Head->GetUnrealWorldTransform().GetRotation();
	const int32 HeadIndex = BodyMesh->GetBoneIndex(TEXT("head"));
	const FReferenceSkeleton& Skeleton = BodyMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const FQuat RestInNeck = Skeleton.GetRefBonePose()[HeadIndex].GetRotation();
	const float Off = FMath::RadiansToDegrees(static_cast<float>((NeckWorld.Inverse() * HeadWorld).AngularDistance(RestInNeck)));
	HeadTwistedSeconds = Off > AedirHeadTwistedDegrees ? HeadTwistedSeconds + DeltaTime : 0.f;
	if (HeadTwistedSeconds >= AedirHeadTwistedSeconds)
	{
		HeadTwistedSeconds = 0.f;
		FTransform Placed = Head->GetUnrealWorldTransform();
		Placed.SetRotation(NeckWorld * RestInNeck);
		Placed.SetScale3D(FVector::OneVector);
		Head->SetBodyTransform(Placed, ETeleportType::TeleportPhysics);
		Head->SetAngularVelocityInRadians(FVector::ZeroVector, false);
		UE_LOG(LogAedir, Log, TEXT("[Head] %s's head stood %.0f degrees off its neck and is set back on it."), *GetNameSafe(GetOwner()), Off);
	}
}

void UAedirBalanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CharacterOwner || !BodyMesh || !BodyCapsule)
	{
		return;
	}
	if (IConsoleVariable* FreeJoint = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.DevFreeJoint")))
	{
		const FString JointSetting = FreeJoint->GetString() + (IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.WideArmJoints")) ? IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.WideArmJoints"))->GetString() : FString());
		if (JointSetting != AppliedFreeJoint)
		{
			AppliedFreeJoint = JointSetting;
			ApplyJointLimits(false);
		}
	}

	// The engine lets a mesh go from what it is attached to whenever its root part starts simulating, as the hips of a fully
	// physical body do: up on its feet the mesh is put straight back on the capsule where it sat, the bodies staying put.
	if (BodyControl && !bIsRagdoll && BodyMesh->GetAttachParent() != BodyCapsule)
	{
		PutMeshOnCapsule();
	}

	// Joints are laid out at the scale the physics was made at, and a race or growing strength changes that: the limbs would stay their old length.
	const float MeshScale = static_cast<float>(BodyMesh->GetComponentScale().X);
	if (!bIsRagdoll && !FMath::IsNearlyEqual(MeshScale, PhysicsShapeScale, AedirPhysicsRescaleStep))
	{
		UE_LOG(LogAedir, Verbose, TEXT("[Body] %s reshaped: scale %.3f, was %.3f, relative %s, actor %s, capsule %s, mode %d, parent %s."), *GetNameSafe(GetOwner()), MeshScale, PhysicsShapeScale, *BodyMesh->GetRelativeTransform().ToString(), *CharacterOwner->GetActorScale3D().ToCompactString(),
			*BodyCapsule->GetComponentScale().ToCompactString(), static_cast<int32>(BodyMesh->PhysicsTransformUpdateMode), *GetNameSafe(BodyMesh->GetAttachParent()));
		PhysicsShapeScale = MeshScale;
		ReshapeBodyPhysics();
	}

	KeepHeadOnNeck(DeltaTime);

	// The muscles pull towards the pose, so they start once a body spawned in mid-game has one.
	if (!MotorComponent && !BodyControl && !BodyMesh->GetEditableComponentSpaceTransforms().IsEmpty())
	{
		SetupMotors();
	}

	ComSampleTimer -= DeltaTime;
	if (ComSampleTimer <= 0.f)
	{
		ComSampleTimer = ComBalanceSampleIntervalSeconds;
		SampleCenterOfMass();
		SampleProtectiveReflex();
	}
	SmoothProtectiveReflex(DeltaTime);

	if (IsAuthoritative())
	{
		UpdateServerState(DeltaTime);
	}

	if (!PendingTopple.IsZero() && --PendingToppleFrames < 0)
	{
		if (bIsRagdoll)
		{
			ApplyTopple(PendingTopple);
		}
		PendingTopple = FVector::ZeroVector;
	}
	UpdateGetUp(DeltaTime);
	UpdatePelvisLean(DeltaTime);
	UpdateMotors(DeltaTime, false);
	UpdateLegBlend(DeltaTime);

	if (!bIsRagdoll && BodyControl)
	{
		UpdateBodyControl(DeltaTime);
	}
	else if (!bIsRagdoll)
	{
		if (bActivePelvis && CVarAedirActivePelvis.GetValueOnGameThread() != 0)
		{
			HoldPelvis(DeltaTime);
		}
		else
		{
			DrivePelvis();
		}
	}
	LastVelocity = CharacterOwner->GetVelocity();
}

float UAedirBalanceComponent::GetUprightStrength() const
{
	const UAedirStatsComponent* Stats = FindStats();
	if (bIsRagdoll || (Stats && (Stats->IsDead() || Stats->IsDying())))
	{
		return 0.f;
	}
	const float Stamina = Stats ? FMath::Clamp(Stats->GetStaminaFraction(), 0.f, 1.f) : 1.f;
	const float Health = Stats ? FMath::Clamp(Stats->GetHealth() / FMath::Max(Stats->GetMaxHealth(), 1.f), 0.f, 1.f) : 1.f;
	return FMath::Lerp(UprightExhaustedShare, 1.f, Stamina) * FMath::Lerp(UprightDyingShare, 1.f, Health)
		* (1.f - 0.5f * GetDazeFactor()) * FMath::Lerp(1.f, GetUpMuscleShare, GetGetUpShare());
}

void UAedirBalanceComponent::HoldPelvis(float DeltaTime)
{
	FBodyInstance* Pelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	const USkeletalMesh* Model = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 PelvisIndex = Model ? Model->GetRefSkeleton().FindBoneIndex(PelvisBoneName) : INDEX_NONE;
	if (!Pelvis || PelvisIndex == INDEX_NONE || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Where the hips belong: on the legs under the capsule, as the pose of the steps moves them, and lower while getting up.
	const FTransform Actor = CharacterOwner->GetActorTransform();
	const FTransform RestFrame = MeshRestRelative * BodyCapsule->GetComponentTransform();
	const FTransform RefPelvis = FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), PelvisIndex);
	FAedirGaitState Pose;
	if (const UAedirGaitComponent* Gait = CharacterOwner->FindComponentByClass<UAedirGaitComponent>())
	{
		Pose = Gait->GetGaitState();
	}
	const float Strength = GetUprightStrength();
	// A weak body cannot hold itself all the way up: the knees give and the hips sink.
	const bool bGettingUp = IsGettingUp();
	// Just off the ground the pose has not caught up with getting up yet, and would pull the hips up to standing: they stay
	// where they lay until it has.
	if (bGettingUp && !Pose.bProceduralActive)
	{
		bHipsInitialized = false;
		return;
	}
	const FVector TargetLocation = RestFrame.TransformPosition(RefPelvis.GetLocation())
		+ Actor.TransformVectorNoScale(FVector(Pose.HipForwardOffset, Pose.HipLateralOffset, Pose.HipOffsetZ - (bGettingUp ? 0.f : (1.f - Strength) * PelvisWeakSagCm)));
	FRotator PoseTurn = Pose.PelvisRotation;
	PoseTurn.Roll += Pose.PelvisRollDegrees;
	const FQuat ActorQuat = Actor.GetRotation();
	const FQuat TargetRotation = ActorQuat * PoseTurn.Quaternion() * ActorQuat.Inverse() * RestFrame.GetRotation() * RefPelvis.GetRotation();

	// The hips are a mass carried on the legs: a spring pulls them to where they belong, as stiff as the body is strong and
	// never harder than the legs can push, and blows knock them off it. They move with their own speed, so they lag into a
	// start, carry on over a stop and swing back after a shove.
	// A body moved at once, by a teleport or a respawn, takes its hips along instead of being torn from them.
	if (!bHipsInitialized || FVector::DistSquared(HipsLocation, TargetLocation) > FMath::Square(HipsTeleportCm))
	{
		HipsLocation = TargetLocation;
		HipsVelocity = CharacterOwner->GetVelocity();
		BodyMassKg = FMath::Max(BodyMesh->GetMass(), 1.f);
		bHipsInitialized = true;
	}
	// Getting up off the ground the hips are carried through the moves; the joint takes them over once the body is up.
	if (bHipJointActive && bGettingUp && Pelvis->IsInstanceSimulatingPhysics())
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}
	// Physical hips: the joint pulls them there as hard as the body is strong, and everything else hangs on them.
	if (bHipJointActive && !bGettingUp)
	{
		if (!Pelvis->IsInstanceSimulatingPhysics())
		{
			Pelvis->SetInstanceSimulatePhysics(true);
			Pelvis->SetLinearVelocity(CharacterOwner->GetVelocity(), false);
			// A body that changes between moved and simulated has to be tied to the joint again.
			if (HipJoint)
			{
				HipJoint->BreakConstraint();
				HipJoint->SetConstrainedComponents(BodyCapsule, NAME_None, BodyMesh, PelvisBoneName);
			}
		}
		const FVector Now = Pelvis->GetUnrealWorldTransform().GetLocation();
		// Moved at once, by a teleport or a respawn, the whole body goes along.
		if (FVector::DistSquared(Now, TargetLocation) > FMath::Square(HipsTeleportCm))
		{
			const FVector Shift = TargetLocation - Now;
			for (FBodyInstance* Body : BodyMesh->Bodies)
			{
				if (Body)
				{
					// A body's transform carries the body's size as its scale; the physics takes poses without one.
					FTransform Moved = Body->GetUnrealWorldTransform();
					Moved.SetScale3D(FVector::OneVector);
					Moved.AddToTranslation(Shift);
					Body->SetBodyTransform(Moved, ETeleportType::TeleportPhysics);
					Body->SetLinearVelocity(CharacterOwner->GetVelocity(), false);
				}
			}
		}
		DriveHipJoint(TargetLocation, TargetRotation, FMath::Max(Strength, PelvisMinHoldShare), DeltaTime);
		const FVector Drift(Now.X - TargetLocation.X, Now.Y - TargetLocation.Y, 0.f);
		if (IsAuthoritative() && !bGettingUp)
		{
			const float Beyond = static_cast<float>(Drift.Size()) - PelvisDriftFreeCm;
			if (Beyond > 0.f)
			{
				ApplyBalanceImpact(Drift.GetSafeNormal(), Beyond / PelvisDriftLossCm * DeltaTime * (1.5f - Strength));
			}
		}
		return;
	}

	// Getting up, the hands, knees and feet on the ground carry the hips along the way up: they are where the body puts them.
	if (bGettingUp)
	{
		HipsVelocity = (TargetLocation - HipsLocation) / DeltaTime;
		HipsLocation = TargetLocation;
		Pelvis->SetBodyTransform(FTransform(TargetRotation, HipsLocation), ETeleportType::None);
		return;
	}
	const float Hold = FMath::Max(Strength, PelvisMinHoldShare);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.008f), 1, 8);
	const float Step = DeltaTime / Steps;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const FVector Accel = ((TargetLocation - HipsLocation) * FMath::Square(PelvisHoldFrequency * Hold)
			- (HipsVelocity - CharacterOwner->GetVelocity()) * (2.f * PelvisHoldDamping * PelvisHoldFrequency * Hold)).GetClampedToMaxSize(PelvisMaxAccel * Hold);
		HipsVelocity += Accel * Step;
		HipsLocation += HipsVelocity * Step;
	}
	const FVector Offset = HipsLocation - TargetLocation;

	// Hips pushed off their legs tip the body over them, and cost balance the more the weaker the body: a blow a fresh man
	// takes in his stride puts a spent one on the ground.
	const FVector Drift(Offset.X, Offset.Y, 0.f);
	const FVector Tip = FVector::CrossProduct(FVector::UpVector, Drift.GetSafeNormal());
	const float TipRadians = FMath::Atan2(static_cast<float>(Drift.Size()), PelvisTipLeverCm);
	const FQuat Rotation = FQuat(Tip.IsNearlyZero() ? FVector::UpVector : Tip, TipRadians) * TargetRotation;
	Pelvis->SetBodyTransform(FTransform(Rotation, HipsLocation), ETeleportType::None);
	if (IsAuthoritative())
	{
		const float Beyond = static_cast<float>(Drift.Size()) - PelvisDriftFreeCm;
		if (Beyond > 0.f)
		{
			ApplyBalanceImpact(Drift.GetSafeNormal(), Beyond / PelvisDriftLossCm * DeltaTime * (1.5f - Strength));
		}
	}
}

bool UAedirBalanceComponent::UsesBodyControl() const
{
	const int32 Forced = CVarAedirBodyControl.GetValueOnGameThread();
	return Forced < 0 ? bUseBodyControl : Forced != 0;
}

void UAedirBalanceComponent::UpdateBodyControl(float DeltaTime)
{
	if (!BodyControl || !BodyControl->IsReady())
	{
		return;
	}
	// Strength from stamina, health, a daze and getting up; each arm loses what its wounds take.
	const AAedirCreature* Creature = Cast<AAedirCreature>(CharacterOwner);
	const UAedirCombatComponent* Combat = Creature ? Creature->Combat.Get() : nullptr;
	// A broken arm hangs limp.
	const auto ArmShare = [Creature, Combat](EAedirLimb Limb)
	{
		if (Creature && Creature->Wounds && Creature->Wounds->IsLimbBroken(Limb))
		{
			return AedirBrokenArmShare;
		}
		return 1.f - 0.75f * (Combat ? Combat->GetLimbInjury(Limb) : 0.f);
	};
	const float LeftArm = ArmShare(EAedirLimb::LeftArm);
	const float RightArm = ArmShare(EAedirLimb::RightArm);
	// What a hand holds slows its arm as a muscle of fixed force would be slowed by the extra mass: an arm of a few kilos
	// swings a light blade almost as fast as nothing, a heavy one much more slowly; strength makes the load lighter, and one
	// thing held in both hands is shared between them.
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	const float StrengthScale = Creature && Creature->Stats ? AedirArmLoadStrengthRef / FMath::Max(Creature->Stats->Strength, 1.f) : 1.f;
	const auto LoadShare = [Interaction, StrengthScale](EAedirHand Hand)
	{
		const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
		if (!Item)
		{
			return 1.f;
		}
		const bool bShared = Interaction->GetHeldItem(EAedirHand::Left) == Interaction->GetHeldItem(EAedirHand::Right);
		const float Kilos = FMath::Max(Item->Weight, 0.f) * (bShared ? 0.5f : 1.f) * StrengthScale;
		return FMath::Sqrt(AedirArmKilos / (AedirArmKilos + Kilos));
	};
	BodyControl->SetArmLoads(LoadShare(EAedirHand::Left), LoadShare(EAedirHand::Right));
	{
		bool bKickLeft = false;
		FVector KickFoot = FVector::ZeroVector;
		float KickAlpha = 0.f;
		const bool bKicking = Combat && Combat->IsKicking() && Combat->GetKickPose(bKickLeft, KickFoot, KickAlpha);
		BodyControl->SetKickLeg(bKicking ? (bKickLeft ? 0 : 1) : -1);
	}
	const float Upright = GetUprightStrength();
	// A crouch remakes the capsule's physical body at its new size: the hips are tied to the new one.
	const float CapsuleHalfHeight = BodyCapsule ? BodyCapsule->GetUnscaledCapsuleHalfHeight() : 0.f;
	if (!FMath::IsNearlyEqual(CapsuleHalfHeight, BoundCapsuleHalfHeight))
	{
		BoundCapsuleHalfHeight = CapsuleHalfHeight;
		BodyControl->RebindHips(BodyCapsule);
	}
	BodyControl->SetStrengths(Upright, LeftArm, RightArm);
	const bool bGettingUp = IsGettingUp();
	BodyControl->SetRising(bGettingUp);

	// A foot the steps have put down stays there; one in the air, or both while jumping or falling, swings free.
	FAedirGaitState Pose;
	if (const UAedirGaitComponent* Gait = CharacterOwner->FindComponentByClass<UAedirGaitComponent>())
	{
		Pose = Gait->GetGaitState();
	}
	const UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	const bool bOnGround = Pose.bProceduralActive && Movement && !Movement->IsFalling();
	BodyControl->SetAirborne(Movement && Movement->IsFalling());
	BodyControl->SetFeetPlanted(bOnGround && Pose.LeftFootLift < AedirPlantedFootLift, bOnGround && Pose.RightFootLift < AedirPlantedFootLift);

	// Hips pushed off where the legs would carry them tip the body over them and cost balance, the more the weaker it is.
	const USkeletalMesh* Model = BodyMesh->GetSkeletalMeshAsset();
	const int32 PelvisIndex = Model ? Model->GetRefSkeleton().FindBoneIndex(PelvisBoneName) : INDEX_NONE;
	const FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);
	if (!Pelvis || PelvisIndex == INDEX_NONE || !IsAuthoritative())
	{
		return;
	}
	const FTransform Actor = CharacterOwner->GetActorTransform();
	// Where the pose has the hips this frame, crouched, reaching, leaning or on the way up as it is.
	const FVector Target = BodyControl->GetPoseLocation(PelvisBoneName);
	const FVector Now = Pelvis->GetUnrealWorldTransform().GetLocation();
	// Moved at once, by a teleport or a respawn, the whole body goes along instead of being torn after the capsule. So it
	// does after a frame longer than the physics can step through: the capsule went the whole way, the body could not.
	// A body on its way up lifts itself from where it lay and is never put anywhere.
	// The physics reports a move only a step later, so right after one the body is left to arrive before it is judged.
	BodyTeleportGrace = FMath::Max(BodyTeleportGrace - DeltaTime, 0.f);
	if (BodyTeleportGrace > 0.f)
	{
		return;
	}
	// A body on its way up is not carried over after a long frame, but neither is the frame held against it: the physics
	// could not keep up with the moves, the body did not lose its balance.
	if (bGettingUp && DeltaTime > AedirBodyHitchSeconds)
	{
		BodyTeleportGrace = AedirBodyTeleportGraceSeconds * 3.f;
		return;
	}
	if (!bGettingUp && (FVector::DistSquared(Now, Target) > FMath::Square(BodyTeleportCm) || DeltaTime > AedirBodyHitchSeconds))
	{
		BodyTeleportGrace = AedirBodyTeleportGraceSeconds;
		UE_LOG(LogAedir, Verbose, TEXT("[Fall] %s's body put back in its pose: hips %.0f cm off, frame %.3f s."), *GetNameSafe(GetOwner()), FVector::Dist(Now, Target), DeltaTime);
		BodyControl->Teleported(Target - Now);
		return;
	}
	// Getting up, the hips go over the knees and hands and may wander more before they tip the body back over.
	// Getting up, hips left low count too: a body that cannot lift itself as fast as the moves go sinks back.
	const FVector Drift(Now.X - Target.X, Now.Y - Target.Y, bGettingUp ? FMath::Min(Now.Z - Target.Z, 0.0) : 0.0);
	const float Beyond = static_cast<float>(Drift.Size()) - PelvisDriftFreeCm * (bGettingUp ? AedirGetUpDriftFreeScale : 1.f);
	if (Beyond > 0.f)
	{
		UE_LOG(LogAedir, Verbose, TEXT("[Fall] %s hips drift %s cm, balance %.2f, at %s speed %.0f, hips Z %.1f over target."), *GetNameSafe(GetOwner()), *(Actor.InverseTransformVectorNoScale(Drift)).ToCompactString(), Balance,
			*CharacterOwner->GetActorLocation().ToCompactString(), CharacterOwner->GetVelocity().Size(), Now.Z - Target.Z);
		ApplyBalanceImpact(Drift.GetSafeNormal(), Beyond / PelvisDriftLossCm * DeltaTime * (1.5f - Upright));
	}
}

bool UAedirBalanceComponent::UsesPhysicalHips() const
{
	const int32 Forced = CVarAedirPhysicalHips.GetValueOnGameThread();
	return bActivePelvis && (Forced < 0 ? bPhysicalHips : Forced != 0);
}

void UAedirBalanceComponent::SetupHipJoint()
{
	const USkeletalMesh* Model = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 PelvisIndex = Model ? Model->GetRefSkeleton().FindBoneIndex(PelvisBoneName) : INDEX_NONE;
	FBodyInstance* Pelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	if (PelvisIndex == INDEX_NONE || !Pelvis || !BodyCapsule)
	{
		return;
	}

	// Where the hips stand on the legs, in the frame of the capsule: the joint's rest, which every pull is measured from.
	const FTransform RefPelvis = FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), PelvisIndex);
	const FTransform Capsule = BodyCapsule->GetComponentTransform();
	const FTransform RestWorld = RefPelvis * MeshRestRelative * Capsule;
	HipJointFrame = FTransform(Capsule.GetRotation().Inverse() * RestWorld.GetRotation(), Capsule.InverseTransformPositionNoScale(RestWorld.GetLocation()));

	if (!HipJoint)
	{
		HipJoint = NewObject<UPhysicsConstraintComponent>(CharacterOwner, TEXT("AedirHipJoint"));
		HipJoint->SetupAttachment(BodyCapsule);
		HipJoint->RegisterComponent();
	}
	else
	{
		HipJoint->BreakConstraint();
	}

	// Free in every direction: only its motors hold the hips, so a blow, a weight or a stumble moves them.
	FConstraintInstance& Joint = HipJoint->ConstraintInstance;
	Joint.SetLinearXLimit(LCM_Free, 0.f);
	Joint.SetLinearYLimit(LCM_Free, 0.f);
	Joint.SetLinearZLimit(LCM_Free, 0.f);
	Joint.SetAngularSwing1Limit(ACM_Free, 0.f);
	Joint.SetAngularSwing2Limit(ACM_Free, 0.f);
	Joint.SetAngularTwistLimit(ACM_Free, 0.f);
	Joint.SetDisableCollision(true);
	Joint.SetRefFrame(EConstraintFrame::Frame1, HipJointFrame);
	Joint.SetRefFrame(EConstraintFrame::Frame2, FTransform::Identity);
	Joint.SetLinearPositionDrive(true, true, true);
	Joint.SetLinearVelocityDrive(true, true, true);
	Joint.SetAngularDriveMode(EAngularDriveMode::SLERP);
	Joint.SetOrientationDriveSLERP(true);
	Joint.SetAngularVelocityDriveSLERP(true);
	Joint.SetLinearDriveParams(0.f, 0.f, 0.f);
	Joint.SetAngularDriveParams(0.f, 0.f, 0.f);

	Pelvis->SetInstanceSimulatePhysics(true);
	HipJoint->SetConstrainedComponents(BodyCapsule, NAME_None, BodyMesh, PelvisBoneName);
	bHipJointActive = true;
	UE_LOG(LogAedir, Log, TEXT("%s: hips tied to the capsule %s below its middle."), *GetNameSafe(GetOwner()), *HipJointFrame.GetLocation().ToCompactString());
}

void UAedirBalanceComponent::DriveHipJoint(const FVector& TargetLocation, const FQuat& TargetRotation, float Strength, float DeltaTime)
{
	if (!HipJoint || !BodyCapsule)
	{
		return;
	}
	FConstraintInstance& Joint = HipJoint->ConstraintInstance;
	// The legs push as hard as the body is strong; the damping goes with the square root, so a weak body is as steady.
	const float Hold = FMath::Max(Strength, 0.f);
	Joint.SetLinearDriveParams(HipJointLinearStiffness * Hold, HipJointLinearDamping * FMath::Sqrt(Hold), 0.f);
	Joint.SetAngularDriveParams(HipJointAngularStiffness * Hold, HipJointAngularDamping * FMath::Sqrt(Hold), 0.f);

	const FTransform Capsule = BodyCapsule->GetComponentTransform();
	const FVector FrameLocation = Capsule.TransformPositionNoScale(HipJointFrame.GetLocation());
	const FQuat FrameRotation = Capsule.GetRotation() * HipJointFrame.GetRotation();
	Joint.SetLinearPositionTarget(FrameRotation.UnrotateVector(TargetLocation - FrameLocation));
	Joint.SetLinearVelocityTarget(CVarAedirHipVelocityTarget.GetValueOnGameThread() != 0 ? FrameRotation.UnrotateVector(CharacterOwner->GetVelocity()) : FVector::ZeroVector);
	Joint.SetAngularOrientationTarget(FrameRotation.Inverse() * TargetRotation);
}

void UAedirBalanceComponent::PushHips(const FVector& Impulse)
{
	// A blow on the body moves the hips by what it carries over the body's own weight.
	if (bHipsInitialized && !bIsRagdoll)
	{
		HipsVelocity += FVector(Impulse.X, Impulse.Y, 0.f) / FMath::Max(BodyMassKg, 1.f);
	}
}

void UAedirBalanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirBalanceComponent, Balance);
	DOREPLIFETIME(UAedirBalanceComponent, LeanDirection);
	DOREPLIFETIME(UAedirBalanceComponent, bIsRagdoll);
	DOREPLIFETIME(UAedirBalanceComponent, StaggerSeconds);
	DOREPLIFETIME(UAedirBalanceComponent, DazeSeconds);
	DOREPLIFETIME(UAedirBalanceComponent, GetUpStyle);
	DOREPLIFETIME(UAedirBalanceComponent, LyingStyle);
	DOREPLIFETIME(UAedirBalanceComponent, bReadyToGetUp);
}

EAedirGetUpStyle UAedirBalanceComponent::ReadLyingStyle() const
{
	const USkeletalMesh* Model = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 ChestIndex = Model ? Model->GetRefSkeleton().FindBoneIndex(ChestBoneName) : INDEX_NONE;
	if (ChestIndex == INDEX_NONE)
	{
		return EAedirGetUpStyle::FromBack;
	}
	// Which way the chest faces, from how the chest bone is turned now against how it is turned in a body standing upright.
	const FTransform RefChest = FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), ChestIndex);
	const FQuat StandingChest = MeshRestRelative.GetRotation() * RefChest.GetRotation();
	const FVector BellyInChest = StandingChest.UnrotateVector(FVector::ForwardVector);
	const FVector Belly = BodyMesh->GetBoneQuaternion(ChestBoneName, EBoneSpaces::WorldSpace).RotateVector(BellyInChest);
	return Belly.Z < 0.0 ? EAedirGetUpStyle::FromFront : EAedirGetUpStyle::FromBack;
}

void UAedirBalanceComponent::RequestGetUp(bool bForwardKey)
{
	// Down, the key starts getting up; on the way up, holding it keeps the body going.
	if (!bIsRagdoll && !IsGettingUp())
	{
		return;
	}
	if (IsAuthoritative())
	{
		ServerRequestGetUp_Implementation(bForwardKey);
		return;
	}
	// Held down, the key would be sent every frame; a few times a second is enough to keep the request alive.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastGetUpRequestSent >= 0.1)
	{
		LastGetUpRequestSent = Now;
		ServerRequestGetUp(bForwardKey);
	}
}

void UAedirBalanceComponent::ServerRequestGetUp_Implementation(bool bForwardKey)
{
	const UWorld* World = GetWorld();
	GetUpRequestTime = World ? World->GetTimeSeconds() : 0.0;
	RequestedGetUpStyle = bForwardKey ? EAedirGetUpStyle::FromBack : EAedirGetUpStyle::FromFront;
	UE_LOG(LogAedir, Verbose, TEXT("[GetUp] %s asked to get up %s, lies %s, ready %d, balance %.2f."), *GetNameSafe(GetOwner()),
		bForwardKey ? TEXT("off its back") : TEXT("from face down"), LyingStyle == EAedirGetUpStyle::FromFront ? TEXT("face down") : TEXT("on its back"),
		bReadyToGetUp ? 1 : 0, Balance);
}

bool UAedirBalanceComponent::GetGetUpStartHips(FVector& OutOffset, FQuat& OutTurn) const
{
	const USkeletalMesh* Model = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const int32 PelvisIndex = Model ? Model->GetRefSkeleton().FindBoneIndex(PelvisBoneName) : INDEX_NONE;
	if (!IsGettingUp() || !CharacterOwner || !BodyCapsule || PelvisIndex == INDEX_NONE)
	{
		return false;
	}
	// The inverse of how the held hips are placed from the pose.
	const FTransform Actor = CharacterOwner->GetActorTransform();
	const FTransform RestFrame = MeshRestRelative * BodyCapsule->GetComponentTransform();
	const FTransform RefPelvis = FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), PelvisIndex);
	OutOffset = Actor.InverseTransformVectorNoScale(GetUpStartPelvis.GetLocation() - RestFrame.TransformPosition(RefPelvis.GetLocation()));
	const FQuat ActorQuat = Actor.GetRotation();
	OutTurn = (ActorQuat.Inverse() * GetUpStartPelvis.GetRotation() * (RestFrame.GetRotation() * RefPelvis.GetRotation()).Inverse() * ActorQuat).GetNormalized();
	return true;
}

float UAedirBalanceComponent::GetPelvisStandHeight() const
{
	return PelvisRestHeight * (BodyMesh ? static_cast<float>(BodyMesh->GetComponentScale().Z) : 1.f);
}

void UAedirBalanceComponent::ApplyBalanceImpact(FVector WorldDirection, float Magnitude)
{
	if (!IsAuthoritative() || Magnitude <= 0.f)
	{
		return;
	}

	// Half up off the ground, on hands and knees or one foot, the body has no footing to take a blow on.
	if (IsGettingUp())
	{
		Magnitude *= AedirGetUpBlowScale;
	}
	AddLeanDirection(WorldDirection, Magnitude);

	StumbleDebt += FMath::Max(Magnitude - Balance, 0.f);
	SetBalance(Balance - Magnitude);
}

void UAedirBalanceComponent::OnRecoveryStepTaken()
{
	if (!IsAuthoritative() || bIsRagdoll)
	{
		return;
	}
	// The foot came down under the body: some of the balance is back, and the lean it was driven into eases.
	const UAedirStatsComponent* StatsComponent = FindStats();
	const float Stamina = StatsComponent ? StatsComponent->GetStaminaFraction() : 1.f;
	const float Gain = AedirStepCatchGain * FMath::Lerp(AedirStepCatchTiredShare, 1.f, Stamina);
	StumbleDebt = FMath::Max(StumbleDebt - Gain, 0.f);
	SetBalance(Balance + Gain);
	LeanDirection *= 0.6f;
	TimeBelowThreshold = FMath::Max(TimeBelowThreshold - 0.35f, 0.f);
}

void UAedirBalanceComponent::ApplyImpulseImpact(FVector WorldImpulse)
{
	ApplyBalanceImpact(WorldImpulse.GetSafeNormal(), static_cast<float>(WorldImpulse.Size()) * ImpactBalanceScale);
}

void UAedirBalanceComponent::HandleLanded(float LandingSpeed)
{
	if (!IsAuthoritative())
	{
		return;
	}

	// The protective reflex softens the landing. The alpha is read once and cleared for the next fall.
	const float ReflexAlpha = FMath::Clamp(LastProtectiveReflexAlpha, 0.f, 1.f);
	LastProtectiveReflexAlpha = 0.f;

	if (LandingSpeed > FallSpeedBalanceThreshold)
	{
		const float Loss = FMath::Clamp((LandingSpeed - FallSpeedBalanceThreshold) / FallSpeedBalanceThreshold, 0.f, 1.f);
		ApplyBalanceImpact(GetOwner()->GetActorForwardVector(), Loss);
	}

	if (UAedirStatsComponent* StatsComponent = FindStats())
	{
		// A trained body absorbs more of a fall: the safe speed rises with athletism, an untrained one crumples at about 1.4 m.
		const float Athletism = StatsComponent->Athletism;
		const float SafeSpeed = FMath::Lerp(AedirUntrainedSafeLandingSpeed, AedirSafeLandingSpeed, FMath::Clamp(Athletism / AedirAthletismRecoveryScale, 0.f, 1.f));
		if (LandingSpeed > SafeSpeed)
		{
			const float ReflexScale = 1.f - ComBalanceReflexFallDamageReduction * ReflexAlpha;
			StatsComponent->ApplyDamage((LandingSpeed - SafeSpeed) * FallDamagePerSpeed * (1.f - Athletism / AedirAthletismDamageScale) * ReflexScale);
			// Past the safe speed the legs give way and the body slams into the ground.
			const float CrumpleLoss = FMath::Clamp((LandingSpeed - SafeSpeed) / (SafeSpeed * 0.3f), 0.5f, 1.f);
			ApplyBalanceImpact(GetOwner()->GetActorForwardVector(), CrumpleLoss);
			if (LandingSpeed > SafeSpeed * AedirCrashLandingShare && !bIsRagdoll)
			{
				EnterRagdoll(MinRagdollSeconds);
			}
		}
	}
}

void UAedirBalanceComponent::ApplyHitReaction(const FAedirImpactInfo& Impact)
{
	if (!IsAuthoritative() || !CharacterOwner)
	{
		return;
	}

	const UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : HitReactionDefaultAthletism;
	const float StaminaFraction = StatsComponent ? StatsComponent->GetStaminaFraction() : 1.f;

	const float Loss = ComputeBalanceLoss(Impact.Zone, Impact.EnergyJoules, Athletism, StaminaFraction);
	ApplyBalanceImpact(Impact.Direction, Loss);

	if (Impact.Bone != NAME_None && BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		const float ImpulseSize = FMath::Min(Impact.MassKg * Impact.RelativeSpeed * HitReactionBoneImpulseScale, HitReactionBoneImpulseMax);
		MulticastBoneImpulse(Impact.Bone, Impact.Direction * (ImpulseSize * (Impact.Zone == EAedirBodyZone::Head ? HitReactionHeadShare : 1.f)), Impact.Location);
		// A blow to the head snaps it aside and takes the neck and the top of the chest along: the head is light and held
		// by little, so the same blow moves it much further than it would a limb.
		if (Impact.Zone == EAedirBodyZone::Head && BodyMesh->GetBodyInstance(TEXT("spine_05")))
		{
			MulticastBoneImpulse(TEXT("spine_05"), Impact.Direction * (ImpulseSize * HitReactionHeadChestShare), Impact.Location);
		}
		if (BodyMesh->GetBodyInstance(TEXT("spine_03")) && Impact.Bone != TEXT("spine_03"))
		{
			MulticastBoneImpulse(TEXT("spine_03"), Impact.Direction * (ImpulseSize * HitReactionTrunkShare), Impact.Location);
		}
	}

	// The head is knocked the way the blow went.
	if (APlayerController* Viewer = Cast<APlayerController>(CharacterOwner->GetController()))
	{
		if (AAedirPlayerCharacter* Player = Cast<AAedirPlayerCharacter>(CharacterOwner))
		{
			Player->ShakeViewFromBlow(Impact.EnergyJoules);
		}
		const float Kick = FMath::Min(Impact.EnergyJoules * HitReactionViewDegreesPerJoule, HitReactionViewMaxDegrees);
		const FVector Local = CharacterOwner->GetActorTransform().InverseTransformVectorNoScale(Impact.Direction);
		Viewer->SetControlRotation(Viewer->GetControlRotation() + FRotator(static_cast<float>(Local.Z - Local.X * 0.5) * Kick, static_cast<float>(Local.Y) * Kick, 0.f));
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bKnockedDown = Impact.EnergyJoules >= GetKnockdownEnergy(Impact.Zone);
	bool bStaggered = false;

	if (bKnockedDown)
	{
		if (!bIsRagdoll)
		{
			EnterRagdoll(MinRagdollSeconds + HitReactionKnockdownExtraSeconds);
			// The legs give and the body goes over the way the blow drove it: back from a blow in front, face down from one behind.
			const FVector PlanarDirection = FVector(Impact.Direction.X, Impact.Direction.Y, 0.f).GetSafeNormal();
			if (!PlanarDirection.IsNearlyZero())
			{
				MulticastTopple(PlanarDirection * (ComputeStaggerSpeed(Impact.EnergyJoules) * KnockdownToppleShare));
			}
		}
		else
		{
			const float ElapsedRagdoll = static_cast<float>(Now - RagdollStartTime);
			CurrentRagdollMinSeconds = FMath::Max(CurrentRagdollMinSeconds, ElapsedRagdoll + HitReactionKnockdownExtraSeconds);
		}
	}
	else if (!bIsRagdoll && ShouldStagger(Loss))
	{
		bStaggered = true;
		StaggerSeconds = FMath::Lerp(HitReactionStaggerMinSeconds, HitReactionStaggerMaxSeconds, Loss);

		const FVector PlanarDirection = FVector(Impact.Direction.X, Impact.Direction.Y, 0.f).GetSafeNormal();
		// The feet stay on the ground and are driven back a step; the hips are knocked further and swing back over them.
		UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
		if (!PlanarDirection.IsNearlyZero() && Movement)
		{
			const float Speed = ComputeStaggerSpeed(Impact.EnergyJoules);
			Movement->AddImpulse(PlanarDirection * (Speed * StaggerFeetShare), true);
			if (bHipsInitialized)
			{
				HipsVelocity += PlanarDirection * (Speed * StaggerHipsShare);
			}
			if (FBodyInstance* PhysicalHips = bHipJointActive ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr)
			{
				PhysicalHips->AddImpulse(PlanarDirection * (Speed * StaggerHipsShare), true);
			}
		}
	}

	DazeSeconds = FMath::Max(DazeSeconds, ComputeDazeSeconds(Impact.Zone, Impact.EnergyJoules));

	OnHitReaction.Broadcast(Impact.Zone, Impact.EnergyJoules, bStaggered, bKnockedDown);
}

float UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone Zone, float EnergyJoules, float Athletism, float StaminaFraction)
{
	const float EnergyShare = FMath::Clamp(EnergyJoules / HitReactionZoneEnergyRef(Zone), 0.f, 1.f);
	const float AthleticsFactor = 1.f - HitReactionAthleticsLossScale * FMath::Clamp(Athletism / HitReactionAthleticsCap, 0.f, 1.f);
	const float StaminaFactor = 1.f + HitReactionStaminaLossScale * (1.f - FMath::Clamp(StaminaFraction, 0.f, 1.f));

	return FMath::Clamp(EnergyShare * AthleticsFactor * StaminaFactor, 0.f, 1.f);
}

bool UAedirBalanceComponent::ShouldStagger(float BalanceLoss)
{
	return BalanceLoss >= HitReactionStaggerThreshold;
}

float UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone Zone)
{
	switch (Zone)
	{
	// A blow that lays a man out at once: a club full on the head, a charge into the chest. Lesser blows unbalance him and he
	// goes down only if he cannot catch himself.
	case EAedirBodyZone::Head: return 320.f;
	case EAedirBodyZone::Torso: return 650.f;
	case EAedirBodyZone::Arm: return 1400.f;
	case EAedirBodyZone::Leg: return 300.f;
	default: checkNoEntry(); return 220.f;
	}
}

float UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone Zone, float EnergyJoules)
{
	if (Zone != EAedirBodyZone::Head)
	{
		return 0.f;
	}

	return FMath::Clamp(EnergyJoules / HitReactionDazeEnergyRef, 0.f, 1.f) * HitReactionDazeMaxSeconds;
}

float UAedirBalanceComponent::ComputeStaggerSpeed(float EnergyJoules)
{
	return FMath::Clamp(EnergyJoules * HitReactionStaggerSpeedPerJoule, 0.f, HitReactionStaggerSpeedMax);
}

float UAedirBalanceComponent::GetBalance() const
{
	return Balance;
}

bool UAedirBalanceComponent::IsRagdoll() const
{
	return bIsRagdoll;
}

bool UAedirBalanceComponent::IsStaggering() const
{
	return StaggerSeconds > 0.f;
}

bool UAedirBalanceComponent::IsDazed() const
{
	return DazeSeconds > 0.f;
}

float UAedirBalanceComponent::GetDazeSeconds() const
{
	return DazeSeconds;
}

float UAedirBalanceComponent::GetDazeFactor() const
{
	// Dazed by the last blow, or groggy from senses that have not all come back yet: below two thirds of them the body moves,
	// swings and stands ever worse.
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	const float Senses = Creature && Creature->Stats ? Creature->Stats->GetConsciousnessFraction() : 1.f;
	const float Groggy = FMath::Clamp((AedirGroggyBelow - Senses) / AedirGroggyBelow, 0.f, 1.f) * AedirGroggyMaxDaze;
	return FMath::Max(FMath::Clamp(DazeSeconds / HitReactionDazeMaxSeconds, 0.f, 1.f), Groggy);
}

FRotator UAedirBalanceComponent::GetPelvisLean() const
{
	return CurrentLean;
}

FVector UAedirBalanceComponent::GetCenterOfMassWorld() const
{
	return CenterOfMassWorld;
}

float UAedirBalanceComponent::GetSupportMargin() const
{
	return SupportMargin;
}

bool UAedirBalanceComponent::IsCenterOfMassOutsideSupport() const
{
	return bSupportGrounded && SupportMargin < 0.f;
}

bool UAedirBalanceComponent::GetRecoveryStepRequest(FVector& OutDirectionWorld, float& OutUrgency) const
{
	if (bIsRagdoll || IsGettingUp())
	{
		return false;
	}
	// Knocked off balance by a blow, the body steps the way it was driven to get its feet back under it, whether or not
	// its centre of mass has yet left the feet: that is what keeps it standing.
	if (Balance < AedirStepCatchBelowBalance && !LeanDirection.IsNearlyZero())
	{
		const FVector Local(LeanDirection.X, LeanDirection.Y, 0.f);
		OutDirectionWorld = GetOwner()->GetActorTransform().TransformVectorNoScale(Local).GetSafeNormal2D();
		OutUrgency = FMath::Clamp((AedirStepCatchBelowBalance - Balance) / AedirStepCatchBelowBalance + 0.3f, 0.3f, 1.f);
		return !OutDirectionWorld.IsNearlyZero();
	}
	if (!bSupportGrounded || SupportMargin >= ComBalanceRecoveryMarginCm || ComOffsetDirection.IsNearlyZero())
	{
		return false;
	}

	// A body that is steady and unhurt never reaches with a foot, however the sampled centre of mass sits.
	if (Balance > ComBalanceSteadyBalance && StaggerSeconds <= 0.f)
	{
		return false;
	}

	OutDirectionWorld = ComOffsetDirection;
	OutUrgency = FMath::Clamp(-SupportMargin / ComBalanceFullLossMarginCm, 0.f, 1.f);
	return true;
}

bool UAedirBalanceComponent::GetProtectiveReflex(float& OutAlpha, FVector& OutGroundTargetWorld) const
{
	const bool bActive = ProtectiveAlpha > KINDA_SMALL_NUMBER;
	OutAlpha = bActive ? ProtectiveAlpha : 0.f;
	OutGroundTargetWorld = bActive ? ProtectiveGroundWorld : FVector::ZeroVector;
	return bActive;
}

void UAedirBalanceComponent::ComputeConvexHull2D(const TArray<FVector2D>& Points, TArray<FVector2D>& OutHull)
{
	OutHull.Reset();

	TArray<FVector2D> Sorted = Points;
	Sorted.Sort([](const FVector2D& A, const FVector2D& B)
	{
		return A.X < B.X || (A.X == B.X && A.Y < B.Y);
	});

	int32 UniqueCount = 0;
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		if (UniqueCount == 0 || !Sorted[Index].Equals(Sorted[UniqueCount - 1]))
		{
			Sorted[UniqueCount++] = Sorted[Index];
		}
	}
	Sorted.SetNum(UniqueCount);

	if (Sorted.Num() < 3)
	{
		OutHull = Sorted;
		return;
	}

	// Andrew's monotone chain: lower hull left to right, then upper hull right to left.
	TArray<FVector2D> Chain;
	Chain.Reserve(Sorted.Num() * 2);
	for (const FVector2D& Point : Sorted)
	{
		while (Chain.Num() >= 2 && ComBalanceCross(Chain[Chain.Num() - 2], Chain.Last(), Point) <= 0.0)
		{
			Chain.Pop();
		}
		Chain.Add(Point);
	}

	const int32 LowerCount = Chain.Num() + 1;
	for (int32 Index = Sorted.Num() - 2; Index >= 0; --Index)
	{
		const FVector2D& Point = Sorted[Index];
		while (Chain.Num() >= LowerCount && ComBalanceCross(Chain[Chain.Num() - 2], Chain.Last(), Point) <= 0.0)
		{
			Chain.Pop();
		}
		Chain.Add(Point);
	}

	// The last point repeats the first one.
	Chain.Pop();
	OutHull = Chain;
}

float UAedirBalanceComponent::ComputeSupportMargin(const TArray<FVector2D>& Hull, const FVector2D& Point)
{
	const int32 Count = Hull.Num();
	if (Count == 0)
	{
		return -TNumericLimits<float>::Max();
	}

	if (Count == 1)
	{
		return -static_cast<float>((Point - Hull[0]).Size());
	}

	if (Count == 2)
	{
		return -static_cast<float>(ComBalanceDistanceToSegment(Point, Hull[0], Hull[1]));
	}

	// The hull is counter-clockwise, so a point is inside when it lies left of every edge.
	bool bInside = true;
	double MinEdgeDistance = TNumericLimits<double>::Max();
	double NearestBoundary = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& A = Hull[Index];
		const FVector2D& B = Hull[(Index + 1) % Count];
		const double EdgeLength = (B - A).Size();
		if (EdgeLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const double Side = ComBalanceCross(A, B, Point) / EdgeLength;
		if (Side < 0.0)
		{
			bInside = false;
		}
		MinEdgeDistance = FMath::Min(MinEdgeDistance, Side);
		NearestBoundary = FMath::Min(NearestBoundary, ComBalanceDistanceToSegment(Point, A, B));
	}

	return bInside ? static_cast<float>(MinEdgeDistance) : -static_cast<float>(NearestBoundary);
}

float UAedirBalanceComponent::ComputeComBalanceLoss(float MarginCm, float DeltaSeconds)
{
	if (MarginCm >= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(-MarginCm / ComBalanceFullLossMarginCm, 0.f, 1.f) * ComBalanceMaxLossPerSecond * DeltaSeconds;
}

void UAedirBalanceComponent::AddLeanDirection(const FVector& WorldDirection, float Magnitude)
{
	const FVector LocalDirection = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(WorldDirection);
	const FVector2D PlanarDirection = FVector2D(LocalDirection.X, LocalDirection.Y).GetSafeNormal();
	LeanDirection = (LeanDirection + PlanarDirection * Magnitude).GetClampedToMaxSize(1.f);
}

void UAedirBalanceComponent::SampleCenterOfMass()
{
	bSupportGrounded = false;
	SupportMargin = 0.f;
	ComOffsetDirection = FVector::ZeroVector;

	if (bIsRagdoll || !BodyMesh || !CharacterOwner)
	{
		return;
	}

	// Mass-weighted mean over every body, the pelvis included.
	FVector WeightedSum = FVector::ZeroVector;
	double TotalMass = 0.0;
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		const float Mass = Body ? Body->GetBodyMass() : 0.f;
		if (Mass <= 0.f)
		{
			continue;
		}

		// The animated bone location is used, not the simulated body: legs and spine follow the pose while balanced,
		// and crouching or reaching would otherwise move the simulated centre of mass away from the pose-driven feet.
		const FName BoneName = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
		const FVector BodyLocation = BoneName != NAME_None && BodyMesh->GetBoneIndex(BoneName) != INDEX_NONE
			? BodyMesh->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace)
			: Body->GetCOMPosition();
		WeightedSum += BodyLocation * Mass;
		TotalMass += Mass;
	}

	if (TotalMass <= UE_SMALL_NUMBER)
	{
		return;
	}
	CenterOfMassWorld = WeightedSum / TotalMass;

	TArray<FVector2D> SupportPoints;
	AddFootSupportPoints(FootLeftBone, BallLeftBone, SupportPoints);
	AddFootSupportPoints(FootRightBone, BallRightBone, SupportPoints);
	if (SupportPoints.Num() == 0)
	{
		return;
	}

	TArray<FVector2D> Hull;
	ComputeConvexHull2D(SupportPoints, Hull);

	const FVector2D ComXY(CenterOfMassWorld.X, CenterOfMassWorld.Y);
	SupportMargin = ComputeSupportMargin(Hull, ComXY) + ComBalanceStaticAllowanceCm;
	// Walking is a controlled fall: the swing foot leaves the polygon every step, so moving bodies get a margin allowance.
	const float MoveSpeed = CharacterOwner->GetVelocity().Size2D();
	SupportMargin += FMath::Clamp(MoveSpeed / ComBalanceMovingSpeedRef, 0.f, 1.f) * ComBalanceMovingAllowanceCm;
	// Any ordinary locomotion cannot topple a balanced body, the recovery step is for pushes and stumbles only.
	// A kick is a controlled one-leg stance, so it neither loses balance nor asks for a catching step.
	const UAedirCombatComponent* KickCombat = CharacterOwner->FindComponentByClass<UAedirCombatComponent>();
	const bool bKicking = KickCombat && KickCombat->IsKicking();
	// Bending down for an item shifts the weight over the feet on purpose and is just as controlled.
	const UAedirInteractionComponent* ReachInteraction = CharacterOwner->FindComponentByClass<UAedirInteractionComponent>();
	const bool bBending = ReachInteraction && ReachInteraction->GetPelvisLowerZ() > ComBalanceBendingLowerCm;
	if ((MoveSpeed > 20.f || bKicking || bBending) && StaggerSeconds <= 0.f)
	{
		SupportMargin = FMath::Max(SupportMargin, 0.f);
	}
	bSupportGrounded = true;

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : Hull)
	{
		Centroid += Vertex;
	}
	Centroid /= static_cast<double>(FMath::Max(Hull.Num(), 1));

	const FVector2D Offset = (ComXY - Centroid).GetSafeNormal();
	ComOffsetDirection = FVector(Offset.X, Offset.Y, 0.0);
}

void UAedirBalanceComponent::AddFootSupportPoints(FName FootBone, FName BallBone, TArray<FVector2D>& OutPoints) const
{
	if (BodyMesh->GetBoneIndex(FootBone) == INDEX_NONE || BodyMesh->GetBoneIndex(BallBone) == INDEX_NONE)
	{
		return;
	}

	const FVector Foot = BodyMesh->GetBoneLocation(FootBone, EBoneSpaces::WorldSpace);
	const FVector Ball = BodyMesh->GetBoneLocation(BallBone, EBoneSpaces::WorldSpace);

	FVector GroundPoint;
	if (!TraceGround(Foot + FVector::UpVector * ComBalanceTraceUpCm, Foot - FVector::UpVector * ComBalanceTraceDownCm, GroundPoint))
	{
		return;
	}

	if (FMath::Abs(Foot.Z - GroundPoint.Z) > ComBalanceGroundToleranceCm)
	{
		return;
	}

	// Forward runs from the heel to the ball, flattened onto the ground plane.
	FVector Forward = FVector(Ball.X - Foot.X, Ball.Y - Foot.Y, 0.0).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = GetOwner()->GetActorForwardVector().GetSafeNormal2D();
	}

	const FVector2D Forward2D(Forward.X, Forward.Y);
	const FVector2D Side2D(-Forward.Y, Forward.X);
	const FVector2D FootXY(Foot.X, Foot.Y);
	const FVector2D BallXY(Ball.X, Ball.Y);
	const FVector2D Heel = FootXY - Forward2D * ComBalanceHeelOffsetCm;
	const FVector2D Toe = BallXY + Forward2D * ComBalanceToeOffsetCm;
	const FVector2D HalfWidth = Side2D * ComBalanceFootHalfWidthCm;

	OutPoints.Add(Heel + HalfWidth);
	OutPoints.Add(Heel - HalfWidth);
	OutPoints.Add(Toe + HalfWidth);
	OutPoints.Add(Toe - HalfWidth);
}

void UAedirBalanceComponent::ApplyCenterOfMassLoss(float DeltaTime)
{
	// Getting up, the hands, knees and seat carry the body, not the feet alone.
	if (!bSupportGrounded || bIsRagdoll || IsGettingUp())
	{
		return;
	}

	const float Loss = ComputeComBalanceLoss(SupportMargin, DeltaTime);
	if (Loss <= 0.f)
	{
		return;
	}

	// The body falls towards the side the centre of mass has left the support polygon on.
	AddLeanDirection(ComOffsetDirection, Loss);
	SetBalance(Balance - Loss);
}

void UAedirBalanceComponent::SampleProtectiveReflex()
{
	TargetProtectiveAlpha = 0.f;

	if (!CharacterOwner || !BodyMesh || IsGettingUp())
	{
		return;
	}

	const UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	const FVector Velocity = GetFallVelocity();
	const bool bFastFall = -Velocity.Z > ComBalanceReflexFallSpeed;
	const bool bFallingNow = Movement && Movement->MovementMode == MOVE_Falling && bFastFall;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bRagdollImpact = bIsRagdoll && bFastFall && (Now - RagdollStartTime) < ComBalanceReflexRagdollSeconds;
	if (!bFallingNow && !bRagdollImpact)
	{
		return;
	}

	const FVector Pelvis = BodyMesh->GetBoneLocation(PelvisBoneName, EBoneSpaces::WorldSpace);
	FVector GroundPoint;
	if (!TraceGround(Pelvis, Pelvis - FVector::UpVector * ComBalanceReflexRangeCm, GroundPoint))
	{
		return;
	}

	FVector Heading = Velocity.GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}

	const float Distance = static_cast<float>(Pelvis.Z - GroundPoint.Z);
	TargetProtectiveAlpha = FMath::Clamp(1.f - Distance / ComBalanceReflexRangeCm, 0.f, 1.f);
	ProtectiveGroundWorld = GroundPoint + Heading * ComBalanceReflexForwardCm;
}

void UAedirBalanceComponent::SmoothProtectiveReflex(float DeltaTime)
{
	ProtectiveAlpha = FMath::FInterpTo(ProtectiveAlpha, TargetProtectiveAlpha, DeltaTime, ComBalanceReflexInterpSpeed);
	if (TargetProtectiveAlpha > KINDA_SMALL_NUMBER)
	{
		LastProtectiveReflexAlpha = ProtectiveAlpha;
	}
}

FVector UAedirBalanceComponent::GetFallVelocity()
{
	if (bIsRagdoll && BodyMesh)
	{
		if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
		{
			return Pelvis->GetUnrealWorldVelocity();
		}
	}

	const UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	return Movement ? Movement->Velocity : FVector::ZeroVector;
}

bool UAedirBalanceComponent::TraceGround(const FVector& Start, const FVector& End, FVector& OutHitPoint) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	const FCollisionQueryParams Params(FName(TEXT("AedirBalanceGround")), false, GetOwner());
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}

	OutHitPoint = Hit.ImpactPoint;
	return true;
}

bool UAedirBalanceComponent::IsAuthoritative() const
{
	return GetOwner() != nullptr && GetOwner()->HasAuthority();
}

UAedirStatsComponent* UAedirBalanceComponent::FindStats() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UAedirStatsComponent>() : nullptr;
}

void UAedirBalanceComponent::SetBalance(float NewBalance)
{
	const bool bWasBalanced = Balance >= FallBalanceThreshold;
	Balance = FMath::Clamp(NewBalance, 0.f, 1.f);

	if (bWasBalanced && Balance < FallBalanceThreshold)
	{
		OnBalanceLost.Broadcast();
	}
}

void UAedirBalanceComponent::DecayLeanDirection(float Amount)
{
	const double Length = LeanDirection.Size();
	LeanDirection = Length <= Amount ? FVector2D::ZeroVector : LeanDirection * ((Length - Amount) / Length);
}

void UAedirBalanceComponent::UpdateFallState()
{
	const UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const bool bFalling = Movement->IsFalling();
	if (bFalling)
	{
		LastFallSpeed = static_cast<float>(FMath::Abs(Movement->Velocity.Z));
	}
	else if (bWasFalling && Movement->IsMovingOnGround())
	{
		HandleLanded(LastFallSpeed);
	}

	bWasFalling = bFalling;
}

void UAedirBalanceComponent::UpdateServerState(float DeltaTime)
{
	UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : 0.f;
	// A dying body is down for good: it does not get up again.
	const bool bDead = StatsComponent && (StatsComponent->IsDead() || StatsComponent->IsDying());
	// Knocked senseless, the body drops where it stands and lies limp until it comes round.
	const bool bSenseless = StatsComponent && StatsComponent->IsKnockedOut();

	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	const float MaxLegInjury = Creature && Creature->Combat
		? FMath::Max(Creature->Combat->GetLimbInjury(EAedirLimb::LeftLeg), Creature->Combat->GetLimbInjury(EAedirLimb::RightLeg))
		: 0.f;

	const float DazeRecoveryScale = IsDazed() ? HitReactionDazeRecoveryScale : 1.f;
	const float StaggerRecoveryScale = IsStaggering() ? HitReactionStaggerRecoveryScale : 1.f;
	const float Recovery = RecoveryPerSecond * (1.f + Athletism / AedirAthletismRecoveryScale) * (1.f - AedirLegInjuryRecoveryPenalty * MaxLegInjury) * DazeRecoveryScale * StaggerRecoveryScale * DeltaTime;
	// Once the legs have given way there is no catching it any more, only getting up again afterwards.
	if (bIsRagdoll || bDead || Balance >= FallBalanceThreshold)
	{
		StumbleDebt = 0.f;
		SetBalance(Balance + Recovery);
		DecayLeanDirection(Recovery);
	}
	else
	{
		// Stumbling: the feet step to catch the body. A fresh, trained body wins it back from a light blow; a hard blow or a
		// tired one leaves too much to make up before the legs give way.
		const float StaminaShare = StatsComponent ? StatsComponent->GetStaminaFraction() : 1.f;
		float Catch = Recovery * StumbleCatchShare * FMath::Lerp(StumbleCatchTiredShare, 1.f, StaminaShare);
		const float Paid = FMath::Min(Catch, StumbleDebt);
		StumbleDebt -= Paid;
		Catch -= Paid;
		SetBalance(Balance + Catch);
		StaggerSeconds = FMath::Max(StaggerSeconds, StumbleStaggerSeconds);
	}
	ApplyCenterOfMassLoss(DeltaTime);

	StaggerSeconds = FMath::Max(0.f, StaggerSeconds - DeltaTime);
	DazeSeconds = FMath::Max(0.f, DazeSeconds - DeltaTime);

	UpdateFallState();

	if (Balance < FallBalanceThreshold)
	{
		TimeBelowThreshold += DeltaTime;
	}
	else
	{
		TimeBelowThreshold = 0.f;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// One leg short, a body only ever gets as far as its knees before it goes down again.
	const bool bLegGivesWay = bLegLost && (!IsGettingUp() || GetGetUpProgress() >= LegLostGetUpGivesWayAt);
	if (!bIsRagdoll && (bDead || bSenseless || bLegGivesWay || TimeBelowThreshold >= FallDelaySeconds))
	{
		UE_LOG(LogAedir, Log, TEXT("[Fall] %s goes down: dead %d, senseless %d, leg %d, balance %.2f."), *GetNameSafe(GetOwner()), bDead ? 1 : 0, bSenseless ? 1 : 0, bLegGivesWay ? 1 : 0, Balance);
		EnterRagdoll(MinRagdollSeconds);
		// Nothing is held by a hand that has gone slack.
		if (bSenseless && Creature && Creature->Interaction)
		{
			Creature->Interaction->DropHeld(EAedirHand::Right);
			Creature->Interaction->DropHeld(EAedirHand::Left);
		}
	}
	else if (bIsRagdoll)
	{
		LyingStyle = ReadLyingStyle();
		const bool bWasReady = bReadyToGetUp;
		bReadyToGetUp = !bDead && !bSenseless && Balance >= GetUpBalanceThreshold && Now - RagdollStartTime >= CurrentRagdollMinSeconds;
		if (bReadyToGetUp && !bWasReady && BodyMesh)
		{
			UE_LOG(LogAedir, Verbose, TEXT("[GetUp] %s can get up, lies %s, head %s from the pelvis."), *GetNameSafe(GetOwner()),
				LyingStyle == EAedirGetUpStyle::FromFront ? TEXT("face down") : TEXT("on its back"),
				*(BodyMesh->GetBoneLocation(HeadBoneName) - BodyMesh->GetBoneLocation(PelvisBoneName)).ToCompactString());
		}
		// A player gets up when told to, and only the way the body lies allows; anyone else as soon as it can.
		const bool bChooses = bPlayerChoosesGetUp && CharacterOwner && CharacterOwner->IsPlayerControlled();
		const bool bTold = Now - GetUpRequestTime <= AedirGetUpRequestHoldSeconds && RequestedGetUpStyle == LyingStyle;
		if (bReadyToGetUp && (!bChooses || bTold))
		{
			GetUpStyle = LyingStyle;
			UE_LOG(LogAedir, Log, TEXT("[GetUp] %s gets up %s%s."), *GetNameSafe(GetOwner()),
				GetUpStyle == EAedirGetUpStyle::FromFront ? TEXT("from face down") : TEXT("off its back"), bChooses ? TEXT(" when told") : TEXT(""));
			bReadyToGetUp = false;
			bIsRagdoll = false;
			SetBalance(GetUpBalanceThreshold);
			SetRagdollPresentation(false);
		}
	}
}

void UAedirBalanceComponent::EnterRagdoll(float MinimumSeconds)
{
	const UWorld* World = GetWorld();
	bIsRagdoll = true;
	RagdollStartTime = World ? World->GetTimeSeconds() : 0.0;
	CurrentRagdollMinSeconds = MinimumSeconds;
	SetRagdollPresentation(true);
}

void UAedirBalanceComponent::MulticastTopple_Implementation(FVector TopVelocity)
{
	// The bodies only just let go this frame and would not take a push yet: it is given on the next one.
	PendingTopple = TopVelocity;
	PendingToppleFrames = 1;
}

void UAedirBalanceComponent::ApplyTopple(const FVector& TopVelocity)
{
	if (!BodyMesh || !CharacterOwner)
	{
		return;
	}
	// Tipped over at the feet: the higher a part of the body, the faster it goes, and the feet are kept where they stand, held a
	// little against the push, so the body turns over them instead of sliding off on them.
	const float FeetZ = static_cast<float>(BodyCapsule ? BodyCapsule->GetComponentLocation().Z - BodyCapsule->GetScaledCapsuleHalfHeight() : CharacterOwner->GetActorLocation().Z);
	const float Height = FMath::Max(static_cast<float>(BodyMesh->GetBoneLocation(HeadBoneName).Z) - FeetZ, 1.f);
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body && Body->IsInstanceSimulatingPhysics())
		{
			const float Up = FMath::Clamp((static_cast<float>(Body->GetUnrealWorldTransform().GetLocation().Z) - FeetZ) / Height, 0.f, 1.f);
			const float Share = (Up - AedirToppleFootHoldShare) / (1.f - AedirToppleFootHoldShare);
			Body->AddImpulse(TopVelocity * Share, true);
		}
	}
}

void UAedirBalanceComponent::MulticastBoneImpulse_Implementation(FName Bone, FVector Impulse, FVector Location)
{
	if (BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		BodyMesh->AddImpulseAtLocation(Impulse, Location, Bone);
	}
	PushHips(Impulse);
	Flinch = FMath::Max(Flinch, FMath::Clamp(static_cast<float>(Impulse.Size()) / HitFlinchFullImpulse, 0.f, 1.f));
	MotorUpdateTimer = 0.f;
}

void UAedirBalanceComponent::UpdatePelvisLean(float DeltaTime)
{
	const float Disturbance = FMath::Clamp(1.f - Balance, 0.f, 1.f);

	// Positive pitch raises the nose, moving the top of the pelvis toward -X; positive roll moves it toward +Y.
	// The lean therefore points opposite to the disturbance on both axes.
	const UAedirStatsComponent* Stats = FindStats();
	const float Tired = Stats ? Stats->GetFatigue() : 0.f;
	const float Hurt = Stats ? 1.f - FMath::Clamp(Stats->GetHealth() / FMath::Max(Stats->GetMaxHealth(), 1.f), 0.f, 1.f) : 0.f;
	const float Sway = bIsRagdoll ? 0.f : BalanceSwayDegrees + Tired * BalanceSwayTiredDegrees + Hurt * BalanceSwayHurtDegrees;
	SwayClock += DeltaTime * BalanceSwayRate;
	const double Pitch = LeanDirection.X * Disturbance * MaxLeanDegrees + Sway * FMath::PerlinNoise1D(SwayClock + SwaySeed);
	const double Roll = -LeanDirection.Y * Disturbance * MaxLeanDegrees + Sway * FMath::PerlinNoise1D(SwayClock * 1.3f + SwaySeed + 17.f);

	CurrentLean = FMath::RInterpTo(CurrentLean, FRotator(Pitch, 0.0, Roll), DeltaTime, AedirLeanInterpSpeed);
}

void UAedirBalanceComponent::DrivePelvis()
{
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);
	if (!Pelvis)
	{
		return;
	}

	// Everything hangs from the pelvis, which goes exactly where the pose has it.
	FTransform PelvisPose = BodyMesh->GetSocketTransform(PelvisBoneName, RTS_World);
	PelvisPose.SetScale3D(FVector::OneVector);
	Pelvis->SetBodyTransform(PelvisPose, ETeleportType::None);
}

void UAedirBalanceComponent::SetRagdollPresentation(bool bRagdoll)
{
	ApplyRagdollPhysics(bRagdoll);

	if (bRagdoll)
	{
		OnFell.Broadcast();
	}
	else
	{
		OnGotUp.Broadcast();
	}
}

void UAedirBalanceComponent::ApplyRagdollPhysics(bool bRagdoll)
{
	if (!CharacterOwner || !BodyMesh || !BodyCapsule)
	{
		return;
	}

	UpdateMotors(0.f, true);
	ApplyBodyContacts(bRagdoll);
	ApplyJointLimits(bRagdoll);
	ApplyLegPhysics(bRagdoll);

	UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);

	if (bRagdoll)
	{
		MeshRelativeAtFall = BodyMesh->GetAttachParent() == BodyCapsule ? BodyMesh->GetRelativeTransform() : MeshRestRelative;
		GetUpLeft = 0.f;
		bHipsInitialized = false;
		if (BodyControl)
		{
			BodyControl->SetHipsCarried(false);
			BodyControl->SetRising(false);
			BodyControl->SetLimp(true);
			BodyControl->SetFeetPlanted(false, false);
		}
		// Down, nothing holds the hips up any more.
		if (bHipJointActive && HipJoint)
		{
			HipJoint->ConstraintInstance.SetLinearDriveParams(0.f, 0.f, 0.f);
			HipJoint->ConstraintInstance.SetAngularDriveParams(0.f, 0.f, 0.f);
		}
		// The body goes down with the way it was moving, not up: a blow or a slip never throws it into the air.
		const FVector Carried = CharacterOwner->GetVelocity();
		if (Pelvis)
		{
			Pelvis->SetInstanceSimulatePhysics(true);
		}
		for (FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				FVector Velocity = Body->GetUnrealWorldVelocity();
				Velocity.Z = FMath::Min(Velocity.Z, FMath::Max(Carried.Z, 0.0) + RagdollEntryMaxRiseSpeed);
				Body->SetLinearVelocity(Velocity, false);
			}
		}

		if (Movement)
		{
			Movement->DisableMovement();
		}

		BodyCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	if (Pelvis)
	{
		GetUpStartPelvis = Pelvis->GetUnrealWorldTransform();
		GetUpStartPelvis.SetScale3D(FVector::OneVector);
		const FVector PelvisLocation = GetUpStartPelvis.GetLocation();
		FVector Along = (BodyMesh->GetBoneLocation(HeadBoneName) - PelvisLocation).GetSafeNormal2D();
		if (Along.IsNearlyZero())
		{
			Along = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
		}
		// The hips are carried through getting up, and only then held by the joint again; a fully physical body lifts its own.
		if (!BodyControl)
		{
			Pelvis->SetInstanceSimulatePhysics(false);
		}
		// Moving the capsule and the mesh back onto it drags the limbs along with them; they are put back where they lay after.
		TArray<FTransform> Lying;
		Lying.Reserve(BodyMesh->Bodies.Num());
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			FTransform Lay = Body ? Body->GetUnrealWorldTransform() : FTransform::Identity;
			Lay.SetScale3D(FVector::OneVector);
			Lying.Add(Lay);
		}

		if (IsAuthoritative())
		{
			// Face down the body comes up facing the way its head lay, over its hips. On its back it sits up facing its feet and
			// stands up over them, a little past where the hips lay.
			const bool bFromBack = GetUpStyle == EAedirGetUpStyle::FromBack;
			const FVector Facing = bFromBack ? -Along : Along;
			const FVector StandXY = PelvisLocation + Facing * (bFromBack ? GetUpFromBackStandCm * static_cast<float>(BodyMesh->GetComponentScale().Z) : 0.f);
			FVector Ground;
			if (!TraceGround(StandXY + FVector(0.f, 0.f, 80.f), StandXY - FVector(0.f, 0.f, 200.f), Ground))
			{
				Ground = StandXY - FVector(0.f, 0.f, 10.f);
			}
			const FVector CapsuleLocation = Ground + FVector(0.f, 0.f, BodyCapsule->GetScaledCapsuleHalfHeight() + 1.f);
			CharacterOwner->SetActorLocationAndRotation(CapsuleLocation, FRotator(0.f, Facing.Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		}

		// A loose pelvis took the mesh along with it. Back on its feet the mesh goes back to the capsule, and the limbs to the pose.
		if (BodyControl)
		{
			PutMeshOnCapsule();
		}
		else
		{
			if (BodyMesh->GetAttachParent() != BodyCapsule)
			{
				BodyMesh->AttachToComponent(BodyCapsule, AedirMeshOnCapsuleRules);
			}
			// Physical hips stay where they lay and are pulled up from there; pose-driven ones are put back on the legs at once.
			BodyMesh->SetRelativeTransform(MeshRelativeAtFall, false, nullptr, bActivePelvis ? ETeleportType::None : ETeleportType::ResetPhysics);
			BodyMesh->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::None);
		}
		if (bActivePelvis || BodyControl)
		{
			for (int32 Index = 0; Index < BodyMesh->Bodies.Num() && Index < Lying.Num(); ++Index)
			{
				if (FBodyInstance* Body = BodyMesh->Bodies[Index])
				{
					Body->SetBodyTransform(Lying[Index], ETeleportType::TeleportPhysics);
					if (Body->IsInstanceSimulatingPhysics())
					{
						Body->SetLinearVelocity(FVector::ZeroVector, false);
						Body->SetAngularVelocityInRadians(FVector::ZeroVector, false);
					}
				}
			}
		}

		// The pose takes the body up from the ground the way it lies, slower for a tired or hurt body.
		const UAedirStatsComponent* StatsComponent = FindStats();
		const float Tired = StatsComponent ? StatsComponent->GetFatigue() : 0.f;
		const float Hurt = StatsComponent ? 1.f - FMath::Clamp(StatsComponent->GetHealth() / FMath::Max(StatsComponent->GetMaxHealth(), 1.f), 0.f, 1.f) : 0.f;
		GetUpTotal = GetUpBaseSeconds * (1.f + Tired * GetUpTiredShare + Hurt * GetUpHurtShare);
		GetUpLeft = GetUpTotal;
		bHipsInitialized = false;
		if (UAedirGaitComponent* Gait = CharacterOwner->FindComponentByClass<UAedirGaitComponent>())
		{
			Gait->BeginGetUpPose();
		}
		// The muscles come back and lift the body through the moves, the hips with them; a blow can still put it back down.
		if (BodyControl)
		{
			BodyControl->SetLimp(false);
			BodyControl->SetRising(true);
			// The pose takes a moment to start from where the body lies; until then it is not measured against it.
			BodyTeleportGrace = AedirBodyTeleportGraceSeconds * 2.f;
		}
	}

	BodyCapsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// The capsule's own physical body is made anew with its collision, and the hips are tied to that one.
	if (bHipJointActive && HipJoint)
	{
		HipJoint->BreakConstraint();
		HipJoint->SetConstrainedComponents(BodyCapsule, NAME_None, BodyMesh, PelvisBoneName);
	}
	if (BodyControl)
	{
		BodyControl->RebindHips(BodyCapsule);
	}
	if (Movement)
	{
		if (GetUpLeft > 0.f)
		{
			Movement->DisableMovement();
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
}

void UAedirBalanceComponent::PutMeshOnCapsule()
{
	// Lying, the mesh was let go and carried by the hips, taking their scale on top of its own. It goes back on the capsule
	// exactly as it sat when the body went down, at the size its race and build give it. The engine attaches nothing whose
	// root part simulates, so the hips are held still for the moment it takes, and set loose again without letting go.
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);
	const bool bSimulating = Pelvis && Pelvis->IsInstanceSimulatingPhysics();
	if (bSimulating)
	{
		Pelvis->SetInstanceSimulatePhysics(false, false, true);
	}
	BodyMesh->AttachToComponent(BodyCapsule, AedirMeshOnCapsuleRules);
	BodyMesh->SetRelativeTransform(MeshRelativeAtFall, false, nullptr, ETeleportType::None);
	// The same relative transform is not applied again, while the world one was the hips': it is worked out anew.
	BodyMesh->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::None);
	if (bSimulating)
	{
		Pelvis->SetInstanceSimulatePhysics(true, false, true);
	}
}

float UAedirBalanceComponent::GetGetUpShare() const
{
	return GetUpLeft > 0.f && GetUpTotal > 0.f ? 1.f - FMath::SmoothStep(0.f, 1.f, 1.f - GetUpLeft / GetUpTotal) : 0.f;
}

void UAedirBalanceComponent::UpdateGetUp(float DeltaTime)
{
	if (GetUpLeft <= 0.f)
	{
		return;
	}
	// A player gets up only as long as the key is held: let go half way and the body sinks back to the ground.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bChooses = bPlayerChoosesGetUp && CharacterOwner && CharacterOwner->IsPlayerControlled();
	const bool bHeld = Now - GetUpRequestTime <= AedirGetUpRequestHoldSeconds && RequestedGetUpStyle == GetUpStyle;
	// A long frame is not a let-go: the key could not be read in it.
	if (bChooses && !bHeld && IsAuthoritative() && DeltaTime <= AedirBodyHitchSeconds)
	{
		ApplyBalanceImpact(-CharacterOwner->GetActorForwardVector(), AedirGetUpLetGoLossPerSecond * DeltaTime);
		return;
	}
	GetUpLeft = FMath::Max(GetUpLeft - DeltaTime, 0.f);
	// The moves are over, but the hips never made it up: the body did not get up.
	if (GetUpLeft <= 0.f && BodyControl && IsAuthoritative())
	{
		const FBodyInstance* Pelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
		const float Short = Pelvis ? static_cast<float>(BodyControl->GetPoseLocation(PelvisBoneName).Z - Pelvis->GetUnrealWorldTransform().GetLocation().Z) : 0.f;
		if (Short > PelvisDriftFreeCm * AedirGetUpDriftFreeScale)
		{
			UE_LOG(LogAedir, Log, TEXT("[Fall] %s did not make it up: hips %.0f cm short."), *GetNameSafe(GetOwner()), Short);
			EnterRagdoll(MinRagdollSeconds);
			return;
		}
	}
	if (GetUpLeft <= 0.f)
	{
		if (UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr)
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
		UpdateMotors(0.f, true);
	}
}

void UAedirBalanceComponent::ApplyBodyContacts(bool bRagdoll)
{
	if (!BodyMesh)
	{
		return;
	}

	// Standing, the legs are placed on the ground by the pose, and the floor under the feet would only push them out of it.
	// A skeletal mesh takes its responses from the component, so a body has to be told through its shapes.
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		const UBodySetup* Setup = Body ? Body->GetBodySetup() : nullptr;
		if (!Setup)
		{
			continue;
		}

		const FString Bone = Setup->BoneName.ToString();
		const bool bLeg = Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("foot")) || Bone.Contains(TEXT("ball"));
		for (int32 Shape = 0; Shape < Setup->AggGeom.GetElementCount(); ++Shape)
		{
			Body->SetShapeCollisionEnabled(Shape, bLeg && !bRagdoll ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
		}
	}
}

void UAedirBalanceComponent::ReshapeBodyPhysics()
{
	BodyMesh->RecreatePhysicsState();
	BodyMesh->SetAllBodiesBelowSimulatePhysics(PelvisBoneName, true, true);
	BodyMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	ApplyBodyContacts(false);
	LooseJointLimits.Reset();
	ApplyJointLimits(false);
	ApplyLegPhysics(false);
	if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}
	// New bodies, so the muscles are made on them again, or the hips are tied to the capsule again.
	if (UsesBodyControl())
	{
		if (!BodyControl)
		{
			BodyControl = NewObject<UAedirBodyControlComponent>(CharacterOwner, TEXT("AedirBodyMuscles"));
			BodyControl->RegisterComponent();
		}
		BodyControl->Setup(BodyMesh, BodyCapsule);
		BodyControl->SetLimp(bIsRagdoll);
	}
	else if (UsesPhysicalHips())
	{
		SetupHipJoint();
	}
	// New bodies, so the mask that keeps its own weapons off it again.
	if (AAedirCreature* Creature = Cast<AAedirCreature>(CharacterOwner))
	{
		Creature->ApplyBodyMask();
	}
	if (MotorComponent)
	{
		MotorComponent->SetSkeletalMeshComponent(BodyMesh);
		UpdateMotors(0.f, true);
	}
}

void UAedirBalanceComponent::ApplyLegPhysics(bool bRagdoll)
{
	// The legs stay physical. A body on its feet carries their weight through the pose, so they do not sag out of the steps, and only when it falls does gravity have them.
	if (BodyMesh)
	{
		for (const FName& Thigh : { FName(TEXT("thigh_l")), FName(TEXT("thigh_r")) })
		{
			BodyMesh->ForEachBodyBelow(Thigh, true, false, [bRagdoll](FBodyInstance* Body)
			{
				Body->SetEnableGravity(bRagdoll);
			});
		}
	}
}

void UAedirBalanceComponent::ApplyJointLimits(bool bRagdoll)
{
	if (!BodyMesh)
	{
		return;
	}

	// The joint limits of the physics asset are made for a limp body. The living pose goes past them, and its muscles would fight them.
	if (LooseJointLimits.IsEmpty())
	{
		for (const FConstraintInstance* Joint : BodyMesh->Constraints)
		{
			LooseJointLimits.Add(Joint->ProfileInstance);
		}
	}

	// Standing, the living pose gets more room than the limp one, but a joint still never turns the way it cannot: a
	// shoulder does not go over backwards, a neck does not turn the head round, whatever the muscles or a blow ask of it.
	const auto Living = [bRagdoll](EAngularConstraintMotion Motion, float Degrees, float& OutDegrees)
	{
		OutDegrees = Degrees;
		if (bRagdoll || Motion == ACM_Free)
		{
			return Motion;
		}
		const float Asset = Motion == ACM_Locked ? 0.f : Degrees;
		OutDegrees = FMath::Min(Asset * AedirLivingJointScale + (Motion == ACM_Locked ? AedirLivingLockedExtraDegrees : AedirLivingJointExtraDegrees), AedirLivingJointMaxDegrees);
		return ACM_Limited;
	};
	for (int32 Index = 0; Index < BodyMesh->Constraints.Num() && Index < LooseJointLimits.Num(); ++Index)
	{
		FConstraintInstance* Joint = BodyMesh->Constraints[Index];
		const FConstraintProfileProperties& Loose = LooseJointLimits[Index];
		float Swing1 = 0.f;
		float Swing2 = 0.f;
		float Twist = 0.f;
		const EAngularConstraintMotion Swing1Motion = Living(Loose.ConeLimit.Swing1Motion, Loose.ConeLimit.Swing1LimitDegrees, Swing1);
		const EAngularConstraintMotion Swing2Motion = Living(Loose.ConeLimit.Swing2Motion, Loose.ConeLimit.Swing2LimitDegrees, Swing2);
		const EAngularConstraintMotion TwistMotion = Living(Loose.TwistLimit.TwistMotion, Loose.TwistLimit.TwistLimitDegrees, Twist);
		// A living wrist and forearm turn a held weapon much further than the limp ones of the asset: the weapon's poses ask it.
		static IConsoleVariable* WideArmJoints = IConsoleManager::Get().RegisterConsoleVariable(TEXT("aedir.WideArmJoints"), 1, TEXT("Living wrists and forearms turn further than the limp limits (1) or not (0)."));
		if (!bRagdoll && WideArmJoints->GetInt() != 0)
		{
			const FString Name = Joint->JointName.ToString();
			if (Name.StartsWith(TEXT("hand")))
			{
				Swing1 = FMath::Max(Swing1, AedirLivingWristDegrees);
				Swing2 = FMath::Max(Swing2, AedirLivingWristDegrees);
				Twist = FMath::Max(Twist, AedirLivingWristDegrees);
			}
			else if (Name.StartsWith(TEXT("lowerarm")))
			{
				Swing1 = FMath::Max(Swing1, AedirLivingForearmSwingDegrees);
				Swing2 = FMath::Max(Swing2, AedirLivingForearmSwingDegrees);
				Twist = FMath::Max(Twist, AedirLivingForearmTwistDegrees);
			}
		}
		static IConsoleVariable* FreeWrist = IConsoleManager::Get().RegisterConsoleVariable(TEXT("aedir.DevFreeJoint"), TEXT(""), TEXT("Dev: joints whose name holds this are left free."));
		const FString FreeName = FreeWrist->GetString();
		TArray<FString> FreeNames;
		FreeName.ParseIntoArray(FreeNames, TEXT(","));
		const bool bFree = FreeNames.ContainsByPredicate([Joint](const FString& Name) { return Joint->JointName.ToString().Contains(Name); });
		if (bFree)
		{
			UE_LOG(LogTemp, Log, TEXT("[Joint] %s left free"), *Joint->JointName.ToString());
		}
		Joint->SetAngularSwing1Limit(bFree ? ACM_Free : Swing1Motion, Swing1);
		Joint->SetAngularSwing2Limit(bFree ? ACM_Free : Swing2Motion, Swing2);
		Joint->SetAngularTwistLimit(bFree ? ACM_Free : TwistMotion, Twist);
	}
}

const FAedirBoneMotor& UAedirBalanceComponent::GetBoneMotor(FName BoneName) const
{
	const FString Bone = BoneName.ToString();
	if (Bone.Contains(TEXT("hand")))
	{
		return HandMotor;
	}
	if (Bone.Contains(TEXT("lowerarm")))
	{
		return LowerArmMotor;
	}
	if (Bone.Contains(TEXT("upperarm")) || Bone.Contains(TEXT("clavicle")))
	{
		return UpperArmMotor;
	}
	if (Bone.Contains(TEXT("foot")) || Bone.Contains(TEXT("ball")))
	{
		return FootMotor;
	}
	if (Bone.Contains(TEXT("calf")))
	{
		return CalfMotor;
	}
	if (Bone.Contains(TEXT("thigh")))
	{
		return ThighMotor;
	}
	if (Bone.Contains(TEXT("head")) || Bone.Contains(TEXT("neck")))
	{
		return HeadMotor;
	}
	return SpineMotor;
}

void UAedirBalanceComponent::ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit)
{
	AActor* Owner = GetOwner();
	if (!IsAuthoritative() || OtherActor == Owner)
	{
		return;
	}
	// Running into someone shoves both: each loses balance by how fast they met, and the trunks jolt apart.
	if (AAedirCreature* Other = Cast<AAedirCreature>(OtherActor))
	{
		BumpInto(Other, Hit);
		return;
	}

	// What the body has just let go of is not an impact.
	if (const AAedirItemActor* Released = Cast<AAedirItemActor>(OtherActor); Released && Released->WasJustReleasedBy(Owner))
	{
		return;
	}
	// A weapon in someone's hand hurts only by what its blow does to the body, which the fight works out; brushing the
	// capsule with it is no second blow.
	if (const AAedirItemActor* Item = Cast<AAedirItemActor>(OtherActor); Item && Item->IsHeld())
	{
		return;
	}

	UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : 0.f;
	// The movement has already stopped the body by now; what it hit with is how fast it was going just before.
	const FVector OwnerVelocity = LastVelocity.SizeSquared() > Owner->GetVelocity().SizeSquared() ? LastVelocity : Owner->GetVelocity();

	if (OtherComponent && OtherComponent->IsSimulatingPhysics())
	{
		// Shins and feet are walked into things all the time; objects only hurt at the knee and above.
		const float FeetZ = static_cast<float>(Owner->GetActorLocation().Z) - (BodyCapsule ? BodyCapsule->GetScaledCapsuleHalfHeight() : 0.f);
		const float KneeHeight = ObjectImpactMinHeightCm * static_cast<float>(Owner->GetActorScale3D().Z);
		if (Hit.ImpactPoint.Z - FeetZ < KneeHeight)
		{
			return;
		}

		// Walking into something that lies still is no blow.
		if (OtherComponent->GetComponentVelocity().SizeSquared() < AedirObjectImpactMinSpeed * AedirObjectImpactMinSpeed)
		{
			return;
		}

		const FVector RelativeVelocity = OtherComponent->GetComponentVelocity() - OwnerVelocity;
		const float ImpactScore = OtherComponent->GetMass() * static_cast<float>(RelativeVelocity.Size());

		if (ImpactScore > ObjectImpactMassSpeedThreshold)
		{
			const FVector Direction = (Owner->GetActorLocation() - OtherComponent->GetComponentLocation()).GetSafeNormal();
			ApplyBalanceImpact(Direction, FMath::Clamp(ImpactScore * ImpactBalanceScale * AedirObjectImpactBalanceMultiplier, 0.f, 1.f));

			if (StatsComponent)
			{
				UE_LOG(LogAedir, Log, TEXT("%s was struck by %s, score %.0f, %.1f damage."), *GetNameSafe(Owner), *GetNameSafe(OtherActor), ImpactScore, ImpactScore * AedirObjectImpactDamagePerScore);
				StatsComponent->ApplyDamage(ImpactScore * AedirObjectImpactDamagePerScore);
			}
		}
		return;
	}

	// Upward-facing surfaces are ground contact, which is handled by the landing logic instead.
	if (Hit.ImpactNormal.Z > AedirWalkableNormalZ)
	{
		return;
	}

	const float ApproachSpeed = static_cast<float>(FVector::DotProduct(OwnerVelocity, -Hit.ImpactNormal));
	if (ApproachSpeed <= WallImpactSpeedThreshold)
	{
		return;
	}

	const float Excess = ApproachSpeed - WallImpactSpeedThreshold;
	UAedirSfxSubsystem::Play(this, EAedirSfx::BodyImpact, Hit.ImpactPoint, FMath::Clamp(Excess / WallImpactSpeedThreshold, 0.2f, 1.f));
	ApplyBalanceImpact(OwnerVelocity.GetSafeNormal(), FMath::Clamp(Excess / WallImpactSpeedThreshold, 0.f, AedirWallImpactMaxLoss));
	// The chest and head carry on into the wall and are thrown back off it.
	if (BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		const FVector Rebound = Hit.ImpactNormal * (ApproachSpeed * WallImpactBodyImpulsePerSpeed);
		MulticastBoneImpulse(TEXT("spine_03"), Rebound, Hit.ImpactPoint);
		MulticastBoneImpulse(TEXT("head"), Rebound * 0.4f, Hit.ImpactPoint);
	}

	if (StatsComponent)
	{
		StatsComponent->ApplyDamage(Excess * AedirWallImpactDamagePerSpeed * (1.f - Athletism / AedirAthletismDamageScale));
	}
}

void UAedirBalanceComponent::BumpInto(AAedirCreature* Other, const FHitResult& Hit)
{
	UAedirBalanceComponent* OtherBalance = Other ? Other->Balance.Get() : nullptr;
	const UWorld* World = GetWorld();
	if (!OtherBalance || !World || bIsRagdoll || OtherBalance->IsRagdoll() || World->GetTimeSeconds() - LastBumpTime < BumpRepeatSeconds)
	{
		return;
	}
	const FVector Mine = LastVelocity.SizeSquared() > GetOwner()->GetVelocity().SizeSquared() ? LastVelocity : GetOwner()->GetVelocity();
	const FVector Theirs = Other->GetVelocity();
	const FVector Toward = (Other->GetActorLocation() - GetOwner()->GetActorLocation()).GetSafeNormal2D();
	const float Closing = static_cast<float>(FVector::DotProduct(Mine - Theirs, Toward));
	if (Closing < BumpMinSpeed)
	{
		return;
	}
	LastBumpTime = World->GetTimeSeconds();
	OtherBalance->LastBumpTime = LastBumpTime;

	// Whoever runs in loses less than whoever is run into, as a charge goes.
	const float Excess = Closing - BumpMinSpeed;
	OtherBalance->ApplyBalanceImpact(Toward, FMath::Min(Excess * BumpLossPerSpeed * 1.4f, BumpMaxLoss));
	ApplyBalanceImpact(-Toward, FMath::Min(Excess * BumpLossPerSpeed * 0.6f, BumpMaxLoss));
	MulticastBoneImpulse(TEXT("spine_03"), -Toward * (Closing * BumpImpulsePerSpeed * 0.6f), Hit.ImpactPoint);
	OtherBalance->MulticastBoneImpulse(TEXT("spine_03"), Toward * (Closing * BumpImpulsePerSpeed), Hit.ImpactPoint);
}

void UAedirBalanceComponent::OnRep_Ragdoll()
{
	SetRagdollPresentation(bIsRagdoll);
}

void UAedirBalanceComponent::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessHit(OtherActor, OtherComponent, Hit);
}

void UAedirBalanceComponent::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessHit(OtherActor, OtherComponent, Hit);
}

void UAedirBalanceComponent::SetupMotors()
{
	if (!BodyMesh || !CharacterOwner)
	{
		return;
	}

	for (TArray<FName>& Group : MotorGroupBones)
	{
		Group.Reset();
	}

	// Group 0 = spine and head, 1 = left arm, 2 = right arm, 3 = left leg, 4 = right leg.
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (!Body || !Body->BodySetup.IsValid())
		{
			continue;
		}

		const FName BoneName = Body->BodySetup->BoneName;
		if (BoneName == PelvisBoneName || BoneName == NAME_None)
		{
			continue;
		}

		const EAedirBodyZone Zone = UAedirCombatComponent::ClassifyBodyZone(BoneName);
		EAedirLimb Limb = EAedirLimb::LeftArm;
		if (UAedirCombatComponent::GetBoneLimb(BoneName, Limb))
		{
			switch (Limb)
			{
			case EAedirLimb::LeftArm:
				MotorGroupBones[1].Add(BoneName);
				break;
			case EAedirLimb::RightArm:
				MotorGroupBones[2].Add(BoneName);
				break;
			case EAedirLimb::LeftLeg:
				MotorGroupBones[3].Add(BoneName);
				break;
			case EAedirLimb::RightLeg:
				MotorGroupBones[4].Add(BoneName);
				break;
			}
		}
		else if (Zone == EAedirBodyZone::Head || Zone == EAedirBodyZone::Torso)
		{
			MotorGroupBones[0].Add(BoneName);
		}
	}

	MotorComponent = NewObject<UPhysicalAnimationComponent>(CharacterOwner, TEXT("AedirPhysicalMotors"));
	if (!MotorComponent)
	{
		return;
	}

	MotorComponent->RegisterComponent();
	MotorComponent->SetSkeletalMeshComponent(BodyMesh);
	UpdateMotors(0.f, true);
}

void UAedirBalanceComponent::UpdateMotors(float DeltaTime, bool bForce)
{
	if (!MotorComponent)
	{
		return;
	}

	const bool bWasFlinching = Flinch > 0.f;
	Flinch = FMath::Max(Flinch - DeltaTime / HitFlinchSeconds, 0.f);
	MotorUpdateTimer -= DeltaTime;
	if (!bForce && MotorUpdateTimer > 0.f)
	{
		return;
	}
	MotorUpdateTimer = IsGettingUp() || bWasFlinching ? HitFlinchMotorInterval : 0.2f;

	const UAedirStatsComponent* Stats = FindStats();
	const float StaminaFraction = Stats ? FMath::Clamp(Stats->GetStaminaFraction(), 0.f, 1.f) : 1.f;
	const UAedirCombatComponent* Combat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;

	float Common = FMath::Lerp(0.35f, 1.f, StaminaFraction) * (1.f - 0.6f * GetDazeFactor());
	if (StaggerSeconds > 0.f)
	{
		Common *= 0.7f;
	}
	// Just struck, the body gives to the blow and finds its strength again over a moment.
	Common *= 1.f - HitFlinchMaxSlack * FMath::SmoothStep(0.f, 1.f, Flinch);
	// Coming up off the ground the limbs are still heavy and find their strength as the body rises.
	Common *= FMath::Lerp(1.f, GetUpMuscleShare, GetGetUpShare());
	// A limp body tenses gradually as it starts to move, instead of being snapped into the first pose of getting up.
	if (IsGettingUp())
	{
		Common *= FMath::Max(FMath::SmoothStep(0.f, AedirGetUpTenseSeconds, GetUpTotal - GetUpLeft), RagdollMotorShare);
	}
	// Fallen but alive and awake, the body keeps some tone: it goes down bending at the knees and hips and catching itself
	// on its arms, not as a log. Dead or senseless it is slack.
	if (Stats && (Stats->IsDead() || Stats->IsDying() || Stats->IsKnockedOut()))
	{
		Common = RagdollMotorShare;
	}
	else if (bIsRagdoll)
	{
		Common = AedirLivingFallMotorShare;
	}

	float Strength[5];
	Strength[0] = Common;
	Strength[1] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::LeftArm) : 0.f));
	Strength[2] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::RightArm) : 0.f));
	Strength[3] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::LeftLeg) : 0.f));
	Strength[4] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::RightLeg) : 0.f));
	// A hand held onto a weapon by the grip follows the weapon; its arm only keeps a little of its own will.
	if (Combat && Combat->GetGripJointForce() >= 0.f && !bIsRagdoll)
	{
		Strength[Combat->GetGripHand() == EAedirHand::Left ? 2 : 1] *= GrippedArmMotorShare;
	}

	for (int32 GroupIndex = 0; GroupIndex < 5; ++GroupIndex)
	{
		if (bForce || FMath::Abs(Strength[GroupIndex] - AppliedMotorStrength[GroupIndex]) > 0.04f)
		{
			ApplyMotorGroup(GroupIndex, Strength[GroupIndex]);
		}
	}
}

void UAedirBalanceComponent::UpdateLegBlend(float DeltaTime)
{
	if (!BodyMesh)
	{
		return;
	}

	// The whole body is physical, held to the pose by its muscles.
	LegPhysicsBlend = 1.f;
	ArmPhysicsBlend = 1.f;
	for (int32 GroupIndex = 0; GroupIndex < UE_ARRAY_COUNT(MotorGroupBones); ++GroupIndex)
	{
		for (const FName& BoneName : MotorGroupBones[GroupIndex])
		{
			if (FBodyInstance* Body = BodyMesh->GetBodyInstance(BoneName))
			{
				Body->PhysicsBlendWeight = 1.f;
			}
		}
	}
}

void UAedirBalanceComponent::ApplyMotorGroup(int32 GroupIndex, float Strength)
{
	if (!MotorComponent || GroupIndex < 0 || GroupIndex >= 5)
	{
		return;
	}

	AppliedMotorStrength[GroupIndex] = Strength;

	// A loose body only keeps a faint memory of the pose at its joints. A living one is pulled to where the pose is in the world, as hard as its muscles can.
	const UAedirStatsComponent* Stats = FindStats();
	const bool bLoose = bIsRagdoll || (Stats && Stats->IsDead());

	// Muscles that hold a pose loosely enough for weight, momentum and blows to show: the legs firm to stand on, the trunk
	// and the head give, the arms most of all.
	const bool bArm = GroupIndex == 1 || GroupIndex == 2;
	const bool bLeg = GroupIndex == 3 || GroupIndex == 4;
	const float Hold = bLoose ? 1.f : bArm ? BalanceArmMuscleShare : bLeg ? 1.f : BalanceTrunkMuscleShare;
	for (const FName& BoneName : MotorGroupBones[GroupIndex])
	{
		const FAedirBoneMotor& Motor = GetBoneMotor(BoneName);
		FPhysicalAnimationData Data;
		// With physical hips every part is held by its joint to the part above it, so the body moves as one chain.
		Data.bIsLocalSimulation = bLoose || bHipJointActive;
		Data.OrientationStrength = Motor.OrientationStrength * Strength * FMath::Sqrt(Hold);
		Data.AngularVelocityStrength = Motor.AngularVelocityStrength * Strength * (GroupIndex == 1 || GroupIndex == 2 ? CVarAedirArmDamping.GetValueOnGameThread() : 1.f);
		Data.PositionStrength = bLoose ? 0.f : Motor.PositionStrength * Strength * Hold;
		Data.VelocityStrength = bLoose ? 0.f : Motor.VelocityStrength * Strength * (GroupIndex == 1 || GroupIndex == 2 ? CVarAedirArmDamping.GetValueOnGameThread() : 1.f);
		Data.MaxLinearForce = Motor.MaxLinearForce;
		Data.MaxAngularForce = Motor.MaxAngularForce;
		MotorComponent->ApplyPhysicalAnimationSettings(BoneName, Data);
	}
}

