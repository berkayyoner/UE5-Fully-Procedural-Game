#include "Animation/AedirGaitComponent.h"
#include "Aedir.h"
#include "HAL/IConsoleManager.h"
#include "Fists/AedirFistArms.h"

#include "Components/CapsuleComponent.h"
#include "Combat/AedirCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Items/AedirItemTypes.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "Physics/AedirBodyControlComponent.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	// Share of its swing left to an arm carrying a weapon.
	constexpr float AedirArmedArmSwingShare = 0.15f;
	constexpr float AedirSwingTwistInterpSpeed = 16.f;
	// A catching step driven at full urgency lands this much further out under the falling body.
	constexpr float AedirRecoveryStepUrgentCm = 26.f;
	// Of the turn back that keeps the face ahead while the chest turns, the head takes this share and the neck the rest.
	constexpr float AedirHeadKeepsAheadShare = 0.55f;
	// A turn of the chest starts from the hips. A fist adds its weight forward.
	constexpr float AedirSwingPelvisShare = 0.3f;
	constexpr float AedirPunchLeanInterpSpeed = 14.f;
	// Trunk inertia: degrees of lean per cm/s2 of the body's change of speed, the spring that brings it back and how far
	// it may go. Underdamped, so a hard stop rocks the chest forward and back once before it settles.
	constexpr float AedirInertiaDegreesPerAccel = 0.012f;
	constexpr float AedirInertiaSideShare = 0.35f;
	constexpr float AedirInertiaFrequency = 7.5f;
	constexpr float AedirInertiaDamping = 0.8f;
	constexpr float AedirInertiaMaxPitch = 6.f;
	constexpr float AedirInertiaMaxRoll = 3.f;
	constexpr float AedirInertiaAccelSmoothing = 14.f;
	constexpr float AedirInertiaHeadShare = 0.5f;
	// Above this the speed jumped (a teleport, a respawn): no inertia from it.
	constexpr float AedirInertiaMaxAccel = 6000.f;
	// Leaning away from a load in one hand: degrees per kilogram of difference between the hands per point of strength.
	constexpr float AedirLoadRollPerRatio = 7.f;
	constexpr float AedirLoadRollMax = 8.f;
	constexpr float AedirLoadRollInterpSpeed = 3.f;
	// The hand eases into and out of holding an item, it does not jump when the item comes or goes.
	constexpr float AedirWeaponAimFadeSeconds = 0.2f;
	constexpr float AedirWristLooseDegrees = 28.f;
	constexpr float AedirWristFirmDegrees = 40.f;
	constexpr float AedirWristHeldOutDegrees = 12.f;
	constexpr float AedirRestPhaseMin = 0.4f;
	constexpr float AedirRestPhaseMax = 0.5f;
	constexpr float AedirRestCatchUpCyclesPerSecond = 2.f;
	constexpr float AedirMinStrideLength = 10.f;
	constexpr float AedirStepHeightClearance = 4.f;
	constexpr float AedirMinStepTriggerCm = 14.f;
	// Setting off from standing: from this speed, below this share of the walking speed, and once the feet are this far behind.
	constexpr float AedirSetOffMinSpeed = 4.f;
	constexpr float AedirSetOffMaxWalkShare = 0.6f;
	constexpr float AedirSetOffStepTriggerCm = 1.5f;
	constexpr float AedirSetOffStandSeconds = 0.25f;
	constexpr float AedirTurnStepTriggerCm = 10.f;
	constexpr float AedirRestStepTriggerCm = 14.f;
	// Moving with both feet down, the foot further from its place than the other by this much is the next to step.
	constexpr float AedirTrailingFootFirstCm = 12.f;
	constexpr float AedirOutOfTurnStepShare = 1.4f;
	constexpr float AedirMinStepSeconds = 0.14f;
	constexpr float AedirMaxStepSeconds = 0.38f;
	constexpr float AedirLandingOverlapStart = 0.75f;
	constexpr float AedirOverlapMinWalkShare = 1.f;
	constexpr float AedirLandingOverlapShare = 1.15f;
	constexpr float AedirLandingLeadSeconds = 0.03f;
	constexpr float AedirPlaceStepSeconds = 0.32f;
	constexpr float AedirPhaseCorrectionSpeed = 10.f;
	constexpr float AedirReachHipHingeShare = 0.5f;
	constexpr float AedirFootDropLiftLimit = 0.25f;
	constexpr float AedirLegComfortShare = 0.995f;
	constexpr float AedirFootDropWalkMaxCm = 16.f;
	constexpr float AedirFootDropRunMaxCm = 6.f;
	constexpr float AedirStairMinSpeedScale = 0.6f;
	constexpr float AedirFootDropDownSpeed = 7.f;
	constexpr float AedirFootDropUpSpeed = 2.5f;
	constexpr float AedirMinGaitCyclesPerSecond = 0.5f;
	constexpr float AedirSlowStrideMinScale = 0.8f;
	constexpr float JumpWindupPeakAt = 0.6f;
	constexpr float AirFootTuckCm = 14.f;
	constexpr float AirLegTuckLift = 0.35f;
	constexpr float FootLeashTriggerShare = 1.25f;
	constexpr float FootLeashMinCm = 36.f;
	// A running foot pushes off further behind the body than it landed ahead.
	constexpr float RunTrailShare = 1.14f;
	constexpr float AedirRunBlendInterpSpeed = 6.f;
	constexpr float AedirMovingBounceFrequency = 2.f;

	float WrapUnit(float Value)
	{
		return Value - FMath::Floor(Value);
	}

	void QueryReachPose(const UAedirInteractionComponent* Interaction, EAedirHand Hand, FVector& OutTargetWorld, float& OutAlpha)
	{
		FVector TargetWorld = FVector::ZeroVector;
		float Alpha = 0.f;
		const bool bReaching = Interaction && Interaction->GetReachPose(Hand, TargetWorld, Alpha);

		OutTargetWorld = bReaching ? TargetWorld : FVector::ZeroVector;
		OutAlpha = bReaching ? FMath::Clamp(Alpha, 0.f, 1.f) : 0.f;
	}

	// Accumulates arm targets from the lowest priority up. Each higher priority target blends over the result so far.
	struct FAedirArmTargetBlend
	{
		FVector Target = FVector::ZeroVector;
		float Alpha = 0.f;

		void Apply(const FVector& HigherTarget, float HigherAlpha)
		{
			if (HigherAlpha <= KINDA_SMALL_NUMBER)
			{
				return;
			}

			const float Weight = FMath::Clamp(HigherAlpha, 0.f, 1.f);
			Target = Alpha > KINDA_SMALL_NUMBER ? FMath::Lerp(Target, HigherTarget, Weight) : HigherTarget;
			Alpha = FMath::Max(Alpha, Weight);
		}
	};
}

UAedirGaitComponent::UAedirGaitComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAedirGaitComponent::BeginPlay()
{
	Super::BeginPlay();

	CreatureOwner = Cast<AAedirCreature>(GetOwner());
	if (!CreatureOwner)
	{
		return;
	}

	MovementComponent = Cast<UAedirCharacterMovementComponent>(CreatureOwner->GetCharacterMovement());
	BalanceComponent = CreatureOwner->Balance;
	BodyMesh = CreatureOwner->GetMesh();

	if (BodyMesh)
	{
		// The mesh pose must be evaluated after the gait state of this frame has been computed.
		BodyMesh->AddTickPrerequisiteComponent(this);
	}
}

void UAedirGaitComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CreatureOwner || !MovementComponent || !BodyMesh || DeltaTime <= 0.f)
	{
		return;
	}

	if (BalanceComponent && BalanceComponent->IsRagdoll())
	{
		ResetForRagdoll();
		FAedirGetUpStage Stage;
		float Progress = 0.f;
		const bool bHoisted = BalanceComponent->GetHoistStage(Stage, Progress);
		GaitState.HoistPoseShare = bHoisted ? BalanceComponent->GetHoistShare() : 0.f;
		GaitState.HoistHipBend = Stage.HipBend;
		GaitState.HoistKneeBend = Stage.KneeBend;
		GaitState.HoistBackBow = Stage.BackBow;
		GaitState.HoistArmRaise = Stage.ArmRaise;
		GaitState.HoistElbowBend = Stage.ElbowBend;
		return;
	}

	const FTransform ActorTransform = CreatureOwner->GetActorTransform();
	const float ActorScale = FMath::Max(FMath::Abs(CreatureOwner->GetActorScale3D().X), KINDA_SMALL_NUMBER);
	const FVector ActorLocation = ActorTransform.GetLocation();
	const FVector ActorUp = CreatureOwner->GetActorUpVector();
	const FVector ActorRight2D = CreatureOwner->GetActorRightVector().GetSafeNormal2D();
	const FVector ActorForward2D = CreatureOwner->GetActorForwardVector().GetSafeNormal2D();
	const float CapsuleHalfHeight = CreatureOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float BottomZ = ActorLocation.Z - CapsuleHalfHeight;

	const FVector Velocity = MovementComponent->Velocity;
	const float Speed2D = Velocity.Size2D();
	const float MaxGroundSpeed = MovementComponent->WalkSpeed * MovementComponent->RunSpeedMultiplier;
	const float SpeedAlpha = MaxGroundSpeed > KINDA_SMALL_NUMBER ? FMath::Clamp(Speed2D / MaxGroundSpeed, 0.f, 1.f) : 0.f;
	// In the air the legs hang and tuck: no stepping, no target search.
	const bool bAirborne = MovementComponent->IsFalling();
	// Setting off: the body is pushed the way it wants to go and has only just begun to move. The first step is taken at once,
	// as a walker leans into it, not once the body has already been carried past its feet.
	const FVector InputAcceleration = MovementComponent->GetCurrentAcceleration();
	const bool bSettingOff = !bAirborne && Speed2D > AedirSetOffMinSpeed && Speed2D < MovementComponent->WalkSpeed * AedirSetOffMaxWalkShare
		&& FVector::DotProduct(InputAcceleration.GetSafeNormal2D(), Velocity.GetSafeNormal2D()) > 0.5f;
	const bool bMoving = !bAirborne && (Speed2D > MovingSpeedThreshold || bSettingOff);
	const bool bTurning = !bAirborne && MovementComponent->IsTurningInPlace();
	// A falling centre of mass requests a stumble step, which also starts a step from standing.
	FVector RecoveryDirection = FVector::ZeroVector;
	float RecoveryUrgency = 0.f;
	const bool bRecovering = !bAirborne && BalanceComponent && BalanceComponent->GetRecoveryStepRequest(RecoveryDirection, RecoveryUrgency);
	const bool bStepping = bMoving || bTurning || bRecovering;

	const UAedirCombatComponent* Combat = CreatureOwner->Combat.Get();
	GaitState.StanceAlpha = FMath::FInterpTo(GaitState.StanceAlpha, (Combat && Combat->IsInFightStance()) ? 1.f : 0.f, DeltaTime, StanceInterpSpeed);
	const float StanceAlpha = GaitState.StanceAlpha;
	const bool bRightLead = Combat && Combat->IsLeadFootRight();
	{
		const UAedirFistArms* StanceFists = CreatureOwner->FindComponentByClass<UAedirFistArms>();
		const float FistsUp = StanceFists ? FMath::Max(StanceFists->GetRaised(EAedirHand::Left), StanceFists->GetRaised(EAedirHand::Right)) : 0.f;
		FistStanceAlpha = FMath::FInterpTo(FistStanceAlpha, FistsUp, DeltaTime, StanceInterpSpeed);
	}
	// How far the feet are set one before the other, and the hips and knees sunk: by the fight stance, or the fists' guard.
	const float StaggerAlpha = FMath::Max(StanceAlpha, FistStanceAlpha * FistStanceFootShare);
	// Crouched, the body is already low: the stance bends it no further.
	const float SinkAlpha = FMath::Max(StanceAlpha, FistStanceAlpha * FistStanceBendShare) * (1.f - CrouchAlpha);

	// Kick: the rear leg throws the kick while the lead leg holds the body up.
	bool bKickLeftLeg = false;
	FVector KickFootTargetWorld = FVector::ZeroVector;
	float KickAlpha = 0.f;
	const bool bKicking = Combat && Combat->GetKickPose(bKickLeftLeg, KickFootTargetWorld, KickAlpha);
	const int32 KickFoot = bKickLeftLeg ? 0 : 1;

	const FVector LocalDirection = ActorTransform.InverseTransformVectorNoScale(Velocity).GetSafeNormal2D();
	const float BackAlpha = FMath::Clamp(-LocalDirection.X, 0.f, 1.f);
	const float SideAlpha = FMath::Clamp(FMath::Abs(LocalDirection.Y), 0.f, 1.f);
	const float DirectionalStrideScale = FMath::Lerp(1.f, BackStrideScale, BackAlpha) * FMath::Lerp(1.f, SideStrideScale, SideAlpha);

	const float Athletism = CreatureOwner->Stats ? CreatureOwner->Stats->Athletism : 0.f;
	const float AthleticsScale = 1.f + Athletism / FMath::Max(AthleticsStrideDivisor, KINDA_SMALL_NUMBER);
	// The stance shortens walking strides only, running keeps its full stride.
	const float StanceStrideScale = FMath::Lerp(1.f, StanceWalkStrideMultiplier, StanceAlpha * (1.f - RunBlend));
	// The stride follows the absolute speed: a walk keeps the walking stride, only speeds past it blend to the running stride,
	// and slower movement (crouching, backing up) takes shorter steps. SpeedAlpha alone is relative to the top run speed.
	const float WalkSpeedCm = FMath::Max(MovementComponent->WalkSpeed, 1.f);
	const float RunSpeedCm = WalkSpeedCm * MovementComponent->RunSpeedMultiplier;
	const float RunMix = FMath::Clamp((Speed2D - WalkSpeedCm) / FMath::Max(RunSpeedCm - WalkSpeedCm, 1.f), 0.f, 1.f);
	const float SlowStrideScale = FMath::Max(FMath::Clamp(Speed2D / WalkSpeedCm, AedirSlowStrideMinScale, 1.f), CrouchStrideShare * CrouchAlpha);
	const float StrideLength = FMath::Max(
		FMath::Lerp(StrideWalkCm * SlowStrideScale, StrideRunCm, RunMix) * ActorScale * DirectionalStrideScale * AthleticsScale * StairFactor * StanceStrideScale,
		AedirMinStrideLength);
	const float SwingFraction = FMath::Lerp(SwingFractionWalk, SwingFractionRun, RunMix);
	const FVector MoveDirection = bMoving ? Velocity.GetSafeNormal2D() : ActorForward2D;

	// How far ahead of the hip a foot lands. A slowing body only reaches as far as it still travels,
	// so the last step before a stop never aims at a point it cannot get to.
	const float LandingShare = FMath::Lerp(StepLandingShareWalk, StepLandingShareRun, RunMix);
	const float LandingOffsetCm = FMath::Min(StrideLength * LandingShare, Speed2D * LandingReachSeconds);
	// A foot is lifted once the body has carried it as far behind the hip as it landed ahead, so its range stays centred under the body.
	const float StepTriggerCm = FMath::Max(2.f * LandingOffsetCm * FMath::Lerp(1.f, RunTrailShare, RunMix), AedirMinStepTriggerCm * ActorScale);
	const float EffectiveStride = StepTriggerCm / FMath::Max(1.f - SwingFraction, KINDA_SMALL_NUMBER);

	AdvancePhase(DeltaTime, bMoving, bTurning, Speed2D, EffectiveStride);

	FVector LandingLocation[2];
	FVector LandingNormal[2];
	FVector AheadLocation[2];
	FVector HomeLocation[2];

	// Crouching, and bending down for something, set the feet wider apart.
	const UAedirInteractionComponent* WidenInteraction = CreatureOwner->Interaction.Get();
	const float ReachWidenAlpha = FMath::Clamp((WidenInteraction ? WidenInteraction->GetPelvisLowerZ() : 0.f) / ReachLowerMaxCm, 0.f, 1.f);
	const float PostureWidthScale = 1.f + (CrouchFootWidenShare * CrouchAlpha) + (CrouchFootWidenShare * ReachWidenAlpha);
	const float StanceWidthScale = FMath::Max(FMath::Lerp(1.f, StanceFootWidthMultiplier, StanceAlpha), FMath::Lerp(1.f, FistStanceWidthMultiplier, FistStanceAlpha)) * PostureWidthScale;
	const float StanceLeadForwardCm = StanceLeadFootForwardCm * StaggerAlpha * ActorScale;
	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const float Side = Foot == 0 ? -1.f : 1.f;
		// Foot 1 is the right foot. The lead foot moves forward, the rear foot back by a share of that.
		const bool bLeadFoot = (Foot == 1) == bRightLead;
		const float StanceForwardCm = StanceLeadForwardCm * (bLeadFoot ? 1.f : -StanceRearFootBackShare);
		// Side-stepping: the foot on the far side crosses in front of the leading foot, which stays behind, so the feet never collide.
		const float StrafeSign = LocalDirection.Y < 0.f ? -1.f : 1.f;
		const float StrafeStaggerCm = bMoving ? -Side * StrafeSign * StrafeFootStaggerCm * SideAlpha * ActorScale : 0.f;
		const float StrafeWidthScale = 1.f + (bMoving ? StrafeFootWidenShare * SideAlpha : 0.f);
		const FVector HomeXY = ActorLocation + ActorRight2D * (Side * HipHalfWidthCm * StanceWidthScale * StrafeWidthScale * ActorScale) + ActorForward2D * (StanceForwardCm + StrafeStaggerCm);
		HomeLocation[Foot] = HomeXY;
		// The body keeps moving while the pose catches up, so the landing leads it by a moment to stay centred under the hip.
		const FVector LandingXY = HomeXY + (bMoving ? MoveDirection * (LandingOffsetCm + Speed2D * AedirLandingLeadSeconds) : FVector::ZeroVector);

		SampleGround(LandingXY, BottomZ, LandingLocation[Foot], LandingNormal[Foot]);

		AheadLocation[Foot] = LandingLocation[Foot];
		if (bMoving)
		{
			FVector AheadNormal;
			SampleGround(LandingXY + MoveDirection * (StairProbeDistanceCm * ActorScale), BottomZ, AheadLocation[Foot], AheadNormal);
		}
	}

	if (!bFeetInitialized)
	{
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			PlantedContact[Foot] = LandingLocation[Foot];
			PlantedNormal[Foot] = LandingNormal[Foot];
			bSwinging[Foot] = false;
		}
		bFeetInitialized = true;
	}

	// A kick needs a planted support leg: a step that is still running lands where the leg stands.
	if (bKicking)
	{
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			if (bSwinging[Foot])
			{
				bSwinging[Foot] = false;
				PlantedContact[Foot] = LandingLocation[Foot];
				PlantedNormal[Foot] = LandingNormal[Foot];
			}
		}
	}

	// Stepping is decided by where each foot is relative to the body, every frame, and never depends on a clock:
	// the next foot in turn lifts as soon as the body has left it behind, so a step cannot be missed.
	RecoveryCooldownRemaining = FMath::Max(RecoveryCooldownRemaining - DeltaTime, 0.f);
	const bool bFootInAir = bSwinging[0] || bSwinging[1];
	if (!bAirborne && !bKicking)
	{
		const float CycleSeconds = EffectiveStride / FMath::Max(Speed2D, 1.f);
		const float MovingStepSeconds = FMath::Clamp(CycleSeconds * SwingFraction, AedirMinStepSeconds, AedirMaxStepSeconds);
		const bool bFirstStep = bSettingOff && !bFootInAir && FMath::Min(PlantedSeconds[0], PlantedSeconds[1]) > AedirSetOffStandSeconds;
		const float Trigger = bFirstStep ? AedirSetOffStepTriggerCm * ActorScale : (bMoving ? StepTriggerCm : (bTurning ? AedirTurnStepTriggerCm : AedirRestStepTriggerCm) * ActorScale);
		const float StepSeconds = bMoving ? MovingStepSeconds : AedirPlaceStepSeconds;

		// In the stance the rear foot may stand further back than its place, up on its toes, without stepping back up to it:
		// only standing still, and only behind its place, is the way back counted the less.
		const FVector Ahead2D = CreatureOwner->GetActorForwardVector().GetSafeNormal2D();
		const int32 RearFoot = bRightLead ? 0 : 1;
		const float RearSlack = bMoving ? 1.f : FMath::Lerp(1.f, FMath::Max(StanceRearToeSlack, 1.f), StaggerAlpha);
		const auto FootDistance = [&](int32 Foot)
		{
			FVector Off = PlantedContact[Foot] - LandingLocation[Foot];
			Off.Z = 0.f;
			const double Along = FVector::DotProduct(Off, Ahead2D);
			if (Foot == RearFoot && Along < 0.0)
			{
				Off -= Ahead2D * (Along * (1.0 - 1.0 / RearSlack));
			}
			return static_cast<float>(Off.Size());
		};

		// Setting off or walking on, the foot furthest from where it should be goes first: a rear foot left back in the
		// stance is not stretched after the body while the other steps.
		if (bMoving && !bFootInAir)
		{
			const float Off0 = FootDistance(0);
			const float Off1 = FootDistance(1);
			const int32 Furthest = Off0 > Off1 ? 0 : 1;
			if (Furthest != NextStepFoot && FMath::Abs(Off0 - Off1) > AedirTrailingFootFirstCm * ActorScale)
			{
				NextStepFoot = Furthest;
			}
		}

		// Normally one foot at a time. A foot the body is pulling away from may lift while the other is just coming down.
		const int32 LaterFoot = 1 - NextStepFoot;
		const bool bLaterLanding = bSwinging[LaterFoot] && SwingElapsed[LaterFoot] / FMath::Max(SwingDuration[LaterFoot], KINDA_SMALL_NUMBER) > AedirLandingOverlapStart;
		// Up to walking pace and on steady feet there is only ever one foot in the air, and the body settles on both for a moment between steps.
		const bool bMayOverlap = Speed2D > WalkSpeedCm * AedirOverlapMinWalkShare || bRecovering || (BalanceComponent && BalanceComponent->IsStaggering());
		// Running, the feet may both be off the ground between steps; the faster, the sooner the next one goes.
		const float OverlapShare = FMath::Lerp(AedirLandingOverlapShare, RunFlightTriggerShare, RunMix);
		const bool bDoubleSupportDone = PlantedSeconds[LaterFoot] >= CycleSeconds * FMath::Max(0.5f - SwingFraction, 0.f);
		const bool bNextMayStep = !bSwinging[NextStepFoot]
			&& (bSwinging[LaterFoot] ? (bMayOverlap && bLaterLanding && FootDistance(NextStepFoot) >= Trigger * OverlapShare) : (bMayOverlap || bDoubleSupportDone || bFirstStep));

		if (bRecovering && !bFootInAir && (RecoveryCooldownRemaining <= 0.f || RecoveryUrgency > 0.7f))
		{
			// The foot furthest towards the fall direction takes the catching step.
			const double ReachLeft = FVector::DotProduct(PlantedContact[0] - ActorLocation, RecoveryDirection);
			const double ReachRight = FVector::DotProduct(PlantedContact[1] - ActorLocation, RecoveryDirection);
			// The harder it was driven, the further the catching foot goes out under it.
			const FVector CatchWorld = BalanceComponent->GetCenterOfMassWorld() + RecoveryDirection * (RecoveryStepLandingCm + RecoveryUrgency * AedirRecoveryStepUrgentCm);
			BeginRecoverySwing(ReachRight > ReachLeft ? 1 : 0, CatchWorld, StrideLength * 0.5f * RecoveryStepMaxStrideMultiplier, RecoveryUrgency, BottomZ);
			RecoveryCooldownRemaining = RecoveryStepCooldownSeconds;
			BalanceComponent->OnRecoveryStepTaken();
		}
		else if (bNextMayStep && FootDistance(NextStepFoot) >= Trigger)
		{
			LogStep(NextStepFoot, LandingLocation[NextStepFoot], Trigger);
			BeginStep(NextStepFoot, StepSeconds);
		}
		else if (!bFootInAir && bMayOverlap && FootDistance(1 - NextStepFoot) >= Trigger * AedirOutOfTurnStepShare && FootDistance(1 - NextStepFoot) >= FootDistance(NextStepFoot))
		{
			LogStep(1 - NextStepFoot, LandingLocation[1 - NextStepFoot], Trigger * AedirOutOfTurnStepShare);
			BeginStep(1 - NextStepFoot, StepSeconds);
		}
	}

	float RiseCm[2] = { 0.f, 0.f };
	float MaxRise = 0.f;
	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		RiseCm[Foot] = FMath::Max(
			FMath::Abs(LandingLocation[Foot].Z - PlantedContact[Foot].Z),
			FMath::Abs(AheadLocation[Foot].Z - LandingLocation[Foot].Z));
		MaxRise = FMath::Max(MaxRise, RiseCm[Foot]);
	}

	const float StairTarget = FMath::Lerp(1.f, StairStrideMultiplier, FMath::Clamp(MaxRise / StairHeightForShorteningCm, 0.f, 1.f));
	StairFactor = FMath::FInterpTo(StairFactor, StairTarget, DeltaTime, StairFactorInterpSpeed);

	// A bigger body lifts its feet as much higher as it is bigger; crouched, higher still under the bent knees.
	const float SpeedStepHeight = (BaseStepHeightCm + StepHeightSpeedBonusCm * SpeedAlpha + RunStepHeightBonusCm * RunMix + CrouchStepHeightBonusCm * CrouchAlpha) * ActorScale;
	const float AnkleRestCm = (AnkleRefHeightCm > 0.f ? AnkleRefHeightCm : AnkleHeightCm) * ActorScale + FootGroundLiftCm;
	FVector Contact[2];
	FVector AnkleWorld[2];
	float Lift[2] = { 0.f, 0.f };
	FVector TargetNormal[2];
	// A broken leg is limped on: which one (-1 none).
	const UAedirWoundComponent* Wounds = CreatureOwner->Wounds.Get();
	const int32 BrokenLeg = !Wounds ? -1 : Wounds->IsLimbBroken(EAedirLimb::LeftLeg) ? 0 : Wounds->IsLimbBroken(EAedirLimb::RightLeg) ? 1 : -1;

	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		if (bAirborne)
		{
			// The foot hangs under its hip, a little tucked, and lands from there.
			bSwinging[Foot] = false;
			const FVector Hanging(HomeLocation[Foot].X, HomeLocation[Foot].Y, BottomZ + AirFootTuckCm * ActorScale);
			Contact[Foot] = Hanging;
			PlantedContact[Foot] = Hanging;
			PlantedNormal[Foot] = FVector::UpVector;
			TargetNormal[Foot] = FVector::UpVector;
			Lift[Foot] = AirLegTuckLift;
			AnkleWorld[Foot] = Hanging + FVector::UpVector * AnkleRestCm;
			continue;
		}

		PlantedSeconds[Foot] = bSwinging[Foot] ? 0.f : PlantedSeconds[Foot] + DeltaTime;

		FVector FootPosition;
		if (bSwinging[Foot])
		{
			SwingElapsed[Foot] += DeltaTime;
			const float T = FMath::Clamp(SwingElapsed[Foot] / FMath::Max(SwingDuration[Foot], KINDA_SMALL_NUMBER), 0.f, 1.f);
			const float Smooth = T * T * (3.f - 2.f * T);
			// The foot comes up off the ground early in the step and is let down more slowly, so the toes leaving it behind
			// clear it at once and do not slide along.
			const float Arc = FMath::Sin(UE_PI * FMath::Pow(T, FMath::Clamp(StepLiftEarlyExponent, 0.3f, 1.f)));
			const FVector& Target = bSwingFixedTarget[Foot] ? FixedLandingWorld[Foot] : LandingLocation[Foot];
			const FVector& TargetUp = bSwingFixedTarget[Foot] ? FixedLandingNormal[Foot] : LandingNormal[Foot];
			const float Rise = bSwingFixedTarget[Foot] ? static_cast<float>(FMath::Abs(Target.Z - SwingStartContact[Foot].Z)) : RiseCm[Foot];
			// A broken leg is swung through low, the knee hardly bent.
			const float LimpLift = Foot == BrokenLeg ? LimpLiftShare : 1.f;
			const float StepHeight = FMath::Clamp(FMath::Max(SpeedStepHeight * LimpLift, Rise + AedirStepHeightClearance), 0.f, MaxStepHeightCm * ActorScale);

			Contact[Foot] = FMath::Lerp(SwingStartContact[Foot], Target, Smooth);
			FootPosition = Contact[Foot] + FVector::UpVector * (StepHeight * Arc);
			Lift[Foot] = Arc * LimpLift;
			TargetNormal[Foot] = TargetUp;

			if (T >= 1.f)
			{
				bSwinging[Foot] = false;
				PlantedContact[Foot] = Target;
				PlantedNormal[Foot] = TargetUp;
			}
		}
		else
		{
			// A planted foot follows the real ground under it, on a slope or while the body changes height, so it never hovers or sinks.
			FVector PlantedGround;
			FVector PlantedGroundNormal;
			if (SampleGround(PlantedContact[Foot], BottomZ, PlantedGround, PlantedGroundNormal))
			{
				PlantedContact[Foot].Z = PlantedGround.Z;
				PlantedNormal[Foot] = PlantedGroundNormal;
			}
			Contact[Foot] = PlantedContact[Foot];
			FootPosition = Contact[Foot];
			TargetNormal[Foot] = PlantedNormal[Foot];
		}

		AnkleWorld[Foot] = FootPosition + FVector::UpVector * AnkleRestCm;
	}

	// Safety leash: whatever happened above, no foot may stay further from under its hip than the stride allows.
	// A foot beyond the leash is pulled back onto it, so a foot left behind is never visible.
	if (!bAirborne)
	{
		const float LeashCm = FMath::Max(StepTriggerCm * FootLeashTriggerShare, FootLeashMinCm * ActorScale);
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			if (bKicking && Foot == KickFoot)
			{
				continue;
			}

			FVector Away = Contact[Foot] - HomeLocation[Foot];
			Away.Z = 0.f;
			const float AwayDistance = static_cast<float>(Away.Size());
			if (AwayDistance > LeashCm)
			{
				const FVector Pull = Away / AwayDistance * (AwayDistance - LeashCm);
				LegLogLeash[Foot] += static_cast<float>(Pull.Size());
				AnkleWorld[Foot] -= Pull;
				Contact[Foot] -= Pull;
				PlantedContact[Foot] -= FVector(Pull.X, Pull.Y, 0.f);
				if (bSwinging[Foot])
				{
					SwingStartContact[Foot] -= FVector(Pull.X, Pull.Y, 0.f);
				}
			}
		}
	}

	// The kicking leg follows the kick arc, blended in by the kick pose alpha.
	if (bKicking)
	{
		AnkleWorld[KickFoot] = FMath::Lerp(AnkleWorld[KickFoot], KickFootTargetWorld, KickAlpha);
		Lift[KickFoot] = FMath::Lerp(Lift[KickFoot], 1.f, KickAlpha);
	}

	GaitState.LeftFootWorld = AnkleWorld[0];
	GaitState.RightFootWorld = AnkleWorld[1];
	GaitState.LeftGroundZ = bAirborne || (bKicking && KickFoot == 0) ? -UE_BIG_NUMBER : static_cast<float>(Contact[0].Z);
	GaitState.RightGroundZ = bAirborne || (bKicking && KickFoot == 1) ? -UE_BIG_NUMBER : static_cast<float>(Contact[1].Z);
	GaitState.LeftFootLift = Lift[0];
	GaitState.RightFootLift = Lift[1];
	GaitState.LeftFootRot = ComputeFootTilt(TargetNormal[0], ActorUp, GaitState.LeftFootRot, DeltaTime);
	GaitState.RightFootRot = ComputeFootTilt(TargetNormal[1], ActorUp, GaitState.RightFootRot, DeltaTime);

	// Stairs make the body climb more slowly.
	MovementComponent->SetStairSpeedScale(FMath::GetMappedRangeValueClamped(FVector2f(StairStrideMultiplier, 1.f), FVector2f(AedirStairMinSpeedScale, 1.f), StairFactor));

	const float RunTarget = MovementComponent->IsRunningNow() ? SpeedAlpha : 0.f;
	RunBlend = FMath::FInterpTo(RunBlend, RunTarget, DeltaTime, AedirRunBlendInterpSpeed);

	const float ContactAverageZ = (Contact[0].Z + Contact[1].Z) * 0.5f;
	// The pelvis follows the average foot height but sinks mostly to the lower foot, so a foot on a lower step can still reach its floor.
	const float LowestContactZ = FMath::Min(Contact[0].Z, Contact[1].Z);
	const float HipFollowZ = LowestContactZ < ContactAverageZ ? FMath::Lerp(ContactAverageZ, LowestContactZ, HipLowerFootShare) : ContactAverageZ;
	const float DynamicHipTarget = bAirborne ? 0.f : FMath::Clamp(HipFollowZ - BottomZ, HipDynamicMinCm, HipDynamicMaxCm);
	DynamicHipOffset = FMath::FInterpTo(DynamicHipOffset, DynamicHipTarget, DeltaTime, HipOffsetInterpSpeed);
	const float RunLowering = -HipRunLoweringCm * RunBlend;
	// A walk is already a clear pelvis motion, so the gait terms start at 70% of their size and not at zero.
	const float GaitIntensity = bMoving ? FMath::Lerp(0.7f, 1.f, SpeedAlpha) : 0.f;
	// The pelvis only ever sinks below its standing height, deepest while both feet are down, so no foot is lifted off the ground by it.
	const float Bounce = -HipBounceCm * FMath::Lerp(1.f, HipBounceRunScale, RunBlend) * (0.5f + 0.5f * FMath::Cos(2.f * UE_PI * Phase * AedirMovingBounceFrequency)) * GaitIntensity;
	GaitState.HipOffsetZ = DynamicHipOffset + RunLowering + Bounce;
	// Limping: while the sound foot is in the air the broken leg alone bears the weight, and gives under it.
	const float LimpBearing = BrokenLeg >= 0 && !bAirborne ? Lift[1 - BrokenLeg] : 0.f;
	const float BrokenSide = BrokenLeg == 1 ? 1.f : -1.f;
	GaitState.HipOffsetZ -= LimpDipCm * ActorScale * LimpBearing;

	// The interaction component smooths its outputs itself, so the reach values are applied directly each frame.
	const UAedirInteractionComponent* Interaction = CreatureOwner->Interaction.Get();
	// Reaching down for something, or striking down low with the fists.
	const UAedirFistArms* LowFists = CreatureOwner->FindComponentByClass<UAedirFistArms>();
	const float ReachLowerCm = (Interaction ? FMath::Max(Interaction->GetPelvisLowerZ(), 0.f) : 0.f) + (LowFists ? LowFists->GetHipDrop() * ActorScale : 0.f);
	const float ReachLowerAlpha = FMath::Clamp(ReachLowerCm / FMath::Max(ReachLowerMaxCm, KINDA_SMALL_NUMBER), 0.f, 1.f);
	GaitState.HipOffsetZ -= ReachLowerCm;
	GaitState.HipForwardOffset = Interaction ? Interaction->GetReachHipForward() * ActorScale : 0.f;
	GaitState.HipOffsetZ -= StanceHipLowerCm * SinkAlpha;

	// Crouching shrinks the capsule, so the pelvis drops by the same amount and the camera boom stays on the body.
	const float CrouchDropCm = FMath::Max(CreatureOwner->GetDefaultHalfHeight() - MovementComponent->GetCrouchedHalfHeight(), 0.f) * ActorScale;
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, CreatureOwner->bIsCrouched ? 1.f : 0.f, DeltaTime, 3.5f);
	GaitState.HipOffsetZ -= CrouchDropCm * CrouchAlpha;
	// Going over, the legs give and the hips sink.
	GaitState.HipOffsetZ -= BalanceComponent ? BalanceComponent->GetBalanceSinkCm() : 0.f;

	// Jump wind-up: the body sinks before the push-off. Landing: the pelvis absorbs the fall and springs back, less so for athletic bodies.
	// The dip deepens to its peak, then the legs extend again: the push-off is visible before the feet leave the ground.
	const float WindupAlpha = MovementComponent->GetJumpWindupAlpha();
	const float WindupDip = WindupAlpha < JumpWindupPeakAt
		? FMath::Sin(WindupAlpha / JumpWindupPeakAt * UE_HALF_PI)
		: FMath::Cos((WindupAlpha - JumpWindupPeakAt) / (1.f - JumpWindupPeakAt) * UE_HALF_PI);
	GaitState.HipOffsetZ -= JumpWindupDipCm * ActorScale * WindupDip;
	if (bAirborne)
	{
		AirFallSpeed = FMath::Max(AirFallSpeed, -static_cast<float>(Velocity.Z));
	}
	else if (AirFallSpeed > 0.f)
	{
		// The landing speed becomes a downward velocity of the pelvis, which a spring-damper then turns into a visible sink and recovery.
		const float AthleticsAbsorb = 1.f - 0.6f * FMath::Clamp(Athletism / 100.f, 0.f, 1.f);
		LandingDipVelocity += FMath::Min(AirFallSpeed, 1500.f) * LandingDipKick * AthleticsAbsorb * ActorScale;
		AirFallSpeed = 0.f;
	}
	const float DipAcceleration = -LandingDipStiffness * LandingDipCm - LandingDipDamping * LandingDipVelocity;
	LandingDipVelocity += DipAcceleration * DeltaTime;
	LandingDipCm = FMath::Clamp(LandingDipCm + LandingDipVelocity * DeltaTime, 0.f,LandingDipMaxCm * ActorScale);
	GaitState.HipOffsetZ -= LandingDipCm;

	// The pelvis sinks just enough for every foot on the ground to be reachable. Without this the leg runs out of length and the foot hovers.
	CacheLegGeometry();
	float NeededFootDrop = 0.f;
	if (!bAirborne && LegLengthCm[0] > 0.f)
	{
		const FTransform MeshTransform = BodyMesh->GetComponentTransform();
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			if (Lift[Foot] > AedirFootDropLiftLimit)
			{
				continue;
			}

			const FVector Thigh = MeshTransform.TransformPosition(ThighRefLocation[Foot]) + FVector::UpVector * GaitState.HipOffsetZ;
			const FVector ToAnkle = AnkleWorld[Foot] - Thigh;
			const float MaxLength = LegLengthCm[Foot] * ActorScale * AedirLegComfortShare;
			const float Horizontal = FMath::Min(static_cast<float>(ToAnkle.Size2D()), MaxLength * 0.95f);
			const float VerticalReach = FMath::Sqrt(MaxLength * MaxLength - Horizontal * Horizontal);
			NeededFootDrop = FMath::Max(NeededFootDrop, static_cast<float>(-ToAnkle.Z) - VerticalReach);
		}
	}
	const float MaxFootDrop = FMath::Lerp(AedirFootDropWalkMaxCm, AedirFootDropRunMaxCm, RunMix) * ActorScale;
	const float FootDropTarget = FMath::Clamp(NeededFootDrop, 0.f, MaxFootDrop);
	FootReachDropCm = FMath::FInterpTo(FootReachDropCm, FootDropTarget, DeltaTime, FootDropTarget > FootReachDropCm ? AedirFootDropDownSpeed : AedirFootDropUpSpeed);
	GaitState.HipOffsetZ -= FootReachDropCm;

	FVector LeftReachTarget = FVector::ZeroVector;
	FVector RightReachTarget = FVector::ZeroVector;
	float LeftReachAlpha = 0.f;
	float RightReachAlpha = 0.f;
	// Protective reflex: both hands reach for the ground ahead of a fall, spread along the character right axis.
	FVector ProtectiveGround = FVector::ZeroVector;
	float ProtectiveAlpha = 0.f;
	if (BalanceComponent)
	{
		BalanceComponent->GetProtectiveReflex(ProtectiveAlpha, ProtectiveGround);
	}
	const FVector ProtectiveLeftTarget = ProtectiveGround - ActorRight2D * (ProtectiveArmSpreadCm * ActorScale);
	const FVector ProtectiveRightTarget = ProtectiveGround + ActorRight2D * (ProtectiveArmSpreadCm * ActorScale);

	UpdateClutch(DeltaTime, Combat, Interaction, ActorTransform);
	LogWrists();
	LogLegs();
	ResolveArmReach(EAedirHand::Left, Combat, Interaction, ActorTransform, ProtectiveLeftTarget, ProtectiveAlpha, LeftReachTarget, LeftReachAlpha);
	ResolveArmReach(EAedirHand::Right, Combat, Interaction, ActorTransform, ProtectiveRightTarget, ProtectiveAlpha, RightReachTarget, RightReachAlpha);

	UpdateTrunkInertia(DeltaTime, ActorTransform);
	{
		const auto HeldWeight = [Interaction](EAedirHand Hand)
		{
			const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
			return Item ? Item->Weight : 0.f;
		};
		const float Left = HeldWeight(EAedirHand::Left);
		const float Right = HeldWeight(EAedirHand::Right);
		// One item in both hands is carried in the middle and pulls to no side.
		const bool bShared = Interaction && Interaction->GetHeldItem(EAedirHand::Left) && Interaction->GetHeldItem(EAedirHand::Left) == Interaction->GetHeldItem(EAedirHand::Right);
		const float Strength = CreatureOwner->Stats ? FMath::Max(CreatureOwner->Stats->Strength, 1.f) : 10.f;
		const float RollTarget = bShared ? 0.f : FMath::Clamp((Left - Right) / Strength * AedirLoadRollPerRatio, -AedirLoadRollMax, AedirLoadRollMax);
		LoadRoll = FMath::FInterpTo(LoadRoll, RollTarget, DeltaTime, AedirLoadRollInterpSpeed);
	}
	const FRotator BalanceLean = BalanceComponent ? BalanceComponent->GetPelvisLean() : FRotator::ZeroRotator;
	// The stride turn of the hips and, against them, of the chest; held back in a fighting stance and when stepping sideways
	// or back.
	const float StrideTurnScale = ArmBlend * GaitSwagger * FMath::Lerp(1.f, StanceStrideTurnShare, FMath::Max(StanceAlpha, FistStanceAlpha))
		* FMath::Lerp(1.f, ArmSideScale, SideAlpha) * FMath::Lerp(1.f, ArmBackScale, BackAlpha);
	const float PelvisYaw = FMath::Sin(2.f * UE_PI * Phase) * FMath::Lerp(WalkPelvisTurnDegrees, RunPelvisTurnDegrees, RunBlend) * StrideTurnScale;
	const float ChestStrideYaw = -FMath::Sin(2.f * UE_PI * (Phase - ArmPhaseLag)) * FMath::Lerp(WalkChestTurnDegrees, RunChestTurnDegrees, RunBlend) * StrideTurnScale;
	// A low reach hinges at the hips as well as along the spine: half of the bend tips the pelvis forward.
	const float ReachLean = Interaction ? Interaction->GetReachLeanDegrees() : 0.f;
	GaitState.PelvisRotation = FRotator(BalanceLean.Pitch - ReachLean * AedirReachHipHingeShare, PelvisYaw + SmoothedSwingTwist * AedirSwingPelvisShare + BalanceLean.Yaw, BalanceLean.Roll + TrunkInertia.Y + LoadRoll);

	const float LookYaw = MovementComponent->GetYawDeltaToControl();
	const float LookPitch = FMath::Clamp(FRotator::NormalizeAxis(CreatureOwner->GetControlRotation().Pitch), -MaxLookPitchDegrees, MaxLookPitchDegrees);

	// The trunk leans the way the body runs, forward going forward and back going backwards, and rocks with the strides.
	const float RunRock = FMath::Sin(4.f * UE_PI * Phase) * RunStridePitchDegrees * RunBlend * GaitSwagger;
	GaitState.SpineLeanPitch = -MaxRunForwardLeanDegrees * SpeedAlpha * static_cast<float>(LocalDirection.X) - RunRock + BalanceLean.Pitch + LookPitch * SpinePitchShare + TrunkInertia.X;
	GaitState.SpineLeanPitch -= ReachLean * (1.f - AedirReachHipHingeShare);
	// Crouching bends the spine forward too, the body does not just sink on its legs.
	GaitState.SpineLeanPitch -= CrouchSpineLeanDegrees * CrouchAlpha;
	GaitState.SpineLeanPitch -= StanceSpineLeanDegrees * StanceAlpha * (1.f - CrouchAlpha);
	GaitState.SpineLeanPitch -= ProtectiveSpineLeanDegrees * ProtectiveAlpha;
	// Backward positive: the torso leans back to bring the kicking leg up.
	GaitState.SpineLeanPitch += KickSpineLeanDegrees * KickAlpha;
	SmoothedPunchLean = FMath::FInterpTo(SmoothedPunchLean, Combat ? Combat->GetPunchLeanDegrees() : 0.f, DeltaTime, AedirPunchLeanInterpSpeed);
	GaitState.SpineLeanPitch -= SmoothedPunchLean;
	// The spine gives with a vertical blow: it bends into one that goes down and arches under one raised overhead.
	SmoothedSwingBend = FMath::FInterpTo(SmoothedSwingBend, Combat ? Combat->GetSwingBendDegrees() : 0.f, DeltaTime, AedirSwingTwistInterpSpeed);
	GaitState.SpineLeanPitch -= SmoothedSwingBend;
	const float TwistTarget = FMath::Clamp(LookYaw * SpineTwistScale, -SpineTwistLimitDegrees, SpineTwistLimitDegrees);
	SmoothedSpineTwist = FMath::FInterpTo(SmoothedSpineTwist, TwistTarget, DeltaTime, LookInterpSpeed);
	SmoothedSwingTwist = FMath::FInterpTo(SmoothedSwingTwist, Combat ? Combat->GetSwingTwistDegrees() : 0.f, DeltaTime, AedirSwingTwistInterpSpeed);
	GaitState.SpineTwistYaw = SmoothedSpineTwist + SmoothedSwingTwist + (Interaction ? Interaction->GetReachTwistDegrees() : 0.f);
	// The spine carries the chest from the hips' turn round to its own.
	GaitState.SpineTwistYaw += ChestStrideYaw - PelvisYaw;

	const FRotator HeadTarget(
		LookPitch * HeadPitchShare + TrunkInertia.X * AedirInertiaHeadShare,
		FMath::Clamp(LookYaw * HeadYawScale, -HeadYawLimitDegrees, HeadYawLimitDegrees),
		0.f);
	const FRotator NeckTarget(
		LookPitch * NeckPitchShare,
		FMath::Clamp(LookYaw * NeckYawScale, -NeckYawLimitDegrees, NeckYawLimitDegrees),
		0.f);
	// The chest turns with a swing, a punch or a reach, and the neck and head turn back against it: the eyes stay on the
	// enemy ahead, not on the weapon.
	const UAedirFistArms* WaistFists = CreatureOwner ? CreatureOwner->FindComponentByClass<UAedirFistArms>() : nullptr;
	const float ChestTurn = SmoothedSwingTwist * (1.f + AedirSwingPelvisShare) + (Interaction ? Interaction->GetReachTwistDegrees() : 0.f) + (WaistFists ? WaistFists->GetWaistYaw() : 0.f) + ChestStrideYaw;
	const FRotator HeadKept(HeadTarget.Pitch, HeadTarget.Yaw - ChestTurn * AedirHeadKeepsAheadShare, HeadTarget.Roll);
	const FRotator NeckKept(NeckTarget.Pitch, NeckTarget.Yaw - ChestTurn * (1.f - AedirHeadKeepsAheadShare), NeckTarget.Roll);
	GaitState.HeadLookRotation = FMath::RInterpTo(GaitState.HeadLookRotation, HeadKept, DeltaTime, LookInterpSpeed);
	GaitState.NeckLookRotation = FMath::RInterpTo(GaitState.NeckLookRotation, NeckKept, DeltaTime, LookInterpSpeed);

	ArmBlend = FMath::FInterpTo(ArmBlend, bMoving ? 1.f : 0.f, DeltaTime, ArmBlendInterpSpeed);
	const float DirectionalArmScale = FMath::Lerp(1.f, ArmSideScale, SideAlpha) * FMath::Lerp(1.f, ArmBackScale, BackAlpha);
	const float ArmAmplitude = FMath::Lerp(MaxArmSwingWalkDegrees, MaxArmSwingRunDegrees, SpeedAlpha * SpeedAlpha) * DirectionalArmScale * ArmBlend * GaitSwagger;
	// The arms follow the legs: their swing is a little behind the stride phase.
	GaitState.ArmSwingLeft = FMath::Sin(2.f * UE_PI * (Phase + 0.5f - ArmPhaseLag)) * ArmAmplitude;
	GaitState.ArmSwingRight = FMath::Sin(2.f * UE_PI * (Phase - ArmPhaseLag)) * ArmAmplitude;
	GaitState.ArmSwingLeft *= 1.f - LeftReachAlpha;
	GaitState.ArmSwingRight *= 1.f - RightReachAlpha;
	// A hand with a weapon in it is carried steady and hardly swings; the free one swings for both.
	if (const UAedirInteractionComponent* Hands = CreatureOwner ? CreatureOwner->Interaction.Get() : nullptr)
	{
		const auto Armed = [Hands](EAedirHand Hand)
		{
			const AAedirItemActor* Item = Hands->GetHeldItem(Hand);
			return Item && Item->ItemClass == EAedirItemClass::Weapon;
		};
		GaitState.ArmSwingLeft *= Armed(EAedirHand::Left) ? AedirArmedArmSwingShare : 1.f;
		GaitState.ArmSwingRight *= Armed(EAedirHand::Right) ? AedirArmedArmSwingShare : 1.f;
	}
	const float BalanceNow = BalanceComponent ? BalanceComponent->GetBalance() : 1.f;
	GaitState.ArmBalanceSpread = FMath::FInterpTo(GaitState.ArmBalanceSpread, FMath::Clamp((0.85f - BalanceNow) / 0.5f, 0.f, 1.f), DeltaTime, 10.f);
	GaitState.ArmRunSpread = RunBlend * RunArmSpreadShare;
	const UAedirInteractionComponent* Hands = CreatureOwner ? CreatureOwner->Interaction.Get() : nullptr;
	// A blade the fists hold is theirs: the older weapon code does not turn its hand, neither raised nor carried.
	const UAedirFistArms* BladeFists = CreatureOwner ? CreatureOwner->FindComponentByClass<UAedirFistArms>() : nullptr;
	const auto ReadWeaponAim = [Combat, Hands, DeltaTime, BladeFists](EAedirHand Hand, FVector& OutDirection, float& OutAlpha)
	{
		FVector Direction = FVector::ForwardVector;
		float Alpha = 0.f;
		if (Combat && !(BladeFists && BladeFists->IsArmed(Hand)) && Combat->GetWeaponAim(Hand, Direction, Alpha))
		{
			// Reversed in the hand, what strikes comes out by the little finger: the hand is pointed the other way round.
			const AAedirItemActor* Held = Hands ? Hands->GetHeldItem(Hand) : nullptr;
			OutDirection = Held && Held->IsAimedReversed() ? -Direction : Direction;
			OutAlpha = FMath::FInterpConstantTo(OutAlpha, Alpha, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
		}
		else
		{
			OutAlpha = FMath::FInterpConstantTo(OutAlpha, 0.f, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
		}
	};
	ReadWeaponAim(EAedirHand::Left, GaitState.LeftWeaponDirWorld, GaitState.LeftWeaponAimAlpha);
	ReadWeaponAim(EAedirHand::Right, GaitState.RightWeaponDirWorld, GaitState.RightWeaponAimAlpha);
	GaitState.LeftWristFrontShare = FMath::FInterpConstantTo(GaitState.LeftWristFrontShare, Combat && Combat->IsHangingWeapon(EAedirHand::Left) ? 1.f : 0.f, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
	GaitState.RightWristFrontShare = FMath::FInterpConstantTo(GaitState.RightWristFrontShare, Combat && Combat->IsHangingWeapon(EAedirHand::Right) ? 1.f : 0.f, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
	if (Combat)
	{
		GaitState.LeftHangDegrees = Combat->GetHangWristDegrees(EAedirHand::Left);
		GaitState.RightHangDegrees = Combat->GetHangWristDegrees(EAedirHand::Right);
	}
	const auto ReadArmShape = [Combat, DeltaTime](EAedirHand Hand, float& OutWristDegrees, FVector& OutHint, float& OutHintAlpha)
	{
		// Held out with the arms straight, the blade runs on from the forearm almost in line (still 30).
		const float WristTarget = FMath::Lerp(FMath::Lerp(AedirWristLooseDegrees, AedirWristFirmDegrees, Combat ? Combat->GetWristFirmShare(Hand) : 0.f), AedirWristHeldOutDegrees, Combat ? Combat->GetHeldOutShare(Hand) : 0.f);
		OutWristDegrees = FMath::FInterpConstantTo(OutWristDegrees, WristTarget, DeltaTime, (AedirWristFirmDegrees - AedirWristHeldOutDegrees) / AedirWeaponAimFadeSeconds);
		FVector Hint = FVector::ZeroVector;
		float HintAlpha = 0.f;
		if (Combat && Combat->GetElbowHint(Hand, Hint, HintAlpha))
		{
			OutHint = Hint;
		}
		OutHintAlpha = FMath::FInterpConstantTo(OutHintAlpha, HintAlpha, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
	};
	ReadArmShape(EAedirHand::Left, GaitState.LeftWristMinDegrees, GaitState.LeftElbowHint, GaitState.LeftElbowHintAlpha);
	ReadArmShape(EAedirHand::Right, GaitState.RightWristMinDegrees, GaitState.RightElbowHint, GaitState.RightElbowHintAlpha);
	GaitState.LeftArmLedShare = FMath::FInterpConstantTo(GaitState.LeftArmLedShare, Combat ? Combat->GetArmLedShare(EAedirHand::Left) : 0.f, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
	GaitState.RightArmLedShare = FMath::FInterpConstantTo(GaitState.RightArmLedShare, Combat ? Combat->GetArmLedShare(EAedirHand::Right) : 0.f, DeltaTime, 1.f / AedirWeaponAimFadeSeconds);
	GaitState.KickLeftLeg = bKickLeftLeg;
	GaitState.KickToePoint = Combat ? Combat->GetKickToePoint() : 0.f;
	GaitState.ObjectAlpha = 0.f;
	if (Combat)
	{
		Combat->GetObjectCarry(GaitState.ObjectCenterWorld, GaitState.ObjectRadiusCm, GaitState.ObjectAlpha);
	}
	EAedirHand GripSecondary = EAedirHand::Left;
	GaitState.GripSecondaryHand = -1;
	if (Combat && Combat->GetSecondaryGrip(GripSecondary, GaitState.GripPointInPrimaryHand, GaitState.GripBladeInPrimaryHand, GaitState.GripAlpha))
	{
		GaitState.GripSecondaryHand = static_cast<int32>(GripSecondary);
		GaitState.GripOverhand = Combat->GetHalfSwordShare();
	}
	GaitState.LeftArmReachAlpha = LeftReachAlpha;
	GaitState.RightArmReachAlpha = RightReachAlpha;
	GaitState.LeftReachTargetWorld = LeftReachTarget;
	GaitState.RightReachTargetWorld = RightReachTarget;
	GaitState.FistAlphaLeft = Combat ? FMath::Clamp(Combat->GetFistAlpha(EAedirHand::Left), 0.f, 1.f) : 0.f;
	GaitState.FistAlphaRight = Combat ? FMath::Clamp(Combat->GetFistAlpha(EAedirHand::Right), 0.f, 1.f) : 0.f;
	if (const UAedirFistArms* Fists = CreatureOwner->FindComponentByClass<UAedirFistArms>())
	{
		Fists->GetArm(EAedirHand::Left, GaitState.FistArmHandWorld[0], GaitState.FistArmElbowWorld[0], GaitState.FistArmAlpha[0]);
		Fists->GetArm(EAedirHand::Right, GaitState.FistArmHandWorld[1], GaitState.FistArmElbowWorld[1], GaitState.FistArmAlpha[1]);
		GaitState.FistArmTurn[0] = Fists->GetFistTurn(EAedirHand::Left);
		GaitState.FistArmTurn[1] = Fists->GetFistTurn(EAedirHand::Right);
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const EAedirHand ArmHand = Slot == 0 ? EAedirHand::Left : EAedirHand::Right;
			const AAedirItemActor* Blade = Interaction ? Interaction->GetHeldItem(ArmHand) : nullptr;
			GaitState.FistArmArmed[Slot] = Fists->IsArmed(ArmHand) && Blade && Blade->GetRootComponent();
			GaitState.FistArmBladeAhead[Slot] = Fists->GetBladeAhead(ArmHand);
			GaitState.FistArmBladeAim[Slot] = Fists->GetBladeAim(ArmHand);
			GaitState.FistArmBladeWristUp[Slot] = Fists->GetBladeWristUp(ArmHand);
			GaitState.FistArmBladeLean[Slot] = Fists->GetBladeLean(ArmHand);
			GaitState.FistArmBladeAimWay[Slot] = Fists->GetBladeAimWay(ArmHand);
			GaitState.FistArmItemInHand[Slot] = GaitState.FistArmArmed[Slot] ? Blade->GetRootComponent()->GetRelativeTransform().GetRotation() : FQuat::Identity;
			GaitState.FistArmItemPlace[Slot] = GaitState.FistArmArmed[Slot] ? Blade->GetRootComponent()->GetRelativeTransform().GetLocation() : FVector::ZeroVector;
		}
		GaitState.FistArmGripAlpha = GaitState.FistArmArmed[1] ? Fists->GetGripAlpha() : 0.f;
		const AAedirItemActor* RightBlade = Interaction ? Interaction->GetHeldItem(EAedirHand::Right) : nullptr;
		GaitState.FistArmGripElbowOutShare = Fists->TwoHandElbowOutShare;
		GaitState.FistArmGripElbowFrontCm = Fists->TwoHandElbowFrontCm;
		GaitState.FistArmGripRaised = Fists->GetRaised(EAedirHand::Right);
		GaitState.FistArmGripRight = FMath::Clamp(static_cast<float>(Fists->GetAim().X), 0.f, 1.f) * GaitState.FistArmGripRaised;
		GaitState.FistArmGripElbowRiseCm = Fists->TwoHandElbowRiseCm;
		GaitState.FistArmGripRestElbowBelowCm = Fists->TwoHandRestElbowBelowShoulderCm;
		GaitState.FistArmGripWristMaxDegrees = Fists->TwoHandWristMaxDegrees;
		GaitState.FistArmGripGapCm = Fists->GetGripGapCm(RightBlade);
		GaitState.FistArmBladeUpShare = Fists->BladeGuardUpShare;
		GaitState.FistArmBladeBendDegrees = Fists->BladeWristBendDegrees;
		GaitState.FistAlphaLeft = FMath::Max(GaitState.FistAlphaLeft, GaitState.FistArmGripAlpha);
		GaitState.FistAlphaLeft = FMath::Max(GaitState.FistAlphaLeft, GaitState.FistArmAlpha[0]);
		GaitState.FistAlphaRight = FMath::Max(GaitState.FistAlphaRight, GaitState.FistArmAlpha[1]);
	}
	// A broken arm's hand hangs open: it closes on nothing.
	if (const UAedirWoundComponent* HandWounds = CreatureOwner->Wounds.Get())
	{
		GaitState.FistAlphaLeft = HandWounds->HasUselessArm(true) ? 0.f : GaitState.FistAlphaLeft;
		GaitState.FistAlphaRight = HandWounds->HasUselessArm(false) ? 0.f : GaitState.FistAlphaRight;
	}
	// Loose arms hang straight while standing; the elbows start to break with the walking and running speed.
	GaitState.ElbowBend = FMath::Lerp(ElbowBendWalkDegrees, ElbowBendRunDegrees, SpeedAlpha * SpeedAlpha) * ArmBlend;
	GaitState.KneeBend = FMath::Max(Lift[0], Lift[1]) * KneeBendMaxDegrees + ReachLowerAlpha * KneeBendMaxDegrees;
	GaitState.KneeBend += StanceKneeBendDegrees * SinkAlpha;

	// Breathing, idle sway, weight shift and inertia. Every term is zero while dead.
	const bool bDead = CreatureOwner->Stats && CreatureOwner->Stats->IsDead();
	const float Alive = bDead ? 0.f : 1.f;
	GaitState.MotionAlpha = Alive;
	MotionClock += DeltaTime;

	const float StaminaFraction = CreatureOwner->Stats ? FMath::Clamp(CreatureOwner->Stats->GetStaminaFraction(), 0.f, 1.f) : 1.f;
	// Exertion is the stamina deficit, plus walking and running effort on top.
	const float Exertion = FMath::Clamp((1.f - StaminaFraction) + SpeedAlpha * FMath::Lerp(0.25f, 1.f, RunBlend), 0.f, 1.f);
	GaitState.BreathExertion = Exertion;
	GaitState.BreathPhase = WrapUnit(GaitState.BreathPhase + FMath::Lerp(BreathRateMinHz, BreathRateMaxHz, Exertion) * DeltaTime);

	// Inertia: planar acceleration of the body in the actor frame, and the turn rate of the actor.
	const FVector PlanarVelocity(Velocity.X, Velocity.Y, 0.f);
	const float ActorYaw = CreatureOwner->GetActorRotation().Yaw;
	if (!bMotionInitialized)
	{
		PreviousPlanarVelocity = PlanarVelocity;
		PreviousActorYaw = ActorYaw;
		bMotionInitialized = true;
	}

	const FVector Acceleration = (PlanarVelocity - PreviousPlanarVelocity) / DeltaTime;
	PreviousPlanarVelocity = PlanarVelocity;
	SmoothedAcceleration = FMath::VInterpTo(SmoothedAcceleration, Acceleration, DeltaTime, InertiaSmoothingSpeed);
	const FVector LocalAcceleration = ActorTransform.InverseTransformVectorNoScale(SmoothedAcceleration);

	const float YawRate = FRotator::NormalizeAxis(ActorYaw - PreviousActorYaw) / DeltaTime;
	PreviousActorYaw = ActorYaw;
	SmoothedYawRate = FMath::FInterpTo(SmoothedYawRate, YawRate, DeltaTime, InertiaSmoothingSpeed);

	const float ForwardLoad = FMath::Clamp(LocalAcceleration.X / InertiaAccelerationScaleCm, -1.f, 1.f);
	const float RightLoad = FMath::Clamp(LocalAcceleration.Y / InertiaAccelerationScaleCm, -1.f, 1.f);
	const float TurnLoad = FMath::Clamp(SmoothedYawRate / YawRateScaleDegreesPerSecond, -1.f, 1.f);
	const float InertiaPitch = FMath::Clamp(-ForwardLoad * InertiaLeanPitchDegrees, -InertiaLeanLimitDegrees, InertiaLeanLimitDegrees) * Alive;
	const float InertiaRoll = FMath::Clamp(RightLoad * InertiaLeanRollDegrees + TurnLoad * YawRateLeanDegrees, -InertiaLeanLimitDegrees, InertiaLeanLimitDegrees) * Alive;
	GaitState.SpineLeanPitch += InertiaPitch;
	// The waist bends with the fists' guard raised or lowered.
	if (const UAedirFistArms* Fists = CreatureOwner->FindComponentByClass<UAedirFistArms>())
	{
		GaitState.SpineLeanPitch += Fists->GetWaistPitch();
		GaitState.SpineTwistYaw += Fists->GetWaistYaw();
	}
	GaitState.SpineLeanRoll = InertiaRoll + LocalDirection.Y * SpeedAlpha * MoveDirectionLeanRollDegrees;
	// The trunk leans off the broken leg while it bears, sparing it.
	if (BrokenLeg >= 0)
	{
		GaitState.SpineLeanRoll += BrokenSide * LimpTrunkRollDegrees * LimpBearing;
	}
	// Blows bend the trunk and it springs back.
	if (BalanceComponent)
	{
		GaitState.HeadHitSway = BalanceComponent->GetHeadSway();
		const FRotator HitSway = BalanceComponent->GetHitSway();
		GaitState.SpineLeanPitch += HitSway.Pitch;
		GaitState.SpineLeanRoll += HitSway.Roll;
		GaitState.SpineTwistYaw += HitSway.Yaw;
	}

	// Idle sway and weight shift only while standing still and out of the fighting stance.
	const float IdleTarget = bStepping ? 0.f : 1.f - StanceAlpha;
	IdleBlend = FMath::FInterpTo(IdleBlend, IdleTarget, DeltaTime, IdleBlendInterpSpeed);
	const float IdleGate = IdleBlend * Alive;

	const float SwayA = FMath::Sin(2.f * UE_PI * IdleSwayFrequencyA * MotionClock);
	const float SwayB = FMath::Sin(2.f * UE_PI * IdleSwayFrequencyB * MotionClock + 1.7f);
	const float SwayC = FMath::Sin(2.f * UE_PI * IdleSwayFrequencyB * MotionClock);
	const float SwayD = FMath::Sin(2.f * UE_PI * IdleSwayFrequencyA * MotionClock + 2.3f);
	// Cosine eased load shift: -1 is the left leg loaded, +1 the right leg, switching every WeightShiftIntervalSeconds.
	const float LoadSide = -FMath::Cos(UE_PI * MotionClock / FMath::Max(WeightShiftIntervalSeconds, KINDA_SMALL_NUMBER));
	const float IdleLateral = (IdleSwayLateralCm * 0.5f * (SwayA + SwayB) + WeightShiftLateralCm * LoadSide) * IdleGate;
	const float IdleRoll = IdleSwayRollDegrees * 0.5f * (SwayC + SwayD) * IdleGate;

	// While walking the stance foot takes the weight and the pelvis rolls towards it. Phase is positive while the right foot is in stance.
	const float StanceSide = FMath::Sin(2.f * UE_PI * Phase);
	// The sideways hip shift is subtle in a walk (about 40%) and reaches full size only when running.
	const float LateralIntensity = bMoving ? FMath::Lerp(0.3f, 1.f, SpeedAlpha * SpeedAlpha) : 0.f;
	const float GaitLateral = StanceSide * WalkWeightShiftLateralCm * LateralIntensity * Alive;
	const float GaitRoll = -StanceSide * WalkWeightShiftRollDegrees * GaitIntensity * Alive;

	GaitState.HipLateralOffset = IdleLateral + GaitLateral + (Interaction ? Interaction->GetReachHipSide() * ActorScale : 0.f);
	GaitState.PelvisRollDegrees = IdleRoll + GaitRoll;
	if (BrokenLeg >= 0 && !bAirborne)
	{
		// Standing, the hips are kept over the sound leg; stepping, they drop and roll onto the broken one while it bears.
		GaitState.HipLateralOffset += -BrokenSide * LimpStandSideCm * ActorScale * (1.f - LimpBearing);
		GaitState.PelvisRollDegrees += -BrokenSide * LimpRollDegrees * LimpBearing;
	}
	GaitState.SpineCounterRollDegrees = -SpineCounterRollShare * GaitState.PelvisRollDegrees;
	GaitState.HeadStabilizeRollDegrees = -HeadStabilizeRollShare * GaitState.PelvisRollDegrees;

	GaitState.Phase = Phase;
	GaitState.bStepping = bStepping;
	GaitState.bTurningInPlace = bTurning;
	GaitState.KneeUpShare = 0.f;
	if (BalanceComponent && BalanceComponent->IsGettingUp())
	{
		PoseGetUp(ActorTransform, BottomZ, ActorScale);
	}
	GaitState.bProceduralActive = true;
}

namespace
{
	// One moment of getting up, in the frame of the body: forward, out to the side and up from the ground, in cm for a body
	// whose hips stand 95 cm high. Angles in degrees, forward negative.
	struct FAedirGetUpKey
	{
		float Time;
		float HipHeight;
		float HipForward;
		float PelvisPitch;
		float SpinePitch;
		float NeckPitch;
		FVector LeftFoot;
		FVector RightFoot;
		float LeftLift;
		float RightLift;
		FVector Hand;
		float HandAlpha;
		float KneeUp;
	};

	constexpr float AedirGetUpReferenceHipCm = 95.f;
	// From here on the moves hand over to the standing pose.
	constexpr float AedirGetUpHandOverFrom = 0.8f;

	// Face down: push up onto the hands and knees, bring the right foot forward, then rise off it with the hands leaving the ground.
	const FAedirGetUpKey AedirGetUpFromFront[] = {
		{ 0.f,   13.f, 0.f, -85.f, -5.f,  25.f, FVector(-82.f, 13.f, 6.f), FVector(-82.f, 13.f, 6.f), 0.3f, 0.3f, FVector(38.f, 30.f, 6.f), 1.f, 0.f },
		{ 0.32f, 50.f, 0.f, -72.f, -12.f, 35.f, FVector(-40.f, 14.f, 6.f), FVector(-40.f, 14.f, 6.f), 0.3f, 0.3f, FVector(50.f, 22.f, 6.f), 1.f, 0.f },
		{ 0.55f, 55.f, 0.f, -50.f, -18.f, 25.f, FVector(-40.f, 14.f, 6.f), FVector(32.f, 16.f, 8.f),  0.3f, 0.f,  FVector(42.f, 24.f, 6.f), 0.5f, 0.f },
		{ 0.8f,  78.f, 4.f, -22.f, -14.f, 10.f, FVector(-12.f, 12.f, 10.f), FVector(10.f, 14.f, 8.f), 0.5f, 0.f,  FVector(30.f, 24.f, 40.f), 0.f, 0.f },
	};

	// On the back: sit up propped on the hands behind while drawing the feet in to the hips with the knees up, then lean over
	// the feet and stand up on them. The knees come up from the first moment: sitting up on straight legs, the knee has no way
	// to bend that is clearly forward and turns over backwards. The body stands where the feet are drawn in to.
	const FAedirGetUpKey AedirGetUpFromBack[] = {
		{ 0.f,   12.f, -32.f, 85.f,  5.f, -15.f, FVector(56.f, 14.f, 8.f), FVector(56.f, 14.f, 8.f), 1.f, 1.f, FVector(-35.f, 30.f, 6.f), 0.6f, 0.8f },
		{ 0.28f, 13.f, -32.f, 30.f,  8.f, -10.f, FVector(28.f, 14.f, 8.f), FVector(28.f, 14.f, 8.f), 0.5f, 0.5f, FVector(-68.f, 24.f, 6.f), 1.f, 1.f },
		{ 0.52f, 14.f, -32.f, 22.f,  4.f, 0.f,   FVector(0.f, 15.f, 8.f),  FVector(0.f, 15.f, 8.f),  0.f, 0.f, FVector(-66.f, 24.f, 6.f), 1.f, 1.f },
		{ 0.8f,  45.f, -10.f, -35.f, -25.f, 15.f, FVector(0.f, 15.f, 8.f),  FVector(0.f, 15.f, 8.f),  0.f, 0.f, FVector(-30.f, 24.f, 40.f), 0.f, 0.f },
	};

	template <int32 Count>
	FAedirGetUpKey SampleGetUpKeys(const FAedirGetUpKey (&Keys)[Count], float Time)
	{
		int32 Next = 1;
		while (Next < Count - 1 && Keys[Next].Time < Time)
		{
			++Next;
		}
		const FAedirGetUpKey& A = Keys[Next - 1];
		const FAedirGetUpKey& B = Keys[Next];
		const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp((Time - A.Time) / FMath::Max(B.Time - A.Time, KINDA_SMALL_NUMBER), 0.f, 1.f));
		FAedirGetUpKey Out;
		Out.Time = Time;
		Out.HipHeight = FMath::Lerp(A.HipHeight, B.HipHeight, Alpha);
		Out.HipForward = FMath::Lerp(A.HipForward, B.HipForward, Alpha);
		Out.PelvisPitch = FMath::Lerp(A.PelvisPitch, B.PelvisPitch, Alpha);
		Out.SpinePitch = FMath::Lerp(A.SpinePitch, B.SpinePitch, Alpha);
		Out.NeckPitch = FMath::Lerp(A.NeckPitch, B.NeckPitch, Alpha);
		Out.LeftFoot = FMath::Lerp(A.LeftFoot, B.LeftFoot, Alpha);
		Out.RightFoot = FMath::Lerp(A.RightFoot, B.RightFoot, Alpha);
		Out.LeftLift = FMath::Lerp(A.LeftLift, B.LeftLift, Alpha);
		Out.RightLift = FMath::Lerp(A.RightLift, B.RightLift, Alpha);
		Out.Hand = FMath::Lerp(A.Hand, B.Hand, Alpha);
		Out.HandAlpha = FMath::Lerp(A.HandAlpha, B.HandAlpha, Alpha);
		Out.KneeUp = FMath::Lerp(A.KneeUp, B.KneeUp, Alpha);
		return Out;
	}
}

void UAedirGaitComponent::PoseGetUp(const FTransform& ActorTransform, float BottomZ, float ActorScale)
{
	const float Progress = BalanceComponent->GetGetUpProgress();
	const FAedirGetUpKey Key = BalanceComponent->GetGetUpStyle() == EAedirGetUpStyle::FromFront
		? SampleGetUpKeys(AedirGetUpFromFront, Progress)
		: SampleGetUpKeys(AedirGetUpFromBack, Progress);
	// Fully the moves until the body is nearly up, then the standing pose takes over.
	const float Weight = 1.f - FMath::SmoothStep(AedirGetUpHandOverFrom, 1.f, Progress);
	const float StandHeight = BalanceComponent->GetPelvisStandHeight();
	const float Size = StandHeight > KINDA_SMALL_NUMBER ? StandHeight / AedirGetUpReferenceHipCm : ActorScale;

	// The feet take their steps from scratch once the body is up.
	if (Weight > 0.99f)
	{
		bFeetInitialized = false;
	}

	// The hips, too, set out from how they lay: moved and turned from there to where the moves start.
	const float SetOut = FMath::SmoothStep(0.f, FMath::Max(BalanceComponent->GetUpSetOutShare, KINDA_SMALL_NUMBER), Progress);
	FVector HipOffset(Key.HipForward * Size, 0.f, Key.HipHeight * Size - StandHeight);
	FQuat HipTurn = FRotator(Key.PelvisPitch, 0.f, 0.f).Quaternion();
	FVector StartOffset;
	FQuat StartTurn;
	if (SetOut < 1.f && BalanceComponent->GetGetUpStartHips(StartOffset, StartTurn))
	{
		HipOffset = FMath::Lerp(StartOffset, HipOffset, SetOut);
		HipTurn = FQuat::Slerp(StartTurn, HipTurn, SetOut);
	}
	const FRotator HipRotation = HipTurn.Rotator();
	GaitState.HipOffsetZ = FMath::Lerp(GaitState.HipOffsetZ, static_cast<float>(HipOffset.Z), Weight);
	// Lifting towards the way of the keys, the hips go over that way, and the trunk leans after them.
	const FVector Lean = BalanceComponent->GetGetUpLeanLocal();
	GaitState.HipForwardOffset = FMath::Lerp(GaitState.HipForwardOffset, static_cast<float>(HipOffset.X + Lean.X), Weight);
	GaitState.HipLateralOffset = FMath::Lerp(GaitState.HipLateralOffset, static_cast<float>(HipOffset.Y + Lean.Y), Weight);
	GaitState.PelvisRotation = FMath::Lerp(GaitState.PelvisRotation, HipRotation, Weight);
	GaitState.PelvisRollDegrees *= 1.f - Weight;
	GaitState.SpineLeanPitch = FMath::Lerp(GaitState.SpineLeanPitch, Key.SpinePitch, Weight);
	GaitState.SpineLeanRoll *= 1.f - Weight;
	// Forward is a negative pitch.
	GaitState.SpineLeanRoll += static_cast<float>(Lean.Y) * GetUpLeanDegreesPerCm * Weight;
	GaitState.SpineLeanPitch -= static_cast<float>(Lean.X) * GetUpLeanDegreesPerCm * Weight;
	GaitState.SpineTwistYaw *= 1.f - Weight;
	GaitState.SpineCounterRollDegrees *= 1.f - Weight;
	GaitState.HeadStabilizeRollDegrees *= 1.f - Weight;
	GaitState.NeckLookRotation = FMath::Lerp(GaitState.NeckLookRotation, FRotator(Key.NeckPitch * 0.6f, 0.f, 0.f), Weight);
	GaitState.HeadLookRotation = FMath::Lerp(GaitState.HeadLookRotation, FRotator(Key.NeckPitch * 0.4f, 0.f, 0.f), Weight);
	GaitState.KneeUpShare = Key.KneeUp * Weight;

	// Hands and feet go on the ground under the points of the moves, wherever that ground is.
	const auto OnGround = [&](const FVector& Local, float Side)
	{
		const FVector Flat = ActorTransform.TransformPositionNoScale(FVector(Local.X * Size, Side * Local.Y * Size, 0.f));
		FVector Ground;
		FVector Normal;
		SampleGround(Flat, BottomZ, Ground, Normal);
		return Ground + FVector(0.f, 0.f, Local.Z * Size);
	};
	// The hands and feet set out from where they lay, not from where the moves would have them lie, so no limb is yanked.
	GaitState.LeftFootWorld = FMath::Lerp(GaitState.LeftFootWorld, FMath::Lerp(GetUpStartFoot[0], OnGround(Key.LeftFoot, -1.f), SetOut), Weight);
	GaitState.RightFootWorld = FMath::Lerp(GaitState.RightFootWorld, FMath::Lerp(GetUpStartFoot[1], OnGround(Key.RightFoot, 1.f), SetOut), Weight);
	GaitState.LeftFootLift = FMath::Lerp(GaitState.LeftFootLift, Key.LeftLift, Weight);
	GaitState.RightFootLift = FMath::Lerp(GaitState.RightFootLift, Key.RightLift, Weight);

	const auto PlaceHand = [&](FVector& Target, float& Alpha, float Side)
	{
		const FVector HandWorld = FMath::Lerp(GetUpStartHand[Side < 0.f ? 0 : 1], OnGround(Key.Hand, Side), SetOut);
		Target = Alpha > 0.01f ? FMath::Lerp(Target, HandWorld, Weight) : HandWorld;
		Alpha = FMath::Lerp(Alpha, Key.HandAlpha, Weight);
	};
	PlaceHand(GaitState.LeftReachTargetWorld, GaitState.LeftArmReachAlpha, -1.f);
	PlaceHand(GaitState.RightReachTargetWorld, GaitState.RightArmReachAlpha, 1.f);
	GaitState.ArmSwingLeft *= 1.f - Weight;
	GaitState.ArmSwingRight *= 1.f - Weight;
	GaitState.LeftWeaponAimAlpha *= 1.f - Weight;
	GaitState.RightWeaponAimAlpha *= 1.f - Weight;
	GaitState.LeftArmLedShare *= 1.f - Weight;
	GaitState.RightArmLedShare *= 1.f - Weight;
	GaitState.LeftElbowHintAlpha *= 1.f - Weight;
	GaitState.RightElbowHintAlpha *= 1.f - Weight;
	GaitState.ObjectAlpha *= 1.f - Weight;
	GaitState.KickToePoint *= 1.f - Weight;
}

void UAedirGaitComponent::BeginGetUpPose()
{
	// Read off the physical bodies: the bone pose is still the one of the frame before the body was moved onto its capsule.
	const auto WhereLying = [this](const FName& Bone)
	{
		const FBodyInstance* Body = BodyMesh->GetBodyInstance(Bone);
		return Body ? Body->GetUnrealWorldTransform().GetLocation() : BodyMesh->GetBoneLocation(Bone);
	};
	if (BodyMesh)
	{
		GetUpStartFoot[0] = WhereLying(LeftFootBoneName);
		GetUpStartFoot[1] = WhereLying(RightFootBoneName);
		GetUpStartHand[0] = WhereLying(LeftHandBoneName);
		GetUpStartHand[1] = WhereLying(RightHandBoneName);
	}
	const UWorld* World = GetWorld();
	TickComponent(World ? FMath::Max(World->GetDeltaSeconds(), 0.001f) : 1.f / 60.f, LEVELTICK_All, nullptr);
	if (BodyMesh)
	{
		BodyMesh->TickAnimation(0.f, false);
		BodyMesh->RefreshBoneTransforms();
	}
}

const FAedirGaitState& UAedirGaitComponent::GetGaitState() const
{
	return GaitState;
}

FAedirGaitState UAedirGaitComponent::GetGaitStateBP() const
{
	return GaitState;
}

void UAedirGaitComponent::FillBoneNames(FAedirGaitBoneNames& OutNames) const
{
	OutNames.Pelvis = PelvisBoneName;
	OutNames.Spine = SpineBoneNames;
	OutNames.Neck = NeckBoneName;
	OutNames.Head = HeadBoneName;
	OutNames.LeftClavicle = LeftClavicleBoneName;
	OutNames.LeftHumerus = LeftHumerusBoneName;
	OutNames.LeftRadius = LeftRadiusBoneName;
	OutNames.LeftHand = LeftHandBoneName;
	OutNames.LeftFemur = LeftFemurBoneName;
	OutNames.LeftTibia = LeftTibiaBoneName;
	OutNames.LeftFoot = LeftFootBoneName;
	OutNames.LeftBall = LeftBallBoneName;
	OutNames.RightClavicle = RightClavicleBoneName;
	OutNames.RightHumerus = RightHumerusBoneName;
	OutNames.RightRadius = RightRadiusBoneName;
	OutNames.RightHand = RightHandBoneName;
	OutNames.RightFemur = RightFemurBoneName;
	OutNames.RightTibia = RightTibiaBoneName;
	OutNames.RightFoot = RightFootBoneName;
	OutNames.RightBall = RightBallBoneName;
}

namespace
{
	// How many people standing over one spot a ground trace looks past.
	constexpr int32 AedirGroundTraceTries = 4;
}

bool UAedirGaitComponent::SampleGround(const FVector& WorldXY, float BottomZ, FVector& OutLocation, FVector& OutNormal) const
{
	const UWorld* World = GetWorld();
	const FVector Start(WorldXY.X, WorldXY.Y, BottomZ + GroundTraceUpCm);
	const FVector End(WorldXY.X, WorldXY.Y, BottomZ - GroundTraceDownCm);

	FHitResult Hit;
	FCollisionQueryParams Params(FName(TEXT("AedirGaitGround")), false, GetOwner());
	// A foot never steps onto someone still on their feet: the trace looks on past them. One lying down, out cold or dead is
	// ground like any other.
	for (int32 Try = 0; World && Try < AedirGroundTraceTries && World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params); ++Try)
	{
		// Someone, or something someone holds or wears.
		AActor* HitActor = Hit.GetActor();
		const AAedirCreature* Standing = Cast<AAedirCreature>(HitActor ? HitActor->GetAttachParentActor() ? HitActor->GetAttachParentActor() : HitActor : nullptr);
		const bool bLyingDown = Standing && ((Standing->Balance && Standing->Balance->IsRagdoll()) || (Standing->Stats && (Standing->Stats->IsDead() || Standing->Stats->IsKnockedOut())));
		if (Standing && !bLyingDown)
		{
			++LegLogLookedPast;
			Params.AddIgnoredActor(HitActor);
			continue;
		}
		OutLocation = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		return true;
	}

	OutLocation = FVector(WorldXY.X, WorldXY.Y, BottomZ);
	OutNormal = FVector::UpVector;
	return false;
}

void UAedirGaitComponent::ResetForRagdoll()
{
	GaitState = FAedirGaitState();
	GaitState.bLimpPose = true;
	bFeetInitialized = false;
	bSwinging[0] = false;
	bSwinging[1] = false;
	StairFactor = 1.f;
	RunBlend = 0.f;
	ArmBlend = 0.f;
	DynamicHipOffset = 0.f;
	SmoothedSpineTwist = 0.f;
	IdleBlend = 0.f;
	SmoothedYawRate = 0.f;
	PreviousActorYaw = 0.f;
	PreviousPlanarVelocity = FVector::ZeroVector;
	SmoothedAcceleration = FVector::ZeroVector;
	bMotionInitialized = false;
}

void UAedirGaitComponent::CacheLegGeometry()
{
	USkeletalMesh* SkeletalMesh = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	if (AnkleRefHeightCm > 0.f || !SkeletalMesh)
	{
		return;
	}

	const FName ThighNames[2] = { LeftFemurBoneName, RightFemurBoneName };
	const FName CalfNames[2] = { LeftTibiaBoneName, RightTibiaBoneName };
	const FName FootNames[2] = { LeftFootBoneName, RightFootBoneName };
	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const FVector ThighRef = SkeletalMesh->GetComposedRefPoseMatrix(ThighNames[Foot]).GetOrigin();
		const FVector CalfRef = SkeletalMesh->GetComposedRefPoseMatrix(CalfNames[Foot]).GetOrigin();
		const FVector FootRef = SkeletalMesh->GetComposedRefPoseMatrix(FootNames[Foot]).GetOrigin();
		ThighRefLocation[Foot] = ThighRef;
		LegLengthCm[Foot] = static_cast<float>((CalfRef - ThighRef).Size() + (FootRef - CalfRef).Size());
		AnkleRefHeightCm = static_cast<float>(FootRef.Z);
	}
}

void UAedirGaitComponent::LogStep(int32 Foot, const FVector& Landing, float Trigger) const
{
	static const IConsoleVariable* StepLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (StepLog && StepLog->GetInt() != 0 && CreatureOwner)
	{
		const FVector Off = PlantedContact[Foot] - Landing;
		const FVector Ahead = CreatureOwner->GetActorForwardVector().GetSafeNormal2D();
		UE_LOG(LogAedir, Log, TEXT("[Step] %s steps its %s foot: it stood %.1f cm from where it should (%.1f ahead of it, trigger %.1f), moving %.0f cm/s, stance %.2f, fists %.2f"), *GetNameSafe(GetOwner()), Foot == 0 ? TEXT("left") : TEXT("right"),
			Off.Size2D(), FVector::DotProduct(Off, Ahead), Trigger, GetOwner()->GetVelocity().Size2D(), GaitState.StanceAlpha, FistStanceAlpha);
	}
}

void UAedirGaitComponent::BeginStep(int32 Foot, float Seconds)
{
	static const IConsoleVariable* StepLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	if (StepLog && StepLog->GetInt() != 0 && bSwinging[1 - Foot])
	{
		UE_LOG(LogAedir, Log, TEXT("[Flight] %s lifts its %s foot before the other is down, at %.0f cm/s"), *GetNameSafe(GetOwner()), Foot == 0 ? TEXT("left") : TEXT("right"), GetOwner() ? GetOwner()->GetVelocity().Size2D() : 0.0);
	}
	bSwinging[Foot] = true;
	bSwingFixedTarget[Foot] = false;
	SwingStartContact[Foot] = PlantedContact[Foot];
	SwingElapsed[Foot] = 0.f;
	SwingDuration[Foot] = Seconds;
	NextStepFoot = 1 - Foot;

	// The visual cycle (arms, pelvis) follows the real steps: left lifts at phase 0, right at 0.5.
	const float PhaseTarget = Foot == 0 ? 0.f : 0.5f;
	PhaseError = PhaseTarget - Phase;
	PhaseError -= FMath::RoundToFloat(PhaseError);
}

void UAedirGaitComponent::BeginRecoverySwing(int32 Foot, const FVector& CatchWorld, float MaxStepCm, float Urgency, float BottomZ)
{
	const FVector Start = PlantedContact[Foot];
	FVector Offset = CatchWorld - Start;
	Offset.Z = 0.f;
	if (Offset.Size() > MaxStepCm)
	{
		Offset = Offset.GetSafeNormal() * MaxStepCm;
	}

	SampleGround(Start + Offset, BottomZ, FixedLandingWorld[Foot], FixedLandingNormal[Foot]);

	// A higher urgency shortens the swing, so the catching foot arrives sooner.
	BeginStep(Foot, FMath::Max(RecoveryStepSeconds * (1.f - 0.5f * Urgency), KINDA_SMALL_NUMBER));
	bSwingFixedTarget[Foot] = true;
}

void UAedirGaitComponent::AdvancePhase(float DeltaTime, bool bMoving, bool bTurning, float Speed2D, float StrideLength)
{
	if (bMoving)
	{
		// Even a slow shuffle keeps the cycle turning, or a swing foot would hang in the air for seconds.
		Phase += FMath::Max(Speed2D / StrideLength, AedirMinGaitCyclesPerSecond) * DeltaTime;
	}
	else if (bTurning)
	{
		Phase += TurnStepCyclesPerSecond * DeltaTime;
	}
	else if (Phase < AedirRestPhaseMin || Phase >= AedirRestPhaseMax)
	{
		// Standing still: settle the cycle into the window where both feet are in stance.
		Phase += AedirRestCatchUpCyclesPerSecond * DeltaTime;
	}

	const float Correction = PhaseError * FMath::Min(AedirPhaseCorrectionSpeed * DeltaTime, 1.f);
	Phase += Correction;
	PhaseError -= Correction;

	Phase -= FMath::Floor(Phase);
}

FRotator UAedirGaitComponent::ComputeFootTilt(const FVector& GroundNormal, const FVector& ActorUp, const FRotator& CurrentTilt, float DeltaTime) const
{
	FQuat Tilt = FQuat::FindBetweenNormals(ActorUp, GroundNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));

	const float AngleDegrees = FMath::RadiansToDegrees(Tilt.GetAngle());
	if (AngleDegrees > MaxFootTiltDegrees && AngleDegrees > KINDA_SMALL_NUMBER)
	{
		Tilt = FQuat::Slerp(FQuat::Identity, Tilt, MaxFootTiltDegrees / AngleDegrees);
	}

	return FMath::RInterpTo(CurrentTilt, Tilt.Rotator(), DeltaTime, FootAlignInterpSpeed);
}

