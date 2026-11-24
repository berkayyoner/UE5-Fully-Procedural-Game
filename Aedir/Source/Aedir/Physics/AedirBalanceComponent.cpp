#include "AedirBalanceComponent.h"

#include "AnimationRuntime.h"
#include "Animation/AedirGaitComponent.h"
#include "Animation/AedirProceduralAnimInstance.h"
#include "Fists/AedirFistArms.h"
#include "Engine/SkeletalMesh.h"

#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#include "Combat/AedirCombatComponent.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Wounds/AedirWoundComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Aedir.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Physics/AedirBodyControlComponent.h"
#include "Player/AedirPlayerCharacter.h"
#include "Audio/AedirSfxSubsystem.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"
#include "GroomComponent.h"

namespace
{
	// A spring of natural frequency W (rad/s), critically damped, pushed by force over a frame of Dt: its pull is taken as
	// the frame will carry it (implicitly), so however stiff it never overshoots in one frame and shakes.
	float AedirStableSpring(float W, float Dt)
	{
		return 1.f / (1.f + 2.f * W * Dt + W * W * Dt * Dt);
	}

	// How long after going down the fall is watched for the dev log.
	constexpr float AedirFallWatchSeconds = 1.5f;
	// How fast the hips go over towards the way of the keys, and back.
	constexpr float AedirGetUpLeanInterpSpeed = 4.f;
	// How many loose things or bodies the ground a body stands up on is looked for past.
	constexpr int32 AedirStandGroundTries = 6;
	constexpr float AedirFallFlyingSpeed = 1000.f;
	// A leg is looked for in the ground this far above and below its shapes; a body is lifted out of it by no more than this.
	constexpr float AedirLegGroundProbeCm = 20.f;
	constexpr float AedirLegGroundLiftMaxCm = 15.f;
	TAutoConsoleVariable<int32> CVarAedirDirectPose(TEXT("aedir.DirectPose"), 0, TEXT("The body is shown and moved as the pose has it, kinematic, with no physics (1), or held to it by its muscles (0)."));
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
	// How far from a part of the body the thing it ran into is looked for, to tell which part met it.
	constexpr float AedirWallPartReachCm = 60.f;
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
	// A loose part that loses this much speed in a frame has run into something.
	constexpr float AedirPartStopCm = 150.f;
	// The mass an arm moves by itself, and the strength at which a held load weighs what it weighs.
	constexpr float AedirArmKilos = 4.f;
	constexpr float AedirArmLoadStrengthRef = 10.f;
	// How much further the hips may stray from the pose while getting up before it costs balance.
	constexpr float AedirGetUpDriftFreeScale = 2.f;
	// How long a body that was just carried over is left to arrive.
	constexpr float AedirBodyTeleportGraceSeconds = 0.15f;
	// Balance lost each second a player getting up lets go of the key: the body sinks back down within half a second.
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
	TAutoConsoleVariable<float> CVarAedirGrippedArmShare(TEXT("aedir.GrippedArmShare"), 0.3f, TEXT("How much of its own will the arm of a hand held onto a weapon's grip keeps."));
	TAutoConsoleVariable<int32> CVarAedirArmTrapRelease(TEXT("aedir.ArmTrapRelease"), 1, TEXT("An arm caught far off its pose against its joint limits is put back on it (1) or left (0)."));
	// How fast a forearm and a hand can turn, in degrees a second: a hard swing turns the hand at about this much.
	constexpr float AedirForearmMaxSpinDegrees = 1100.f;
	constexpr float AedirHandMaxSpinDegrees = 1400.f;
	TAutoConsoleVariable<int32> CVarAedirArmSpinLimit(TEXT("aedir.ArmSpinLimit"), 1, TEXT("The forearms and hands turn no faster than a strong swing (1), or as fast as the physics flings them (0)."));
	// Held this far off its pose for this long, an arm is caught; after being put back it is left alone this long.
	constexpr float AedirArmTrapDegrees = 45.f;
	constexpr float AedirArmTrapCm = 25.f;
	constexpr float AedirArmTrapFarCm = 35.f;
	constexpr float AedirArmTrapStillCmPerSecond = 30.f;
	constexpr float AedirArmTrapSeconds = 0.35f;
	// Only a hand that has come to rest off its pose is caught; one still swinging round to it is left to get there.
	constexpr float AedirArmTrapStillRadPerSecond = 1.5f;
	constexpr float AedirArmTrapCooldownSeconds = 0.5f;
	TAutoConsoleVariable<float> CVarAedirArmHold(TEXT("aedir.ArmHold"), 0.5f, TEXT("How firmly the arms hold their pose, against the trunk's 1."));
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
	constexpr float AedirLivingFallMotorShare = 0.15f;
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

	// Hip height, back, trunk, hip bend, knee bend, back bow, arm raise, elbow bend, hands, legs, seconds.
	const auto Step = [](float HipHeight, float HipBack, float Trunk, float HipBend, float KneeBend, float BackBow, float ArmRaise, float ElbowBend, float Hands, float Legs, float Seconds)
	{
		FAedirGetUpStage Stage;
		Stage.HipHeightCm = HipHeight;
		Stage.HipBackCm = HipBack;
		Stage.TrunkDegrees = Trunk;
		Stage.HipBend = HipBend;
		Stage.KneeBend = KneeBend;
		Stage.BackBow = BackBow;
		Stage.ArmRaise = ArmRaise;
		Stage.ElbowBend = ElbowBend;
		Stage.Hands = Hands;
		Stage.Legs = Legs;
		Stage.Seconds = Seconds;
		return Stage;
	};
	// Face down: lying; on the hands and knees, the back flat; sat back on the heels with the trunk upright; one foot put
	// forward, kneeling on the other knee, the hips over that knee; up off the forward foot. The hips are where the legs
	// can have them: the knees held 22 cm back, a thigh 45 cm long. Sat on the heels 33 cm up they are 36 cm behind the
	// knees (asked for over the knees, as they were, the hips stayed 25 cm forward of it and the trunk leant forward on
	// them); kneeling on one knee they stand over it, the forward foot set down under its knee, a thigh ahead.
	FrontGetUpStages = {
		Step(13.f, 0.f, 0.f, 10.f, 10.f, 0.f, 20.f, 40.f, 1.f, 0.f, 0.f),
		Step(50.f, 0.f, 0.f, 90.f, 90.f, 0.f, 80.f, 5.f, 1.f, 1.f, 1.1f),
		Step(33.f, 58.f, 95.f, 85.f, 150.f, -5.f, 10.f, 30.f, 0.f, 1.f, 1.2f),
		Step(50.f, 24.f, 92.f, 10.f, 95.f, 0.f, 10.f, 30.f, 0.f, 1.f, 1.f),
		Step(88.f, 5.f, 90.f, 10.f, 10.f, 0.f, 0.f, 10.f, 0.f, 0.f, 1.3f),
	};
	FrontGetUpStages[3].Lead = 1.f;
	FrontGetUpStages[3].LeadHipBend = 95.f;
	FrontGetUpStages[3].LeadKneeBend = 95.f;
	FrontGetUpStages[3].LeadFootAheadCm = 42.f;
	FrontGetUpStages[4].LeadHipBend = 10.f;
	FrontGetUpStages[4].LeadKneeBend = 10.f;
	// On the back: lying; the belly tightened, back and head a little up on the hands, the feet drawn in; sat up on the
	// ground, the hips still down, the feet in; leant forward over the feet, the hips only now off the ground and right over
	// the feet (held 34 cm back: 4 cm short of them the step waited on the hips, crouched, for up to 0.6 s); up.
	BackGetUpStages = {
		Step(12.f, 0.f, 0.f, 10.f, 10.f, 0.f, -10.f, 10.f, 1.f, 0.f, 0.f),
		Step(14.f, 0.f, 30.f, 75.f, 115.f, 25.f, -30.f, 10.f, 1.f, 1.f, 1.f),
		Step(15.f, 8.f, 80.f, 100.f, 130.f, 20.f, -10.f, 10.f, 0.7f, 1.f, 1.f),
		Step(40.f, 36.f, 108.f, 115.f, 135.f, 15.f, 30.f, 30.f, 0.f, 1.f, 1.f),
		Step(88.f, 36.f, 90.f, 10.f, 10.f, 0.f, 0.f, 10.f, 0.f, 0.f, 1.1f),
	};
}

