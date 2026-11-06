#include "Animation/AedirGaitComponent.h"

#include "Components/CapsuleComponent.h"
#include "Combat/AedirCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
	constexpr float AedirSettleStepDistanceCm = 5.f;
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
	const bool bMoving = Speed2D > MovingSpeedThreshold;
	const bool bTurning = MovementComponent->IsTurningInPlace();
	// A falling centre of mass requests a stumble step, which also starts a step from standing.
	FVector RecoveryDirection = FVector::ZeroVector;
	float RecoveryUrgency = 0.f;
	const bool bRecovering = BalanceComponent && BalanceComponent->GetRecoveryStepRequest(RecoveryDirection, RecoveryUrgency);
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
	const float StrideLength = FMath::Max(
		FMath::Lerp(StrideWalkCm, StrideRunCm, SpeedAlpha) * ActorScale * DirectionalStrideScale * AthleticsScale * StairFactor * StanceStrideScale,
		AedirMinStrideLength);
	const float SwingFraction = FMath::Lerp(SwingFractionWalk, SwingFractionRun, SpeedAlpha);
	const FVector MoveDirection = bMoving ? Velocity.GetSafeNormal2D() : ActorForward2D;

	AdvancePhase(DeltaTime, bMoving, bTurning, Speed2D, StrideLength);

	const float FootFraction[2] = { WrapUnit(Phase), WrapUnit(Phase + 0.5f) };

	FVector LandingLocation[2];
	FVector LandingNormal[2];
	FVector AheadLocation[2];
	FVector HomeLocation[2];

	const float StanceWidthScale = FMath::Lerp(1.f, StanceFootWidthMultiplier, StanceAlpha);
	const float StanceLeadForwardCm = StanceLeadFootForwardCm * StanceAlpha * ActorScale;
	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const float Side = Foot == 0 ? -1.f : 1.f;
		// Foot 1 is the right foot. The lead foot moves forward, the rear foot back by half as much.
		const bool bLeadFoot = (Foot == 1) == bRightLead;
		const float StanceForwardCm = StanceLeadForwardCm * (bLeadFoot ? 1.f : -0.5f);
		const FVector HomeXY = ActorLocation + ActorRight2D * (Side * HipHalfWidthCm * StanceWidthScale * ActorScale) + ActorForward2D * StanceForwardCm;
		HomeLocation[Foot] = HomeXY;
		const FVector LandingXY = HomeXY +(bMoving ? MoveDirection * (StrideLength * 0.3f) : FVector::ZeroVector);

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
			PreviousFootFraction[Foot] = FootFraction[Foot];
			bSwinging[Foot] = false;
		}
		bFeetInitialized = true;
	}

	RecoveryCooldownRemaining = FMath::Max(RecoveryCooldownRemaining - DeltaTime, 0.f);

	// A planted foot the body has walked away from steps early instead of stretching the leg towards a target it cannot reach.
	if (bMoving && !bKicking && !bRecoverySwinging[0] && !bRecoverySwinging[1])
	{
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			if (!bSwinging[Foot] && (PlantedContact[Foot] - HomeLocation[Foot]).Size2D() > StrideLength * MaxPlantedDriftStrideFraction)
			{
				BeginRecoverySwing(Foot, LandingLocation[Foot], StrideLength, 0.f, BottomZ);
				break;
			}
		}
	}

	if (bRecovering && !bRecoverySwinging[0] && !bRecoverySwinging[1] && (RecoveryCooldownRemaining <= 0.f || RecoveryUrgency > 0.7f))
	{
		// The free foot furthest towards the fall direction takes the catching step.
		int32 RecoveryFoot = INDEX_NONE;
		double BestReach = 0.0;
		for (int32 Foot = 0; Foot < 2; ++Foot)
		{
			if (bSwinging[Foot])
			{
				continue;
			}

			const double Reach = FVector::DotProduct(PlantedContact[Foot] - ActorLocation, RecoveryDirection);
			if (RecoveryFoot == INDEX_NONE || Reach > BestReach)
			{
				RecoveryFoot = Foot;
				BestReach = Reach;
			}
		}

		if (RecoveryFoot != INDEX_NONE)
		{
			const FVector CatchWorld = BalanceComponent->GetCenterOfMassWorld() + RecoveryDirection * RecoveryStepLandingCm;
			BeginRecoverySwing(RecoveryFoot, CatchWorld, StrideLength * 0.5f * RecoveryStepMaxStrideMultiplier, RecoveryUrgency, BottomZ);
			RecoveryCooldownRemaining = RecoveryStepCooldownSeconds;
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

	const float SpeedStepHeight = BaseStepHeightCm + StepHeightSpeedBonusCm * SpeedAlpha;
	FVector Contact[2];
	FVector AnkleWorld[2];
	float Lift[2] = { 0.f, 0.f };
	FVector TargetNormal[2];

	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const float Fraction = FootFraction[Foot];
		const bool bWrapped = Fraction < PreviousFootFraction[Foot];
		PreviousFootFraction[Foot] = Fraction;
		const bool bInSwingWindow = Fraction < SwingFraction;

		// The support leg of a kick is planted, so a swing that is still running is landed where the leg stands.
		if (bKicking && Foot != KickFoot && bSwinging[Foot])
		{
			bSwinging[Foot] = false;
			PlantedContact[Foot] = LandingLocation[Foot];
			PlantedNormal[Foot] = LandingNormal[Foot];
		}

		if (!bSwinging[Foot] && !bRecoverySwinging[Foot] && !bStepping)
		{
			// Standing still a foot that is away from its home takes one settling step there instead of jumping, one foot at a time.
			const FVector ToHome = LandingLocation[Foot] - PlantedContact[Foot];
			if (ToHome.Size2D() > AedirSettleStepDistanceCm * ActorScale || FMath::Abs(ToHome.Z) > 15.f)
			{
				if (!bRecoverySwinging[1 - Foot] && !bKicking)
				{
					BeginRecoverySwing(Foot, LandingLocation[Foot], 1000.f, 0.f, BottomZ);
				}
			}
			else
			{
				PlantedContact[Foot] = LandingLocation[Foot];
				PlantedNormal[Foot] = LandingNormal[Foot];
			}
		}

		if (!bSwinging[Foot])
		{
			if (!bRecoverySwinging[Foot] && !bKicking && bStepping && bWrapped && bInSwingWindow)
			{
				bSwinging[Foot] = true;
				SwingStartContact[Foot] = PlantedContact[Foot];
			}
		}
		else if (!bInSwingWindow)
		{
			bSwinging[Foot] = false;
			PlantedContact[Foot] = LandingLocation[Foot];
			PlantedNormal[Foot] = LandingNormal[Foot];
		}

		FVector FootPosition;
		if (bSwinging[Foot])
		{
			const float T = FMath::Clamp(Fraction / FMath::Max(SwingFraction, KINDA_SMALL_NUMBER), 0.f, 1.f);
			const float Smooth = T * T * (3.f - 2.f * T);
			const float Arc = FMath::Sin(UE_PI * T);
			const float StepHeight = FMath::Clamp(FMath::Max(SpeedStepHeight, RiseCm[Foot] + AedirStepHeightClearance), 0.f, MaxStepHeightCm);

			Contact[Foot] = FMath::Lerp(SwingStartContact[Foot], LandingLocation[Foot], Smooth);
			FootPosition = Contact[Foot] + FVector::UpVector * (StepHeight * Arc);
			Lift[Foot] = Arc;
			TargetNormal[Foot] = LandingNormal[Foot];
		}
		else
		{
			Contact[Foot] = PlantedContact[Foot];
			FootPosition = Contact[Foot];
			TargetNormal[Foot] = PlantedNormal[Foot];
		}

		if (bRecoverySwinging[Foot])
		{
			RecoverySwingElapsed[Foot] += DeltaTime;
			const float T = FMath::Clamp(RecoverySwingElapsed[Foot] / RecoverySwingDuration[Foot], 0.f, 1.f);
			const float Smooth = T * T * (3.f - 2.f * T);
			const float Arc = FMath::Sin(UE_PI * T);
			const float RecoveryRiseCm = FMath::Abs(RecoveryLandingWorld[Foot].Z - SwingStartContact[Foot].Z);
			const float RecoveryStepHeight = FMath::Clamp(FMath::Max(SpeedStepHeight, RecoveryRiseCm + AedirStepHeightClearance), 0.f, MaxStepHeightCm);

			Contact[Foot] = FMath::Lerp(SwingStartContact[Foot], RecoveryLandingWorld[Foot], Smooth);
			FootPosition = Contact[Foot] + FVector::UpVector * (RecoveryStepHeight * Arc);
			Lift[Foot] = Arc;
			TargetNormal[Foot] = RecoveryLandingNormal[Foot];

			if (T >= 1.f)
			{
				bRecoverySwinging[Foot] = false;
				PlantedContact[Foot] = RecoveryLandingWorld[Foot];
				PlantedNormal[Foot] = RecoveryLandingNormal[Foot];
			}
		}

		AnkleWorld[Foot] = FootPosition + FVector::UpVector * (AnkleHeightCm * ActorScale);
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

	const float RunTarget = MovementComponent->IsRunningNow() ? SpeedAlpha : 0.f;
	RunBlend = FMath::FInterpTo(RunBlend, RunTarget, DeltaTime, AedirRunBlendInterpSpeed);

	const float ContactAverageZ = (Contact[0].Z + Contact[1].Z) * 0.5f;
	const float DynamicHipTarget = FMath::Clamp(ContactAverageZ - BottomZ, HipDynamicMinCm, HipDynamicMaxCm);
	DynamicHipOffset = FMath::FInterpTo(DynamicHipOffset, DynamicHipTarget, DeltaTime, HipOffsetInterpSpeed);
	const float RunLowering = -HipRunLoweringCm * RunBlend;
	const float Bounce = HipBounceCm * FMath::Sin(2.f * UE_PI * Phase * AedirMovingBounceFrequency) * SpeedAlpha;
	GaitState.HipOffsetZ = DynamicHipOffset + RunLowering + Bounce;

	// The interaction component smooths its outputs itself, so the reach values are applied directly each frame.
	const UAedirInteractionComponent* Interaction = CreatureOwner->Interaction.Get();
	const float ReachLowerCm = Interaction ? FMath::Max(Interaction->GetPelvisLowerZ(), 0.f) : 0.f;
	const float ReachLowerAlpha = FMath::Clamp(ReachLowerCm / FMath::Max(ReachLowerMaxCm, KINDA_SMALL_NUMBER), 0.f, 1.f);
	GaitState.HipOffsetZ -= ReachLowerCm;
	GaitState.HipOffsetZ -= StanceHipLowerCm * StanceAlpha;

	// Crouching shrinks the capsule, so the pelvis drops by the same amount and the camera boom stays on the body.
	const float CrouchDropCm = FMath::Max(CreatureOwner->GetDefaultHalfHeight() - MovementComponent->GetCrouchedHalfHeight(), 0.f) * ActorScale;
	CrouchAlpha = FMath::FInterpTo(CrouchAlpha, CreatureOwner->bIsCrouched ? 1.f : 0.f, DeltaTime, 3.5f);
	GaitState.HipOffsetZ -= CrouchDropCm * CrouchAlpha;

	// Jump wind-up: the body sinks before the push-off. Landing: the pelvis absorbs the fall and springs back, less so for athletic bodies.
	const float WindupAlpha = MovementComponent->GetJumpWindupAlpha();
	GaitState.HipOffsetZ -= JumpWindupDipCm * ActorScale * FMath::Sin(WindupAlpha * UE_HALF_PI);
	const bool bAirborne = MovementComponent->IsFalling();
	if (bAirborne)
	{
		AirFallSpeed = FMath::Max(AirFallSpeed, -static_cast<float>(Velocity.Z));
	}
	else if (AirFallSpeed > 0.f)
	{
		const float AthleticsAbsorb = 1.f - 0.5f * FMath::Clamp(Athletism / 100.f, 0.f, 1.f);
		LandingDipCm = FMath::Min(AirFallSpeed / 900.f, 1.f) * LandingDipMaxCm * ActorScale * AthleticsAbsorb;
		AirFallSpeed = 0.f;
	}
	LandingDipCm = FMath::FInterpTo(LandingDipCm, 0.f, DeltaTime, LandingDipRecoverSpeed);
	GaitState.HipOffsetZ -= LandingDipCm;

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
	const float PelvisYaw = FMath::Sin(2.f * UE_PI * Phase) * PelvisYawSwingDegrees * SpeedAlpha;
	GaitState.PelvisRotation = FRotator(BalanceLean.Pitch, PelvisYaw, BalanceLean.Roll);

	const float LookYaw = MovementComponent->GetYawDeltaToControl();
	const float LookPitch = FMath::Clamp(FRotator::NormalizeAxis(CreatureOwner->GetControlRotation().Pitch), -MaxLookPitchDegrees, MaxLookPitchDegrees);

	GaitState.SpineLeanPitch = -MaxRunForwardLeanDegrees * SpeedAlpha + BalanceLean.Pitch + LookPitch * SpinePitchShare;
	GaitState.SpineLeanPitch -= ReachTorsoLeanMaxDegrees * ReachLowerAlpha;
	GaitState.SpineLeanPitch -= StanceSpineLeanDegrees * StanceAlpha;
	GaitState.SpineLeanPitch -= ProtectiveSpineLeanDegrees * ProtectiveAlpha;
	// Backward positive: the torso leans back to bring the kicking leg up.
	GaitState.SpineLeanPitch += KickSpineLeanDegrees * KickAlpha;
	const float TwistTarget = FMath::Clamp(LookYaw * SpineTwistScale, -SpineTwistLimitDegrees, SpineTwistLimitDegrees);
	SmoothedSpineTwist = FMath::FInterpTo(SmoothedSpineTwist, TwistTarget, DeltaTime, LookInterpSpeed);
	GaitState.SpineTwistYaw = SmoothedSpineTwist;

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
	GaitState.ArmSwingLeft = FMath::Sin(2.f * UE_PI * (Phase + 0.5f)) * ArmAmplitude;
	GaitState.ArmSwingRight = FMath::Sin(2.f * UE_PI * Phase) * ArmAmplitude;
	GaitState.ArmSwingLeft *= 1.f - LeftReachAlpha;
	GaitState.ArmSwingRight *= 1.f - RightReachAlpha;
	const float BalanceNow = BalanceComponent ? BalanceComponent->GetBalance() : 1.f;
	GaitState.ArmBalanceSpread = FMath::FInterpTo(GaitState.ArmBalanceSpread, FMath::Clamp((0.85f - BalanceNow) / 0.5f, 0.f, 1.f), DeltaTime, 10.f);
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
	const float GaitLateral = StanceSide * WalkWeightShiftLateralCm * SpeedAlpha * Alive;
	const float GaitRoll = -StanceSide * WalkWeightShiftRollDegrees * SpeedAlpha * Alive;

	GaitState.HipLateralOffset = IdleLateral + GaitLateral;
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
	bRecoverySwinging[0] = false;
	bRecoverySwinging[1] = false;
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

void UAedirGaitComponent::BeginRecoverySwing(int32 Foot, const FVector& CatchWorld, float MaxStepCm, float Urgency, float BottomZ)
{
	const FVector Start = PlantedContact[Foot];
	FVector Offset = CatchWorld - Start;
	Offset.Z = 0.f;
	if (Offset.Size() > MaxStepCm)
	{
		Offset = Offset.GetSafeNormal() * MaxStepCm;
	}

	SampleGround(Start + Offset, BottomZ, RecoveryLandingWorld[Foot], RecoveryLandingNormal[Foot]);

	bSwinging[Foot] = false;
	bRecoverySwinging[Foot] = true;
	SwingStartContact[Foot] = Start;
	RecoverySwingElapsed[Foot] = 0.f;
	// A higher urgency shortens the swing, so the catching foot arrives sooner.
	RecoverySwingDuration[Foot] = FMath::Max(RecoveryStepSeconds * (1.f - 0.5f * Urgency), KINDA_SMALL_NUMBER);
}

void UAedirGaitComponent::AdvancePhase(float DeltaTime, bool bMoving, bool bTurning, float Speed2D, float StrideLength)
{
	if (bMoving)
	{
		Phase += Speed2D * DeltaTime / StrideLength;
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