void UAedirGaitComponent::LogWrists() const
{
	static IConsoleVariable* WristLog = IConsoleManager::Get().RegisterConsoleVariable(TEXT("aedir.WristLog"), 0, TEXT("Log how far each hand has come from the end of its forearm and how far it is bent there, while a hand is held to a hurt place (1)."));
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Owner ? Owner->GetMesh() : nullptr;
	if (!WristLog || WristLog->GetInt() == 0 || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return;
	}
	const FReferenceSkeleton& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (ClutchAlpha[Slot] <= 0.02f)
		{
			continue;
		}
		const FName HandName = Slot == 0 ? TEXT("hand_l") : TEXT("hand_r");
		const FName ForeName = Slot == 0 ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
		const int32 HandIndex = Ref.FindBoneIndex(HandName);
		const int32 ForeIndex = Ref.FindBoneIndex(ForeName);
		if (HandIndex == INDEX_NONE || ForeIndex == INDEX_NONE)
		{
			continue;
		}
		const FTransform HandWorld = Mesh->GetBoneTransform(HandIndex);
		const FTransform ForeWorld = Mesh->GetBoneTransform(ForeIndex);
		const float Scale = static_cast<float>(Mesh->GetComponentScale().X);
		const float RefFore = static_cast<float>(Ref.GetRefBonePose()[HandIndex].GetTranslation().Size()) * Scale;
		const float Gap = static_cast<float>(FVector::Dist(HandWorld.GetLocation(), ForeWorld.GetLocation())) - RefFore;
		const FQuat Local = ForeWorld.GetRotation().Inverse() * HandWorld.GetRotation();
		const float Bend = FMath::RadiansToDegrees(static_cast<float>(Local.AngularDistance(Ref.GetRefBonePose()[HandIndex].GetRotation())));
		const FBodyInstance* HandBody = Mesh->GetBodyInstance(HandName);
		UE_LOG(LogAedir, Log, TEXT("[Wrist] %s %s: clutch %.2f%s, gap %.1f cm, bent %.0f deg from rest, hand simulating %d"), *Owner->GetName(), Slot == 0 ? TEXT("left") : TEXT("right"),
			ClutchAlpha[Slot], bClutchFlinch[Slot] ? TEXT(" (flinch)") : TEXT(""), Gap, Bend, HandBody && HandBody->IsInstanceSimulatingPhysics() ? 1 : 0);
	}
}

