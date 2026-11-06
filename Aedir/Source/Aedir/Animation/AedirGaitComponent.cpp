#include "Animation/AedirGaitComponent.h"

#include "Components/CapsuleComponent.h"
#include "Combat/AedirCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemTypes.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Physics/AedirBalanceComponent.h"

namespace
{
	constexpr float AedirRestPhaseMin = 0.4f;
	constexpr float AedirRestPhaseMax = 0.5f;
	constexpr float AedirRestCatchUpCyclesPerSecond = 2.f;
	constexpr float AedirMinStrideLength = 10.f;
	constexpr float AedirStepHeightClearance = 4.f;
	constexpr float AedirMinStepTriggerCm = 14.f;
	constexpr float AedirTurnStepTriggerCm = 10.f;
	constexpr float AedirRestStepTriggerCm = 14.f;
	constexpr float AedirOutOfTurnStepShare = 1.4f;
	constexpr float AedirMinStepSeconds = 0.14f;
	constexpr float AedirMaxStepSeconds = 0.38f;
	constexpr float AedirLandingOverlapStart = 0.75f;
	constexpr float AedirOverlapMinWalkShare = 1.2f;
	constexpr float AedirLandingOverlapShare = 1.15f;
	constexpr float AedirLandingLeadSeconds = 0.03f;
	constexpr float AedirPlaceStepSeconds = 0.32f;
	constexpr float AedirPhaseCorrectionSpeed = 10.f;
	constexpr float AedirReachHipHingeShare = 0.5f;
	constexpr float AedirFootDropLiftLimit = 0.25f;
	constexpr float AedirLegComfortShare = 0.985f;
	constexpr float AedirFootDropWalkMaxCm = 16.f;
	constexpr float AedirFootDropRunMaxCm = 6.f;
	constexpr float AedirStairMinSpeedScale = 0.6f;
	constexpr float AedirFootDropDownSpeed = 20.f;
	constexpr float AedirFootDropUpSpeed = 8.f;
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
	const bool bMoving = !bAirborne && Speed2D > MovingSpeedThreshold;
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
	const float SlowStrideScale = FMath::Clamp(Speed2D / WalkSpeedCm, AedirSlowStrideMinScale, 1.f);
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
	const float StanceWidthScale = FMath::Lerp(1.f, StanceFootWidthMultiplier, StanceAlpha) * PostureWidthScale;
	const float StanceLeadForwardCm = StanceLeadFootForwardCm * StanceAlpha * ActorScale;
	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const float Side = Foot == 0 ? -1.f : 1.f;
		// Foot 1 is the right foot. The lead foot moves forward, the rear foot back by half as much.
		const bool bLeadFoot = (Foot == 1) == bRightLead;
		const float StanceForwardCm = StanceLeadForwardCm * (bLeadFoot ? 1.f : -0.5f);
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
		const float Trigger = bMoving ? StepTriggerCm : (bTurning ? AedirTurnStepTriggerCm : AedirRestStepTriggerCm) * ActorScale;
		const float StepSeconds = bMoving ? MovingStepSeconds : AedirPlaceStepSeconds;

		const auto FootDistance = [&](int32 Foot)
		{
			return static_cast<float>((PlantedContact[Foot] - LandingLocation[Foot]).Size2D());
		};

		// Normally one foot at a time. A foot the body is pulling away from may lift while the other is just coming down.
		const int32 LaterFoot = 1 - NextStepFoot;
		const bool bLaterLanding = bSwinging[LaterFoot] && SwingElapsed[LaterFoot] / FMath::Max(SwingDuration[LaterFoot], KINDA_SMALL_NUMBER) > AedirLandingOverlapStart;
		// Up to walking pace and on steady feet there is only ever one foot in the air, and the body settles on both for a moment between steps.
		const bool bMayOverlap = Speed2D > WalkSpeedCm * AedirOverlapMinWalkShare || bRecovering || (BalanceComponent && BalanceComponent->IsStaggering());
		const bool bDoubleSupportDone = PlantedSeconds[LaterFoot] >= CycleSeconds * FMath::Max(0.5f - SwingFraction, 0.f);
		const bool bNextMayStep = !bSwinging[NextStepFoot]
			&& (bSwinging[LaterFoot] ? (bMayOverlap && bLaterLanding && FootDistance(NextStepFoot) >= Trigger * AedirLandingOverlapShare) : (bMayOverlap || bDoubleSupportDone));