void UAedirBalanceComponent::WidenKnees()
{
	if (!BodyMesh || KneeExtraBendDegrees <= 0.f)
	{
		return;
	}
	for (FConstraintInstance* Joint : BodyMesh->Constraints)
	{
		if (!Joint || !Joint->JointName.ToString().StartsWith(TEXT("calf")))
		{
			continue;
		}
		// The knee bends about the joint's twist, within a range set off towards the bend. The range is widened by half the
		// extra and its middle turned that much further into the bend: the straight end stays where it was, so the knee bends
		// no further backwards, and the bent end goes the whole extra further.
		// The joints are made again from the asset whenever the body's physics is (going down, among others): widened
		// already, they are left as they are.
		const float Half = Joint->GetAngularTwistLimit();
		const float Shift = KneeExtraBendDegrees * 0.5f;
		if (KneeAssetTwistHalf > 0.f && Half > KneeAssetTwistHalf + 0.5f)
		{
			continue;
		}
		KneeAssetTwistHalf = Half;
		FTransform Frame = Joint->GetRefFrame(EConstraintFrame::Frame1);
		Frame.SetRotation(Frame.GetRotation() * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Shift * KneeBendTwistSign)));
		Joint->SetRefFrame(EConstraintFrame::Frame1, Frame);
		Joint->SetAngularTwistLimit(Joint->GetAngularTwistMotion(), Half + Shift);
		UE_LOG(LogAedir, Log, TEXT("[Knee] %s's %s: twist range %.0f either way, now %.0f, turned %.0f further into the bend"), *GetNameSafe(GetOwner()), *Joint->JointName.ToString(), Half, Half + Shift, Shift * KneeBendTwistSign);
	}
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
	WidenKnees();
	// Which way the face looks from the head bone: the body faces along the mesh's +Y in its rest pose.
	if (const USkeletalMesh* FaceModel = BodyMesh->GetSkeletalMeshAsset())
	{
		const int32 HeadIndex = FaceModel->GetRefSkeleton().FindBoneIndex(HeadBoneName);
		if (HeadIndex != INDEX_NONE)
		{
			HoistFaceLocal = FAnimationRuntime::GetComponentSpaceTransformRefPose(FaceModel->GetRefSkeleton(), HeadIndex).GetRotation().UnrotateVector(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
		}
	}
	MeshRestRelative = BodyMesh->GetRelativeTransform();
	MeshRelativeAtFall = MeshRestRelative;
	SpawnPosedLeft = SpawnPosedSeconds;
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BodyMesh->SetNotifyRigidBodyCollision(true);
	ApplyBodyContacts(false);
	FitJointsToSkeleton();
	ApplyJointLimits(false);
	ApplyLegPhysics(false);
	BodyMesh->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnMeshHit);

	BodyCapsule->SetNotifyRigidBodyCollision(true);
	BodyCapsule->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnCapsuleHit);

	PelvisRestHeight = static_cast<float>(BodyMesh->GetBoneLocation(PelvisBoneName, EBoneSpaces::ComponentSpace).Z);
	ChestAboveHips = BodyMesh->GetBoneIndex(TEXT("spine_05")) != INDEX_NONE ? static_cast<float>(BodyMesh->GetBoneLocation(TEXT("spine_05"), EBoneSpaces::ComponentSpace).Z) - PelvisRestHeight : 40.f;

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
	WatchFall(DeltaTime);
	SettleRagdollEntry(DeltaTime);
	ForgetThingsSteppedOff();

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
	// For the hit log: a lying body thrown up into the air, and how long the frame was.
	static const IConsoleVariable* LeapLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (bIsRagdoll && LeapLog && LeapLog->GetInt() != 0)
	{
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics() && Body->GetUnrealWorldVelocity().Z > 300.f)
			{
				UE_LOG(LogAedir, Log, TEXT("[Leap] %s %s goes up at %.0f cm/s, frame %.4f s, dilation %.4f"), *GetNameSafe(GetOwner()), *Body->BodySetup->BoneName.ToString(), Body->GetUnrealWorldVelocity().Z, DeltaTime, GetWorld()->GetWorldSettings()->TimeDilation);
				break;
			}
		}
	}
	WatchPartImpacts();
	WatchFistKnocks();
	UpdateHoist(DeltaTime);
	LookWhileDown();
	HoldHurtWhileDown(DeltaTime);
	{
		// Just hauled up, the capsule stays under the body while it finds its feet; walked or pushed off, it would leave the
		// body behind, and the body would then be put back after it in one jump.
		const bool bWasSettling = HoistSettleLeft > 0.f;
		HoistSettleLeft = FMath::Max(HoistSettleLeft - DeltaTime, 0.f);
		if (UCharacterMovementComponent* SettleMovement = CharacterOwner->GetCharacterMovement(); SettleMovement && !bIsRagdoll)
		{
			if (HoistSettleLeft > 0.f)
			{
				SettleMovement->StopMovementImmediately();
			}
			else if (bWasSettling && SettleMovement->MovementMode == MOVE_None && !IsGettingUp())
			{
				SettleMovement->SetMovementMode(MOVE_Walking);
			}
		}
	}
	UpdateGetUp(DeltaTime);
	UpdatePelvisLean(DeltaTime);
	UpdateBalanceSink(DeltaTime);
	UpdateStumbleWalk(DeltaTime);
	UpdatePendingPushes(DeltaTime);
	UpdateHitSway(DeltaTime);
	UpdateMotors(DeltaTime, false);
	SpawnAge += DeltaTime;
	if (SpawnPosedLeft > 0.f)
	{
		SpawnPosedLeft = FMath::Max(SpawnPosedLeft - DeltaTime, 0.f);
		UE_CLOG(SpawnPosedLeft <= 0.f, LogAedir, Log, TEXT("[Spawn] %s is let into its physics, standing in its pose."), *GetNameSafe(GetOwner()));
	}
	UpdatePosedArms();
	UpdateArmTraps(DeltaTime);
	LimitArmSpin();
	UpdateLegBlend(DeltaTime);

	if (!bIsRagdoll && BodyControl)
	{
		UpdateBodyControl(DeltaTime);
	}
	else if (BodyControl && BodyControl->IsReady())
	{
		// Down, the muscles still follow what the body is: killed or knocked senseless on the ground it goes slack there.
		UpdateDownMuscles();
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
	// The less breath it has left, all the way from full to none, the weaker it stands and holds its trunk.
	return FMath::Lerp(UprightExhaustedShare, 1.f, Stamina) * FMath::Lerp(UprightDyingShare, 1.f, Health)
		* (1.f - 0.5f * GetDazeFactor()) * FMath::Lerp(1.f, GetUpMuscleShare, GetGetUpShare())
		* (IsGettingUp() ? FMath::Lerp(GetUpWeakMuscleShare, 1.f, GetUpLift) : 1.f);
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

void UAedirBalanceComponent::UpdateDownMuscles()
{
	if (!BodyControl)
	{
		return;
	}
	const UAedirStatsComponent* Stats = FindStats();
	const AAedirCreature* Creature = Cast<AAedirCreature>(CharacterOwner);
	BodyControl->SetDead(Creature && Creature->Stats && (Creature->Stats->IsDead() || Creature->Stats->IsDying()));
	// Down but awake the body keeps a share of the muscle it has on its feet, less with breath gone or the senses dazed;
	// the senseless and the dead are slack.
	const bool bSenseless = Stats && (Stats->IsDead() || Stats->IsDying() || Stats->IsKnockedOut());
	const float Breath = FMath::Lerp(TiredStrengthShare, 1.f, Stats ? FMath::Clamp(Stats->GetStaminaFraction(), 0.f, 1.f) : 1.f);
	// Hauling itself up it puts all it has into it.
	const float Share = bHoisting ? 1.f : DownAwakeMotorShare;
	const float Senses = Breath * (1.f - 0.6f * GetDazeFactor());
	BodyControl->SetDownAwake(bSenseless ? 0.f : Share * Senses, bSenseless ? 0.f : (bHoisting ? 1.f : DownAwakeArmShare) * Senses);
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
	const UAedirFistArms* FistArms = CharacterOwner->FindComponentByClass<UAedirFistArms>();
	BodyControl->SetTwoHandGrip(FistArms ? FistArms->GetGripAlpha() : 0.f);
	// Held slack as long as the blow lasts, then the muscles take hold again quickly.
	if (!GetWorld() || GetWorld()->GetTimeSeconds() >= BlowSoftUntil)
	{
		BlowSoftness = FMath::Max(BlowSoftness - DeltaTime / FMath::Max(BlowSlackReturnSeconds, 0.01f), 0.f);
	}
	BodyControl->SetBlowSoftness(BlowSoftness);
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
	// The less breath left, from full to none, the less firmly the arms hold their pose.
	const UAedirStatsComponent* BreathStats = FindStats();
	const float Breath = FMath::Lerp(TiredStrengthShare, 1.f, BreathStats ? FMath::Clamp(BreathStats->GetStaminaFraction(), 0.f, 1.f) : 1.f);
	BodyControl->SetStrengths(Upright, LeftArm * Breath, RightArm * Breath);
	// Dying, the body goes down as the dead do: what muscle it has left only holds its shape a while longer.
	BodyControl->SetDead(Creature && Creature->Stats && (Creature->Stats->IsDead() || Creature->Stats->IsDying()));
	const UAedirWoundComponent* Wounds = Creature ? Creature->Wounds.Get() : nullptr;
	// A broken arm turns as far as a limp body's does: its joints go back to the limits of the physics asset.
	const uint8 BrokenArmsNow = (Wounds && Wounds->IsLimbBroken(EAedirLimb::LeftArm) ? 1 : 0) | (Wounds && Wounds->IsLimbBroken(EAedirLimb::RightArm) ? 2 : 0);
	if (BrokenArmsNow != BrokenArmJoints)
	{
		BrokenArmJoints = BrokenArmsNow;
		ApplyJointLimits(IsRagdoll());
	}
	BodyControl->SetBrokenLimbs(Wounds && Wounds->IsLimbBroken(EAedirLimb::LeftArm), Wounds && Wounds->IsLimbBroken(EAedirLimb::RightArm),
		Wounds && Wounds->IsLimbBroken(EAedirLimb::LeftLeg), Wounds && Wounds->IsLimbBroken(EAedirLimb::RightLeg));
	const bool bGettingUp = IsGettingUp();
	BodyControl->SetRising(bGettingUp);
	UpdateDownMuscles();

	// A foot the steps have put down stays there; one in the air, or both while jumping or falling, swings free.
	FAedirGaitState Pose;
	if (const UAedirGaitComponent* Gait = CharacterOwner->FindComponentByClass<UAedirGaitComponent>())
	{
		Pose = Gait->GetGaitState();
	}
	const UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	const bool bOnGround = Pose.bProceduralActive && Movement && !Movement->IsFalling();
	BodyControl->SetAirborne(Movement && Movement->IsFalling());
	// Moving, a foot rolls off the ground on its muscles; held by the pose it would skate behind the stride.
	const float GroundSpeed = Movement ? Movement->Velocity.Size2D() : 0.f;
	bStandingForPlant = GroundSpeed < (bStandingForPlant ? PlantedFootSpeedMaxCm : PlantedFootSpeedMinCm);
	const bool bPlantable = bOnGround && bStandingForPlant;
	BodyControl->SetFeetPlanted(bPlantable && Pose.LeftFootLift < AedirPlantedFootLift, bPlantable && Pose.RightFootLift < AedirPlantedFootLift);

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

FVector UAedirBalanceComponent::GetGetUpLeanLocal() const
{
	return IsGettingUp() ? FVector(GetUpLean.X, GetUpLean.Y, 0.f) * GetUpLeanCm : FVector::ZeroVector;
}

void UAedirBalanceComponent::RequestGetUp(const FVector& KeyWay)
{
	if (!bIsRagdoll && !IsGettingUp())
	{
		return;
	}
	if (IsAuthoritative())
	{
		ServerRequestGetUp_Implementation(KeyWay);
		return;
	}
	// Held down, the keys would be sent every frame; a few times a second is enough to keep them alive.
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LastGetUpRequestSent >= 0.1)
	{
		LastGetUpRequestSent = Now;
		ServerRequestGetUp(KeyWay);
	}
}

void UAedirBalanceComponent::ServerRequestGetUp_Implementation(FVector KeyWay)
{
	const UWorld* World = GetWorld();
	GetUpRequestTime = World ? World->GetTimeSeconds() : 0.0;
	GetUpKeyWay = FVector(KeyWay.X, KeyWay.Y, 0.f).GetClampedToMaxSize(1.0);
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
	// Whatever threw it off its feet, the hurried walk after it starts from now.
	if (const UWorld* ImpactWorld = GetWorld())
	{
		StumbleBlowTime = ImpactWorld->GetTimeSeconds();
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

	// Worn down, the same blow throws the body further and costs it more of its balance.
	const float Weak = FMath::Lerp(HealthyHitShare, 1.f + LowHealthHitGain, GetHurtShare()) + TiredHitGain * GetTiredShare();
	const bool bHead = Impact.Zone == EAedirBodyZone::Head;
	const float Loss = FMath::Min(ComputeBalanceLoss(Impact.Zone, Impact.EnergyJoules, Athletism, StaminaFraction) * Weak * (bHead ? HeadBalanceLossShare : 1.f) * HitBalanceLossShare, 1.f);
	ApplyBalanceImpact(Impact.Direction, Loss);
	if (const UWorld* WatchWorld = GetWorld())
	{
		RecoveryWatchSince = WatchWorld->GetTimeSeconds();
		RecoveryWatchLowest = Balance;
		RecoveryWatchFrom = CharacterOwner->GetActorLocation();
	}
	// The harder it was, and on the head, the slower the balance comes back for a moment.
	HitRecoveryScale = FMath::Min(HitRecoveryScale, FMath::Lerp(1.f, HardHitRecoveryShare, Loss) * (bHead ? HeadHitRecoveryShare : 1.f));

	if (Impact.Bone != NAME_None && BodyMesh)
	{
		const float ImpulseSize = FMath::Min(Impact.MassKg * Impact.RelativeSpeed * HitReactionBoneImpulseScale, HitReactionBoneImpulseMax) * Weak
			* (bIsRagdoll && !bHoisting ? DownBlowPushShare : 1.f);
		// A shoulder or an upper arm struck: the trunk takes most of it, so the arm is not flung up beside the head.
		const FString StruckName = Impact.Bone.ToString();
		const bool bShoulder = StruckName.StartsWith(TEXT("clavicle")) || StruckName.StartsWith(TEXT("upperarm"));
		MulticastBoneImpulse(Impact.Bone, Impact.Direction * (ImpulseSize * (bHead ? HitReactionHeadShare : 1.f) * (bShoulder ? ShoulderBlowArmShare : 1.f)), Impact.Location);
		if (bShoulder)
		{
			// The highest part of the trunk that has a body of its own.
			for (const TCHAR* Upper : { TEXT("spine_05"), TEXT("spine_04"), TEXT("spine_03") })
			{
				if (BodyMesh->GetBodyInstance(Upper))
				{
					MulticastBoneImpulse(Upper, Impact.Direction * (ImpulseSize * (1.f - ShoulderBlowArmShare)), Impact.Location);
					break;
				}
			}
		}
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
		const float Kick = FMath::Min(Impact.EnergyJoules * HitReactionViewDegreesPerJoule, HitReactionViewMaxDegrees);
		const FVector Local = CharacterOwner->GetActorTransform().InverseTransformVectorNoScale(Impact.Direction);
		Viewer->SetControlRotation(Viewer->GetControlRotation() + FRotator(static_cast<float>(Local.Z - Local.X * 0.5) * Kick, static_cast<float>(Local.Y) * Kick, 0.f));
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bKnockedDown = Impact.EnergyJoules >= GetKnockdownEnergy(Impact.Zone) * FMath::Lerp(HealthyKnockdownShare, LowHealthKnockdownShare, GetHurtShare()) * FMath::Lerp(1.f, TiredKnockdownShare, GetTiredShare());
	bool bStaggered = false;

	// Struck while being hauled up by the hips, the pull weakens and the body sinks under its weight; a blow that would fell
	// it lets go of it altogether.
	if (bIsRagdoll && bHoisting)
	{
		if (bKnockedDown || Loss >= GetUpKnockLoss)
		{
			GetUpLift = 0.f;
			StopHoist(TEXT("knocked down"));
		}
		else
		{
			GetUpLift = FMath::Max(GetUpLift - Loss * GetUpBlowLiftLoss, 0.f);
			// And it is set back through the steps it had come.
			HoistProgress = FMath::Max(HoistProgress - Loss * GetUpBlowStepLoss, 0.f);
			UE_LOG(LogAedir, Log, TEXT("[GetUp] %s is struck being hauled up, losing %.2f of its balance: lift %.2f, back to step %.2f"), *GetNameSafe(GetOwner()), Loss, GetUpLift, HoistProgress);
			OnHitReaction.Broadcast(Impact.Zone, Impact.EnergyJoules, false, false);
			return;
		}
	}
	// Struck while getting up, the body is not felled outright: it lifts itself the less, and may sink back under it.
	if (!bKnockedDown && !bIsRagdoll && IsGettingUp())
	{
		GetUpLift = FMath::Max(GetUpLift - Loss * GetUpBlowLiftLoss, 0.f);
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s is struck getting up, losing %.2f of its balance: lift %.2f"), *GetNameSafe(GetOwner()), Loss, GetUpLift);
		OnHitReaction.Broadcast(Impact.Zone, Impact.EnergyJoules, false, false);
		return;
	}

	if (bKnockedDown)
	{
		if (!bIsRagdoll)
		{
			EnterRagdoll(MinRagdollSeconds + HitReactionKnockdownExtraSeconds);
			// The legs give and the body goes over the way the blow drove it: back from a blow in front, face down from one behind.
			const FVector PlanarDirection = FVector(Impact.Direction.X, Impact.Direction.Y, 0.f).GetSafeNormal();
			if (!PlanarDirection.IsNearlyZero())
			{
				MulticastTopple(PlanarDirection * (ComputeStaggerSpeed(Impact.EnergyJoules) * KnockdownToppleShare * Impact.ThrowShare));
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
			const float Speed = ComputeStaggerSpeed(Impact.EnergyJoules) * Weak * Impact.ThrowShare;
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

float UAedirBalanceComponent::GetTiredShare() const
{
	const UAedirStatsComponent* StatsComponent = FindStats();
	if (!StatsComponent || TiredFrom <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return FMath::Clamp((TiredFrom - StatsComponent->GetStaminaFraction()) / TiredFrom, 0.f, 1.f);
}

float UAedirBalanceComponent::GetWornShare() const
{
	const UAedirStatsComponent* StatsComponent = FindStats();
	if (!StatsComponent)
	{
		return 0.f;
	}
	const float Health = FMath::Clamp(StatsComponent->GetHealth() / FMath::Max(StatsComponent->GetMaxHealth(), 1.f), 0.f, 1.f);
	return FMath::Clamp(0.5f * (1.f - Health) + 0.5f * (1.f - StatsComponent->GetStaminaFraction()), 0.f, 1.f);
}

float UAedirBalanceComponent::GetHurtShare() const
{
	const UAedirStatsComponent* StatsComponent = FindStats();
	if (!StatsComponent || LowHealthFrom <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return FMath::Clamp((LowHealthFrom - StatsComponent->GetHealthFraction()) / LowHealthFrom, 0.f, 1.f);
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
	case EAedirBodyZone::Head: return 480.f;
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
	ComFromFeetLocal = FVector2D::ZeroVector;
	ComDriftLocal = FVector2D::ZeroVector;
	ComEdgeCm = 100.f;
	bComEdgeCounts = false;

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
		const FVector PoseLocation = BoneName != NAME_None && BodyMesh->GetBoneIndex(BoneName) != INDEX_NONE
			? BodyMesh->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace)
			: Body->GetCOMPosition();
		// Pushed about by blows, the bodies are where the weight really is.
		const FVector BodyLocation = Body->IsInstanceSimulatingPhysics() ? FMath::Lerp(PoseLocation, Body->GetCOMPosition(), ComPhysicalShare) : PoseLocation;
		WeightedSum += BodyLocation * Mass;
		TotalMass += Mass;
	}

	if (TotalMass <= UE_SMALL_NUMBER)
	{
		return;
	}
	CenterOfMassWorld = WeightedSum / TotalMass;
	// A trunk thrown off the feet carries the weight on the way it goes.
	if (const FBodyInstance* Trunk = BodyMesh->GetBodyInstance(TEXT("spine_03")); Trunk && Trunk->IsInstanceSimulatingPhysics())
	{
		const FVector Thrown = Trunk->GetUnrealWorldVelocity() - CharacterOwner->GetVelocity();
		CenterOfMassWorld += FVector(Thrown.X, Thrown.Y, 0.0) * ComCarrySeconds;
	}

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
	// Every distance of the weight from the feet is measured for a body of the usual size: a bigger one's weight is as much
	// further off for the same lean.
	const UAedirStatsComponent* GrowthStats = FindStats();
	const float Growth = GrowthStats ? FMath::Max(GrowthStats->GetStrengthGrowth(), 0.1f) : 1.f;
	ComEdgeCm = ComputeSupportMargin(Hull, ComXY) / Growth;
	SupportMargin = ComEdgeCm + ComBalanceStaticAllowanceCm;
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
	bComEdgeCounts = !bKicking && !bBending && (MoveSpeed <= 20.f || StaggerSeconds > 0.f);
	bSupportGrounded = true;

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : Hull)
	{
		Centroid += Vertex;
	}
	Centroid /= static_cast<double>(FMath::Max(Hull.Num(), 1));

	const FVector2D Offset = (ComXY - Centroid).GetSafeNormal();
	ComOffsetDirection = FVector(Offset.X, Offset.Y, 0.0);
	const FVector OffLocal = CharacterOwner->GetActorTransform().InverseTransformVectorNoScale(FVector(ComXY - Centroid, 0.0));
	ComFromFeetLocal = FVector2D(OffLocal.X, OffLocal.Y) / Growth;
	// Steady on its feet, the body teaches where its weight belongs. Moving, or just set its feet somewhere new, it takes
	// that afresh: a stance a step leaves it in is where it now stands.
	const bool bSteady = Balance >= 0.95f && StaggerSeconds <= 0.f && !bStumbling && !bKicking && !bBending;
	const double ComNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (MoveSpeed > 20.f || FVector2D::Distance(Centroid, LastFeetCentre) / Growth > ComFeetShiftCm)
	{
		ComResettleUntil = ComNow + ComResettleSeconds;
	}
	LastFeetCentre = Centroid;
	if (!bComSettledKnown || ComNow < ComResettleUntil)
	{
		ComSettledLocal = ComFromFeetLocal;
		bComSettledKnown = true;
	}
	else if (bSteady)
	{
		ComSettledLocal += (ComFromFeetLocal - ComSettledLocal) * FMath::Min(ComBalanceSampleIntervalSeconds / FMath::Max(ComSettleSeconds, 0.1f), 1.f);
	}
	ComDriftLocal = ComFromFeetLocal - ComSettledLocal;

	static const IConsoleVariable* ComLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (ComLog && ComLog->GetInt() != 0 && GetWorld() && GetWorld()->GetTimeSeconds() >= NextComLogTime)
	{
		NextComLogTime = GetWorld()->GetTimeSeconds() + 0.5;
		UE_LOG(LogAedir, Log, TEXT("[Com] %s weight %.1f forward %.1f right of the feet, off where it settles by %.1f %.1f, %.1f cm from their edge%s, balance %.2f, hips %s"), *GetNameSafe(GetOwner()), ComFromFeetLocal.X, ComFromFeetLocal.Y,
			ComDriftLocal.X, ComDriftLocal.Y, ComEdgeCm, bComEdgeCounts ? TEXT("") : TEXT(" (walking)"), Balance, *CurrentLean.ToCompactString());
	}
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
	// A bigger, stronger body stands on bigger feet.
	const UAedirStatsComponent* FootStats = FindStats();
	const float FootGrowth = FootStats ? FootStats->GetStrengthGrowth() : 1.f;
	const FVector2D Heel = FootXY - Forward2D * (ComBalanceHeelOffsetCm * FootGrowth);
	const FVector2D Toe = BallXY + Forward2D * (ComBalanceToeOffsetCm * FootGrowth);
	const FVector2D HalfWidth = Side2D * (ComBalanceFootHalfWidthCm * FootGrowth);

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

	// Not walking, the weight gone off from where it settles over the feet takes the balance with it, before it is past them.
	const float Drift = static_cast<float>(ComDriftLocal.Size());
	const float DriftLoss = bComEdgeCounts ? FMath::Clamp((Drift - ComDriftLossFromCm) / FMath::Max(ComDriftLossFullCm - ComDriftLossFromCm, 1.f), 0.f, 1.f) * ComDriftLossPerSecond * DeltaTime : 0.f;
	const float Loss = FMath::Max(ComputeComBalanceLoss(SupportMargin, DeltaTime), DriftLoss);
	if (Loss <= 0.f)
	{
		return;
	}

	// The body falls towards the side the centre of mass has left the support polygon on, or has gone off to.
	const FVector DriftWorld = CharacterOwner ? CharacterOwner->GetActorTransform().TransformVectorNoScale(FVector(ComDriftLocal, 0.0)) : FVector::ZeroVector;
	AddLeanDirection(SupportMargin < 0.f || DriftWorld.IsNearlyZero() ? ComOffsetDirection : DriftWorld.GetSafeNormal2D(), Loss);
	SetBalance(Balance - Loss);
	// Going over, the feet hurry after it.
	if (const UWorld* LossWorld = GetWorld())
	{
		StumbleBlowTime = LossWorld->GetTimeSeconds();
	}
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

void UAedirBalanceComponent::IgnoreLooseThingsAtFeet()
{
	UWorld* World = GetWorld();
	if (!World || !BodyCapsule)
	{
		return;
	}
	// Whatever loose thing the body stood up in the middle of, a sword it lay on, is passed through until the body has
	// stepped off it: the capsule would otherwise be pushed up out of it and stand on it, the feet hanging in the air.
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirGetUpLoose), false, GetOwner());
	World->OverlapMultiByChannel(Overlaps, BodyCapsule->GetComponentLocation(), BodyCapsule->GetComponentQuat(), ECC_Pawn,
		FCollisionShape::MakeCapsule(BodyCapsule->GetScaledCapsuleRadius(), BodyCapsule->GetScaledCapsuleHalfHeight()), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Part = Overlap.GetComponent();
		AActor* Thing = Overlap.GetActor();
		if (Part && Thing && Part->IsSimulatingPhysics() && !Cast<ACharacter>(Thing))
		{
			BodyCapsule->IgnoreActorWhenMoving(Thing, true);
			Part->IgnoreActorWhenMoving(GetOwner(), true);
			SteppedOffThings.AddUnique(Thing);
			UE_LOG(LogAedir, Log, TEXT("[GetUp] %s gets up in %s and passes through it until it is off it."), *GetOwner()->GetName(), *Thing->GetName());
		}
	}
}

void UAedirBalanceComponent::ForgetThingsSteppedOff()
{
	if (SteppedOffThings.IsEmpty() || !BodyCapsule)
	{
		return;
	}
	for (int32 Index = SteppedOffThings.Num() - 1; Index >= 0; --Index)
	{
		AActor* Thing = SteppedOffThings[Index].Get();
		const UPrimitiveComponent* Part = Thing ? Cast<UPrimitiveComponent>(Thing->GetRootComponent()) : nullptr;
		if (!Thing || !Part || !BodyCapsule->OverlapComponent(Part->GetComponentLocation(), Part->GetComponentQuat(), Part->GetCollisionShape()))
		{
			if (Thing)
			{
				BodyCapsule->IgnoreActorWhenMoving(Thing, false);
				if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Thing->GetRootComponent()))
				{
					Root->IgnoreActorWhenMoving(GetOwner(), false);
				}
			}
			SteppedOffThings.RemoveAt(Index);
		}
	}
}

bool UAedirBalanceComponent::TraceStandingGround(const FVector& Start, const FVector& End, FVector& OutHitPoint, bool bLog) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// What a body stands up on is the ground itself: not a sword or a shield lying under it, nor a body, nor anything a body
	// holds or wears. Standing on those it was lifted off the ground onto them and left hanging over it once they moved.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirStandingGround), false, GetOwner());
	FHitResult Hit;
	for (int32 Try = 0; Try < AedirStandGroundTries && World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params); ++Try)
	{
		AActor* Thing = Hit.GetActor();
		const AActor* Holder = Thing && Thing->GetAttachParentActor() ? Thing->GetAttachParentActor() : Thing;
		const UPrimitiveComponent* Part = Hit.GetComponent();
		if ((Part && Part->IsSimulatingPhysics()) || Cast<ACharacter>(Holder))
		{
			UE_CLOG(bLog, LogAedir, Log, TEXT("[GetUp] %s stands up past %s lying under it."), *GetOwner()->GetName(), *GetNameSafe(Thing));
			Params.AddIgnoredActor(Thing);
			continue;
		}
		OutHitPoint = Hit.ImpactPoint;
		UE_CLOG(bLog, LogAedir, Log, TEXT("[GetUp] %s stands up on %s (%s) at %.1f."), *GetOwner()->GetName(), *GetNameSafe(Thing), *GetNameSafe(Part), Hit.ImpactPoint.Z);
		return true;
	}
	return false;
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
	// A dying hand lets go of what it held: the weapon falls where the body falls.
	if (bDead && !bDroppedOnDeath && Creature && Creature->Interaction)
	{
		bDroppedOnDeath = true;
		Creature->Interaction->DropHeld(EAedirHand::Right);
		Creature->Interaction->DropHeld(EAedirHand::Left);
	}
	// So does a senseless one, whether it was standing or already down when it went out.
	if (bSenseless && !bDroppedSenseless && Creature && Creature->Interaction)
	{
		UE_LOG(LogAedir, Log, TEXT("[Senses] %s's hands go slack and let go of what they held."), *GetNameSafe(GetOwner()));
		Creature->Interaction->DropHeld(EAedirHand::Right);
		Creature->Interaction->DropHeld(EAedirHand::Left);
	}
	bDroppedSenseless = bSenseless;
	const float MaxLegInjury = Creature && Creature->Combat
		? FMath::Max(Creature->Combat->GetLimbInjury(EAedirLimb::LeftLeg), Creature->Combat->GetLimbInjury(EAedirLimb::RightLeg))
		: 0.f;

	const float DazeRecoveryScale = IsDazed() ? HitReactionDazeRecoveryScale : 1.f;
	const float StaggerRecoveryScale = IsStaggering() ? HitReactionStaggerRecoveryScale : 1.f;
	const float Recovery = RecoveryPerSecond * (1.f + Athletism / AedirAthletismRecoveryScale) * (1.f - AedirLegInjuryRecoveryPenalty * MaxLegInjury) * DazeRecoveryScale * StaggerRecoveryScale * FMath::Lerp(HealthyRecoveryShare, LowHealthRecoveryShare, GetHurtShare()) * FMath::Lerp(1.f, TiredRecoveryShare, GetTiredShare()) * HitRecoveryScale * DeltaTime;
	HitRecoveryScale = FMath::Min(HitRecoveryScale + (1.f - HitRecoveryScale) * FMath::Min(HitRecoveryReturnPerSecond * DeltaTime, 1.f) + 0.02f * DeltaTime, 1.f);
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
	// For the hit log: how long the balance took to come back after the last blow, from how low.
	if (RecoveryWatchSince >= 0.0)
	{
		RecoveryWatchLowest = FMath::Min(RecoveryWatchLowest, Balance);
		static const IConsoleVariable* WatchLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
		if (bIsRagdoll)
		{
			RecoveryWatchSince = -1.0;
		}
		else if (Balance >= 0.95f && GetWorld())
		{
			if (WatchLog && WatchLog->GetInt() != 0)
			{
				UE_LOG(LogAedir, Log, TEXT("[Balance] %s has its balance back %.2f s after the blow, from %.2f, %.0f cm from where it was struck"), *GetNameSafe(GetOwner()), GetWorld()->GetTimeSeconds() - RecoveryWatchSince, RecoveryWatchLowest,
					FVector::Dist2D(RecoveryWatchFrom, CharacterOwner->GetActorLocation()));
			}
			RecoveryWatchSince = -1.0;
		}
	}

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
	// Unable to catch itself, the body may go over at any moment: the further its weight is beyond the feet, or the less
	// balance it has left, the likelier; worn down, likelier still.
	// A lost balance brings the body down as its hips sink: once they are near the ground it goes over.
	const float SinkScale = CharacterOwner ? FMath::Max(static_cast<float>(CharacterOwner->GetActorScale3D().Z), 0.1f) : 1.f;
	const bool bTipsOver = !bIsRagdoll && !IsGettingUp() && SinkCm >= FallSinkCm * SinkScale;
	if (!bIsRagdoll && (bDead || bSenseless || bLegGivesWay || bTipsOver))
	{
		UE_LOG(LogAedir, Log, TEXT("[Fall] %s goes down: dead %d, senseless %d, leg %d, tipped over %d, balance %.2f, weight %.0f cm beyond the feet."), *GetNameSafe(GetOwner()), bDead ? 1 : 0, bSenseless ? 1 : 0, bLegGivesWay ? 1 : 0, bTipsOver ? 1 : 0, Balance, -SupportMargin);
		EnterRagdoll(MinRagdollSeconds);
		// Sunk down, it goes over the way it was going.
		const FVector Over = CharacterOwner ? CharacterOwner->GetActorTransform().TransformVectorNoScale(FVector(LeanDirection.X, LeanDirection.Y, 0.f)).GetSafeNormal2D() : FVector::ZeroVector;
		if (bTipsOver && !(Over.IsNearlyZero() && ComOffsetDirection.IsNearlyZero()))
		{
			MulticastTopple((Over.IsNearlyZero() ? ComOffsetDirection : Over) * (ComputeStaggerSpeed(200.f) * KnockdownToppleShare));
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
		// Told: the keys held towards the feet.
		const bool bTold = IsGetUpKeyHeld();
		if (bHoistGetUp && bHoistFinished)
		{
			// Hauled up onto its feet: it stands where it is, facing the way its hips do.
			GetUpStyle = EAedirGetUpStyle::None;
			bReadyToGetUp = false;
			bIsRagdoll = false;
			SetBalance(GetUpBalanceThreshold);
			SetRagdollPresentation(false);
			bHoistFinished = false;
		}
		else if (bHoistGetUp)
		{
			if (bHoisting && (bDead || bSenseless))
			{
				StopHoist(bDead ? TEXT("dead") : TEXT("senseless"));
			}
			else if (!bHoisting && bReadyToGetUp && (!bChooses || bTold))
			{
				StartHoist();
			}
		}
		else if (bReadyToGetUp && (!bChooses || bTold))
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
	// Part of the push lands at once, the rest keeps pushing over a moment after it.
	const FVector Push = Impulse * HitPushScale;
	// The blow itself comes first; the pushes on the chest and the trunk sent with it at the same moment are not new blows.
	if (const UWorld* World = GetWorld(); World && BodyMesh && Bone != NAME_None && BodyMesh->GetBoneIndex(Bone) != INDEX_NONE)
	{
		const double Now = World->GetTimeSeconds();
		// Sent from where the blow landed at the moment it did, the pushes on the chest and the trunk are that blow; one from
		// anywhere else, even at the same moment, is another blow.
		FAedirBlowLanded* Same = RecentBlows.Num() && Now - RecentBlows.Last().Time <= 0.05 && FVector::Dist(Location, RecentBlows.Last().World) < 1.f ? &RecentBlows.Last() : nullptr;
		if (!Same)
		{
			if (RecentBlows.Num() >= 4)
			{
				RecentBlows.RemoveAt(0);
			}
			RecentBlows.Add({ Bone, BodyMesh->GetBoneTransform(Bone).InverseTransformPosition(Location), Location, Now, static_cast<float>(Push.Size()) });
		}
		// The share of a blow on a shoulder sent on into the trunk is still that blow.
		else if (Bone.ToString().StartsWith(TEXT("spine")) && (Same->Bone.ToString().StartsWith(TEXT("clavicle")) || Same->Bone.ToString().StartsWith(TEXT("upperarm"))))
		{
			Same->Push += static_cast<float>(Push.Size());
		}
		// The more worn the body, the more and the longer a blow slackens it.
		const float Worn = GetWornShare();
		const float Size = FMath::Clamp(static_cast<float>(Push.Size()) / FMath::Max(BlowSoftFullPush, 1.f), 0.f, 1.f);
		const float Slack = FMath::Lerp(BlowSlackFitShare, BlowSlackWornShare, Worn) * Size;
		if (Slack > BlowSoftness || Now >= BlowSoftUntil)
		{
			BlowSoftness = FMath::Max(BlowSoftness, Slack);
		}
		// As long as the blow was hard: in proportion to its push, longer still for one past the full push.
		const float Hard = FMath::Clamp(static_cast<float>(Push.Size()) / FMath::Max(BlowSoftFullPush, 1.f), 0.f, FMath::Max(BlowSlackMaxScale, 1.f));
		BlowSoftUntil = FMath::Max(BlowSoftUntil, Now + FMath::Lerp(BlowSlackFitSeconds, BlowSlackWornSeconds, Worn) * Hard);
		static const IConsoleVariable* SlackLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
		if (SlackLog && SlackLog->GetInt() != 0 && Slack > 0.02f && Now >= NextSlackLogTime)
		{
			NextSlackLogTime = Now + 0.05;
			UE_LOG(LogAedir, Log, TEXT("[Slack] %s, worn %.2f, push %.0f: muscles %.0f%% softer for %.2f s"), *GetNameSafe(GetOwner()), Worn, Push.Size(), BlowSoftness * 100.f, BlowSoftUntil - Now);
		}
	}
	const float Now = HitPushSpreadSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(HitPushNowShare, 0.f, 1.f) : 1.f;
	LandPush(Bone, Push * Now, Location);
	if (Now < 1.f)
	{
		FPendingPush& Pending = PendingPushes.AddDefaulted_GetRef();
		Pending.Bone = Bone;
		Pending.Impulse = Push * (1.f - Now);
		Pending.Location = Location;
		Pending.Left = HitPushSpreadSeconds;
	}
	// The head is light and held by little: struck, it is knocked back on its neck the way the blow went, all of the push.
	const FString Name = Bone.ToString();
	if (CharacterOwner && !bIsRagdoll && BodyMesh && (Name.StartsWith(TEXT("head")) || Name.StartsWith(TEXT("neck"))))
	{
		const FTransform& Frame = CharacterOwner->GetActorTransform();
		const FVector Local = Frame.InverseTransformVectorNoScale(Push);
		const FVector Off = Frame.InverseTransformVectorNoScale(Location - BodyMesh->GetBoneLocation(TEXT("head")));
		HeadSwayVelocity.X -= static_cast<float>(Local.X) * HeadSwayDegreesPerSecondPerPush;
		HeadSwayVelocity.Y += static_cast<float>(Local.Y) * HeadSwayDegreesPerSecondPerPush;
		HeadSwayVelocity.Z += static_cast<float>(Off.X * Local.Y - Off.Y * Local.X) * HitSwayTwistPerPushCm * 4.f;
	}
	Flinch = FMath::Max(Flinch, FMath::Clamp(static_cast<float>(Push.Size()) / HitFlinchFullImpulse, 0.f, 1.f));
	MotorUpdateTimer = 0.f;
}

void UAedirBalanceComponent::LandPush(FName Bone, const FVector& Impulse, const FVector& Location)
{
	if (BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		BodyMesh->AddImpulseAtLocation(Impulse, Location, Bone);
	}
	// The trunk is bent the way the blow drove, and turned by one that struck off its middle, physics or not.
	if (CharacterOwner && !bIsRagdoll)
	{
		const FTransform& Frame = CharacterOwner->GetActorTransform();
		const FVector Push = Frame.InverseTransformVectorNoScale(Impulse);
		const FVector Off = Frame.InverseTransformPositionNoScale(Location);
		HitSwayVelocity.X -= static_cast<float>(Push.X) * HitSwayDegreesPerSecondPerPush;
		HitSwayVelocity.Y += static_cast<float>(Push.Y) * HitSwayDegreesPerSecondPerPush;
		HitSwayVelocity.Z += static_cast<float>(Off.X * Push.Y - Off.Y * Push.X) * HitSwayTwistPerPushCm;
	}
	PushHips(Impulse);
}

void UAedirBalanceComponent::UpdatePendingPushes(float DeltaTime)
{
	// Each blow still pushing gives what is left of it, more at first and less as it runs out.
	for (int32 Index = PendingPushes.Num() - 1; Index >= 0; --Index)
	{
		FPendingPush& Pending = PendingPushes[Index];
		const float Total = FMath::Max(HitPushSpreadSeconds, KINDA_SMALL_NUMBER);
		const float Before = Pending.Left / Total;
		Pending.Left = FMath::Max(Pending.Left - DeltaTime, 0.f);
		const float After = Pending.Left / Total;
		// Falling off: the share given so far is 1 - (left share) squared.
		const float Share = Before * Before - After * After;
		LandPush(Pending.Bone, Pending.Impulse * Share, Pending.Location);
		if (Pending.Left <= 0.f)
		{
			PendingPushes.RemoveAtSwap(Index);
		}
	}
}

void UAedirBalanceComponent::UpdateHitSway(float DeltaTime)
{
	if (bIsRagdoll || DeltaTime <= 0.f)
	{
		HitSway = HitSwayVelocity = FVector::ZeroVector;
		HeadSway = HeadSwayVelocity = FVector::ZeroVector;
		return;
	}
	// A damped spring back to straight, stepped in small steps so a long frame does not throw it.
	const float Omega = 2.f * UE_PI * FMath::Max(HitSwayHz, 0.1f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.008f), 1, 8);
	const float Step = DeltaTime / Steps;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		HitSwayVelocity += (-HitSway * (Omega * Omega) - HitSwayVelocity * (2.f * HitSwayDampingRatio * Omega)) * Step;
		HitSway += HitSwayVelocity * Step;
	}
	HitSway = HitSway.BoundToCube(HitSwayMaxDegrees);
	const float HeadOmega = 2.f * UE_PI * FMath::Max(HeadSwayHz, 0.1f);
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		HeadSwayVelocity += (-HeadSway * (HeadOmega * HeadOmega) - HeadSwayVelocity * (2.f * HeadSwayDampingRatio * HeadOmega)) * Step;
		HeadSway += HeadSwayVelocity * Step;
	}
	HeadSway = HeadSway.BoundToCube(HeadSwayMaxDegrees);
}