namespace
{
	// The ball of a foot this near the ground touches it: it stands 1.8 cm off it flat.
	constexpr float AedirLegLogTouchCm = 2.3f;
	// The very start and end of a step, when the foot is still at or already on the ground, are left out of how low it comes.
	constexpr float AedirLegLogSwingSkipSeconds = 0.06f;
}

void UAedirGaitComponent::LogLegs()
{
	static IConsoleVariable* LegLog = IConsoleManager::Get().RegisterConsoleVariable(TEXT("aedir.LegLog"), 0, TEXT("Log the feet and knees against the body, each leg's length and how far each foot wandered, every this many frames."));
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Owner ? Owner->GetMesh() : nullptr;
	const int32 Every = LegLog ? LegLog->GetInt() : 0;
	if (Every <= 0 || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		LegLogFrames = -1;
		return;
	}
	const FTransform Body = Owner->GetActorTransform();
	FVector Foot[2], Knee[2], Thigh[2];
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		Foot[Slot] = Mesh->GetBoneLocation(Slot == 0 ? TEXT("foot_l") : TEXT("foot_r"));
		Knee[Slot] = Mesh->GetBoneLocation(Slot == 0 ? TEXT("calf_l") : TEXT("calf_r"));
		Thigh[Slot] = Mesh->GetBoneLocation(Slot == 0 ? TEXT("thigh_l") : TEXT("thigh_r"));
		if (LegLogFrames < 0)
		{
			LegLogLast[Slot] = Foot[Slot];
			LegLogStart[Slot] = Foot[Slot];
			LegLogPath[Slot] = 0.f;
		}
		LegLogPath[Slot] += static_cast<float>(FVector::Dist(Foot[Slot], LegLogLast[Slot]));
		LegLogLast[Slot] = Foot[Slot];
		// The shake: how far each joint's step this frame differs from its step the frame before, in the body's own frame.
		const FVector KneeHere = Body.InverseTransformPositionNoScale(Knee[Slot]);
		const FVector FootHere = Body.InverseTransformPositionNoScale(Foot[Slot]);
		if (LegLogFrames >= 0)
		{
			LegLogShake[Slot] += static_cast<float>((KneeHere - 2.0 * LegLogKnee[Slot][0] + LegLogKnee[Slot][1]).Size());
			LegLogShake[2 + Slot] += static_cast<float>((FootHere - 2.0 * LegLogFoot[Slot][0] + LegLogFoot[Slot][1]).Size());
		}
		else
		{
			LegLogKnee[Slot][0] = KneeHere;
			LegLogFoot[Slot][0] = FootHere;
		}
		LegLogKnee[Slot][1] = LegLogKnee[Slot][0];
		LegLogKnee[Slot][0] = KneeHere;
		LegLogFoot[Slot][1] = LegLogFoot[Slot][0];
		LegLogFoot[Slot][0] = FootHere;
		// The scrape: how far the ball of the foot slides along while it touches the ground.
		const FVector Ball = Mesh->GetBoneLocation(Slot == 0 ? TEXT("ball_l") : TEXT("ball_r"));
		FVector Ground, GroundNormal;
		SampleGround(Ball, Owner->GetActorLocation().Z - Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Ground, GroundNormal);
		const float BallHeight = static_cast<float>(Ball.Z - Ground.Z);
		LegLogBallLow[Slot] = LegLogFrames <= 0 ? BallHeight : FMath::Min(LegLogBallLow[Slot], BallHeight);
		if (LegLogFrames >= 0 && BallHeight < AedirLegLogTouchCm)
		{
			LegLogScrape[Slot] += static_cast<float>(FVector::Dist2D(Ball, LegLogBall[Slot]));
			LegLogScrapeSwinging[Slot] += bSwinging[Slot] ? static_cast<float>(FVector::Dist2D(Ball, LegLogBall[Slot])) : 0.f;
		}
		LegLogBall[Slot] = Ball;
		// While stepping: how low the ball of the foot comes, as posed and as the body really has it.
		if (bSwinging[Slot] && SwingElapsed[Slot] > AedirLegLogSwingSkipSeconds && SwingElapsed[Slot] < SwingDuration[Slot] - AedirLegLogSwingSkipSeconds)
		{
			const UAedirBodyControlComponent* Posed = Owner->FindComponentByClass<UAedirBodyControlComponent>();
			const float PosedHeight = Posed ? static_cast<float>(Posed->GetPoseLocation(Slot == 0 ? TEXT("ball_l") : TEXT("ball_r")).Z - Ground.Z) : BallHeight;
			LegLogSwingLowPose[Slot] = FMath::Min(LegLogSwingLowPose[Slot], PosedHeight);
			LegLogSwingLowBody[Slot] = FMath::Min(LegLogSwingLowBody[Slot], BallHeight);
		}
		// The jitter: how much the physical knee's lag behind the pose changes from frame to frame. The pose itself moving
		// smoothly does not count, only the bodies shaking about it.
		if (const UAedirBodyControlComponent* Physical = Owner->FindComponentByClass<UAedirBodyControlComponent>())
		{
			const FName KneeBone = Slot == 0 ? TEXT("calf_l") : TEXT("calf_r");
			const FVector Lag = Mesh->GetBoneLocation(KneeBone) - Physical->GetPoseLocation(KneeBone);
			if (LegLogFrames >= 0)
			{
				LegLogJitter[Slot] += static_cast<float>((Lag - 2.0 * LegLogLag[Slot][0] + LegLogLag[Slot][1]).Size());
			}
			else
			{
				LegLogLag[Slot][0] = Lag;
			}
			LegLogLag[Slot][1] = LegLogLag[Slot][0];
			LegLogLag[Slot][0] = Lag;
			LegLogLagSize[Slot] = static_cast<float>(Lag.Size());
		}
	}
	// How far the shoulders are turned against the body: the line across them, seen from above.
	const FVector Across = Body.InverseTransformVectorNoScale(Mesh->GetBoneLocation(TEXT("upperarm_r")) - Mesh->GetBoneLocation(TEXT("upperarm_l")));
	const float ChestTurn = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(-Across.X), static_cast<float>(Across.Y)));
	// How fast the chest turns at most, in degrees a second.
	const float ChestDt = GetWorld()->GetDeltaSeconds();
	if (LegLogFrames >= 0 && ChestDt > KINDA_SMALL_NUMBER)
	{
		LegLogChestSpeed = FMath::Max(LegLogChestSpeed, FMath::Abs(FMath::FindDeltaAngleDegrees(LegLogChestLast, ChestTurn)) / ChestDt);
	}
	LegLogChestLast = ChestTurn;
	LegLogChestMin = LegLogFrames <= 0 ? ChestTurn : FMath::Min(LegLogChestMin, ChestTurn);
	// How far the trunk leans forward (positive) from upright: the line from the hips to the neck, seen from the side.
	const FVector TrunkLine = Body.InverseTransformVectorNoScale(Mesh->GetBoneLocation(TEXT("neck_01")) - Mesh->GetBoneLocation(TEXT("pelvis")));
	const float TrunkLean = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(TrunkLine.X), static_cast<float>(TrunkLine.Z)));
	LegLogLeanMin = LegLogFrames <= 0 ? TrunkLean : FMath::Min(LegLogLeanMin, TrunkLean);
	LegLogLeanMax = LegLogFrames <= 0 ? TrunkLean : FMath::Max(LegLogLeanMax, TrunkLean);
	LegLogChestMax = LegLogFrames <= 0 ? ChestTurn : FMath::Max(LegLogChestMax, ChestTurn);
	LegLogFrames = FMath::Max(LegLogFrames, 0) + 1;
	if (LegLogFrames < Every)
	{
		return;
	}
	const auto Local = [&Body](const FVector& World) { return Body.InverseTransformPositionNoScale(World); };
	const FVector FL = Local(Foot[0]), FR = Local(Foot[1]), KL = Local(Knee[0]), KR = Local(Knee[1]);
	UE_LOG(LogAedir, Log, TEXT("[Legs] %s stance %.2f fists %.2f lead %d dt %.5f | feet L %.1f %.1f R %.1f %.1f apart %.1f ahead %.1f | knees L %.1f R %.1f apart %.1f | thigh-calf %.1f %.1f calf-foot %.1f %.1f hip-foot %.1f %.1f | path %.2f %.2f net %.2f %.2f | shake knees %.2f %.2f feet %.2f %.2f | chest turn %.0f to %.0f | ball low %.1f %.1f scrape %.1f %.1f | jitter %.3f %.3f lag %.2f %.2f | looked past %d | scrape swinging %.1f %.1f leash %.1f %.1f | stepping ball low pose %.1f %.1f body %.1f %.1f | chest speed %.0f | trunk lean %.1f to %.1f"),
		*Owner->GetName(), GaitState.StanceAlpha, FistStanceAlpha, CreatureOwner && CreatureOwner->Combat && CreatureOwner->Combat->IsLeadFootRight() ? 1 : 0, GetWorld()->GetDeltaSeconds(), FL.X, FL.Y, FR.X, FR.Y, FR.Y - FL.Y, FR.X - FL.X, KL.Y, KR.Y, KR.Y - KL.Y,
		FVector::Dist(Thigh[0], Knee[0]), FVector::Dist(Thigh[1], Knee[1]), FVector::Dist(Knee[0], Foot[0]), FVector::Dist(Knee[1], Foot[1]), FVector::Dist(Thigh[0], Foot[0]), FVector::Dist(Thigh[1], Foot[1]),
		LegLogPath[0], LegLogPath[1], FVector::Dist(Foot[0], LegLogStart[0]), FVector::Dist(Foot[1], LegLogStart[1]),
		LegLogShake[0], LegLogShake[1], LegLogShake[2], LegLogShake[3], LegLogChestMin, LegLogChestMax, LegLogBallLow[0], LegLogBallLow[1], LegLogScrape[0], LegLogScrape[1], LegLogJitter[0], LegLogJitter[1], LegLogLagSize[0], LegLogLagSize[1], LegLogLookedPast, LegLogScrapeSwinging[0], LegLogScrapeSwinging[1], LegLogLeash[0], LegLogLeash[1], LegLogSwingLowPose[0], LegLogSwingLowPose[1], LegLogSwingLowBody[0], LegLogSwingLowBody[1], LegLogChestSpeed, LegLogLeanMin, LegLogLeanMax);
	LegLogChestSpeed = 0.f;
	for (float& Shake : LegLogShake)
	{
		Shake = 0.f;
	}
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		LegLogPath[Slot] = 0.f;
		LegLogStart[Slot] = Foot[Slot];
		LegLogScrape[Slot] = 0.f;
		LegLogJitter[Slot] = 0.f;
		LegLogScrapeSwinging[Slot] = 0.f;
		LegLogLeash[Slot] = 0.f;
		LegLogSwingLowPose[Slot] = 99.f;
		LegLogSwingLowBody[Slot] = 99.f;
	}
	LegLogFrames = 0;
}