		if (bRecovering && !bFootInAir && (RecoveryCooldownRemaining <= 0.f || RecoveryUrgency > 0.7f))
		{
			// The foot furthest towards the fall direction takes the catching step.
			const double ReachLeft = FVector::DotProduct(PlantedContact[0] - ActorLocation, RecoveryDirection);
			const double ReachRight = FVector::DotProduct(PlantedContact[1] - ActorLocation, RecoveryDirection);
			const FVector CatchWorld = BalanceComponent->GetCenterOfMassWorld() + RecoveryDirection * RecoveryStepLandingCm;
			BeginRecoverySwing(ReachRight > ReachLeft ? 1 : 0, CatchWorld, StrideLength * 0.5f * RecoveryStepMaxStrideMultiplier, RecoveryUrgency, BottomZ);
			RecoveryCooldownRemaining = RecoveryStepCooldownSeconds;
		}
		else if (bNextMayStep && FootDistance(NextStepFoot) >= Trigger)
		{
			BeginStep(NextStepFoot, StepSeconds);
		}
		else if (!bFootInAir && bMayOverlap && FootDistance(1 - NextStepFoot) >= Trigger * AedirOutOfTurnStepShare)
		{
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

	const float SpeedStepHeight = BaseStepHeightCm + StepHeightSpeedBonusCm * SpeedAlpha + RunStepHeightBonusCm * RunMix;
	const float AnkleRestCm = (AnkleRefHeightCm > 0.f ? AnkleRefHeightCm : AnkleHeightCm) * ActorScale;
	FVector Contact[2];
	FVector AnkleWorld[2];
	float Lift[2] = { 0.f, 0.f };
	FVector TargetNormal[2];

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
			const float Arc = FMath::Sin(UE_PI * T);
			const FVector& Target = bSwingFixedTarget[Foot] ? FixedLandingWorld[Foot] : LandingLocation[Foot];
			const FVector& TargetUp = bSwingFixedTarget[Foot] ? FixedLandingNormal[Foot] : LandingNormal[Foot];
			const float Rise = bSwingFixedTarget[Foot] ? static_cast<float>(FMath::Abs(Target.Z - SwingStartContact[Foot].Z)) : RiseCm[Foot];
			const float StepHeight = FMath::Clamp(FMath::Max(SpeedStepHeight, Rise + AedirStepHeightClearance), 0.f, MaxStepHeightCm);

			Contact[Foot] = FMath::Lerp(SwingStartContact[Foot], Target, Smooth);
			FootPosition = Contact[Foot] + FVector::UpVector * (StepHeight * Arc);
			Lift[Foot] = Arc;
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

	// The interaction component smooths its outputs itself, so the reach values are applied directly each frame.
	const UAedirInteractionComponent* Interaction = CreatureOwner->Interaction.Get();
	const float ReachLowerCm = Interaction ? FMath::Max(Interaction->GetPelvisLowerZ(), 0.f) : 0.f;
	const float ReachLowerAlpha = FMath::Clamp(ReachLowerCm / FMath::Max(ReachLowerMaxCm, KINDA_SMALL_NUMBER), 0.f, 1.f);
	GaitState.HipOffsetZ -= ReachLowerCm;
	GaitState.HipForwardOffset = Interaction ? Interaction->GetReachHipForward() * ActorScale : 0.f;
	GaitState.HipOffsetZ -= StanceHipLowerCm * StanceAlpha;

	// Crouching shrinks the capsule, so the pelvis drops by the same amount and the camera boom stays on the body.
	const float CrouchDropCm = FMath::Max(CreatureOwner->GetDefaultHalfHeight() - MovementComponent->GetCrouchedHalfHeight(), 0.f) * ActorScale;
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, CreatureOwner->bIsCrouched ? 1.f : 0.f, DeltaTime, 3.5f);
	GaitState.HipOffsetZ -= CrouchDropCm * CrouchAlpha;

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

	ResolveArmReach(EAedirHand::Left, Combat, Interaction, ActorTransform, ProtectiveLeftTarget, ProtectiveAlpha, LeftReachTarget, LeftReachAlpha);
	ResolveArmReach(EAedirHand::Right, Combat, Interaction, ActorTransform, ProtectiveRightTarget, ProtectiveAlpha, RightReachTarget, RightReachAlpha);