void UAedirBalanceComponent::UpdatePelvisLean(float DeltaTime)
{
	// Even a little lost balance shows in the hips: the lean grows quickly at first.
	const float Disturbance = FMath::Sqrt(FMath::Clamp(1.f - Balance, 0.f, 1.f));

	// Positive pitch raises the nose, moving the top of the pelvis toward -X; positive roll moves it toward +Y.
	// The lean therefore points opposite to the disturbance on both axes.
	const UAedirStatsComponent* Stats = FindStats();
	const float Tired = Stats ? Stats->GetFatigue() : 0.f;
	const float Hurt = Stats ? 1.f - FMath::Clamp(Stats->GetHealth() / FMath::Max(Stats->GetMaxHealth(), 1.f), 0.f, 1.f) : 0.f;
	const float Sway = bIsRagdoll ? 0.f : BalanceSwayDegrees + Tired * BalanceSwayTiredDegrees + Hurt * BalanceSwayHurtDegrees;
	SwayClock += DeltaTime * BalanceSwayRate;
	// The hips tip the way the balance is lost, so the body is seen going over that way as the feet hurry to catch it.
	const FVector2D Way = LeanDirection.GetSafeNormal();
	const double Pitch = -Way.X * Disturbance * MaxLeanDegrees + Sway * FMath::PerlinNoise1D(SwayClock + SwaySeed);
	const double Roll = Way.Y * Disturbance * MaxLeanDegrees + Sway * FMath::PerlinNoise1D(SwayClock * 1.3f + SwaySeed + 17.f);

	// And they turn towards the side it goes over to.
	double Yaw = Way.Y * Disturbance * LeanTwistDegrees;

	// The weight off the middle of the feet tips and turns them that way too, however it got there.
	double ComPitch = 0.0;
	double ComRoll = 0.0;
	if (bSupportGrounded && !bIsRagdoll && !IsGettingUp())
	{
		const float Off = static_cast<float>(ComDriftLocal.Size());
		const float Speed = CharacterOwner ? static_cast<float>(CharacterOwner->GetVelocity().Size2D()) : 0.f;
		const float Walking = FMath::Lerp(1.f, ComLeanWalkingShare, FMath::Clamp(Speed / FMath::Max(ComLeanWalkSpeed, 1.f), 0.f, 1.f));
		const float Degrees = FMath::Min(FMath::Max(Off - ComLeanFromCm, 0.f) * ComLeanDegreesPerCm, ComLeanMaxDegrees) * Walking;
		if (Degrees > 0.f)
		{
			const FVector2D ComWay = ComDriftLocal / Off;
			ComPitch = -ComWay.X * Degrees;
			ComRoll = ComWay.Y * Degrees;
			Yaw += ComWay.Y * Degrees * LeanTwistDegrees / FMath::Max(MaxLeanDegrees, 1.f);
		}
	}
	CurrentLean = FMath::RInterpTo(CurrentLean, FRotator(FMath::Clamp(Pitch + ComPitch, -MaxLeanDegrees, MaxLeanDegrees), Yaw, FMath::Clamp(Roll + ComRoll, -MaxLeanDegrees, MaxLeanDegrees)), DeltaTime, AedirLeanInterpSpeed);
}

void UAedirBalanceComponent::UpdateBalanceSink(float DeltaTime)
{
	// Down, getting up, or kicking (one leg holds it then), there is nothing to sink.
	if (bIsRagdoll || IsGettingUp() || DeltaTime <= 0.f)
	{
		SinkCm = 0.f;
		SinkSpeed = 0.f;
		SinkLastBalance = Balance;
		return;
	}
	const float Lost = FMath::Clamp((SinkFromBalance - Balance) / FMath::Max(SinkFromBalance, 0.01f), 0.f, 1.f);
	if (Lost > 0.f)
	{
		// The legs give: the hips drop the faster the longer they go, the more of the balance is gone. Coming back, the legs
		// take hold of the drop again.
		SinkSpeed += 981.f * SinkGravityShare * FMath::Pow(Lost, SinkLossPower) * DeltaTime;
		if (Balance > SinkLastBalance)
		{
			SinkSpeed *= FMath::Exp(-SinkRecoverBrake * DeltaTime);
		}
		SinkCm += SinkSpeed * DeltaTime;
	}
	else
	{
		SinkSpeed = 0.f;
		const float Scale = CharacterOwner ? FMath::Max(static_cast<float>(CharacterOwner->GetActorScale3D().Z), 0.1f) : 1.f;
		SinkCm = FMath::Max(SinkCm - SinkRiseSpeed * Scale * DeltaTime, 0.f);
	}
	SinkLastBalance = Balance;
	static const IConsoleVariable* SinkLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	const UWorld* World = GetWorld();
	if (SinkLog && SinkLog->GetInt() != 0 && SinkCm > 0.5f && World && World->GetTimeSeconds() >= NextSinkLogTime)
	{
		NextSinkLogTime = World->GetTimeSeconds() + 0.1;
		UE_LOG(LogAedir, Log, TEXT("[Sink] %s balance %.2f, hips sunk %.1f cm, going down at %.0f cm/s"), *GetNameSafe(GetOwner()), Balance, SinkCm, SinkSpeed);
	}
}

void UAedirBalanceComponent::UpdateStumbleWalk(float DeltaTime)
{
	UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	const UWorld* StumbleWorld = GetWorld();
	// Dying away after the blow that started it.
	const float Fade = StumbleWorld ? 1.f - FMath::Clamp(static_cast<float>(StumbleWorld->GetTimeSeconds() - StumbleBlowTime) / FMath::Max(StumbleWalkSeconds, 0.05f), 0.f, 1.f) : 0.f;
	// On one leg for a kick there is no hurried walk: the body goes over its standing foot as it is, not dragged on the way it went.
	// Just hauled up, the body settles where it stood up: walked off, the capsule would leave it behind.
	const UAedirCombatComponent* StumbleCombat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;
	const bool bOneLeg = StumbleCombat && StumbleCombat->IsKicking();
	bStumbling = Movement && !bIsRagdoll && !IsGettingUp() && HoistSettleLeft <= 0.f && !bOneLeg && Movement->IsMovingOnGround() && (Balance < StumbleWalkBelowBalance || StaggerSeconds > 0.f) && !LeanDirection.IsNearlyZero() && Fade > 0.f;
	if (!bStumbling)
	{
		return;
	}
	// Steered where the steering is: the player's own machine, or the server for anyone else.
	if (!CharacterOwner->IsLocallyControlled() && !(CharacterOwner->HasAuthority() && !CharacterOwner->IsPlayerControlled()))
	{
		return;
	}
	const float Share = FMath::Clamp((StumbleWalkBelowBalance - Balance) / FMath::Max(StumbleWalkBelowBalance, KINDA_SMALL_NUMBER), 0.f, 1.f);
	const FVector Way = CharacterOwner->GetActorTransform().TransformVectorNoScale(FVector(LeanDirection.X, LeanDirection.Y, 0.f)).GetSafeNormal2D();
	// At least this fast the way it goes over, whatever else the legs were doing; quicker than that it may go.
	const float Wanted = StumbleWalkSpeed * FMath::Lerp(StumbleWalkMinShare, 1.f, Share) * Fade;
	const float Along = static_cast<float>(FVector::DotProduct(Movement->Velocity, Way));
	// The legs walk it, so they are not braking against it, and are hurried on where they lag behind.
	CharacterOwner->AddMovementInput(Way, FMath::Clamp(Wanted / FMath::Max(Movement->GetMaxSpeed(), 1.f), 0.f, 1.f));
	if (Along < Wanted)
	{
		Movement->Velocity += Way * ((Wanted - Along) * FMath::Min(DeltaTime * 12.f, 1.f));
	}
	static const IConsoleVariable* HitLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	const UWorld* World = GetWorld();
	if (HitLog && HitLog->GetInt() != 0 && World && World->GetTimeSeconds() >= NextStumbleLogTime)
	{
		NextStumbleLogTime = World->GetTimeSeconds() + 0.1;
		UE_LOG(LogAedir, Log, TEXT("[Stumble] %s balance %.2f, walked %s at %.0f cm/s, hips %s"), *GetNameSafe(GetOwner()), Balance, *Way.ToCompactString(), CharacterOwner->GetVelocity().Size2D(), *CurrentLean.ToCompactString());
	}
}

void UAedirBalanceComponent::LiftLegsOutOfGround()
{
	UWorld* World = GetWorld();
	if (!World || !BodyMesh)
	{
		return;
	}
	// Standing, the feet are set on the ground by the pose and the legs pass through it, so their shapes sit a little in it.
	// The whole body is lifted by as much as any leg is in the ground the moment it meets the ground, or that leg is shot out.
	float Deepest = 0.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirLegsOutOfGround), false, GetOwner());
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		const FString Bone = Body && Body->GetBodySetup() ? Body->GetBodySetup()->BoneName.ToString() : FString();
		if (!(Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("foot")) || Bone.Contains(TEXT("ball"))))
		{
			continue;
		}
		const FBox Bounds = Body->GetBodyBounds();
		const FVector Middle = Bounds.GetCenter();
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(Middle.X, Middle.Y, Bounds.Max.Z + AedirLegGroundProbeCm), FVector(Middle.X, Middle.Y, Bounds.Min.Z - AedirLegGroundProbeCm), ECC_WorldStatic, Params))
		{
			Deepest = FMath::Max(Deepest, static_cast<float>(Hit.ImpactPoint.Z - Bounds.Min.Z));
		}
	}
	Deepest = FMath::Min(Deepest, AedirLegGroundLiftMaxCm);
	if (Deepest <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			FTransform Moved = Body->GetUnrealWorldTransform();
			Moved.AddToTranslation(FVector(0.f, 0.f, Deepest));
			Body->SetBodyTransform(Moved, ETeleportType::TeleportPhysics);
		}
	}
	UE_LOG(LogAedir, Log, TEXT("[Fall] %s's legs were %.1f cm in the ground going down and the body is lifted out of it."), *GetOwner()->GetName(), Deepest);
}

void UAedirBalanceComponent::SettleRagdollEntry(float DeltaTime)
{
	if (RagdollSettleLeft <= 0.f || !BodyMesh)
	{
		return;
	}
	RagdollSettleLeft = bIsRagdoll ? RagdollSettleLeft - DeltaTime : 0.f;
	CapRagdollEntrySpeeds();
}

void UAedirBalanceComponent::CapRagdollEntrySpeeds()
{
	// Going down, and for a moment after, nothing in the body rises faster than it did then, and no part of a leg moves or
	// spins faster against the body than a loose leg could: what the joints and the ground push back as they take hold of a
	// body caught mid-step does not throw it up or fling its legs.
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (!Body || !Body->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		FVector Velocity = Body->GetUnrealWorldVelocity();
		const FVector Was = Velocity;
		Velocity.Z = FMath::Min(Velocity.Z, static_cast<double>(RagdollRiseLimit));
		const FString Bone = Body->GetBodySetup() ? Body->GetBodySetup()->BoneName.ToString() : FString();
		const bool bLeg = Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("foot")) || Bone.Contains(TEXT("ball"));
		const FVector Own = Velocity - RagdollCarried;
		if (bLeg && Own.Size() > RagdollEntryLegSpeed)
		{
			Velocity = RagdollCarried + Own.GetSafeNormal() * RagdollEntryLegSpeed;
		}
		if (!Velocity.Equals(Was, 0.1))
		{
			Body->SetLinearVelocity(Velocity, false);
		}
		const FVector Spin = Body->GetUnrealWorldAngularVelocityInRadians();
		const float MaxSpin = FMath::DegreesToRadians(RagdollEntryLegSpinDegrees);
		if (bLeg && Spin.Size() > MaxSpin)
		{
			Body->SetAngularVelocityInRadians(Spin.GetSafeNormal() * MaxSpin, false);
		}
	}
}