void UAedirGaitComponent::ResolveArmReach(EAedirHand Hand, const UAedirCombatComponent* Combat, const UAedirInteractionComponent* Interaction, const FTransform& ActorTransform, const FVector& ProtectiveTargetWorld, float ProtectiveAlpha, FVector& OutTargetWorld, float& OutAlpha) const
{
	FAedirArmTargetBlend Blend;

	// Guard is the lowest priority: the hand rests at the guard offset relative to the actor.
	float GuardAlpha = 0.f;
	if (Combat && Combat->GetGuardPose(Hand, GuardAlpha))
	{
		const FVector& GuardOffset = Hand == EAedirHand::Left ? GuardHandOffsetLeft : GuardHandOffsetRight;
		Blend.Apply(ActorTransform.TransformPosition(GuardOffset), GuardAlpha);
	}
	else if (Combat && Interaction && !Interaction->GetHeldItem(Hand))
	{
		// The other hand's guard brings this one up with it.
		float OtherGuardAlpha = 0.f;
		if (Combat->GetGuardPose(Hand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left, OtherGuardAlpha))
		{
			FVector ReadyOffset = ReadyHandOffsetRight;
			if (Hand == EAedirHand::Left)
			{
				ReadyOffset.Y = -ReadyOffset.Y;
			}
			// It is held before the chest, so it turns with the chest when a swing turns it; held still in front of the hips
			// it would pull the arm out straight across the body.
			ReadyOffset = FRotator(0.f, SmoothedSwingTwist, 0.f).RotateVector(ReadyOffset);
			Blend.Apply(ActorTransform.TransformPosition(ReadyOffset), FMath::SmoothStep(0.f, 1.f, OtherGuardAlpha) * ReadyHandShare);
		}
	}

	// A hand held to a hurt place, over the guard; reaching for something and swinging come over it, but not over a flinch.
	// On its way there it comes round in front of the body on an arc, never through it or round behind.
	const int32 ClutchSlot = Hand == EAedirHand::Left ? 0 : 1;
	const float Way = ClutchAlpha[ClutchSlot];
	const FVector ClutchOnArc = ClutchTarget[ClutchSlot] + ActorTransform.GetUnitAxis(EAxis::X) * (4.f * Way * (1.f - Way) * ClutchArcCm * static_cast<float>(ActorTransform.GetScale3D().X));
	Blend.Apply(ClutchOnArc, Way);

	FVector ReachTarget = FVector::ZeroVector;
	float ReachAlpha = 0.f;
	QueryReachPose(Interaction, Hand, ReachTarget, ReachAlpha);
	Blend.Apply(ReachTarget, ReachAlpha);

	// The swing is the highest priority and overrides both the guard and the interaction reach, except while the hand puts a
	// weapon on the belt or draws it: then the way to the belt leads, or the hand that holds the weapon stays where it is
	// carried and never gets round to the far hip.
	FVector SwingTarget = FVector::ZeroVector;
	float SwingAlpha = 0.f;
	if (Combat && Combat->GetSwingPose(Hand, SwingTarget, SwingAlpha))
	{
		Blend.Apply(SwingTarget, SwingAlpha);
	}
	FVector HolsterDir;
	float HolsterShare = 0.f;
	if (Interaction && Interaction->GetHolsterAim(Hand, HolsterDir, HolsterShare))
	{
		Blend.Apply(ReachTarget, ReachAlpha);
	}

	if (bClutchFlinch[ClutchSlot])
	{
		Blend.Apply(ClutchOnArc, Way);
	}

	// The protective reflex is the highest priority while falling and overrides the swing.
	Blend.Apply(ProtectiveTargetWorld, ProtectiveAlpha);

	OutTargetWorld = Blend.Target;
	OutAlpha = Blend.Alpha;
}

