#include "Animation/AedirGaitComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "Movement/AedirCharacterMovementComponent.h"
#include "Physics/AedirBalanceComponent.h"

namespace
{
	constexpr float AedirRestPhaseMin = 0.4f;
	constexpr float AedirRestPhaseMax = 0.5f;
	constexpr float AedirRestCatchUpCyclesPerSecond = 2.f;
	constexpr float AedirMinStrideLength = 10.f;
	constexpr float AedirStepHeightClearance = 4.f;
	constexpr float AedirRunBlendInterpSpeed = 6.f;
	constexpr float AedirMovingBounceFrequency = 2.f;

	float WrapUnit(float Value)
	{
		return Value - FMath::Floor(Value);
	}
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
	const bool bStepping = bMoving || bTurning;

	const FVector LocalDirection = ActorTransform.InverseTransformVectorNoScale(Velocity).GetSafeNormal2D();
	const float BackAlpha = FMath::Clamp(-LocalDirection.X, 0.f, 1.f);
	const float SideAlpha = FMath::Clamp(FMath::Abs(LocalDirection.Y), 0.f, 1.f);
	const float DirectionalStrideScale = FMath::Lerp(1.f, BackStrideScale, BackAlpha) * FMath::Lerp(1.f, SideStrideScale, SideAlpha);

	const float Athletism = CreatureOwner->Stats ? CreatureOwner->Stats->Athletism : 0.f;
	const float AthleticsScale = 1.f + Athletism / FMath::Max(AthleticsStrideDivisor, KINDA_SMALL_NUMBER);
	const float StrideLength = FMath::Max(
		FMath::Lerp(StrideWalkCm, StrideRunCm, SpeedAlpha) * ActorScale * DirectionalStrideScale * AthleticsScale * StairFactor,
		AedirMinStrideLength);
	const float SwingFraction = FMath::Lerp(SwingFractionWalk, SwingFractionRun, SpeedAlpha);
	const FVector MoveDirection = bMoving ? Velocity.GetSafeNormal2D() : ActorForward2D;

	AdvancePhase(DeltaTime, bMoving, bTurning, Speed2D, StrideLength);

	const float FootFraction[2] = { WrapUnit(Phase), WrapUnit(Phase + 0.5f) };

	FVector LandingLocation[2];
	FVector LandingNormal[2];
	FVector AheadLocation[2];

	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const float Side = Foot == 0 ? -1.f : 1.f;
		const FVector HomeXY = ActorLocation + ActorRight2D * (Side * HipHalfWidthCm * ActorScale);
		const FVector LandingXY = HomeXY + (bMoving ? MoveDirection * (StrideLength * 0.5f) : FVector::ZeroVector);

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

		if (!bSwinging[Foot])
		{
			if (bStepping && bWrapped && bInSwingWindow)
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

		AnkleWorld[Foot] = FootPosition + FVector::UpVector * (AnkleHeightCm * ActorScale);
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

	const FRotator BalanceLean = BalanceComponent ? BalanceComponent->GetPelvisLean() : FRotator::ZeroRotator;
	const float PelvisYaw = FMath::Sin(2.f * UE_PI * Phase) * PelvisYawSwingDegrees * SpeedAlpha;
	GaitState.PelvisRotation = FRotator(BalanceLean.Pitch, PelvisYaw, BalanceLean.Roll);

	const float LookYaw = MovementComponent->GetYawDeltaToControl();
	const float LookPitch = FMath::Clamp(FRotator::NormalizeAxis(CreatureOwner->GetControlRotation().Pitch), -MaxLookPitchDegrees, MaxLookPitchDegrees);

	GaitState.SpineLeanPitch = -MaxRunForwardLeanDegrees * SpeedAlpha + BalanceLean.Pitch + LookPitch * SpinePitchShare;
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
	const float ArmAmplitude = FMath::Lerp(MaxArmSwingWalkDegrees, MaxArmSwingRunDegrees, SpeedAlpha) * DirectionalArmScale * ArmBlend;
	GaitState.ArmSwingLeft = FMath::Sin(2.f * UE_PI * (Phase + 0.5f)) * ArmAmplitude;
	GaitState.ArmSwingRight = FMath::Sin(2.f * UE_PI * Phase) * ArmAmplitude;
	GaitState.ElbowBend = FMath::Lerp(ElbowBendWalkDegrees, ElbowBendRunDegrees, SpeedAlpha);
	GaitState.KneeBend = FMath::Max(Lift[0], Lift[1]) * KneeBendMaxDegrees;

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