	const FRotator BalanceLean = BalanceComponent ? BalanceComponent->GetPelvisLean() : FRotator::ZeroRotator;
	const float PelvisYaw = FMath::Sin(2.f * UE_PI * Phase) * PelvisYawSwingDegrees * GaitIntensity;
	// A low reach hinges at the hips as well as along the spine: half of the bend tips the pelvis forward.
	const float ReachLean = Interaction ? Interaction->GetReachLeanDegrees() : 0.f;
	GaitState.PelvisRotation = FRotator(BalanceLean.Pitch - ReachLean * AedirReachHipHingeShare, PelvisYaw, BalanceLean.Roll);

	const float LookYaw = MovementComponent->GetYawDeltaToControl();
	const float LookPitch = FMath::Clamp(FRotator::NormalizeAxis(CreatureOwner->GetControlRotation().Pitch), -MaxLookPitchDegrees, MaxLookPitchDegrees);

	GaitState.SpineLeanPitch = -MaxRunForwardLeanDegrees * SpeedAlpha + BalanceLean.Pitch + LookPitch * SpinePitchShare;
	GaitState.SpineLeanPitch -= ReachLean * (1.f - AedirReachHipHingeShare);
	// Crouching bends the spine forward too, the body does not just sink on its legs.
	GaitState.SpineLeanPitch -= CrouchSpineLeanDegrees * CrouchAlpha;
	GaitState.SpineLeanPitch -= StanceSpineLeanDegrees * StanceAlpha;
	GaitState.SpineLeanPitch -= ProtectiveSpineLeanDegrees * ProtectiveAlpha;
	// Backward positive: the torso leans back to bring the kicking leg up.
	GaitState.SpineLeanPitch += KickSpineLeanDegrees * KickAlpha;
	const float TwistTarget = FMath::Clamp(LookYaw * SpineTwistScale, -SpineTwistLimitDegrees, SpineTwistLimitDegrees);
	SmoothedSpineTwist = FMath::FInterpTo(SmoothedSpineTwist, TwistTarget, DeltaTime, LookInterpSpeed);
	GaitState.SpineTwistYaw = SmoothedSpineTwist + (Interaction ? Interaction->GetReachTwistDegrees() : 0.f);

	const FRotator HeadTarget(
		LookPitch * HeadPitchShare,
		FMath::Clamp(LookYaw * HeadYawScale, -HeadYawLimitDegrees, HeadYawLimitDegrees),
		0.f);
	const FRotator NeckTarget(
		LookPitch * NeckPitchShare,
		FMath::Clamp(LookYaw * NeckYawScale, -NeckYawLimitDegrees, NeckYawLimitDegrees),
		0.f);
	GaitState.HeadLookRotation = FMath::RInterpTo(GaitState.HeadLookRotation, HeadTarget, DeltaTime, LookInterpSpeed);
	GaitState.NeckLookRotation = FMath::RInterpTo(GaitState.NeckLookRotation, NeckTarget, DeltaTime, LookInterpSpeed);