void UAedirBalanceComponent::WatchFall(float DeltaTime)
{
	if (FallWatchLeft <= 0.f || !BodyMesh)
	{
		return;
	}
	FallWatchLeft -= DeltaTime;
	// How high the hips go above where they were when the body went down, and the fastest any part rises and moves.
	if (const FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		FallMaxRise = FMath::Max(FallMaxRise, static_cast<float>(Pelvis->GetUnrealWorldTransform().GetLocation().Z) - FallStartZ);
		if (Pelvis->GetUnrealWorldVelocity().Z > AedirFallFlyingSpeed)
		{
			const UAedirStatsComponent* FlyingStats = FindStats();
			// What the body is in or against just then.
			FString Touching;
			if (UWorld* FlyWorld = GetWorld())
			{
				TArray<FOverlapResult> Overlaps;
				FCollisionQueryParams FlyParams(SCENE_QUERY_STAT(AedirFlying), false, GetOwner());
				FlyWorld->OverlapMultiByObjectType(Overlaps, Pelvis->GetUnrealWorldTransform().GetLocation(), FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeSphere(60.f), FlyParams);
				for (const FOverlapResult& Overlap : Overlaps)
				{
					Touching += FString::Printf(TEXT(" %s/%s"), *GetNameSafe(Overlap.GetActor()), *GetNameSafe(Overlap.GetComponent()));
				}
			}
			UE_LOG(LogAedir, Log, TEXT("[Fall] %s's hips fly up at %.0f cm/s, %.2f s in: down %d, getting up %d (%.2f), dead %d, simulating %d, kinematic %d, mesh on capsule %d, near:%s"), *GetOwner()->GetName(), Pelvis->GetUnrealWorldVelocity().Z, AedirFallWatchSeconds - FallWatchLeft,
				bIsRagdoll ? 1 : 0, IsGettingUp() ? 1 : 0, GetGetUpProgress(), FlyingStats && FlyingStats->IsDead() ? 1 : 0, Pelvis->IsInstanceSimulatingPhysics() ? 1 : 0, Pelvis->IsInstanceSimulatingPhysics() ? 0 : 1, BodyMesh->GetAttachParent() == BodyCapsule ? 1 : 0, *Touching);
		}
	}
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body && Body->IsInstanceSimulatingPhysics())
		{
			const FVector Velocity = Body->GetUnrealWorldVelocity();
			FallMaxUpSpeed = FMath::Max(FallMaxUpSpeed, static_cast<float>(Velocity.Z));
			if (Velocity.Size() > FallMaxSpeed)
			{
				FallMaxSpeed = static_cast<float>(Velocity.Size());
				FallFastestBone = Body->GetBodySetup() ? Body->GetBodySetup()->BoneName : NAME_None;
				FallFastestAt = AedirFallWatchSeconds - FallWatchLeft;
			}
		}
	}
	if (FallWatchLeft <= 0.f)
	{
		UE_LOG(LogAedir, Log, TEXT("[Fall] %s went down at %.0f cm/s: the hips rose %.1f cm, fastest rise %.0f cm/s, fastest part %.0f cm/s (%s, %.2f s in)"), *GetOwner()->GetName(), FallEntrySpeed, FallMaxRise, FallMaxUpSpeed, FallMaxSpeed, *FallFastestBone.ToString(), FallFastestAt);
	}
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
	// Every part tells when it strikes something, so a hard landing hurts the part that took it.
	for (FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body)
		{
			Body->SetInstanceNotifyRBCollision(true);
		}
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
		bHoisting = false;
		bHoistFinished = false;
		HoistDrive = 0.f;
		HoistSettleLeft = 0.f;
		if (BodyControl)
		{
			BodyControl->SetHoist(0.f);
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
		// Standing, the legs went through the ground and through each other untouched; now they meet it, and a leg found in it
		// is let out slowly instead of shot out of it.
		for (FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (Body)
			{
				Body->SetMaxDepenetrationVelocity(RagdollEntryOutOfGroundSpeed);
			}
		}
		LiftLegsOutOfGround();
		RagdollSettleLeft = RagdollEntrySettleSeconds;
		RagdollRiseLimit = static_cast<float>(FMath::Max(Carried.Z, 0.0)) + RagdollEntryMaxRiseSpeed;
		RagdollCarried = Carried;
		CapRagdollEntrySpeeds();

		if (Movement)
		{
			Movement->DisableMovement();
		}

		BodyCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FallWatchLeft = AedirFallWatchSeconds;
		FallStartZ = Pelvis ? static_cast<float>(Pelvis->GetUnrealWorldTransform().GetLocation().Z) : 0.f;
		FallMaxRise = 0.f;
		FallMaxUpSpeed = 0.f;
		FallMaxSpeed = 0.f;
		FallEntrySpeed = static_cast<float>(Carried.Size());
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
			// Hauled up by the hips, it already stands: it faces the way its hips do, over where they are.
			FVector HipsFacing = FVector::CrossProduct(BodyMesh->GetBoneLocation(TEXT("thigh_r")) - BodyMesh->GetBoneLocation(TEXT("thigh_l")), FVector::UpVector).GetSafeNormal2D();
			if (HipsFacing.IsNearlyZero())
			{
				HipsFacing = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
			}
			const FVector Facing = bHoistFinished ? HipsFacing : bFromBack ? -Along : Along;
			const FVector StandXY = PelvisLocation + Facing * (bFromBack && !bHoistFinished ? GetUpFromBackStandCm * static_cast<float>(BodyMesh->GetComponentScale().Z) : 0.f);
			FVector Ground;
			if (!TraceStandingGround(StandXY + FVector(0.f, 0.f, 80.f), StandXY - FVector(0.f, 0.f, 200.f), Ground))
			{
				Ground = StandXY - FVector(0.f, 0.f, 10.f);
			}
			const FVector CapsuleLocation = Ground + FVector(0.f, 0.f, BodyCapsule->GetScaledCapsuleHalfHeight() + 1.f);
			CharacterOwner->SetActorLocationAndRotation(CapsuleLocation, FRotator(0.f, Facing.Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			IgnoreLooseThingsAtFeet();
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
					// The hips were held still a moment to put the mesh back on the capsule, and moving it left them a target
					// where the pose has them on it; the next step took them there, simulating or not, at 50 to 80 m/s, 20 to 30
					// cm at once. The target is set to where they lie.
					if (Body == Pelvis && Body->IsInstanceSimulatingPhysics())
					{
						Body->SetInstanceSimulatePhysics(false, false, true);
						Body->SetBodyTransform(Lying[Index], ETeleportType::None);
						Body->SetInstanceSimulatePhysics(true, false, true);
					}
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
		const float Tired = StatsComponent ? 1.f - FMath::Clamp(StatsComponent->GetStaminaFraction(), 0.f, 1.f) : 0.f;
		const float Hurt = StatsComponent ? 1.f - FMath::Clamp(StatsComponent->GetHealth() / FMath::Max(StatsComponent->GetMaxHealth(), 1.f), 0.f, 1.f) : 0.f;
		GetUpTotal = bHoistFinished ? 0.f : FMath::Lerp(GetUpSecondsMin, GetUpSecondsMax, FMath::Clamp(Tired * GetUpTiredShare + Hurt * GetUpHurtShare, 0.f, 1.f));
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s gets up in %.2f s (stamina gone %.2f, health gone %.2f), its feet at %.1f"), *GetOwner()->GetName(), GetUpTotal, Tired, Hurt, CharacterOwner->GetActorLocation().Z - BodyCapsule->GetScaledCapsuleHalfHeight());
		GetUpLeft = GetUpTotal;
		bHipsInitialized = false;
		// It sets out lifting itself with all it has.
		GetUpLift = 1.f;
		GetUpHipTrailMost = 0.f;
		GetUpWaitedSeconds = 0.f;
		GetUpHipTrailSum = 0.f;
		GetUpRiseSeconds = 0.f;
		GetUpLean = FVector2D::ZeroVector;
		if (UAedirGaitComponent* Gait = CharacterOwner->FindComponentByClass<UAedirGaitComponent>(); Gait && !bHoistFinished)
		{
			Gait->BeginGetUpPose();
		}
		// Hauled up already, the muscles take the body from how it stands into the standing pose, physical for a moment.
		if (BodyControl && bHoistFinished)
		{
			BodyControl->SetHoist(0.f);
			BodyControl->SetLimp(false);
			BodyControl->SetRising(false);
			HoistSettleLeft = HoistSettleSeconds;
			// The hips' and feet's hold comes in from nothing from this very step on, before the hips are tied to the capsule.
			BodyControl->SetHoldEase(0.f);
			BodyTeleportGrace = AedirBodyTeleportGraceSeconds * 2.f;
		}
		// The muscles come back and lift the body through the moves, the hips with them; a blow can still put it back down.
		else if (BodyControl)
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
		if (GetUpLeft > 0.f || HoistSettleLeft > 0.f)
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
	const FTransform MeshWas = BodyMesh->GetComponentTransform();
	BodyMesh->AttachToComponent(BodyCapsule, AedirMeshOnCapsuleRules);
	// Moved as a teleport: moved as a move, the held hips were left a target to go to where the pose had them on the capsule,
	// which the next step still took them to although they simulate again (see where they are put back as they lay).
	BodyMesh->SetRelativeTransform(MeshRelativeAtFall, false, nullptr, ETeleportType::TeleportPhysics);
	// The same relative transform is not applied again, while the world one was the hips': it is worked out anew.
	BodyMesh->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::TeleportPhysics);
	if (bSimulating)
	{
		Pelvis->SetInstanceSimulatePhysics(true, false, true);
	}
	// The mesh was left where the body fell, its bones going on with the body: put back on the capsule it moves at once,
	// though the head does not. Hair simulated against the mesh would take that move as a fling: it starts afresh instead.
	const FTransform MeshNow = BodyMesh->GetComponentTransform();
	if (FVector::Dist(MeshNow.GetLocation(), MeshWas.GetLocation()) > HairResetMeshMoveCm || MeshNow.GetRotation().AngularDistance(MeshWas.GetRotation()) > FMath::DegreesToRadians(20.f))
	{
		TArray<USceneComponent*> Children;
		BodyMesh->GetChildrenComponents(true, Children);
		for (USceneComponent* Child : Children)
		{
			if (UGroomComponent* Hair = Cast<UGroomComponent>(Child))
			{
				Hair->ResetSimulation();
			}
		}
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s: the mesh went %.0f cm back onto the capsule, the hair starts afresh."), *GetNameSafe(GetOwner()), FVector::Dist(MeshNow.GetLocation(), MeshWas.GetLocation()));
	}
}

float UAedirBalanceComponent::GetGetUpShare() const
{
	return GetUpLeft > 0.f && GetUpTotal > 0.f ? 1.f - FMath::SmoothStep(0.f, 1.f, 1.f - GetUpLeft / GetUpTotal) : 0.f;
}

void UAedirBalanceComponent::StartHoist()
{
	const FBodyInstance* Pelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	if (!Pelvis || !CharacterOwner)
	{
		return;
	}
	bHoisting = true;
	bHoistFinished = false;
	HoistDrive = 0.f;
	WidenKnees();
	HoistDoneFor = 0.f;
	HoistRestFor = 0.f;
	HoistSeconds = 0.f;
	HoistHighest = 0.f;
	GetUpLift = 1.f;
	HoistAnchor = Pelvis->GetUnrealWorldTransform().GetLocation();
	HoistHeldSeconds = 0.f;
	HoistHand = -1;
	HoistFreeHand = -1;
	HoistNeutralKey = GetUpKeyWay;
	HoistLeanLogged = 0.f;
	HoistLastStepFor = 0.f;
	HoistSelfLean = FVector::ZeroVector;
	HoistLeanSmoothed = FVector::ZeroVector;
	HoistLeadSide = FMath::RandBool() ? 1 : 0;
	HoistProgress = 0.f;
	HoistStyle = LyingStyle;
	HoistHeadward = (BodyMesh->GetBoneLocation(ChestBoneName) - HoistAnchor).GetSafeNormal2D();
	if (HoistHeadward.IsNearlyZero())
	{
		HoistHeadward = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	FVector Floor;
	HoistGroundZ = TraceStandingGround(HoistAnchor + FVector(0.f, 0.f, 20.f), HoistAnchor - FVector(0.f, 0.f, 300.f), Floor, false) ? static_cast<float>(Floor.Z) : static_cast<float>(HoistAnchor.Z) - 13.f;
	// Which way each palm faces from its hand bone: in the rest pose the palms face in towards the thighs, the normal of the
	// plane of the knuckles.
	if (const USkeletalMesh* Model = BodyMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& Bones = Model->GetRefSkeleton();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
			const int32 Hand = Bones.FindBoneIndex(FName(FString(TEXT("hand")) + Suffix));
			const int32 Middle = Bones.FindBoneIndex(FName(FString(TEXT("middle_metacarpal")) + Suffix));
			const int32 Index = Bones.FindBoneIndex(FName(FString(TEXT("index_metacarpal")) + Suffix));
			const int32 Pinky = Bones.FindBoneIndex(FName(FString(TEXT("pinky_metacarpal")) + Suffix));
			if (Hand == INDEX_NONE || Middle == INDEX_NONE || Index == INDEX_NONE || Pinky == INDEX_NONE)
			{
				continue;
			}
			const FTransform HandRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, Hand);
			const FVector Along = FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, Middle).GetLocation() - HandRef.GetLocation();
			const FVector Across = FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, Index).GetLocation() - FAnimationRuntime::GetComponentSpaceTransformRefPose(Bones, Pinky).GetLocation();
			FVector Normal = FVector::CrossProduct(Along, Across).GetSafeNormal();
			// In towards the middle of the body (the left hand lies on +X).
			if (Normal.X * (Side == 0 ? 1.f : -1.f) > 0.f)
			{
				Normal = -Normal;
			}
			HoistPalmLocal[Side] = HandRef.GetRotation().UnrotateVector(Normal);
		}
	}
	// Which way the face looks from the head bone: the body faces along the mesh's +Y in its rest pose.
	if (const USkeletalMesh* Model = BodyMesh->GetSkeletalMeshAsset())
	{
		const int32 Head = Model->GetRefSkeleton().FindBoneIndex(HeadBoneName);
		if (Head != INDEX_NONE)
		{
			HoistFaceLocal = FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), Head).GetRotation().UnrotateVector(FVector(0.f, 1.f, 0.f)).GetSafeNormal();
		}
	}
	UE_LOG(LogAedir, Log, TEXT("[GetUp] %s starts hauling itself up by the hips, lying %s."), *GetNameSafe(GetOwner()), LyingStyle == EAedirGetUpStyle::FromFront ? TEXT("face down") : TEXT("on its back"));
}

void UAedirBalanceComponent::LookWhileDown()
{
	// A body held by its muscles to the pose looks with the pose, which turns the neck and the waist about the chest.
	if (!bIsRagdoll || bHoisting || !BodyMesh || !CharacterOwner || !IsAuthoritative() || (BodyControl && BodyControl->IsReady()))
	{
		return;
	}
	const UAedirStatsComponent* StatsComponent = FindStats();
	if (StatsComponent && (StatsComponent->IsDead() || StatsComponent->IsDying() || StatsComponent->IsKnockedOut()))
	{
		return;
	}
	FBodyInstance* HeadBody = BodyMesh->GetBodyInstance(HeadBoneName);
	if (!HeadBody || !HeadBody->IsInstanceSimulatingPhysics())
	{
		return;
	}
	// A player looks where the camera does, anyone else at whom it fights.
	FVector Wanted = FVector::ZeroVector;
	if (CharacterOwner->IsPlayerControlled())
	{
		Wanted = CharacterOwner->GetControlRotation().Vector();
	}
	else if (const AAedirCreature* Self = Cast<AAedirCreature>(CharacterOwner); Self && Self->Combat && Self->Combat->GetAITarget())
	{
		Wanted = (Self->Combat->GetAITarget()->GetActorLocation() - HeadBody->GetUnrealWorldTransform().GetLocation()).GetSafeNormal();
	}
	if (Wanted.IsNearlyZero())
	{
		return;
	}
	const FVector Face = HeadBody->GetUnrealWorldTransform().GetRotation().RotateVector(HoistFaceLocal);
	const FVector Axis = FVector::CrossProduct(Face, Wanted);
	const float Angle = FMath::Atan2(static_cast<float>(Axis.Size()), static_cast<float>(FVector::DotProduct(Face, Wanted)));
	const float W = UE_TWO_PI * DownLookHz;
	// The neck turns the head against the chest, and the waist the chest against the hips: every turn is pushed off the
	// part below, so the body as a whole is not spun round on the ground like a top.
	FBodyInstance* ChestBody = BodyMesh->GetBodyInstance(TEXT("spine_05"));
	FBodyInstance* HipsBody = BodyMesh->GetBodyInstance(PelvisBoneName);
	const auto TurnAgainst = [](FBodyInstance* Part, FBodyInstance* Below, const FVector& AccelTorque)
	{
		// The torque is asked as an acceleration of the part turned (by its mean inertia); the part below takes as much back.
		const FVector Inertia = Part->GetBodyInertiaTensor();
		const FVector Torque = AccelTorque * static_cast<float>((Inertia.X + Inertia.Y + Inertia.Z) / 3.0);
		Part->AddTorqueInRadians(Torque, true, false);
		if (Below && Below->IsInstanceSimulatingPhysics())
		{
			Below->AddTorqueInRadians(-Torque, true, false);
		}
	};
	const FVector HeadRelative = HeadBody->GetUnrealWorldAngularVelocityInRadians() - (ChestBody ? ChestBody->GetUnrealWorldAngularVelocityInRadians() : FVector::ZeroVector);
	TurnAgainst(HeadBody, ChestBody, (Axis.GetSafeNormal() * (FMath::Min(Angle, FMath::DegreesToRadians(DownLookMaxDegrees)) * W * W) - HeadRelative * (2.f * W)) * DownLookShare);

	// The trunk turns from the waist after the head: the chest twisted about the spine towards where it looks, the belly less.
	if (ChestFaceLocal.IsNearlyZero())
	{
		if (const USkeletalMesh* Model = BodyMesh->GetSkeletalMeshAsset())
		{
			const int32 ChestIndex = Model->GetRefSkeleton().FindBoneIndex(TEXT("spine_05"));
			ChestFaceLocal = ChestIndex != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), ChestIndex).GetRotation().UnrotateVector(FVector(0.f, 1.f, 0.f)).GetSafeNormal() : FVector(0.f, 1.f, 0.f);
		}
	}
	const FVector SpineAxis = (BodyMesh->GetBoneLocation(TEXT("neck_01")) - BodyMesh->GetBoneLocation(TEXT("spine_01"))).GetSafeNormal();
	if (ChestBody && ChestBody->IsInstanceSimulatingPhysics() && !SpineAxis.IsNearlyZero())
	{
		const FVector ChestFace = ChestBody->GetUnrealWorldTransform().GetRotation().RotateVector(ChestFaceLocal);
		const FVector FaceFlat = (ChestFace - SpineAxis * FVector::DotProduct(ChestFace, SpineAxis)).GetSafeNormal();
		const FVector WantFlat = (Wanted - SpineAxis * FVector::DotProduct(Wanted, SpineAxis)).GetSafeNormal();
		if (!FaceFlat.IsNearlyZero() && !WantFlat.IsNearlyZero())
		{
			const float Twist = FMath::Atan2(static_cast<float>(FVector::DotProduct(FVector::CrossProduct(FaceFlat, WantFlat), SpineAxis)), static_cast<float>(FVector::DotProduct(FaceFlat, WantFlat)));
			const float Most = FMath::DegreesToRadians(DownTwistMaxDegrees);
			const float Turn = FMath::Clamp(Twist, -Most, Most);
			for (const TPair<const TCHAR*, float>& Part : { TPair<const TCHAR*, float>(TEXT("spine_05"), 1.f), TPair<const TCHAR*, float>(TEXT("spine_03"), DownTwistBellyShare) })
			{
				if (FBodyInstance* Piece = BodyMesh->GetBodyInstance(Part.Key); Piece && Piece->IsInstanceSimulatingPhysics())
				{
					const FVector HipsSpin = HipsBody ? HipsBody->GetUnrealWorldAngularVelocityInRadians() : FVector::ZeroVector;
					const float Spin = static_cast<float>(FVector::DotProduct(Piece->GetUnrealWorldAngularVelocityInRadians() - HipsSpin, SpineAxis));
					TurnAgainst(Piece, HipsBody, SpineAxis * ((Turn * W * W - Spin * 2.f * W) * DownTwistShare * Part.Value));
				}
			}
		}
	}
}