namespace
{
	// The parts a hand can be put to, and how the hand for each is chosen.
	enum class EAedirClutchPart : uint8
	{
		None,
		Head,
		Neck,
		Shoulder,
		Arm,
		Trunk,
		Thigh
	};

	// What part of the body this bone is, and on which side it is: -1 the left, 1 the right, 0 the middle (then the side the
	// place itself is on decides).
	EAedirClutchPart AedirClutchPartOf(FName Bone, int32& OutSide)
	{
		const FString Name = Bone.ToString();
		OutSide = Name.EndsWith(TEXT("_l")) ? -1 : Name.EndsWith(TEXT("_r")) ? 1 : 0;
		if (Name.StartsWith(TEXT("clavicle")) || Name.StartsWith(TEXT("upperarm")))
		{
			return EAedirClutchPart::Shoulder;
		}
		if (Name.StartsWith(TEXT("lowerarm")) || Name.StartsWith(TEXT("hand")))
		{
			return EAedirClutchPart::Arm;
		}
		if (Name.StartsWith(TEXT("thigh")))
		{
			return EAedirClutchPart::Thigh;
		}
		if (Name.StartsWith(TEXT("head")))
		{
			return EAedirClutchPart::Head;
		}
		if (Name.StartsWith(TEXT("neck")))
		{
			return EAedirClutchPart::Neck;
		}
		if (Name.StartsWith(TEXT("spine")) || Name == TEXT("pelvis"))
		{
			return EAedirClutchPart::Trunk;
		}
		return EAedirClutchPart::None;
	}
}