	ArmBlend = FMath::FInterpTo(ArmBlend, bMoving ? 1.f : 0.f, DeltaTime, ArmBlendInterpSpeed);
	const float DirectionalArmScale = FMath::Lerp(1.f, ArmSideScale, SideAlpha) * FMath::Lerp(1.f, ArmBackScale, BackAlpha);
	const float ArmAmplitude = FMath::Lerp(MaxArmSwingWalkDegrees, MaxArmSwingRunDegrees, SpeedAlpha * SpeedAlpha) * DirectionalArmScale * ArmBlend;
	// The arms follow the legs: their swing is a little behind the stride phase.
	GaitState.ArmSwingLeft = FMath::Sin(2.f * UE_PI * (Phase + 0.5f - ArmPhaseLag)) * ArmAmplitude;
	GaitState.ArmSwingRight = FMath::Sin(2.f * UE_PI * (Phase - ArmPhaseLag)) * ArmAmplitude;
	GaitState.ArmSwingLeft *= 1.f - LeftReachAlpha;
	GaitState.ArmSwingRight *= 1.f - RightReachAlpha;
	const float BalanceNow = BalanceComponent ? BalanceComponent->GetBalance() : 1.f;
	GaitState.ArmBalanceSpread = FMath::FInterpTo(GaitState.ArmBalanceSpread, FMath::Clamp((0.85f - BalanceNow) / 0.5f, 0.f, 1.f), DeltaTime, 10.f);
	const auto ReadWeaponAim = [Combat](EAedirHand Hand, FVector& OutDirection, float& OutAlpha)
	{
		FVector Direction = FVector::ForwardVector;
		float Alpha = 0.f;
		if (Combat && Combat->GetWeaponAim(Hand, Direction, Alpha))
		{
			OutDirection = Direction;
			OutAlpha = Alpha;
		}
		else
		{
			OutAlpha = 0.f;
		}
	};
	ReadWeaponAim(EAedirHand::Left, GaitState.LeftWeaponDirWorld, GaitState.LeftWeaponAimAlpha);
	ReadWeaponAim(EAedirHand::Right, GaitState.RightWeaponDirWorld, GaitState.RightWeaponAimAlpha);
	EAedirHand GripSecondary = EAedirHand::Left;
	GaitState.GripSecondaryHand = -1;
	if (Combat && Combat->GetSecondaryGrip(GripSecondary, GaitState.GripPointInPrimaryHand, GaitState.GripBladeInPrimaryHand, GaitState.GripAlpha))
	{
		GaitState.GripSecondaryHand = static_cast<int32>(GripSecondary);
	}
	GaitState.LeftArmReachAlpha = LeftReachAlpha;
	GaitState.RightArmReachAlpha = RightReachAlpha;
	GaitState.LeftReachTargetWorld = LeftReachTarget;
	GaitState.RightReachTargetWorld = RightReachTarget;
	GaitState.FistAlphaLeft = Combat ? FMath::Clamp(Combat->GetFistAlpha(EAedirHand::Left), 0.f, 1.f) : 0.f;
	GaitState.FistAlphaRight = Combat ? FMath::Clamp(Combat->GetFistAlpha(EAedirHand::Right), 0.f, 1.f) : 0.f;
	// Loose arms hang straight while standing; the elbows start to break with the walking and running speed.
	GaitState.ElbowBend = FMath::Lerp(ElbowBendWalkDegrees, ElbowBendRunDegrees, SpeedAlpha * SpeedAlpha) * ArmBlend;
	GaitState.KneeBend = FMath::Max(Lift[0], Lift[1]) * KneeBendMaxDegrees + ReachLowerAlpha * KneeBendMaxDegrees;
	GaitState.KneeBend += StanceKneeBendDegrees * StanceAlpha;

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
	GaitState.SpineLeanRoll = InertiaRoll + LocalDirection.Y * SpeedAlpha * MoveDirectionLeanRollDegrees;

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
	GaitState.SpineCounterRollDegrees = -SpineCounterRollShare * GaitState.PelvisRollDegrees;
	GaitState.HeadStabilizeRollDegrees = -HeadStabilizeRollShare * GaitState.PelvisRollDegrees;

	GaitState.Phase = Phase;
	GaitState.bStepping = bStepping;
	GaitState.bTurningInPlace = bTurning;
	GaitState.bProceduralActive = true;
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
	OutNames.RightClavicle = RightClavicleBoneName;
	OutNames.RightHumerus = RightHumerusBoneName;
	OutNames.RightRadius = RightRadiusBoneName;
	OutNames.RightHand = RightHandBoneName;
	OutNames.RightFemur = RightFemurBoneName;
	OutNames.RightTibia = RightTibiaBoneName;
	OutNames.RightFoot = RightFootBoneName;
}

bool UAedirGaitComponent::SampleGround(const FVector& WorldXY, float BottomZ, FVector& OutLocation, FVector& OutNormal) const
{
	const UWorld* World = GetWorld();
	const FVector Start(WorldXY.X, WorldXY.Y, BottomZ + GroundTraceUpCm);
	const FVector End(WorldXY.X, WorldXY.Y, BottomZ - GroundTraceDownCm);

	FHitResult Hit;
	const FCollisionQueryParams Params(FName(TEXT("AedirGaitGround")), false, GetOwner());
	if (World && World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
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

void UAedirGaitComponent::BeginStep(int32 Foot, float Seconds)
{
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

	FVector ReachTarget = FVector::ZeroVector;
	float ReachAlpha = 0.f;
	QueryReachPose(Interaction, Hand, ReachTarget, ReachAlpha);
	Blend.Apply(ReachTarget, ReachAlpha);

	// The swing is the highest priority and overrides both the guard and the interaction reach.
	FVector SwingTarget = FVector::ZeroVector;
	float SwingAlpha = 0.f;
	if (Combat && Combat->GetSwingPose(Hand, SwingTarget, SwingAlpha))
	{
		Blend.Apply(SwingTarget, SwingAlpha);
	}

	// The protective reflex is the highest priority while falling and overrides the swing.
	Blend.Apply(ProtectiveTargetWorld, ProtectiveAlpha);

	OutTargetWorld = Blend.Target;
	OutAlpha = Blend.Alpha;
}