void UAedirBalanceComponent::HoldHurtWhileDown(float DeltaTime)
{
	const UAedirStatsComponent* StatsComponent = FindStats();
	const bool bAwake = !(StatsComponent && (StatsComponent->IsDead() || StatsComponent->IsDying() || StatsComponent->IsKnockedOut()));
	if (!bIsRagdoll || bHoisting || !bAwake || !BodyMesh || !CharacterOwner || !IsAuthoritative())
	{
		DownClutchLeft[0] = DownClutchLeft[1] = 0.f;
		DownBlowSeen = RecentBlows.Num() ? RecentBlows.Last().Time : DownBlowSeen;
		return;
	}
	const AAedirCreature* Self = Cast<AAedirCreature>(CharacterOwner);
	const UAedirFistArms* Fists = CharacterOwner->FindComponentByClass<UAedirFistArms>();
	const auto Busy = [&](int32 Side)
	{
		const EAedirHand Hand = Side == 0 ? EAedirHand::Left : EAedirHand::Right;
		return (Fists && Fists->IsHeld(Hand)) || (Self && Self->Interaction && Self->Interaction->GetHeldItem(Hand)) || (Self && Self->Wounds && Self->Wounds->HasUselessArm(Side == 0));
	};
	// A new blow hard enough: the hand that reaches it goes there, the head and the thighs by their own side, the chest, the
	// neck, a shoulder or an arm by the other.
	const FVector BodyRight = (BodyMesh->GetBoneLocation(TEXT("thigh_r")) - BodyMesh->GetBoneLocation(TEXT("thigh_l"))).GetSafeNormal();
	const FVector Middle = BodyMesh->GetBoneLocation(TEXT("spine_03"));
	for (const FAedirBlowLanded& Blow : RecentBlows)
	{
		if (Blow.Time <= DownBlowSeen)
		{
			continue;
		}
		DownBlowSeen = Blow.Time;
		const FString Name = Blow.Bone.ToString();
		if (Blow.Push < DownClutchMinPush || Name.StartsWith(TEXT("hand")) || Name.StartsWith(TEXT("lowerarm")) || Name.StartsWith(TEXT("calf")) || Name.StartsWith(TEXT("foot")) || BodyMesh->GetBoneIndex(Blow.Bone) == INDEX_NONE)
		{
			continue;
		}
		const int32 PointSide = FVector::DotProduct(Blow.World - Middle, BodyRight) >= 0.0 ? 1 : 0;
		const bool bOwnSide = Name.StartsWith(TEXT("head")) || Name.StartsWith(TEXT("thigh")) || Name.StartsWith(TEXT("pelvis"));
		int32 Side = bOwnSide ? PointSide : 1 - PointSide;
		if (Busy(Side))
		{
			Side = 1 - Side;
		}
		if (Busy(Side) || ((Name.StartsWith(TEXT("upperarm")) || Name.StartsWith(TEXT("clavicle"))) && Name.EndsWith(Side == 0 ? TEXT("_l") : TEXT("_r"))))
		{
			continue;
		}
		const float Hard = FMath::Clamp((Blow.Push - DownClutchMinPush) / FMath::Max(DownClutchFullPush - DownClutchMinPush, 1.f), 0.f, 1.f);
		DownClutchBone[Side] = Blow.Bone;
		DownClutchLocal[Side] = Blow.Local;
		DownClutchLeft[Side] = FMath::Lerp(DownClutchMinSeconds, DownClutchMaxSeconds, Hard);
		UE_LOG(LogAedir, Log, TEXT("[Clutch] %s lying down holds its %s hand to %s for %.1f s"), *GetNameSafe(GetOwner()), Side == 0 ? TEXT("left") : TEXT("right"), *Blow.Bone.ToString(), DownClutchLeft[Side]);
	}
	const float Gravity = GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) : 980.f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		DownClutchLeft[Side] = Busy(Side) ? 0.f : FMath::Max(DownClutchLeft[Side] - DeltaTime, 0.f);
		const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
		FBodyInstance* HandBody = BodyMesh->GetBodyInstance(FName(FString(TEXT("hand")) + Suffix));
		if (DownClutchLeft[Side] <= 0.f || !HandBody || !HandBody->IsInstanceSimulatingPhysics() || BodyMesh->GetBoneIndex(DownClutchBone[Side]) == INDEX_NONE)
		{
			continue;
		}
		// Held on the skin over the place, the palm on it, and let go of gently at the end.
		const FTransform BoneWorld = BodyMesh->GetBoneTransform(DownClutchBone[Side]);
		const FVector Point = BoneWorld.TransformPosition(DownClutchLocal[Side]);
		const FVector Out = (Point - BoneWorld.GetLocation()).GetSafeNormal();
		const FVector Spot = Point + Out * (5.f * static_cast<float>(BodyMesh->GetComponentScale().Z));
		const float Share = FMath::Clamp(DownClutchLeft[Side] / 0.4f, 0.f, 1.f);
		const float K = DownClutchSpringPerSecondSq;
		const FVector HandAt = HandBody->GetUnrealWorldTransform().GetLocation();
		const FVector Accel = ((Spot - HandAt) * K - HandBody->GetUnrealWorldVelocity() * (2.f * FMath::Sqrt(K)) + FVector(0.f, 0.f, Gravity * 0.5f)) * Share;
		float ArmMass = 0.f;
		for (const TCHAR* Part : { TEXT("hand"), TEXT("lowerarm"), TEXT("upperarm") })
		{
			if (const FBodyInstance* Piece = BodyMesh->GetBodyInstance(FName(FString(Part) + Suffix)))
			{
				ArmMass += Piece->GetBodyMass();
			}
		}
		// The arm pulls the hand there off the body it holds: the place held takes the pull back, or the hand dragged the
		// whole body round on the ground after it.
		const FVector Pull = Accel.GetClampedToMaxSize(3000.f) * FMath::Max(ArmMass, HandBody->GetBodyMass());
		HandBody->AddForce(Pull, true, false);
		FBodyInstance* Held = BodyMesh->GetBodyInstance(DownClutchBone[Side]);
		FBodyInstance* Shoulder = BodyMesh->GetBodyInstance(FName(FString(TEXT("upperarm")) + Suffix));
		if (Held && Held->IsInstanceSimulatingPhysics() && Held != HandBody)
		{
			Held->AddForceAtPosition(-Pull * 0.5f, Point, true, false);
		}
		if (Shoulder && Shoulder->IsInstanceSimulatingPhysics())
		{
			Shoulder->AddForce(-Pull * (Held && Held != HandBody ? 0.5f : 1.f), true, false);
		}
	}
}

void UAedirBalanceComponent::StopHoist(const TCHAR* Why)
{
	if (!bHoisting)
	{
		return;
	}
	bHoisting = false;
	HoistDrive = 0.f;
	if (BodyControl)
	{
		BodyControl->SetHoist(0.f);
	}
	UE_LOG(LogAedir, Log, TEXT("[GetUp] %s stops hauling itself up (%s) after %.2f s, the hips at most %.0f%% of the way up."), *GetNameSafe(GetOwner()), Why, HoistSeconds, HoistHighest * 100.f);
}

bool UAedirBalanceComponent::IsGetUpKeyHeld() const
{
	static const IConsoleVariable* AutoGetUp = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.AutoGetUp"));
	const bool bChooses = bPlayerChoosesGetUp && CharacterOwner && CharacterOwner->IsPlayerControlled() && !(AutoGetUp && AutoGetUp->GetInt() != 0);
	if (!bChooses)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - GetUpRequestTime > AedirGetUpRequestHoldSeconds || !BodyMesh)
	{
		return false;
	}
	// Once on the way up any of the walking keys keep it going: off the way they were first held, they lean the body back
	// into balance.
	if (bHoisting && HoistHeldSeconds > 0.f)
	{
		return true;
	}
	// Towards the feet: from the chest to the hips, as the body lies (or as it lay when it started getting up).
	const FVector Feetward = bHoisting ? -HoistHeadward : (BodyMesh->GetBoneLocation(PelvisBoneName) - BodyMesh->GetBoneLocation(ChestBoneName)).GetSafeNormal2D();
	return !Feetward.IsNearlyZero() && FVector::DotProduct(GetUpKeyWay.GetSafeNormal2D(), Feetward) >= GetUpKeyFeetCosine;
}

const TArray<FAedirGetUpStage>& UAedirBalanceComponent::HoistStages() const
{
	return (bHoisting ? HoistStyle : LyingStyle) == EAedirGetUpStyle::FromFront ? FrontGetUpStages : BackGetUpStages;
}

FAedirGetUpStage UAedirBalanceComponent::SampleHoistStage(float Progress) const
{
	const TArray<FAedirGetUpStage>& Stages = HoistStages();
	if (Stages.Num() == 0)
	{
		return FAedirGetUpStage();
	}
	const int32 From = FMath::Clamp(FMath::FloorToInt(Progress), 0, Stages.Num() - 1);
	const int32 To = FMath::Min(From + 1, Stages.Num() - 1);
	const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Progress - From, 0.f, 1.f));
	const FAedirGetUpStage& A = Stages[From];
	const FAedirGetUpStage& B = Stages[To];
	FAedirGetUpStage Out;
	Out.HipHeightCm = FMath::Lerp(A.HipHeightCm, B.HipHeightCm, Alpha);
	Out.HipBackCm = FMath::Lerp(A.HipBackCm, B.HipBackCm, Alpha);
	Out.TrunkDegrees = FMath::Lerp(A.TrunkDegrees, B.TrunkDegrees, Alpha);
	Out.HipBend = FMath::Lerp(A.HipBend, B.HipBend, Alpha);
	Out.KneeBend = FMath::Lerp(A.KneeBend, B.KneeBend, Alpha);
	Out.BackBow = FMath::Lerp(A.BackBow, B.BackBow, Alpha);
	Out.ArmRaise = FMath::Lerp(A.ArmRaise, B.ArmRaise, Alpha);
	Out.ElbowBend = FMath::Lerp(A.ElbowBend, B.ElbowBend, Alpha);
	Out.Hands = FMath::Lerp(A.Hands, B.Hands, Alpha);
	Out.Legs = FMath::Lerp(A.Legs, B.Legs, Alpha);
	Out.Seconds = B.Seconds;
	Out.Lead = FMath::Lerp(A.Lead, B.Lead, Alpha);
	// A step that gives the forward leg no bends of its own bends it as the other.
	const auto LeadHip = [](const FAedirGetUpStage& S) { return S.Lead > 0.f || S.LeadHipBend != 0.f ? S.LeadHipBend : S.HipBend; };
	const auto LeadKnee = [](const FAedirGetUpStage& S) { return S.Lead > 0.f || S.LeadKneeBend != 0.f ? S.LeadKneeBend : S.KneeBend; };
	Out.LeadHipBend = FMath::Lerp(LeadHip(A), LeadHip(B), Alpha);
	Out.LeadKneeBend = FMath::Lerp(LeadKnee(A), LeadKnee(B), Alpha);
	Out.LeadFootAheadCm = FMath::Lerp(A.LeadFootAheadCm, B.LeadFootAheadCm, Alpha);
	return Out;
}

bool UAedirBalanceComponent::GetHoistStage(FAedirGetUpStage& OutStage, float& OutProgress) const
{
	if (!bHoisting)
	{
		return false;
	}
	OutStage = SampleHoistStage(HoistProgress);
	OutProgress = HoistProgress;
	return true;
}