void UAedirGaitComponent::UpdateClutch(float DeltaTime, const UAedirCombatComponent* Combat, const UAedirInteractionComponent* Interaction, const FTransform& ActorTransform)
{
	const USkeletalMeshComponent* Body = CreatureOwner ? CreatureOwner->GetMesh() : nullptr;
	const UAedirWoundComponent* WoundComponent = CreatureOwner ? CreatureOwner->Wounds.Get() : nullptr;
	const UWorld* World = GetWorld();
	float Wanted[2] = { 0.f, 0.f };
	FVector Goal[2] = { ClutchTarget[0], ClutchTarget[1] };
	bool bFlinchHand[2] = { false, false };
	const bool bDown = !Body || !World || (BalanceComponent && BalanceComponent->IsRagdoll()) || (CreatureOwner->Stats && CreatureOwner->Stats->IsDead());

	const auto Free = [&](int32 Slot)
	{
		const EAedirHand Hand = Slot == 0 ? EAedirHand::Left : EAedirHand::Right;
		return !(Interaction && Interaction->GetHeldItem(Hand)) && !(WoundComponent && WoundComponent->HasUselessArm(Slot == 0));
	};
	// The hand for a place on a bone: the head by its own side, the neck, a shoulder and the middle of the trunk by the other
	// side, a side of the trunk and a thigh by their own side. When that one is busy the other one may go, except to a
	// shoulder, which the hand on its own side cannot reach.
	const auto PickHand = [&](FName Bone, const FVector& PointWorld, TFunctionRef<bool(int32)> Idle, TFunctionRef<bool(int32)> Takeable) -> int32
	{
		int32 PartSide = 0;
		const EAedirClutchPart Part = AedirClutchPartOf(Bone, PartSide);
		if (Part == EAedirClutchPart::None)
		{
			return INDEX_NONE;
		}
		const FVector BoneWorld = Body->GetBoneLocation(Bone);
		const FVector Round = ActorTransform.InverseTransformVectorNoScale(PointWorld - BoneWorld);
		// Nobody reaches round to hold the back, the back of the neck or of the head.
		if ((Part == EAedirClutchPart::Head || Part == EAedirClutchPart::Neck || Part == EAedirClutchPart::Trunk) && Round.X < -ClutchBackCm)
		{
			return INDEX_NONE;
		}
		int32 Side = PartSide != 0 ? PartSide : (Round.Y < 0.0 ? -1 : 1);
		bool bOwnSide = Part == EAedirClutchPart::Head || Part == EAedirClutchPart::Thigh;
		// Only the low trunk has sides a hand on its own side holds; high up by the shoulder the other hand comes across.
		const FString Name = Bone.ToString();
		const float ArmpitZ = static_cast<float>(FMath::Min(Body->GetBoneLocation(TEXT("upperarm_l")).Z, Body->GetBoneLocation(TEXT("upperarm_r")).Z)) - ClutchArmpitBelowCm;
		if (Part == EAedirClutchPart::Trunk && !Name.StartsWith(TEXT("spine_04")) && !Name.StartsWith(TEXT("spine_05")) && PointWorld.Z < ArmpitZ)
		{
			bOwnSide = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Round.Y), Round.X)) > ClutchFlankDegrees;
		}
		const int32 First = bOwnSide ? (Side < 0 ? 0 : 1) : (Side < 0 ? 1 : 0);
		// A shoulder or an arm is never held by its own hand.
		const bool bOwnLimb = Part == EAedirClutchPart::Shoulder || Part == EAedirClutchPart::Arm;
		const bool bOtherMay = !bOwnLimb && Free(1 - First);
		// A hand with nothing to do first, the right one before the other; only then one taken from a place less bad.
		if (Free(First) && Idle(First))
		{
			return First;
		}
		if (bOtherMay && Idle(1 - First))
		{
			return 1 - First;
		}
		if (Free(First) && Takeable(First))
		{
			return First;
		}
		return bOtherMay && Takeable(1 - First) ? 1 - First : INDEX_NONE;
	};
	const auto Always = [](int32) { return true; };
	// A hand's flinch set, or ended (0), in the fight and the fists alike: the hand is out of them exactly while it holds.
	const auto SetFlinch = [&](int32 Slot, float Seconds)
	{
		const EAedirHand FlinchHand = Slot == 0 ? EAedirHand::Left : EAedirHand::Right;
		if (AAedirCreature* Owner = CreatureOwner.Get())
		{
			if (Owner->Combat)
			{
				Owner->Combat->Flinch(FlinchHand, Seconds);
			}
			if (UAedirFistArms* Fists = Owner->FindComponentByClass<UAedirFistArms>())
			{
				Fists->Flinch(FlinchHand, Seconds);
			}
		}
	};
	// The wrist a little off the skin and back towards the hand's own shoulder, so the palm and fingers lie over the place.
	const auto WristAt = [&](int32 Slot, const FVector& PointWorld, const FVector& OutwardWorld)
	{
		const FVector Shoulder = Body->GetBoneLocation(Slot == 0 ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		const FVector Back = FVector::VectorPlaneProject(Shoulder - PointWorld, OutwardWorld).GetSafeNormal();
		const float Scale = static_cast<float>(ActorTransform.GetScale3D().X);
		return PointWorld + (OutwardWorld * ClutchSkinCm + Back * ClutchPalmCm) * Scale;
	};

	if (bDown)
	{
		HandFlinch[0].Left = HandFlinch[1].Left = 0.f;
	}
	else
	{
		// Everything new that hurts: a blow, and the wounds opened just now.
		struct FNewHurt
		{
			FName Bone;
			FVector Local;
			float Severity;
		};
		TArray<FNewHurt, TInlineAllocator<4>> NewHurts;
		// Every blow since the last frame: two landing at once on two places are two hurts. A shoulder's push sent on into
		// the trunk with it is still one blow, held at the shoulder.
		const double Seen = FlinchBlowSeen;
		if (BalanceComponent)
		{
			for (const UAedirBalanceComponent::FAedirBlowLanded& Blow : BalanceComponent->GetRecentBlows())
			{
				if (Blow.Time <= Seen)
				{
					continue;
				}
				FlinchBlowSeen = FMath::Max(FlinchBlowSeen, Blow.Time);
				static const IConsoleVariable* HitLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
				if (HitLog && HitLog->GetInt() != 0)
				{
					UE_LOG(LogAedir, Log, TEXT("[Clutch] %s takes a blow of %.0f on %s"), *GetNameSafe(CreatureOwner), Blow.Push, *Blow.Bone.ToString());
				}
				int32 PartSide = 0;
				const EAedirClutchPart Struck = AedirClutchPartOf(Blow.Bone, PartSide);
				if (Blow.Push >= ClutchBlowMinPush && Struck != EAedirClutchPart::None && Struck != EAedirClutchPart::Arm && Body->GetBoneIndex(Blow.Bone) != INDEX_NONE)
				{
					NewHurts.Add({ Blow.Bone, Blow.Local, FMath::Clamp((Blow.Push - ClutchBlowMinPush) / FMath::Max(ClutchBlowFullPush - ClutchBlowMinPush, 1.f), 0.f, 1.f) });
				}
			}
		}
		if (WoundComponent)
		{
			for (const FAedirWound& Wound : WoundComponent->ActiveWounds)
			{
				bool bAlreadySeen = false;
				FlinchWoundsSeen.Add(Wound.Id, &bAlreadySeen);
				int32 PartSide = 0;
				if (bAlreadySeen || !bFlinchWoundsKnown || Wound.Type == EAedirWoundType::Bruise || Wound.GetSeverity() < ClutchFlinchWoundSeverity
					|| AedirClutchPartOf(Wound.Bone, PartSide) == EAedirClutchPart::None || Body->GetBoneIndex(Wound.Bone) == INDEX_NONE)
				{
					continue;
				}
				NewHurts.Add({ Wound.Bone, Wound.Offset, Wound.GetSeverity() });
			}
			// A healed wound's number may come again for a new one.
			for (auto It = FlinchWoundsSeen.CreateIterator(); It; ++It)
			{
				if (!WoundComponent->ActiveWounds.ContainsByPredicate([Id = *It](const FAedirWound& Wound) { return Wound.Id == Id; }))
				{
					It.RemoveCurrent();
				}
			}
			bFlinchWoundsKnown = true;
		}
		// The worst first. A blow and the wound it opened are one hurt, the worse of the two.
		NewHurts.Sort([](const FNewHurt& A, const FNewHurt& B) { return A.Severity > B.Severity; });
		const float SamePlace = ClutchSamePlaceCm * static_cast<float>(ActorTransform.GetScale3D().X);
		TArray<FNewHurt, TInlineAllocator<4>> Hurts;
		for (const FNewHurt& Hurt : NewHurts)
		{
			if (!Hurts.ContainsByPredicate([&](const FNewHurt& Kept) { return Kept.Bone == Hurt.Bone && FVector::Dist(Kept.Local, Hurt.Local) < SamePlace; }))
			{
				Hurts.Add(Hurt);
			}
		}
		for (const FNewHurt& Hurt : Hurts)
		{
			const float NewSeconds = FMath::Lerp(ClutchFlinchMinSeconds, ClutchFlinchMaxSeconds, Hurt.Severity);
			const FVector At = Body->GetBoneTransform(Hurt.Bone).TransformPosition(Hurt.Local);
			// A place a hand holds already stays that hand's, the longer and the worse of the two.
			int32 Slot = INDEX_NONE;
			for (int32 Hand = 0; Hand < 2; ++Hand)
			{
				if (HandFlinch[Hand].Left > 0.f && HandFlinch[Hand].Bone == Hurt.Bone && FVector::Dist(HandFlinch[Hand].Local, Hurt.Local) < SamePlace)
				{
					Slot = Hand;
				}
			}
			if (Slot != INDEX_NONE)
			{
				FAedirHandFlinch& Held = HandFlinch[Slot];
				Held.Severity = FMath::Max(Held.Severity, Hurt.Severity);
				Held.Left = FMath::Max(Held.Left, NewSeconds);
				Held.Seconds = FMath::Max(Held.Seconds, Held.Left);
				SetFlinch(Slot, Held.Left);
				continue;
			}
			// Otherwise a hand with nothing to hold goes there; failing one, it takes over from a place less bad, or half over.
			const auto Idle = [&](int32 Hand) { return HandFlinch[Hand].Left <= 0.f; };
			const auto Takeable = [&](int32 Hand) { return HandFlinch[Hand].Left <= 0.f || Hurt.Severity >= HandFlinch[Hand].Severity || HandFlinch[Hand].Left < NewSeconds * 0.5f; };
			Slot = PickHand(Hurt.Bone, At, Idle, Takeable);
			if (Slot == INDEX_NONE)
			{
				continue;
			}
			FAedirHandFlinch& Flinch = HandFlinch[Slot];
			Flinch.Bone = Hurt.Bone;
			Flinch.Local = Hurt.Local;
			Flinch.Severity = Hurt.Severity;
			Flinch.Left = NewSeconds;
			Flinch.Seconds = NewSeconds;
			Flinch.ElbowJump = 0.f;
			Flinch.ElbowLast.Reset();
			UE_LOG(LogAedir, Log, TEXT("[Clutch] %s flinches for %.1f s, the %s hand to %s (the %s hand %s)"), *GetNameSafe(CreatureOwner), NewSeconds, Slot == 0 ? TEXT("left") : TEXT("right"), *Hurt.Bone.ToString(),
				Slot == 0 ? TEXT("right") : TEXT("left"), HandFlinch[1 - Slot].Left > 0.f ? *FString::Printf(TEXT("holds %s"), *HandFlinch[1 - Slot].Bone.ToString()) : TEXT("free"));
			// That hand comes out of the fight while it is there; the other one goes on.
			SetFlinch(Slot, NewSeconds);
		}
		for (int32 FlinchSlot = 0; FlinchSlot < 2; ++FlinchSlot)
		{
			FAedirHandFlinch& Flinch = HandFlinch[FlinchSlot];
			if (Flinch.Left <= 0.f)
			{
				continue;
			}
			if (Body->GetBoneIndex(Flinch.Bone) == INDEX_NONE)
			{
				// The place is gone (cut off): the hand is back in the fight at once.
				Flinch.Left = 0.f;
				SetFlinch(FlinchSlot, 0.f);
				continue;
			}
			const float Before = Flinch.Left;
			Flinch.Left = FMath::Max(Flinch.Left - DeltaTime, 0.f);
			const FName FlinchBone = Flinch.Bone;
			const FTransform Frame = Body->GetBoneTransform(FlinchBone);
			const FVector At = Frame.TransformPosition(Flinch.Local);
			// Once, well into it: how near the hand came to the place.
			const float Into = Flinch.Seconds - 0.6f;
			if (Before > Into && Flinch.Left <= Into)
			{
				const FVector Palm = Body->GetBoneLocation(FlinchSlot == 0 ? TEXT("hand_l") : TEXT("hand_r"));
				const FVector OtherPalm = Body->GetBoneLocation(FlinchSlot == 0 ? TEXT("hand_r") : TEXT("hand_l"));
				UE_LOG(LogAedir, Log, TEXT("[Clutch] %s: the %s hand is %.0f cm from the place on %s, the other %.0f cm; the elbow jumped at most %.1f cm in a frame"), *GetNameSafe(CreatureOwner), FlinchSlot == 0 ? TEXT("left") : TEXT("right"), FVector::Dist(Palm, At), *FlinchBone.ToString(), FVector::Dist(OtherPalm, At), Flinch.ElbowJump);
			}
			// A blow by the ear or behind it is held from the side of the face: a hand reached round to behind the ear twists
			// the arm up over itself and it thrashes about. The place is taken forward to the temple, and held from outside.
			FVector HoldAt = At;
			FVector Outward = (At - Frame.GetLocation()).GetSafeNormal();
			int32 PartSide = 0;
			if (AedirClutchPartOf(FlinchBone, PartSide) == EAedirClutchPart::Head)
			{
				const float Scale = static_cast<float>(ActorTransform.GetScale3D().X);
				FVector Round = ActorTransform.InverseTransformVectorNoScale(At - Frame.GetLocation());
				Round.X = FMath::Max(Round.X, static_cast<double>(ClutchHeadFrontCm * Scale));
				HoldAt = Frame.GetLocation() + ActorTransform.TransformVectorNoScale(Round);
				const FVector SideOut = ActorTransform.TransformVectorNoScale(FVector(0.f, FlinchSlot == 0 ? -1.f : 1.f, 0.f));
				Outward = (Outward + SideOut).GetSafeNormal();
			}
			Wanted[FlinchSlot] = ClutchBlowShare;
			Goal[FlinchSlot] = WristAt(FlinchSlot, HoldAt, Outward);
			bFlinchHand[FlinchSlot] = true;
			// Dev: the biggest jump the elbow makes from one frame to the next while the hand goes there.
			const FVector Elbow = Body->GetBoneLocation(FlinchSlot == 0 ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
			if (Flinch.ElbowLast.IsSet())
			{
				Flinch.ElbowJump = FMath::Max(Flinch.ElbowJump, static_cast<float>(FVector::Dist(Elbow, Flinch.ElbowLast.GetValue())));
			}
			Flinch.ElbowLast = Elbow;
		}

		// Out of the fight and not reaching for anything, the worst of what still hurts is held while it lasts: a stump, a
		// broken limb, or a wound still bleeding.
		const UAedirFistArms* Fists = CreatureOwner->FindComponentByClass<UAedirFistArms>();
		const bool bBusy = (Combat && Combat->IsInFightStance()) || (Fists && Fists->IsSteering()) || (Interaction && Interaction->GetReachLeanDegrees() > 1.f);
		if (WoundComponent && !bBusy)
		{
			float Worst = 0.f;
			int32 Slot = INDEX_NONE;
			FVector Point = FVector::ZeroVector;
			FVector Outward = FVector::ZeroVector;
			const auto Consider = [&](float Score, int32 Hand, const FVector& At, const FVector& Out)
			{
				if (Hand != INDEX_NONE && HandFlinch[Hand].Left <= 0.f && Score > Worst)
				{
					Worst = Score;
					Slot = Hand;
					Point = At;
					Outward = Out;
				}
			};
			const AGameStateBase* GameState = World->GetGameState();
			const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
			for (const FAedirWound& Wound : WoundComponent->ActiveWounds)
			{
				int32 PartSide = 0;
				const float Severity = Wound.GetSeverity();
				if (Severity < ClutchWoundMinSeverity || AedirClutchPartOf(Wound.Bone, PartSide) == EAedirClutchPart::None || Body->GetBoneIndex(Wound.Bone) == INDEX_NONE
					|| UAedirWoundComponent::ComputeBleedRate(Wound.Type, Severity, static_cast<float>(Now - Wound.StartServerTime)) <= 0.f)
				{
					continue;
				}
				const FTransform Frame = Body->GetBoneTransform(Wound.Bone);
				const FVector At = Frame.TransformPosition(Wound.Offset);
				Consider(Severity, PickHand(Wound.Bone, At, Always, Always), At, Frame.TransformVectorNoScale(Wound.Normal).GetSafeNormal());
			}
			// A lost arm is held at the stump by the other hand, a lost thigh by the hand on its side; a broken arm at the
			// middle of the upper arm by the other hand, a broken leg above the knee by the hand on its side.
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const bool bLeft = Side == 0;
				const int32 Other = Free(1 - Side) ? 1 - Side : INDEX_NONE;
				const int32 Own = Free(Side) ? Side : Other;
				const FVector Ahead = ActorTransform.GetUnitAxis(EAxis::X);
				for (const TCHAR* Stump : { TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand") })
				{
					const FName Part(FString::Printf(TEXT("%s%s"), Stump, bLeft ? TEXT("_l") : TEXT("_r")));
					if (WoundComponent->IsSevered(Part) && Body->GetBoneIndex(Part) != INDEX_NONE)
					{
						Consider(1.2f, Other, Body->GetBoneLocation(Part), Ahead);
					}
				}
				const FName Thigh = bLeft ? TEXT("thigh_l") : TEXT("thigh_r");
				const FName Calf = bLeft ? TEXT("calf_l") : TEXT("calf_r");
				if (WoundComponent->IsSevered(Calf) || WoundComponent->IsSevered(Thigh))
				{
					Consider(1.2f, Own, Body->GetBoneLocation(WoundComponent->IsSevered(Thigh) ? Thigh : Calf), Ahead);
				}
				if (WoundComponent->IsLimbBroken(bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm) && !WoundComponent->HasLostArm(bLeft))
				{
					Consider(1.f, Other, FMath::Lerp(Body->GetBoneLocation(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r")), Body->GetBoneLocation(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r")), 0.5f), Ahead);
				}
				if (WoundComponent->IsLimbBroken(bLeft ? EAedirLimb::LeftLeg : EAedirLimb::RightLeg))
				{
					Consider(1.f, Own, FMath::Lerp(Body->GetBoneLocation(Thigh), Body->GetBoneLocation(Calf), 0.55f), Ahead);
				}
			}
			if (Slot != INDEX_NONE)
			{
				Wanted[Slot] = ClutchWoundShare;
				Goal[Slot] = WristAt(Slot, Point, Outward);
			}
		}
	}
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		// The place moves with the body: the hand keeps to it while it is there, and lets go where it was.
		if (Wanted[Slot] > 0.f || ClutchAlpha[Slot] <= KINDA_SMALL_NUMBER)
		{
			ClutchTarget[Slot] = Goal[Slot];
		}
		ClutchAlpha[Slot] = FMath::FInterpConstantTo(ClutchAlpha[Slot], Wanted[Slot], DeltaTime, Wanted[Slot] > ClutchAlpha[Slot] ? ClutchInSpeed : ClutchOutSpeed);
		// Still over the rest while it lets go, so the arm does not snap back to the guard.
		bClutchFlinch[Slot] = bFlinchHand[Slot] || (bClutchFlinch[Slot] && Wanted[Slot] <= 0.f && ClutchAlpha[Slot] > KINDA_SMALL_NUMBER);
	}
}