void UAedirBalanceComponent::UpdateHoist(float FrameSeconds)
{
	if (!bHoisting || !bIsRagdoll || !IsAuthoritative() || !BodyMesh || !CharacterOwner || FrameSeconds <= 0.f)
	{
		return;
	}
	// A long frame goes on as a short one: the pulls are forces, and the keys and the lift are read as they are.
	const float DeltaTime = FMath::Min(FrameSeconds, AedirBodyHitchSeconds);
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);
	const TArray<FAedirGetUpStage>& Stages = HoistStages();
	if (!Pelvis || !Pelvis->IsInstanceSimulatingPhysics() || Stages.Num() < 2)
	{
		StopHoist(TEXT("no hips"));
		return;
	}
	const UWorld* World = GetWorld();
	const bool bFront = HoistStyle == EAedirGetUpStyle::FromFront;
	const bool bHeld = IsGetUpKeyHeld();

	// Tired or hurt, the steps come the slower, the springs are the weaker and the lift the blows took comes back the slower.
	const UAedirStatsComponent* StatsComponent = FindStats();
	const float Tired = StatsComponent ? 1.f - FMath::Clamp(StatsComponent->GetStaminaFraction(), 0.f, 1.f) : 0.f;
	const float Hurt = StatsComponent ? 1.f - FMath::Clamp(StatsComponent->GetHealth() / FMath::Max(StatsComponent->GetMaxHealth(), 1.f), 0.f, 1.f) : 0.f;
	const float Strength = FMath::Lerp(1.f, HoistWeakestShare, FMath::Clamp(Tired * GetUpTiredShare + Hurt * GetUpHurtShare, 0.f, 1.f));
	GetUpLift = FMath::Min(GetUpLift + GetUpLiftRecoverPerSecond * Strength * DeltaTime, 1.f);
	HoistSeconds += DeltaTime;

	const FVector Up = FVector::UpVector;
	const float Scale = static_cast<float>(BodyMesh->GetComponentScale().Z);
	const float Stand = FMath::Max(GetPelvisStandHeight(), 10.f);
	const float Size = Stand / 95.f;
	const FVector Hips = Pelvis->GetUnrealWorldTransform().GetLocation();
	const FVector HipsSpeed = Pelvis->GetUnrealWorldVelocity();
	FVector Ground;
	if (!TraceStandingGround(Hips + FVector(0.f, 0.f, 20.f), Hips - FVector(0.f, 0.f, 300.f), Ground, false))
	{
		Ground = FVector(Hips.X, Hips.Y, HoistGroundZ);
	}
	const float Height = static_cast<float>(Hips.Z - Ground.Z);
	HoistHighest = FMath::Max(HoistHighest, Height / Stand);
	// Someone standing walks into the body getting up: their capsule comes up against any part of it, its legs as well.
	if (World)
	{
		static const FName Parts[] = { TEXT("pelvis"), TEXT("spine_03"), TEXT("spine_05"), TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r") };
		for (TActorIterator<AAedirCreature> It(World); It; ++It)
		{
			const AAedirCreature* Other = *It;
			const UCapsuleComponent* OtherCapsule = Other && Other != CharacterOwner ? Other->GetCapsuleComponent() : nullptr;
			if (!OtherCapsule || (Other->Balance && Other->Balance->IsRagdoll()) || FVector::Dist2D(Other->GetActorLocation(), Hips) > 250.f)
			{
				continue;
			}
			const float Reach = OtherCapsule->GetScaledCapsuleRadius() + 8.f * Size;
			const FVector At = Other->GetActorLocation();
			FName Nearest = NAME_None;
			float NearestCm = UE_BIG_NUMBER;
			for (const FName& Part : Parts)
			{
				const FVector PartAt = BodyMesh->GetBoneIndex(Part) != INDEX_NONE ? BodyMesh->GetBoneLocation(Part) : Hips;
				const float Off = static_cast<float>(FVector::Dist2D(At, PartAt));
				if (Off < NearestCm && At.Z - OtherCapsule->GetScaledCapsuleHalfHeight() < PartAt.Z + 10.f)
				{
					NearestCm = Off;
					Nearest = Part;
				}
			}
			static const IConsoleVariable* NearLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
			UE_CLOG(NearLog && NearLog->GetInt() != 0 && GFrameCounter % 30 == 0, LogAedir, Log, TEXT("[GetUp] %s getting up has %s %.0f cm from its %s (touching from %.0f), %.0f cm up, going %.0f cm/s"), *GetNameSafe(GetOwner()), *GetNameSafe(Other),
				NearestCm, *Nearest.ToString(), Reach, At.Z - OtherCapsule->GetScaledCapsuleHalfHeight() - Ground.Z, Other->GetVelocity().Size2D());
			if (NearestCm < Reach)
			{
				BumpedGettingUp(Other, BodyMesh->GetBoneLocation(Nearest), Nearest);
			}
		}
	}
	const FVector Headward = HoistHeadward;
	const FVector Right = FVector::CrossProduct(Up, Headward).GetSafeNormal();
	// The steps start from where the body lies when they begin: until then it may still be sliding or rolling to a stop.
	if (HoistProgress <= 0.f)
	{
		HoistAnchor = Hips;
	}

	// Where the step has the hips, and how far behind it they are.
	const auto HipsAt = [&](const FAedirGetUpStage& Stage)
	{
		const FVector Flat = FVector(HoistAnchor.X, HoistAnchor.Y, 0.f) - Headward * (Stage.HipBackCm * Size);
		return FVector(Flat.X, Flat.Y, Ground.Z + Stage.HipHeightCm * Size);
	};
	const float Last = static_cast<float>(Stages.Num() - 1);
	HoistTrailLogged = static_cast<float>(FVector::Dist(Hips, HipsAt(SampleHoistStage(HoistProgress))));

	// Held, the head comes up, then the hands go down, and once both are down the steps go on, as fast as the hips keep up
	// with them. Let go, the steps go back, slower, and back on the ground the pull fades and the body lies down again.
	const float HandsDownAt = (HoistHandDelaySeconds + HoistHandReachSeconds + HoistSecondHandLateSeconds) / FMath::Max(HoistPace, 0.25f);
	const int32 Segment = FMath::Clamp(FMath::FloorToInt(HoistProgress), 0, Stages.Num() - 2);
	const float SegmentSeconds = FMath::Max(Stages[Segment + 1].Seconds, 0.05f);
	if (bHeld)
	{
		HoistHeldSeconds += DeltaTime;
		HoistDrive = FMath::FInterpConstantTo(HoistDrive, 1.f, DeltaTime, 1.f / FMath::Max(HoistRampSeconds, 0.01f));
		if (HoistHeldSeconds >= HandsDownAt)
		{
			const float Keeping = 1.f - FMath::SmoothStep(HoistTrailFreeCm * Size, FMath::Max(HoistTrailStopCm, HoistTrailFreeCm + 1.f) * Size, HoistTrailLogged);
			const float Next = FMath::Min(HoistProgress + DeltaTime * HoistPace / SegmentSeconds * Strength * GetUpLift * Keeping, Last);
			// Each step is reached before the next begins: the body comes into it (the hips within HoistStepArriveCm of
			// where it has them) and only then goes on, or after HoistStepWaitSeconds at most if it cannot come closer.
			const float StepAt = FMath::FloorToFloat(Next);
			const bool bCrossing = StepAt > FMath::FloorToFloat(HoistProgress) && StepAt < Last;
			HoistStepWaited = bCrossing ? HoistStepWaited + DeltaTime : 0.f;
			HoistProgress = bCrossing && HoistTrailLogged > HoistStepArriveCm * Size && HoistStepWaited < HoistStepWaitSeconds ? StepAt - 0.001f : Next;
		}
	}
	else
	{
		HoistProgress = FMath::Max(HoistProgress - DeltaTime / SegmentSeconds * GetUpSinkPace, 0.f);
		if (HoistProgress <= 0.f)
		{
			HoistDrive = FMath::FInterpConstantTo(HoistDrive, 0.f, DeltaTime, 1.f / FMath::Max(HoistRampSeconds, 0.01f));
			HoistHeldSeconds = FMath::Max(HoistHeldSeconds - DeltaTime, 0.f);
		}
	}
	if (BodyControl)
	{
		BodyControl->SetHoist(HoistDrive);
	}
	const FAedirGetUpStage Stage = SampleHoistStage(HoistProgress);
	const FVector HipsWanted = HipsAt(Stage);
	// Until the hands are down the hips are left where they lie and only the head and the hands move.
	const float Lift = FMath::Clamp((HoistHeldSeconds - HandsDownAt) / FMath::Max(HoistRampSeconds, 0.01f), 0.f, 1.f);

	// The whole weight of the body is hauled at the hips.
	float Mass = 0.f;
	float UpperMass = 0.f;
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (Body && Body->IsInstanceSimulatingPhysics())
		{
			Mass += Body->GetBodyMass();
			const FName Bone = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
			if (Bone != NAME_None && BodyMesh->BoneIsChildOf(Bone, TEXT("spine_02")))
			{
				UpperMass += Body->GetBodyMass();
			}
		}
	}
	const float Gravity = World ? FMath::Abs(World->GetGravityZ()) : 980.f;
	const float Pull = HoistDrive * GetUpLift * Lift;
	const float WantedHeight = static_cast<float>(HipsWanted.Z - Ground.Z);
	// Above where the step has them the hips are carried the less, down to none HoistCarryEaseCm above: the body sits down
	// into the step under its own weight instead of hanging from the pull.
	const float Carry = Gravity * HoistWeightShare * FMath::Clamp(1.f - (Height - WantedHeight) / FMath::Max(HoistCarryEaseCm * Size, 1.f), 0.f, 1.f);
	// Too high, the hips are drawn down into the step as well, as the legs fold under them (sitting back on the heels).
	const float UpAccel = FMath::Clamp(Pull * (Carry + HoistSpringPerSecondSq * Strength * (WantedHeight - Height)) - Pull * HoistDampingPerSecond * Strength * static_cast<float>(HipsSpeed.Z), -HoistMaxExtraAccel, Carry + HoistMaxExtraAccel);
	const FVector Over = HipsWanted - Hips;
	const float AcrossK = HoistAcrossSpringPerSecondSq * Strength;
	const FVector AcrossAccel = (FVector(Over.X, Over.Y, 0.f) * AcrossK - FVector(HipsSpeed.X, HipsSpeed.Y, 0.f) * (2.f * FMath::Sqrt(AcrossK))) * Pull;

	// The way the step has the trunk: flat towards where the head lay, up, and on over the feet.
	const float TrunkRadians = FMath::DegreesToRadians(Stage.TrunkDegrees);
	const FVector Trunk = (Headward * FMath::Cos(TrunkRadians) + Up * FMath::Sin(TrunkRadians)).GetSafeNormal();
	// The chest is drawn out from the hips that way, carrying part of the weight above them; what it is lifted by is taken
	// off what the hips are, so the body as a whole is lifted no more than the hips ask.
	FBodyInstance* Chest = nullptr;
	for (const TCHAR* Name : { TEXT("spine_05"), TEXT("spine_04"), TEXT("spine_03") })
	{
		Chest = Chest ? Chest : BodyMesh->GetBodyInstance(Name);
	}
	FVector ChestForce = FVector::ZeroVector;
	// How steady the body is on what it has on the ground: as much as its lift and strength. The less, the more the weight
	// of a chest leaning off its step draws it on over, and the less the trunk is held up; the keys lean it back.
	const float Steady = FMath::Clamp(Strength * GetUpLift, 0.f, 1.f);
	FVector Lean = FVector::ZeroVector;
	FVector Balancing = FVector::ZeroVector;
	if (Chest && Chest->IsInstanceSimulatingPhysics() && UpperMass > 0.f)
	{
		const FVector ChestAt = Chest->GetUnrealWorldTransform().GetLocation();
		const FVector ChestWanted = Hips + Trunk * (ChestAboveHips * Scale);
		// Off the step: the trunk tipped from the way the step has it, and the hips gone aside from where it has them. On the
		// hands the body cannot tip; it leans only as it comes up off them.
		const FVector HipsAside = Right * FVector::DotProduct(Hips - HipsWanted, Right);
		const float Unsupported = 1.f - FMath::Clamp(Stage.Hands * 2.f, 0.f, 1.f);
		Lean = FVector(ChestAt.X - ChestWanted.X + HipsAside.X, ChestAt.Y - ChestWanted.Y + HipsAside.Y, 0.f) * Unsupported;
		HoistLeanSmoothed = FMath::VInterpTo(HoistLeanSmoothed, Lean, DeltaTime, 8.f);
		const float ChestHeight = FMath::Max(static_cast<float>(ChestAt.Z - Ground.Z), 20.f * Size);
		const FVector Tip = Lean * (Gravity / ChestHeight * HoistTipShare * (1.f - Steady) * Lift);
		// The keys held off the way they first were lean the body that way; a body nobody plays leans back from its lean
		// a moment after it starts, as it feels it go.
		FVector Keys = FVector::ZeroVector;
		if (CharacterOwner->IsPlayerControlled())
		{
			Keys = bHeld ? (GetUpKeyWay - HoistNeutralKey).GetClampedToMaxSize(1.0) : FVector::ZeroVector;
		}
		else
		{
			HoistSelfLean = FMath::VInterpTo(HoistSelfLean, -HoistLeanSmoothed / FMath::Max(10.f * Size, 1.f), DeltaTime, 4.f);
			Keys = HoistSelfLean.GetClampedToMaxSize(1.0) * HoistSelfBalanceShare;
		}
		Balancing = Keys * (HoistKeyBalanceAccel * HoistDrive * Lift);
		const float ChestK = HoistChestSpringPerSecondSq * Strength * FMath::Lerp(HoistUnsteadyHoldShare, 1.f, Steady);
		const FVector ChestAccel = ((ChestWanted - ChestAt) * ChestK - (Chest->GetUnrealWorldVelocity() - HipsSpeed) * (2.f * FMath::Sqrt(ChestK))) * Pull + FVector(0.f, 0.f, Gravity * HoistChestCarryShare * Pull) + Tip + Balancing;
		ChestForce = ChestAccel.GetClampedToMaxSize(Gravity * HoistWeightShare + HoistMaxExtraAccel + HoistKeyBalanceAccel) * UpperMass;
		Chest->AddForce(ChestForce, true, false);
	}
	Pelvis->AddForce((AcrossAccel * FMath::Lerp(HoistUnsteadyHoldShare, 1.f, Steady) + Balancing * 0.5f + FVector(0.f, 0.f, UpAccel)) * Mass - FVector(0.f, 0.f, FMath::Max(ChestForce.Z, 0.0)), true, false);
	const float LeanCm = static_cast<float>(Lean.Size());
	if (LeanCm > HoistLeanLogged + 6.f * Size)
	{
		HoistLeanLogged = LeanCm;
		UE_CLOG(Steady < HoistTipUnsteady, LogAedir, Log, TEXT("[GetUp] %s leans %.0f cm off its step (steady %.2f, lift %.2f), leaning back at %.0f cm/s2."), *GetNameSafe(GetOwner()), LeanCm, Steady, GetUpLift, Balancing.Size());
	}
	// Leaning over past what it can hold, unsteady, it goes over; steady, a trunk still coming round to its step is no lean.
	if (LeanCm > HoistTipOverCm * Size && Lift > 0.5f && Steady < HoistTipUnsteady)
	{
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s leans %.0f cm off its step and goes over."), *GetNameSafe(GetOwner()), LeanCm);
		GetUpLift = 0.f;
		StopHoist(TEXT("tipped over"));
		return;
	}

	// The trunk is turned the way of the step about the hips, the chest more than the belly.
	const float W = UE_TWO_PI * HoistUprightHz * FMath::Sqrt(Strength);
	const auto Align = [&](const FName& BodyName, const FName& From, const FName& To, float Share)
	{
		FBodyInstance* Part = BodyMesh->GetBodyInstance(BodyName);
		const FVector Along = (BodyMesh->GetBoneLocation(To) - BodyMesh->GetBoneLocation(From)).GetSafeNormal();
		if (!Part || !Part->IsInstanceSimulatingPhysics())
		{
			return static_cast<float>(Along.Z);
		}
		const FVector Axis = FVector::CrossProduct(Along, Trunk);
		const float Angle = FMath::Atan2(static_cast<float>(Axis.Size()), static_cast<float>(FVector::DotProduct(Along, Trunk)));
		const FVector Spin = Part->GetUnrealWorldAngularVelocityInRadians();
		const FVector Tilting = Spin - Trunk * FVector::DotProduct(Spin, Trunk);
		Part->AddTorqueInRadians((Axis.GetSafeNormal() * (Angle * W * W) - Tilting * (2.f * W)) * (Pull * Share * FMath::Lerp(HoistUnsteadyHoldShare, 1.f, Steady)), true, true);
		return static_cast<float>(Along.Z);
	};
	Align(PelvisBoneName, PelvisBoneName, TEXT("spine_03"), 0.6f);
	Align(TEXT("spine_03"), TEXT("spine_01"), TEXT("spine_05"), 0.8f);
	const float ChestUp = Align(TEXT("spine_05"), TEXT("spine_01"), TEXT("neck_01"), 1.f);

	// The head is taken up first, the face turned the way the body will stand (face down where the head lay, on the back
	// the feet's way), the neck the harder the longer the keys are held.
	const FVector StandFacing = HoistStyle == EAedirGetUpStyle::FromFront ? Headward : -Headward;
	const float HeadShare = FMath::Max(FMath::Clamp(HoistHeldSeconds * HoistPace / FMath::Max(HoistHeadSeconds, 0.01f), 0.f, 1.f), HoistDrive * Lift);
	if (FBodyInstance* HeadBody = BodyMesh->GetBodyInstance(HeadBoneName); HeadBody && HeadBody->IsInstanceSimulatingPhysics() && HeadShare > 0.f)
	{
		const FVector Wanted = StandFacing * (1.f - HoistHeadLiftShare) + Up * HoistHeadLiftShare;
		const FVector Face = HeadBody->GetUnrealWorldTransform().GetRotation().RotateVector(HoistFaceLocal);
		const FVector Axis = FVector::CrossProduct(Face, Wanted.GetSafeNormal());
		const float Angle = FMath::Atan2(static_cast<float>(Axis.Size()), static_cast<float>(FVector::DotProduct(Face, Wanted.GetSafeNormal())));
		const float Wh = UE_TWO_PI * HoistHeadHz;
		HeadBody->AddTorqueInRadians((Axis.GetSafeNormal() * (Angle * Wh * Wh) - HeadBody->GetUnrealWorldAngularVelocityInRadians() * (2.f * Wh)) * (HeadShare * AedirStableSpring(Wh, DeltaTime)), true, true);
	}

	// The hands go down on the ground, the nearer one first, palms flat: face down beside the shoulders, on the back beside
	// the hips; the body leans on them for as long as the step has them down.
	if (HoistHand < 0 && HoistHeldSeconds * HoistPace >= HoistHandDelaySeconds)
	{
		HoistHand = BodyMesh->GetBoneLocation(TEXT("hand_l")).Z <= BodyMesh->GetBoneLocation(TEXT("hand_r")).Z ? 0 : 1;
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s puts its hands to the ground, the %s first."), *GetNameSafe(GetOwner()), HoistHand == 0 ? TEXT("left") : TEXT("right"));
	}
	// A hand in use, its button held or holding something, stays off the ground to fight or hold with; only one may.
	{
		const AAedirCreature* Self = Cast<AAedirCreature>(CharacterOwner);
		const UAedirFistArms* Fists = CharacterOwner->FindComponentByClass<UAedirFistArms>();
		const auto Pressed = [&](int32 Side) { return Fists && Fists->IsHeld(Side == 0 ? EAedirHand::Left : EAedirHand::Right); };
		const auto HeldIn = [&](int32 Side) { return Self && Self->Interaction ? Self->Interaction->GetHeldItem(Side == 0 ? EAedirHand::Left : EAedirHand::Right) : nullptr; };
		const int32 Was = HoistFreeHand;
		HoistFreeHand = Pressed(1) ? 1 : Pressed(0) ? 0 : (HoistFreeHand >= 0 && HeldIn(HoistFreeHand)) ? HoistFreeHand : HeldIn(1) ? 1 : HeldIn(0) ? 0 : -1;
		// Both hands on one thing: neither is free of it, both go down with it.
		if (HoistFreeHand >= 0 && !Pressed(HoistFreeHand) && HeldIn(0) && HeldIn(0) == HeldIn(1))
		{
			HoistFreeHand = -1;
		}
		UE_CLOG(Was != HoistFreeHand, LogAedir, Log, TEXT("[GetUp] %s getting up keeps %s off the ground."), *GetNameSafe(GetOwner()), HoistFreeHand == 0 ? TEXT("its left hand") : HoistFreeHand == 1 ? TEXT("its right hand") : TEXT("neither hand"));
		if (HoistHand >= 0 && HoistHand == HoistFreeHand)
		{
			HoistHand = 1 - HoistFreeHand;
		}
	}
	float HandOff = 0.f;
	float PalmDown = 0.f;
	const float OneHandShare = HoistFreeHand >= 0 ? 2.f : 1.f;
	for (int32 Side = 0; HoistHand >= 0 && Side < 2; ++Side)
	{
		if (Side == HoistFreeHand)
		{
			continue;
		}
		const float Late = Side == HoistHand || HoistFreeHand >= 0 ? 0.f : HoistSecondHandLateSeconds;
		const float Share = FMath::Clamp((HoistHeldSeconds * HoistPace - HoistHandDelaySeconds - Late) / FMath::Max(HoistHandReachSeconds, 0.01f), 0.f, 1.f) * Stage.Hands * HoistDrive;
		const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
		FBodyInstance* HandBody = BodyMesh->GetBodyInstance(FName(FString(TEXT("hand")) + Suffix));
		if (Share <= 0.f || !HandBody || !HandBody->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		FVector Spot;
		if (bFront)
		{
			// Beside the shoulder, out from the chest and a little towards the head: not under the body lying on it.
			const FVector RawShoulder = BodyMesh->GetBoneLocation(FName(FString(TEXT("upperarm")) + Suffix));
			const FVector Outward = (RawShoulder - BodyMesh->GetBoneLocation(TEXT("spine_05"))).GetSafeNormal2D();
			Spot = RawShoulder + (Outward * HoistHandOutCm + Headward * HoistHandAheadCm) * Scale;
		}
		else
		{
			// Beside the hip, out to its side and a little towards the head, to push the trunk up from.
			const FVector RawHip = BodyMesh->GetBoneLocation(FName(FString(TEXT("thigh")) + Suffix));
			const float Outward = FVector::DotProduct(RawHip - Hips, Right) >= 0.0 ? 1.f : -1.f;
			Spot = RawHip + (Right * (Outward * HoistBackHandOutCm) + Headward * HoistBackHandAheadCm) * Scale;
		}
		FVector Under;
		if (!TraceStandingGround(Spot + FVector(0.f, 0.f, 10.f), Spot - FVector(0.f, 0.f, 200.f), Under, false))
		{
			Under = FVector(Spot.X, Spot.Y, Ground.Z);
		}
		Spot = Under + FVector(0.f, 0.f, HoistHandAboveGroundCm * Scale);
		const FVector HandAt = HandBody->GetUnrealWorldTransform().GetLocation();
		HandOff = FMath::Max(HandOff, static_cast<float>(FVector::Dist(Spot, HandAt)));
		const float Hk = HoistHandSpringPerSecondSq;
		const FVector HandAccel = ((Spot - HandAt) * Hk - HandBody->GetUnrealWorldVelocity() * (2.f * FMath::Sqrt(Hk))) * (Share * AedirStableSpring(FMath::Sqrt(Hk), DeltaTime)) + FVector(0.f, 0.f, Gravity * 0.5f * Share);
		// The hand carries the arm after it: the force is for the whole arm's weight.
		float ArmMass = 0.f;
		for (const TCHAR* Part : { TEXT("hand"), TEXT("lowerarm"), TEXT("upperarm") })
		{
			if (const FBodyInstance* Piece = BodyMesh->GetBodyInstance(FName(FString(Part) + Suffix)))
			{
				ArmMass += Piece->GetBodyMass();
			}
		}
		HandBody->AddForce(HandAccel.GetClampedToMaxSize(HoistHandMaxAccel) * FMath::Max(ArmMass, HandBody->GetBodyMass()), true, false);
		// The palm is turned flat to the ground, and the forearm with it, as it turns to lay a hand flat.
		const FVector Palm = HandBody->GetUnrealWorldTransform().GetRotation().RotateVector(HoistPalmLocal[Side]);
		const FVector PalmAxis = FVector::CrossProduct(Palm, -Up);
		const float PalmAngle = FMath::Atan2(static_cast<float>(PalmAxis.Size()), static_cast<float>(FVector::DotProduct(Palm, -Up)));
		// Stiff as it is, the turn is worked out as the step will take it, or each frame would overshoot the last and the hand
		// shake on the ground.
		const float Wp = UE_TWO_PI * HoistPalmHz;
		const float PalmStable = AedirStableSpring(Wp, DeltaTime);
		HandBody->AddTorqueInRadians((PalmAxis.GetSafeNormal() * (PalmAngle * Wp * Wp) - HandBody->GetUnrealWorldAngularVelocityInRadians() * (2.f * Wp)) * (Share * PalmStable), true, true);
		if (FBodyInstance* Forearm = BodyMesh->GetBodyInstance(FName(FString(TEXT("lowerarm")) + Suffix)); Forearm && Forearm->IsInstanceSimulatingPhysics())
		{
			Forearm->AddTorqueInRadians((PalmAxis.GetSafeNormal() * (PalmAngle * Wp * Wp) - Forearm->GetUnrealWorldAngularVelocityInRadians() * (2.f * Wp)) * (Share * 0.5f * PalmStable), true, true);
		}
		PalmDown += static_cast<float>(-Palm.Z) * 0.5f;
		// Leaning on the hands the chest comes up off the ground a little, before the hips are hauled up.
		if (Chest && Chest->IsInstanceSimulatingPhysics())
		{
			Chest->AddForce(FVector(0.f, 0.f, Gravity * HoistHandPushShare * 0.5f * OneHandShare * UpperMass * Share * (1.f - Lift)), true, false);
		}
	}
	HoistPalmDownLogged = PalmDown;

	// Face down the knees, on the back the feet, are held to the ground at their spot towards the feet: the knees under the
	// hips as they come up onto them, the feet drawn in to the hips.
	// A leg put forward has its foot held instead, flat on the ground ahead of where the step has the hips.
	float LegOff = 0.f;
	for (int32 Side = 0; Side < 2 && Stage.Legs > 0.f; ++Side)
	{
		const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
		const float Forward = Side == HoistLeadSide ? Stage.Lead : 0.f;
		float LegMass = 0.f;
		for (const TCHAR* Part : { TEXT("thigh"), TEXT("calf"), TEXT("foot") })
		{
			if (const FBodyInstance* Piece = BodyMesh->GetBodyInstance(FName(FString(Part) + Suffix)))
			{
				LegMass += Piece->GetBodyMass();
			}
		}
		const float Outward = FVector::DotProduct(BodyMesh->GetBoneLocation(FName(FString(TEXT("thigh")) + Suffix)) - Hips, Right) >= 0.0 ? 1.f : -1.f;
		const float Lk = GetUpLegSpringPerSecondSq * Strength;
		const auto Hold = [&](const TCHAR* Part, const FVector& Spot, float Share)
		{
			FBodyInstance* LegBody = BodyMesh->GetBodyInstance(FName(FString(Part) + Suffix));
			if (Share <= 0.f || !LegBody || !LegBody->IsInstanceSimulatingPhysics())
			{
				return;
			}
			const FVector LegAt = LegBody->GetUnrealWorldTransform().GetLocation();
			LegOff = FMath::Max(LegOff, static_cast<float>(FVector::Dist(Spot, LegAt)));
			const FVector LegAccel = ((Spot - LegAt) * Lk - LegBody->GetUnrealWorldVelocity() * (2.f * FMath::Sqrt(Lk))) * (Share * HoistDrive);
			LegBody->AddForce(LegAccel.GetClampedToMaxSize(HoistHandMaxAccel) * FMath::Max(LegMass, LegBody->GetBodyMass()), true, false);
		};
		const FVector Flat = FVector(HoistAnchor.X, HoistAnchor.Y, Ground.Z) - Headward * ((bFront ? GetUpKneeBackCm : GetUpFeetBackCm) * Size) + Right * (Outward * GetUpLegOutCm * Size);
		Hold(bFront ? TEXT("calf") : TEXT("foot"), Flat + FVector(0.f, 0.f, (bFront ? 6.f : 5.f) * Size), Stage.Legs * (1.f - Forward));
		const FVector Ahead = FVector(HipsWanted.X, HipsWanted.Y, Ground.Z) + Headward * (Stage.LeadFootAheadCm * Size) + Right * (Outward * GetUpLegOutCm * Size);
		// The foot coming forward is carried over the ground, not dragged along it, and set down flat where it goes.
		const FVector FootNow = BodyMesh->GetBoneLocation(FName(FString(TEXT("foot")) + Suffix));
		const float StillToGo = static_cast<float>(FVector::Dist2D(FootNow, Ahead));
		const float StepLift = Forward > 0.f && Forward < 0.98f ? FMath::Min(StillToGo * HoistStepLiftShare, HoistStepLiftMaxCm * Size) : 0.f;
		Hold(TEXT("foot"), Ahead + FVector(0.f, 0.f, 5.f * Size + StepLift), Stage.Legs * Forward);
	}

	// And the hips are turned to face the way the body will stand: face down where the head lay, on the back the feet's way.
	const FVector Facing = FVector::CrossProduct(BodyMesh->GetBoneLocation(TEXT("thigh_r")) - BodyMesh->GetBoneLocation(TEXT("thigh_l")), Up).GetSafeNormal2D();
	if (!Facing.IsNearlyZero())
	{
		const float Off = FMath::Atan2(static_cast<float>(FVector::CrossProduct(Facing, StandFacing).Z), static_cast<float>(FVector::DotProduct(Facing, StandFacing)));
		const float Wt = UE_TWO_PI * HoistTurnHz;
		const float YawSpin = static_cast<float>(Pelvis->GetUnrealWorldAngularVelocityInRadians().Z);
		Pelvis->AddTorqueInRadians(Up * ((Off * Wt * Wt - YawSpin * 2.f * Wt) * Pull), true, true);
	}

	static const IConsoleVariable* HitLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (HitLog && HitLog->GetInt() != 0 && GFrameCounter % 15 == 0)
	{
		const auto KneeBent = [&](const TCHAR* Suffix)
		{
			const FVector Thigh = BodyMesh->GetBoneLocation(FName(FString(TEXT("calf")) + Suffix)) - BodyMesh->GetBoneLocation(FName(FString(TEXT("thigh")) + Suffix));
			const FVector Shin = BodyMesh->GetBoneLocation(FName(FString(TEXT("foot")) + Suffix)) - BodyMesh->GetBoneLocation(FName(FString(TEXT("calf")) + Suffix));
			return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Thigh.GetSafeNormal(), Shin.GetSafeNormal())), -1.f, 1.f)));
		};
		const FConstraintInstance* LeftKnee = BodyMesh->FindConstraintInstance(TEXT("calf_l"));
		UE_LOG(LogAedir, Log, TEXT("[Hoist] %s knees bent %.0f and %.0f deg (the step %.0f), the left knee's twist range %.0f, its twist now %.0f"), *GetNameSafe(GetOwner()), KneeBent(TEXT("_l")), KneeBent(TEXT("_r")), Stage.KneeBend,
			LeftKnee ? LeftKnee->GetAngularTwistLimit() : -1.f, LeftKnee ? FMath::RadiansToDegrees(LeftKnee->GetCurrentTwist()) : 0.f);
		UE_LOG(LogAedir, Log, TEXT("[Hoist] %s %.2f s held: step %.2f, hips %.0f cm up (wanted %.0f), %.0f cm off the step, rising %.0f cm/s, pull %.2f (lift %.2f, keys %d), chest up %.2f, trunk %.0f deg, hands %.2f (the furthest %.0f cm off), palms down %.2f, %s %.2f (the furthest %.0f cm off)"),
			*GetNameSafe(GetOwner()), HoistHeldSeconds, HoistProgress, Height, WantedHeight, HoistTrailLogged, HipsSpeed.Z, Pull, GetUpLift, bHeld ? 1 : 0, ChestUp, Stage.TrunkDegrees,
			Stage.Hands, HandOff, HoistPalmDownLogged, bFront ? TEXT("knees") : TEXT("feet"), Stage.Legs, LegOff);
	}

	// Into the last step, up far enough, upright and steady for a moment: on its feet, standing and stepping take it over.
	HoistLastStepFor = HoistProgress >= Last - 0.5f ? HoistLastStepFor + DeltaTime : 0.f;
	const bool bEasy = HoistLastStepFor >= HoistDoneEasySeconds;
	const bool bUp = HoistProgress >= Last - 0.5f && Height >= Stand * (bEasy ? HoistDoneEasyHeightShare : HoistDoneHeightShare) && ChestUp >= (bEasy ? HoistDoneEasyUpright : HoistDoneUpright) && HipsSpeed.Size() < HoistDoneSpeedCm;
	if (!bUp && HoistLastStepFor >= HoistGiveUpSeconds)
	{
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s cannot come up the last of the way (hips %.0f of %.0f cm, chest up %.2f): it lies down to try again."), *GetNameSafe(GetOwner()), Height, Stand, ChestUp);
		GetUpLift = 0.f;
		StopHoist(TEXT("could not stand"));
		return;
	}
	HoistDoneFor = bUp ? HoistDoneFor + DeltaTime : 0.f;
	if (HoistDoneFor >= HoistDoneHoldSeconds)
	{
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s is hauled up onto its feet in %.2f s, the hips %.0f cm up, the chest up %.2f, lift %.2f."), *GetNameSafe(GetOwner()), HoistSeconds, Height, ChestUp, GetUpLift);
		bHoisting = false;
		bHoistFinished = true;
		return;
	}
	// Let go and come to rest back on the ground, it lies there again.
	HoistRestFor = !bHeld && HoistDrive <= 0.f && HipsSpeed.Size() < HoistDoneSpeedCm ? HoistRestFor + DeltaTime : 0.f;
	if (HoistRestFor >= 0.4f)
	{
		StopHoist(TEXT("let go"));
	}
}

void UAedirBalanceComponent::UpdateGetUp(float DeltaTime)
{
	if (GetUpLeft <= 0.f)
	{
		return;
	}
	// The body lifts itself as long as the keys are held (a body nobody plays, always), at the pace of what lift it still has
	// after the blows it took; let go, or with no lift left, it sinks back, and back on the ground it lies down again.
	static const IConsoleVariable* AutoGetUp = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.AutoGetUp"));
	const bool bChooses = bPlayerChoosesGetUp && CharacterOwner && CharacterOwner->IsPlayerControlled() && !(AutoGetUp && AutoGetUp->GetInt() != 0);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bHeld = !bChooses || Now - GetUpRequestTime <= AedirGetUpRequestHoldSeconds;
	// A long frame is neither a let-go nor a rise: the keys could not be read in it.
	if (DeltaTime > AedirBodyHitchSeconds)
	{
		return;
	}
	GetUpLift = FMath::Min(GetUpLift + GetUpLiftRecoverPerSecond * DeltaTime, 1.f);
	float Pace = bHeld ? FMath::Lerp(-GetUpSinkPace, 1.f, GetUpLift) : -GetUpSinkPace;
	// The moves go on only as fast as the hips are really lifted: the muscles pull the physical hips after the pose, and
	// the further they trail below it, the slower the pose goes on, until it waits for them. Struck, or weak, the body
	// rises the slower for it; it is the hips that get up, the rest following them.
	const FBodyInstance* LiftedPelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	if (BodyControl && LiftedPelvis && Pace > 0.f)
	{
		const float Trail = static_cast<float>(BodyControl->GetPoseLocation(PelvisBoneName).Z - LiftedPelvis->GetUnrealWorldTransform().GetLocation().Z);
		const float Wait = FMath::Clamp((Trail - GetUpHipTrailFreeCm) / FMath::Max(GetUpHipTrailStopCm - GetUpHipTrailFreeCm, 1.f), 0.f, 1.f);
		Pace *= 1.f - Wait;
		GetUpHipTrailMost = FMath::Max(GetUpHipTrailMost, Trail);
		GetUpHipTrailSum += Trail * DeltaTime;
		UE_CLOG(GFrameCounter % 12 == 0, LogAedir, Verbose, TEXT("[GetUp] %s at %.0f%%: the pose has the hips at %.1f, the body at %.1f, the bone at %.1f"), *GetNameSafe(GetOwner()), GetGetUpProgress() * 100.f,
			BodyControl->GetPoseLocation(PelvisBoneName).Z, LiftedPelvis->GetUnrealWorldTransform().GetLocation().Z, BodyMesh->GetBoneLocation(PelvisBoneName).Z);
		GetUpRiseSeconds += DeltaTime;
		GetUpWaitedSeconds += Wait * DeltaTime;
	}
	GetUpLeft = FMath::Clamp(GetUpLeft - DeltaTime * Pace, 0.f, GetUpTotal);
	if (IsAuthoritative() && Pace < 0.f && GetUpLeft >= GetUpTotal)
	{
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s sank back to the ground (%s)."), *GetNameSafe(GetOwner()), bHeld ? TEXT("no lift left") : TEXT("let go"));
		GetUpLeft = 0.f;
		EnterRagdoll(GetUpRetrySeconds);
		return;
	}
	// Towards the way of the keys: the hips go over that way, and the body turns round to face it as it rises.
	const FVector KeyWay = bChooses && bHeld ? GetUpKeyWay : FVector::ZeroVector;
	const FVector KeyLocal = CharacterOwner ? CharacterOwner->GetActorTransform().InverseTransformVectorNoScale(KeyWay) : FVector::ZeroVector;
	GetUpLean = FMath::Vector2DInterpTo(GetUpLean, FVector2D(KeyLocal.X, KeyLocal.Y) * FMath::Max(Pace, 0.f), DeltaTime, AedirGetUpLeanInterpSpeed);
	if (IsAuthoritative() && CharacterOwner && !KeyWay.IsNearlyZero() && Pace > 0.f)
	{
		const float Wanted = static_cast<float>(KeyWay.Rotation().Yaw);
		const float Yaw = static_cast<float>(CharacterOwner->GetActorRotation().Yaw);
		const float Turned = FMath::FixedTurn(Yaw, Wanted, GetUpTurnDegreesPerSecond * Pace * DeltaTime);
		CharacterOwner->SetActorRotation(FRotator(0.f, Turned, 0.f), ETeleportType::None);
	}
	if (GetUpLeft <= 0.f)
	{
		UE_LOG(LogAedir, Log, TEXT("[GetUp] %s is up, lift %.2f, facing %.0f; the hips trailed the pose by %.0f cm on average, at most %.0f, and it waited for them %.2f s of %.2f."), *GetNameSafe(GetOwner()), GetUpLift, CharacterOwner ? CharacterOwner->GetActorRotation().Yaw : 0.0, GetUpRiseSeconds > 0.f ? GetUpHipTrailSum / GetUpRiseSeconds : 0.f, GetUpHipTrailMost, GetUpWaitedSeconds, GetUpRiseSeconds);
	}
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
	FitJointsToSkeleton();
	ApplyJointLimits(false);
	ApplyLegPhysics(false);
	if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}
	// Made again at the size it was just given on coming into the world: held in its pose a moment again first.
	if (SpawnAge < 3.f && !bIsRagdoll)
	{
		SpawnPosedLeft = SpawnPosedSeconds;
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

void UAedirBalanceComponent::FitJointsToSkeleton()
{
	const USkeletalMesh* Asset = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
	{
		return;
	}
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	for (FConstraintInstance* Joint : BodyMesh->Constraints)
	{
		const int32 Child = Skeleton.FindBoneIndex(Joint->ConstraintBone1);
		const int32 Parent = Skeleton.FindBoneIndex(Joint->ConstraintBone2);
		if (Child == INDEX_NONE || Parent == INDEX_NONE)
		{
			continue;
		}
		// Only a joint up the bone's own chain; one tied across (a shin to the hips) is left as it was made.
		int32 Up = Skeleton.GetParentIndex(Child);
		while (Up != INDEX_NONE && Up != Parent)
		{
			Up = Skeleton.GetParentIndex(Up);
		}
		if (Up != Parent)
		{
			continue;
		}
		const FVector OnParent = Asset->GetComposedRefPoseMatrix(Parent).InverseTransformPosition(Asset->GetComposedRefPoseMatrix(Child).GetOrigin());
		// The joint in the simulation is laid at the body's size; the stored one stays unscaled.
		Joint->SetRefPosition(EConstraintFrame::Frame1, FVector::ZeroVector);
		Joint->SetRefPosition(EConstraintFrame::Frame2, OnParent * Joint->GetLastKnownScale());
		Joint->Pos2 = OnParent;
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
	const auto Living = [](bool bLimpJoint, EAngularConstraintMotion Motion, float Degrees, float& OutDegrees)
	{
		OutDegrees = Degrees;
		if (bLimpJoint || Motion == ACM_Free)
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
		// The joints of a broken arm (from the shoulder down) keep the limp limits standing too.
		const FString JointName = Joint->JointName.ToString();
		const bool bBrokenArm = (JointName.StartsWith(TEXT("upperarm")) || JointName.StartsWith(TEXT("lowerarm")) || JointName.StartsWith(TEXT("hand")))
			&& ((JointName.EndsWith(TEXT("_l")) && (BrokenArmJoints & 1)) || (JointName.EndsWith(TEXT("_r")) && (BrokenArmJoints & 2)));
		const bool bLimpJoint = bRagdoll || bBrokenArm;
		const EAngularConstraintMotion Swing1Motion = Living(bLimpJoint, Loose.ConeLimit.Swing1Motion, Loose.ConeLimit.Swing1LimitDegrees, Swing1);
		const EAngularConstraintMotion Swing2Motion = Living(bLimpJoint, Loose.ConeLimit.Swing2Motion, Loose.ConeLimit.Swing2LimitDegrees, Swing2);
		const EAngularConstraintMotion TwistMotion = Living(bLimpJoint, Loose.TwistLimit.TwistMotion, Loose.TwistLimit.TwistLimitDegrees, Twist);
		// A living wrist and forearm turn a held weapon much further than the limp ones of the asset: the weapon's poses ask it.
		static IConsoleVariable* WideArmJoints = IConsoleManager::Get().RegisterConsoleVariable(TEXT("aedir.WideArmJoints"), 1, TEXT("Living wrists and forearms turn further than the limp limits (1) or not (0)."));
		if (!bLimpJoint && WideArmJoints->GetInt() != 0)
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
		// No joint pulls towards a pose of its own: the muscles alone hold the living pose, and a dead or broken limb falls
		// where its weight takes it, only dragged a little.
		Joint->SetAngularDriveMode(EAngularDriveMode::SLERP);
		Joint->SetOrientationDriveSLERP(false);
		Joint->SetOrientationDriveTwistAndSwing(false, false);
		Joint->SetAngularVelocityDriveSLERP(JointDragPerSecond > 0.f);
		Joint->SetAngularVelocityDriveTwistAndSwing(false, false);
		Joint->SetAngularDriveParams(0.f, JointDragPerSecond, 0.f);
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
	static const IConsoleVariable* BumpLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (BumpLog && BumpLog->GetInt() != 0 && OwnerVelocity.Size2D() > 150.f)
	{
		UE_LOG(LogAedir, Log, TEXT("[Bump] %s runs into %s (%s), normal %s, going %.0f cm/s, %.0f into it"), *GetNameSafe(Owner), *GetNameSafe(OtherActor), *GetNameSafe(OtherComponent),
			*Hit.ImpactNormal.ToCompactString(), OwnerVelocity.Size2D(), FVector::DotProduct(OwnerVelocity, -Hit.ImpactNormal));
	}

	if (OtherComponent && OtherComponent->IsSimulatingPhysics())
	{
		// Shins and feet are walked into things all the time; objects only hurt at the knee and above.
		const float FeetZ = static_cast<float>(Owner->GetActorLocation().Z) - (BodyCapsule ? BodyCapsule->GetScaledCapsuleHalfHeight() : 0.f);
		const float KneeHeight = ObjectImpactMinHeightCm * static_cast<float>(Owner->GetActorScale3D().Z);
		if (Hit.ImpactPoint.Z - FeetZ < KneeHeight)
		{
			return;
		}

		// Walking into something light that lies still is no blow: it is pushed. Something heavy is run into as a wall.
		const bool bStill = OtherComponent->GetComponentVelocity().SizeSquared() < AedirObjectImpactMinSpeed * AedirObjectImpactMinSpeed;
		if (bStill && OtherComponent->GetMass() < StillObjectWallMassKg)
		{
			return;
		}
		if (bStill)
		{
			RunIntoWall(OwnerVelocity, Hit, StatsComponent, Athletism);
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

	RunIntoWall(OwnerVelocity, Hit, StatsComponent, Athletism);
}

void UAedirBalanceComponent::RunIntoWall(const FVector& OwnerVelocity, const FHitResult& Hit, UAedirStatsComponent* StatsComponent, float Athletism)
{
	// What is stood on is no wall.
	if (bIsRagdoll || Hit.ImpactNormal.Z > AedirWalkableNormalZ)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastBumpTime < BumpRepeatSeconds)
	{
		return;
	}
	const float ApproachSpeed = static_cast<float>(FVector::DotProduct(OwnerVelocity, -Hit.ImpactNormal));
	if (ApproachSpeed <= WallImpactSpeedThreshold)
	{
		return;
	}

	if (World)
	{
		LastBumpTime = World->GetTimeSeconds();
	}
	const float Excess = ApproachSpeed - WallImpactSpeedThreshold;
	const float Share = Excess / FMath::Max(WallImpactFullLossSpeed, 1.f);
	UAedirSfxSubsystem::Play(this, EAedirSfx::BodyImpact, Hit.ImpactPoint, FMath::Clamp(Share, 0.2f, 1.f));
	// Thrown back off it, the way it came.
	ApplyBalanceImpact(Hit.ImpactNormal.GetSafeNormal2D(), FMath::Clamp(Share, 0.f, AedirWallImpactMaxLoss));
	UE_LOG(LogAedir, Log, TEXT("[Bump] %s runs into %s at %.0f cm/s, balance %.2f"), *GetNameSafe(GetOwner()), *GetNameSafe(Hit.GetActor()), ApproachSpeed, Balance);
	// The part of the body that met it is thrown back off it, and the trunk a little with it, each by its own weight and
	// only by how much harder than harmless it came in. Held up by the pose, the trunk is bent back off it all the same.
	if (BodyMesh)
	{
		FName Part = NAME_None;
		double Nearest = TNumericLimits<double>::Max();
		float PartMass = 0.f;
		// The part nearest the thing itself, looked for straight towards it from each part; failing that, the one nearest
		// where the body touched it.
		UPrimitiveComponent* Met = Hit.GetComponent();
		const FVector Into = -Hit.ImpactNormal.GetSafeNormal2D();
		bool bReached = false;
		// Something standing higher than the knee is met by the body above it: the feet step on under it, the trunk, arms
		// and head carry on into it. A lower thing is tripped over with the legs.
		const double FeetZ = GetOwner()->GetActorLocation().Z - (BodyCapsule ? BodyCapsule->GetScaledCapsuleHalfHeight() : 0.f);
		const double KneeZ = FeetZ + ObjectImpactMinHeightCm * GetOwner()->GetActorScale3D().Z;
		const bool bTall = Met && Met->Bounds.GetBox().Max.Z > KneeZ;
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (!Body || !Body->BodySetup.IsValid())
			{
				continue;
			}
			const FVector At = BodyMesh->GetBoneLocation(Body->BodySetup->BoneName);
			if (bTall && At.Z < KneeZ)
			{
				continue;
			}
			FHitResult Reach;
			const bool bReaches = Met && Met->LineTraceComponent(Reach, At, At + Into * AedirWallPartReachCm, FCollisionQueryParams(SCENE_QUERY_STAT(AedirWallPart), false));
			const double Away = bReaches ? FVector::DistSquared(At, Reach.ImpactPoint) : FVector::DistSquared(At, Hit.ImpactPoint);
			if ((bReaches && !bReached) || (bReaches == bReached && Away < Nearest))
			{
				bReached = bReached || bReaches;
				Nearest = Away;
				Part = Body->BodySetup->BoneName;
				PartMass = Body->GetBodyMass();
			}
		}
		const FVector Back = Hit.ImpactNormal.GetSafeNormal2D();
		if (Part != NAME_None)
		{
			MulticastBoneImpulse(Part, Back * (PartMass * Excess * WallImpactPartRebound), Hit.ImpactPoint);
		}
		const FBodyInstance* Trunk = BodyMesh->GetBodyInstance(TEXT("spine_03"));
		if (Trunk && Part != TEXT("spine_03"))
		{
			MulticastBoneImpulse(TEXT("spine_03"), Back * (Trunk->GetBodyMass() * Excess * WallImpactTrunkRebound), BodyMesh->GetBoneLocation(TEXT("spine_03")));
		}
		UE_LOG(LogAedir, Log, TEXT("[Bump] %s meets it with its %s, thrown back by %.0f cm/s; it stands %.0f cm up from the feet, touched %.0f cm up"), *GetNameSafe(GetOwner()), *Part.ToString(), Excess * WallImpactPartRebound,
			Met ? Met->Bounds.GetBox().Max.Z - (GetOwner()->GetActorLocation().Z - (BodyCapsule ? BodyCapsule->GetScaledCapsuleHalfHeight() : 0.f)) : 0.0,
			Hit.ImpactPoint.Z - (GetOwner()->GetActorLocation().Z - (BodyCapsule ? BodyCapsule->GetScaledCapsuleHalfHeight() : 0.f)));
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
	// Walked into while getting up: the body has no footing to take it on, loses its lift and is shoved over. Whichever of
	// the two tells of the touch.
	if (OtherBalance && World && bHoisting && !OtherBalance->IsRagdoll())
	{
		BumpedGettingUp(Other, Hit.ImpactPoint);
		return;
	}
	if (OtherBalance && World && OtherBalance->IsHoisting() && !bIsRagdoll)
	{
		OtherBalance->BumpedGettingUp(Cast<AAedirCreature>(GetOwner()), Hit.ImpactPoint);
		return;
	}
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

void UAedirBalanceComponent::BumpedGettingUp(const AAedirCreature* By, const FVector& Point, FName Part)
{
	const UWorld* World = GetWorld();
	if (!By || !World || !bHoisting || World->GetTimeSeconds() - LastBumpTime < BumpRepeatSeconds)
	{
		return;
	}
	const FBodyInstance* Pelvis = BodyMesh ? BodyMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	const FVector Hips = Pelvis ? Pelvis->GetUnrealWorldTransform().GetLocation() : GetOwner()->GetActorLocation();
	const FVector Pushed = (Hips - By->GetActorLocation()).GetSafeNormal2D();
	// Held up against it, a walker still leans into it as hard as it walks: what it would be going counts, not only what it is.
	const UCharacterMovementComponent* ByMovement = By->GetCharacterMovement();
	const FVector Meant = ByMovement && ByMovement->GetMaxAcceleration() > 0.f ? ByMovement->GetCurrentAcceleration() / ByMovement->GetMaxAcceleration() * ByMovement->GetMaxSpeed() : FVector::ZeroVector;
	const FVector Going = Meant.SizeSquared() > By->GetVelocity().SizeSquared() ? Meant : By->GetVelocity();
	const float Met = static_cast<float>(FVector::DotProduct(Going - (Pelvis ? Pelvis->GetUnrealWorldVelocity() : FVector::ZeroVector), Pushed));
	if (Met < GetUpBumpMinSpeed)
	{
		return;
	}
	LastBumpTime = World->GetTimeSeconds();
	GetUpLift = FMath::Max(GetUpLift - (Met - GetUpBumpMinSpeed + 40.f) * GetUpBumpLiftPerSpeed, 0.f);
	MulticastBoneImpulse(Part != NAME_None && BodyMesh && BodyMesh->GetBodyInstance(Part) ? Part : FName(TEXT("spine_03")), Pushed * (Met * BumpImpulsePerSpeed), Point);
	UE_LOG(LogAedir, Log, TEXT("[GetUp] %s is run into by %s at %.0f cm/s on its %s getting up: lift %.2f"), *GetNameSafe(GetOwner()), *GetNameSafe(By), Met, *Part.ToString(), GetUpLift);
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
	// A part of the body striking the ground or something fixed hard is hurt by it.
	if (OtherActor != GetOwner() && !Cast<AAedirCreature>(OtherActor) && !(OtherComponent && OtherComponent->IsSimulatingPhysics()))
	{
		HurtPartOnImpact(Hit.MyBoneName, NormalImpulse, Hit);
	}
	ProcessHit(OtherActor, OtherComponent, Hit);
}

void UAedirBalanceComponent::HurtPartOnImpact(FName Bone, const FVector& NormalImpulse, const FHitResult& Hit)
{
	const FBodyInstance* Part = BodyMesh && Bone != NAME_None ? BodyMesh->GetBodyInstance(Bone) : nullptr;
	const UWorld* World = GetWorld();
	static const IConsoleVariable* TouchLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (TouchLog && TouchLog->GetInt() > 2)
	{
		UE_LOG(LogAedir, Log, TEXT("[Impact] %s mesh hit on %s, simulating %d"), *GetNameSafe(GetOwner()), *Bone.ToString(), Part && Part->IsInstanceSimulatingPhysics() ? 1 : 0);
	}
	if (!IsAuthoritative() || !Part || !Part->IsInstanceSimulatingPhysics() || !World)
	{
		return;
	}
	// How fast it was stopped: the push the hit gave it over its weight.
	const float Mass = FMath::Max(Part->GetBodyMass(), 0.5f);
	const float Speed = static_cast<float>(NormalImpulse.Size()) / Mass;
	const double Now = World->GetTimeSeconds();
	static const IConsoleVariable* ImpactLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (ImpactLog && ImpactLog->GetInt() > 1)
	{
		UE_LOG(LogAedir, Log, TEXT("[Impact] %s %s touches %s, stopped by %.0f cm/s"), *GetNameSafe(GetOwner()), *Bone.ToString(), *GetNameSafe(Hit.GetActor()), Speed);
	}
	// Lying awake, an arm swung against the ground or brought down on it is moved on purpose: a punch or a guard thrown
	// from the ground, a hand put down. It is hurt only by a blow far harder than that, as from a fall.
	const FString PartName = Bone.ToString();
	const bool bArm = PartName.StartsWith(TEXT("hand")) || PartName.StartsWith(TEXT("lowerarm")) || PartName.StartsWith(TEXT("upperarm")) || PartName.StartsWith(TEXT("clavicle"))
		|| PartName.StartsWith(TEXT("index")) || PartName.StartsWith(TEXT("middle")) || PartName.StartsWith(TEXT("ring")) || PartName.StartsWith(TEXT("pinky")) || PartName.StartsWith(TEXT("thumb"));
	const UAedirStatsComponent* LyingStats = FindStats();
	const bool bAwakeDown = bIsRagdoll && FallWatchLeft <= 0.f && !(LyingStats && (LyingStats->IsDead() || LyingStats->IsDying() || LyingStats->IsKnockedOut()));
	const float Harmless = bArm && bAwakeDown ? FMath::Max(ImpactHarmlessSpeed, DownArmHarmlessSpeed) : ImpactHarmlessSpeed;
	double& Last = LastPartImpactTime.FindOrAdd(Bone);
	if (Speed <= Harmless || Now - Last < 0.25)
	{
		return;
	}
	Last = Now;
	const float Joules = 0.5f * Mass * FMath::Square(Speed / 100.f) * ImpactWoundShare;
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	if (Creature && Creature->Wounds)
	{
		Creature->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, EAedirDamageType::Blunt, Joules, 0.f, Hit.ImpactNormal, Speed);
	}
	if (UAedirStatsComponent* StatsComponent = FindStats())
	{
		StatsComponent->ApplyDamage((Speed - Harmless) * ImpactDamagePerSpeed);
	}
	UAedirSfxSubsystem::Play(this, EAedirSfx::BodyImpact, Hit.ImpactPoint, FMath::Clamp((Speed - Harmless) / ImpactHarmlessSpeed, 0.2f, 1.f));
	UE_LOG(LogAedir, Log, TEXT("[Impact] %s %s hits %s at %.0f cm/s, %.0f J, %.1f damage"), *GetNameSafe(GetOwner()), *Bone.ToString(), *GetNameSafe(Hit.GetActor()), Speed, Joules, (Speed - Harmless) * ImpactDamagePerSpeed);
}

void UAedirBalanceComponent::WatchFistKnocks()
{
	UWorld* World = GetWorld();
	if (bIsRagdoll || !BodyMesh || !World || !IsAuthoritative())
	{
		bLastFistValid[0] = bLastFistValid[1] = false;
		return;
	}
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const FName Bone = Slot == 0 ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
		const FBodyInstance* Hand = BodyMesh->GetBodyInstance(Bone);
		if (!Hand)
		{
			bLastFistValid[Slot] = false;
			continue;
		}
		// The knuckles: a little on from the wrist along the hand.
		const FTransform At = Hand->GetUnrealWorldTransform();
		const FVector Now = At.GetLocation();
		const FVector Was = LastFistAt[Slot];
		const bool bWasValid = bLastFistValid[Slot];
		LastFistAt[Slot] = Now;
		bLastFistValid[Slot] = true;
		if (!bWasValid || FVector::DistSquared(Was, Now) < 0.25)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirFistKnock), false, GetOwner());
		FCollisionObjectQueryParams Types;
		Types.AddObjectTypesToQuery(ECC_WorldStatic);
		Types.AddObjectTypesToQuery(ECC_WorldDynamic);
		FHitResult Hit;
		if (!World->SweepSingleByObjectType(Hit, Was, Now, FQuat::Identity, Types, FCollisionShape::MakeSphere(FistKnockRadiusCm), Params)
			|| Cast<AAedirCreature>(Hit.GetActor()) || (Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics()) || Hit.bStartPenetrating)
		{
			continue;
		}
		const float Dt = World->GetDeltaSeconds();
		const FVector Velocity = Dt > KINDA_SMALL_NUMBER ? (Now - Was) / Dt : FVector::ZeroVector;
		const float Speed = static_cast<float>(FVector::DotProduct(Velocity, -Hit.ImpactNormal));
		if (Speed > FistWorldSoundMinSpeed && World->GetTimeSeconds() - LastFistKnock[Slot] > 0.25)
		{
			LastFistKnock[Slot] = World->GetTimeSeconds();
			KnockFist(Bone, Speed, Hit);
		}
	}
}

void UAedirBalanceComponent::KnockFist(FName Bone, float Speed, const FHitResult& Hit)
{
	// A fist driven into something fixed: the knuckles' knock, the hand hurt by it and sprung back off it.
	const float SpeedShare = FMath::Clamp(FMath::GetRangePct(FistWorldSoundMinSpeed, FistWorldSoundMaxSpeed, Speed), 0.f, 1.f);
	UAedirSfxSubsystem::Play(this, EAedirSfx::PunchForearm, Hit.ImpactPoint, FMath::Lerp(0.5f, 1.f, SpeedShare), FMath::Lerp(0.3f, 0.6f, SpeedShare));
	const FVector Away = Hit.ImpactNormal.GetSafeNormal();
	FBodyInstance* Struck = BodyMesh ? BodyMesh->GetBodyInstance(Bone) : nullptr;
	if (Struck)
	{
		Struck->SetLinearVelocity(Away * (Speed * FistWorldBounceShare), true);
	}
	float Damage = 0.f;
	if (Speed > FistWorldHarmlessSpeed)
	{
		const float Mass = Struck ? FMath::Max(Struck->GetBodyMass(), 0.3f) : 0.5f;
		const float Joules = 0.5f * Mass * FMath::Square(Speed / 100.f) * FistWorldWoundShare;
		const AAedirCreature* Puncher = Cast<AAedirCreature>(GetOwner());
		if (Puncher && Puncher->Wounds)
		{
			Puncher->Wounds->AddWound(Bone, Hit.ImpactPoint, Away, EAedirDamageType::Blunt, Joules, 0.f, -Away, Speed);
		}
		Damage = (Speed - FistWorldHarmlessSpeed) * FistWorldDamagePerSpeed;
		if (UAedirStatsComponent* StatsComponent = FindStats())
		{
			StatsComponent->ApplyDamage(Damage);
		}
	}
	UE_LOG(LogAedir, Log, TEXT("[Impact] %s punches %s with the %s at %.0f cm/s: %.1f damage, sprung back at %.0f cm/s"), *GetNameSafe(GetOwner()), *GetNameSafe(Hit.GetActor()), *Bone.ToString(), Speed, Damage, Speed * FistWorldBounceShare);
}

void UAedirBalanceComponent::WatchPartImpacts()
{
	if (!bIsRagdoll || !BodyMesh || !IsAuthoritative())
	{
		LastPartVelocity.Reset();
		return;
	}
	const UWorld* World = GetWorld();
	if (LastPartVelocity.Num() != BodyMesh->Bodies.Num())
	{
		LastPartVelocity.Init(FVector::ZeroVector, BodyMesh->Bodies.Num());
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirPartImpact), false, GetOwner());
	for (int32 Index = 0; Index < BodyMesh->Bodies.Num(); ++Index)
	{
		const FBodyInstance* Body = BodyMesh->Bodies[Index];
		if (!Body || !Body->IsInstanceSimulatingPhysics() || !Body->BodySetup.IsValid())
		{
			continue;
		}
		const FVector Before = LastPartVelocity[Index];
		const FVector Now = Body->GetUnrealWorldVelocity();
		LastPartVelocity[Index] = Now;
		// Stopped short against the ground or something that does not give: as hard as it was going into it.
		const float Lost = static_cast<float>(Before.Size() - Now.Size());
		if (Lost <= AedirPartStopCm || !World)
		{
			continue;
		}
		const FVector From = Body->GetCOMPosition();
		const float Radius = static_cast<float>(Body->GetBodyBounds().GetExtent().GetMin()) + 12.f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, From, From + Before.GetSafeNormal() * Radius, ECC_WorldStatic, Params)
			&& !Cast<AAedirCreature>(Hit.GetActor()) && !(Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics()))
		{
			const float Into = static_cast<float>(FVector::DotProduct(Before, -Hit.ImpactNormal));
			Hit.MyBoneName = Body->BodySetup->BoneName;
			HurtPartOnImpact(Hit.MyBoneName, -Hit.ImpactNormal * (FMath::Max(Into, 0.f) * Body->GetBodyMass()), Hit);
		}
	}
}