void UAedirGaitComponent::UpdateTrunkInertia(float DeltaTime, const FTransform& ActorTransform)
{
	if (DeltaTime <= KINDA_SMALL_NUMBER || !CreatureOwner)
	{
		return;
	}
	const FVector Velocity = CreatureOwner->GetVelocity();
	FVector2D Accel = FVector2D::ZeroVector;
	if (bHasPreviousVelocity)
	{
		const FVector Change = ActorTransform.InverseTransformVectorNoScale((Velocity - PreviousVelocity) / DeltaTime);
		Accel = FVector2D(Change.X, Change.Y);
		if (Accel.Size() > AedirInertiaMaxAccel)
		{
			Accel = FVector2D::ZeroVector;
		}
	}
	PreviousVelocity = Velocity;
	bHasPreviousVelocity = true;
	SmoothedAccel = FMath::Vector2DInterpTo(SmoothedAccel, Accel, DeltaTime, AedirInertiaAccelSmoothing);

	// Speeding up leaves the chest behind (a lean back, positive), slowing down carries it on (forward, negative); a turn
	// throws it to the outside. The spring then pulls it back over the hips.
	const FVector2D Target(
		FMath::Clamp(SmoothedAccel.X * AedirInertiaDegreesPerAccel, -AedirInertiaMaxPitch, AedirInertiaMaxPitch),
		FMath::Clamp(-SmoothedAccel.Y * AedirInertiaDegreesPerAccel * AedirInertiaSideShare, -AedirInertiaMaxRoll, AedirInertiaMaxRoll));
	const float Omega = AedirInertiaFrequency;
	// Small steps so a long frame cannot blow the spring up.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.008f), 1, 8);
	const float Step = DeltaTime / Steps;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const FVector2D Spring = (Target - TrunkInertia) * (Omega * Omega) - TrunkInertiaRate * (2.f * AedirInertiaDamping * Omega);
		TrunkInertiaRate += Spring * Step;
		TrunkInertia += TrunkInertiaRate * Step;
	}
	TrunkInertia.X = FMath::Clamp(TrunkInertia.X, -AedirInertiaMaxPitch * 1.3f, AedirInertiaMaxPitch * 1.3f);
	TrunkInertia.Y = FMath::Clamp(TrunkInertia.Y, -AedirInertiaMaxRoll * 1.3f, AedirInertiaMaxRoll * 1.3f);
}