void UAedirBalanceComponent::HoldBodiesStill(bool bHold)
{
	if (!BodyMesh)
	{
		return;
	}
	if (bHold)
	{
		HeldBodyMotion.Reset();
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			HeldBodyMotion.Emplace(Body ? Body->GetUnrealWorldVelocity() : FVector::ZeroVector, Body ? Body->GetUnrealWorldAngularVelocityInRadians() : FVector::ZeroVector);
		}
		return;
	}
	// In time all but stopped the solver pushes bodies out of the ground with speeds of a whole step's work done in a sliver
	// of one, which they would leave with: they take up again what they were doing.
	float Fastest = 0.f;
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		Fastest = Body && Body->IsInstanceSimulatingPhysics() ? FMath::Max(Fastest, static_cast<float>(Body->GetUnrealWorldVelocity().Size())) : Fastest;
	}
	UE_LOG(LogAedir, Log, TEXT("[PhotoCamera] %s: its fastest part was going %.0f cm/s, put back to what it did before"), *GetNameSafe(GetOwner()), Fastest);
	for (int32 Index = 0; Index < BodyMesh->Bodies.Num() && Index < HeldBodyMotion.Num(); ++Index)
	{
		if (FBodyInstance* Body = BodyMesh->Bodies[Index]; Body && Body->IsInstanceSimulatingPhysics())
		{
			Body->SetLinearVelocity(HeldBodyMotion[Index].Key, false);
			Body->SetAngularVelocityInRadians(HeldBodyMotion[Index].Value, false);
		}
	}
	HeldBodyMotion.Reset();
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

void UAedirBalanceComponent::UpdatePosedArms()
{
	// An arm holding a weapon up in the stance is moved by the pose (its bodies follow the animation as it is), and goes back to
	// its muscles when the stance is let go or the body falls.
	const UAedirCombatComponent* Combat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;
	if (!BodyControl)
	{
		return;
	}
	// aedir.DirectPose: the body shown and moved as the pose has it, with no physics in it, to get the pose right before the
	// physics is let back in. Getting up is never posed: the body lifts itself off the ground by its own muscles, its hips
	// first, against its weight and the blows it takes (bPhysicalGetUp).
	BodyControl->SetWholePosed((CVarAedirDirectPose.GetValueOnGameThread() != 0 || SpawnPosedLeft > 0.f) && !bIsRagdoll && !(bPhysicalGetUp && IsGettingUp()) && HoistSettleLeft <= 0.f);
	BodyControl->SetHoldEase(HoistSettleLeft > 0.f && HoistSettleSeconds > KINDA_SMALL_NUMBER ? FMath::SmoothStep(0.f, 1.f, 1.f - HoistSettleLeft / HoistSettleSeconds) : 1.f);
	const bool bLeft = Combat && !bIsRagdoll && Combat->WantsPosedArm(EAedirHand::Left);
	const bool bRight = Combat && !bIsRagdoll && Combat->WantsPosedArm(EAedirHand::Right);
	BodyControl->SetArmsPosed(bLeft, bRight);
}

void UAedirBalanceComponent::LimitArmSpin()
{
	// A living arm never whips round faster than its muscles can turn it: a hard tug at the hand (a sudden throw of the mouse,
	// a blade striking something) flung the hand and the weapon welded to it round a whole turn within a few frames, the
	// weapon tumbling in the fist. The forearm and hand are held to what a strong swing can reach.
	if (!BodyMesh || bIsRagdoll || CVarAedirArmSpinLimit.GetValueOnGameThread() == 0)
	{
		return;
	}
	const TCHAR* const Parts[] = { TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("lowerarm_r"), TEXT("hand_r") };
	const float Limits[] = { AedirForearmMaxSpinDegrees, AedirHandMaxSpinDegrees, AedirForearmMaxSpinDegrees, AedirHandMaxSpinDegrees };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parts); ++Index)
	{
		FBodyInstance* Body = BodyMesh->GetBodyInstance(FName(Parts[Index]));
		if (!Body || !Body->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		const FVector Spin = Body->GetUnrealWorldAngularVelocityInRadians();
		const float Limit = FMath::DegreesToRadians(Limits[Index]);
		if (Spin.SizeSquared() > Limit * Limit)
		{
			Body->SetAngularVelocityInRadians(Spin.GetSafeNormal() * Limit, false);
		}
	}
}

void UAedirBalanceComponent::UpdateArmTraps(float DeltaTime)
{
	UpdateArmFree(DeltaTime);
	// A hand turned far round against its joint limits can come to rest there, the wrong way over, and the muscles that
	// should bring it back only press it harder into the limit. A living arm does not stay caught like that: an arm held
	// that far off its pose for a moment is put back where the pose has it, upper arm, forearm and hand together.
	UAedirProceduralAnimInstance* Pose = BodyMesh ? Cast<UAedirProceduralAnimInstance>(BodyMesh->GetAnimInstance()) : nullptr;
	if (!Pose || bIsRagdoll || CVarAedirArmTrapRelease.GetValueOnGameThread() == 0)
	{
		ArmTrapSeconds[0] = ArmTrapSeconds[1] = 0.f;
		return;
	}
	const UAedirStatsComponent* Stats = FindStats();
	if (Stats && (Stats->IsDead() || Stats->IsDying() || Stats->IsKnockedOut()))
	{
		return;
	}
	const UAedirCombatComponent* Combat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;
	const bool bBothHands = Combat && Combat->GetGripJointForce() >= 0.f;
	bool bRelease[2] = { false, false };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FName HandName = Side == 0 ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
		const FBodyInstance* Hand = BodyMesh->GetBodyInstance(HandName);
		// An arm moved by the pose itself cannot be caught against its joints: it is where the pose puts it.
		if (!Hand || (BodyControl && BodyControl->IsArmPosed(Side == 1)))
		{
			ArmTrapSeconds[Side] = 0.f;
			continue;
		}
		const FQuat Target = Pose->GetPoseArmWorld(Side, 2).GetRotation();
		const float OffDegrees = FMath::RadiansToDegrees(static_cast<float>(Hand->GetUnrealWorldTransform().GetRotation().AngularDistance(Target)));
		const bool bStill = Hand->GetUnrealWorldAngularVelocityInRadians().Size() < AedirArmTrapStillRadPerSecond && Hand->GetUnrealWorldVelocity().Size() < AedirArmTrapStillCmPerSecond;
		// Caught also where the hand stands well away from where the pose has it: an arm stopped against the limits of its
		// joints on the way, which the muscles only press harder into.
		const float OffCm = static_cast<float>(FVector::Dist(Hand->GetUnrealWorldTransform().GetLocation(), Pose->GetPoseArmWorld(Side, 2).GetLocation()));
		if (CVarAedirArmTrapRelease.GetValueOnGameThread() == 2 && GFrameCounter % 30 == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[ArmTrapDbg] side %d off %.0f spin %.2f"), Side, OffDegrees, Hand->GetUnrealWorldAngularVelocityInRadians().Size());
		}
		// Far enough off, it is caught even while it shakes there.
		const bool bCaught = ((OffDegrees > AedirArmTrapDegrees || OffCm > AedirArmTrapCm) && bStill) || OffCm > AedirArmTrapFarCm;
		ArmTrapSeconds[Side] = bCaught ? ArmTrapSeconds[Side] + DeltaTime : 0.f;
		ArmTrapCooldown[Side] = FMath::Max(ArmTrapCooldown[Side] - DeltaTime, 0.f);
		if (ArmTrapSeconds[Side] >= AedirArmTrapSeconds && ArmTrapCooldown[Side] <= 0.f)
		{
			bRelease[Side] = true;
		}
	}
	// Both hands on one grip go back together, or the one left caught pulls the other in again.
	if (bBothHands && (bRelease[0] || bRelease[1]))
	{
		bRelease[0] = bRelease[1] = true;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!bRelease[Side] || (BodyControl && BodyControl->IsArmPosed(Side == 1)))
		{
			continue;
		}
		ArmTrapSeconds[Side] = 0.f;
		ArmTrapCooldown[Side] = AedirArmTrapCooldownSeconds;
		// Not put back at once, which is seen as a jump: the arm passes through what caught it for a moment and its muscles,
		// harder for that moment, pull it back to its pose on the way there.
		SetArmFree(Side, true);
		ArmFreeLeft[Side] = ArmFreeSeconds;
		UE_LOG(LogTemp, Log, TEXT("[ArmTrap] %s arm of %s let go to pull itself back"), Side == 0 ? TEXT("left") : TEXT("right"), *GetNameSafe(CharacterOwner));
	}
}

void UAedirBalanceComponent::SetArmFree(int32 Side, bool bFree)
{
	const TCHAR* Suffix = Side == 0 ? TEXT("_l") : TEXT("_r");
	const TCHAR* const ArmBones[] = { TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand") };
	const ECollisionResponse Response = bFree ? ECR_Ignore : ECR_Block;
	for (const TCHAR* Part : ArmBones)
	{
		if (FBodyInstance* Body = BodyMesh ? BodyMesh->GetBodyInstance(FName(*FString::Printf(TEXT("%s%s"), Part, Suffix))) : nullptr)
		{
			Body->SetResponseToChannel(ECC_WorldStatic, Response);
			Body->SetResponseToChannel(ECC_WorldDynamic, Response);
			Body->SetResponseToChannel(ECC_PhysicsBody, Response);
		}
	}
	if (BodyControl)
	{
		BodyControl->SetArmBoost(Side == 1, bFree);
	}
}

void UAedirBalanceComponent::UpdateArmFree(float DeltaTime)
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (ArmFreeLeft[Side] > 0.f)
		{
			ArmFreeLeft[Side] -= DeltaTime;
			if (ArmFreeLeft[Side] <= 0.f)
			{
				SetArmFree(Side, false);
			}
		}
	}
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
	// The arm tuning changed from the console: everything is applied again.
	const FVector ArmTuning(CVarAedirArmHold.GetValueOnGameThread(), CVarAedirGrippedArmShare.GetValueOnGameThread(), CVarAedirArmDamping.GetValueOnGameThread());
	if (ArmTuning != AppliedArmTuning)
	{
		AppliedArmTuning = ArmTuning;
		bForce = true;
	}
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
		Common = FMath::Max(AedirLivingFallMotorShare, (bHoisting ? 1.f : DownAwakeMotorShare) * FMath::Lerp(TiredStrengthShare, 1.f, StaminaFraction) * (1.f - 0.6f * GetDazeFactor()));
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
		Strength[Combat->GetGripHand() == EAedirHand::Left ? 2 : 1] *= CVarAedirGrippedArmShare.GetValueOnGameThread();
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
	const float Hold = bLoose ? 1.f : bArm ? CVarAedirArmHold.GetValueOnGameThread() : bLeg ? 1.f : BalanceTrunkMuscleShare;
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

