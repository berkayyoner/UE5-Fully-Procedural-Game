#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirGaitComponent.generated.h"

class AAedirCreature;
class UAedirBalanceComponent;
class UAedirCharacterMovementComponent;
class UAedirCombatComponent;
class UAedirInteractionComponent;
class USkeletalMeshComponent;
enum class EAedirHand : uint8;

USTRUCT(BlueprintType)
struct AEDIR_API FAedirGaitState
{
	GENERATED_BODY()

	// False while ragdolled or before the gait has been initialised; the procedural pose is skipped then.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	bool bProceduralActive = false;

	// Hip offset in cm along the creature up axis, negative = lowered.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float HipOffsetZ = 0.f;

	// Pelvis rotation in actor space: balance lean plus the hip yaw sway of the gait.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FRotator PelvisRotation = FRotator::ZeroRotator;

	// Ankle targets in world space (ground contact plus ankle height).
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FVector LeftFootWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FVector RightFootWorld = FVector::ZeroVector;

	// World-space sole tilt that aligns the foot with the ground normal. Applied on top of the rest foot pose.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FRotator LeftFootRot = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FRotator RightFootRot = FRotator::ZeroRotator;

	// 0 = planted, 1 = at the top of the swing arc.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float LeftFootLift = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float RightFootLift = 0.f;

	// Swing knee flexion in degrees, driven by the lift of the swinging foot.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float KneeBend = 0.f;

	// Arm pitch in degrees, positive swings the arm forward.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float ArmSwingLeft = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float ArmSwingRight = 0.f;

	// 0..1, rises as balance is lost: the arms spread out to help the body catch itself.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float ArmBalanceSpread = 0.f;

	// Elbow flexion in degrees.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float ElbowBend = 0.f;

	// Reach blend per arm, 0 = free swinging, 1 = fully reaching for the target.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float LeftArmReachAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float RightArmReachAlpha = 0.f;

	// Hand reach targets in world space, valid while the matching reach alpha is above zero.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FVector LeftReachTargetWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FVector RightReachTargetWorld = FVector::ZeroVector;

	// Fist blend per hand, 0 = open, 1 = fully curled. Only rises while the hand is empty and guarding.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float FistAlphaLeft = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float FistAlphaRight = 0.f;

	// Fighting stance blend, 0 = relaxed, 1 = fully in stance.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float StanceAlpha = 0.f;

	// Total forward (negative) or backward (positive) torso pitch in degrees, FRotator convention.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float SpineLeanPitch = 0.f;

	// Total torso yaw in degrees, distributed evenly over the spine chain.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float SpineTwistYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FRotator HeadLookRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	FRotator NeckLookRotation = FRotator::ZeroRotator;

	// Gait cycle in [0, 1). Left foot is at Phase, right foot at Phase + 0.5.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float Phase = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	bool bStepping = false;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	bool bTurningInPlace = false;

	// Breathing cycle in [0, 1). Advances faster while exerted.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float BreathPhase = 0.f;

	// Breathing exertion in [0, 1], from low stamina and running.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float BreathExertion = 0.f;

	// 1 while alive, 0 while dead. Scales the idle, breathing, rest and inertia terms.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float MotionAlpha = 1.f;

	// Lateral pelvis offset in cm along the character right axis, from idle weight shift and gait sway.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float HipLateralOffset = 0.f;

	// Forward shift of the pelvis in cm while reaching for something low, so the shoulder gets over the item.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float HipForwardOffset = 0.f;

	// Pelvis roll in degrees from idle sway and the gait weight shift. Added to PelvisRotation.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float PelvisRollDegrees = 0.f;

	// Upper spine roll in degrees that counters the pelvis roll.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float SpineCounterRollDegrees = 0.f;

	// Head roll in degrees that keeps the look direction against the pelvis roll.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float HeadStabilizeRollDegrees = 0.f;

	// Torso roll in degrees from acceleration and turn rate, distributed over the spine chain. Positive leans right.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Gait")
	float SpineLeanRoll = 0.f;
};

// Plain bone name copy consumed by the animation proxy on the worker thread.
struct FAedirGaitBoneNames
{
	FName Pelvis;
	TArray<FName> Spine;
	FName Neck;
	FName Head;
	FName LeftClavicle;
	FName LeftHumerus;
	FName LeftRadius;
	FName LeftHand;
	FName LeftFemur;
	FName LeftTibia;
	FName LeftFoot;
	FName RightClavicle;
	FName RightHumerus;
	FName RightRadius;
	FName RightHand;
	FName RightFemur;
	FName RightTibia;
	FName RightFoot;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirGaitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirGaitComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	const FAedirGaitState& GetGaitState() const;
	void FillBoneNames(FAedirGaitBoneNames& OutNames) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Gait")
	FAedirGaitState GetGaitStateBP() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName PelvisBoneName = FName(TEXT("pelvis"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	TArray<FName> SpineBoneNames = { FName(TEXT("spine_01")), FName(TEXT("spine_02")), FName(TEXT("spine_03")), FName(TEXT("spine_04")), FName(TEXT("spine_05")) };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName NeckBoneName = FName(TEXT("neck_01"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName HeadBoneName = FName(TEXT("head"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftClavicleBoneName = FName(TEXT("clavicle_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftHumerusBoneName = FName(TEXT("upperarm_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftRadiusBoneName = FName(TEXT("lowerarm_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftHandBoneName = FName(TEXT("hand_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftFemurBoneName = FName(TEXT("thigh_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftTibiaBoneName = FName(TEXT("calf_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftFootBoneName = FName(TEXT("foot_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightClavicleBoneName = FName(TEXT("clavicle_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightHumerusBoneName = FName(TEXT("upperarm_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightRadiusBoneName = FName(TEXT("lowerarm_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightHandBoneName = FName(TEXT("hand_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightFemurBoneName = FName(TEXT("thigh_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightTibiaBoneName = FName(TEXT("calf_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightFootBoneName = FName(TEXT("foot_r"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrideWalkCm = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrideRunCm = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SideStrideScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float BackStrideScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float AthleticsStrideDivisor = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MovingSpeedThreshold = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float TurnStepCyclesPerSecond = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SwingFractionWalk = 0.47f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SwingFractionRun = 0.46f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float BaseStepHeightCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StepHeightSpeedBonusCm = 6.f;

	// Extra foot lift at a full run, so the stride arcs high like a real run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float RunStepHeightBonusCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxStepHeightCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StairProbeDistanceCm = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StairHeightForShorteningCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StairStrideMultiplier = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StairFactorInterpSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipHalfWidthCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float AnkleHeightCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float GroundTraceUpCm = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float GroundTraceDownCm = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float FootAlignInterpSpeed = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxFootTiltDegrees = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipRunLoweringCm = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipBounceCm = 6.f;

	// Factor on the pelvis bounce at a full run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipBounceRunScale = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipDynamicMinCm = -40.f;

	// How much of the way to the lower foot the pelvis sinks when the feet stand at different heights.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipLowerFootShare = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipDynamicMaxCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipOffsetInterpSpeed = 10.f;

	// Pelvis drop in cm that counts as a full reach-down; scales the knee bend and torso lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ReachLowerMaxCm = 60.f;

	// Forward torso lean in degrees at a full reach-down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ReachTorsoLeanMaxDegrees = 70.f;

	// How far the pelvis moves forward at a full low reach.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ReachHipForwardCm = 25.f;

	// Speed of the fighting stance blend in and out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceInterpSpeed = 8.f;

	// Pelvis drop in cm at a full fighting stance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceHipLowerCm = 12.f;

	// Extra knee flexion in degrees at a full fighting stance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceKneeBendDegrees = 20.f;

	// Multiplier on the foot home half-width at a full fighting stance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceFootWidthMultiplier = 1.7f;

	// Forward shift in cm of the lead foot's home location at a full fighting stance. The rear foot moves back by half of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceLeadFootForwardCm = 22.f;

	// Walking stride multiplier at a full fighting stance. Running is not affected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceWalkStrideMultiplier = 0.7f;

	// Forward torso lean in degrees at a full fighting stance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	float StanceSpineLeanDegrees = 6.f;

	// Guard hand position in cm relative to the actor: forward, right, up. Scaled by the actor transform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	FVector GuardHandOffsetLeft = FVector(30.f, -16.f, 50.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Stance")
	FVector GuardHandOffsetRight = FVector(30.f, 16.f, 50.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxArmSwingWalkDegrees = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxArmSwingRunDegrees = 42.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmSideScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmBackScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmBlendInterpSpeed = 2.8f;

	// Fraction of a stride cycle the arm swing trails the legs by.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmPhaseLag = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ElbowBendWalkDegrees = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ElbowBendRunDegrees = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float KneeBendMaxDegrees = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxRunForwardLeanDegrees = 14.f;

	// Roll of the torso towards the side the body is travelling to, scaled by speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MoveDirectionLeanRollDegrees = 7.f;

	// Share of the stride the landing point lies ahead of the hip, in a walk and at a full run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StepLandingShareWalk = 0.27f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StepLandingShareRun = 0.26f;

	// The landing point lies at most this many seconds of travel ahead of the hip.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float LandingReachSeconds = 0.28f;

	// While side-stepping the crossing foot is placed this far in front and the leading foot this far behind.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrafeFootStaggerCm = 14.f;

	// Extra foot spacing while side-stepping, as a share of the normal hip width.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrafeFootWidenShare = 0.5f;

	// A planted foot further than this share of the stride from under its hip takes an early step. A normal step drifts about 0.35.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxPlantedDriftStrideFraction = 0.5f;

	// Pause between two stumble steps unless the fall is urgent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Recovery")
	float RecoveryStepCooldownSeconds = 0.5f;

	// How far the pelvis sinks in the wind-up of a jump.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Jump")
	float JumpWindupDipCm = 24.f;

	// Pelvis sink at a 900 cm/s landing for an untrained body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Jump")
	float LandingDipMaxCm = 52.f;

	// Landing spring: share of the landing speed that becomes pelvis sink speed, spring stiffness and damping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Jump")
	float LandingDipKick = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Jump")
	float LandingDipStiffness = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Jump")
	float LandingDipDamping = 15.f;

	// 0 standing, 1 fully crouched. Lags the instant capsule change so the pelvis and camera lower gradually.
	float GetCrouchAlpha() const { return CrouchAlpha; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float PelvisYawSwingDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HeadYawScale = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float NeckYawScale = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SpineTwistScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HeadYawLimitDegrees = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float NeckYawLimitDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SpineTwistLimitDegrees = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HeadPitchShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float NeckPitchShare = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SpinePitchShare = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxLookPitchDegrees = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float LookInterpSpeed = 12.f;

	// Arm rest pose: the arms fall from the A-pose towards the body while no reach, swing or guard target drives them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Rest")
	float ArmRestAdductionDegrees = 24.f;

	// Sign of the adduction roll on each side. Flip a sign if that arm moves away from the body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Rest")
	float ArmRestSignLeft = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Rest")
	float ArmRestSignRight = 1.f;

	// Forward rotation of the upper arm in degrees at a full rest pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Rest")
	float ArmRestForwardDegrees = 6.f;

	// Natural elbow flexion in degrees at a full rest pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Rest")
	float ElbowRestBendDegrees = 2.f;

	// Breathing rate in cycles per second at rest and at full exertion.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Breathing")
	float BreathRateMinHz = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Breathing")
	float BreathRateMaxHz = 0.65f;

	// Chest pitch in degrees per upper spine bone at a full breath, before the exertion scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Breathing")
	float BreathChestDegrees = 0.9f;

	// Clavicle lift in degrees at a full breath.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Breathing")
	float BreathClavicleDegrees = 0.8f;

	// Neck pitch in degrees that counters the chest at a full breath.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Breathing")
	float BreathNeckCounterDegrees = 0.5f;

	// Idle sway of the pelvis while standing still. Two incommensurate sines per axis.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float IdleSwayLateralCm = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float IdleSwayRollDegrees = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float IdleSwayFrequencyA = 0.11f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float IdleSwayFrequencyB = 0.07f;

	// Seconds between weight shifts from one leg to the other.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float WeightShiftIntervalSeconds = 5.f;

	// Lateral pelvis shift in cm towards the loaded leg while standing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float WeightShiftLateralCm = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float IdleBlendInterpSpeed = 4.f;

	// Lateral pelvis shift in cm towards the stance foot while walking or running, at full speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float WalkWeightShiftLateralCm = 3.5f;

	// Pelvis roll in degrees towards the stance foot while walking or running, at full speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float WalkWeightShiftRollDegrees = 6.f;

	// Share of the pelvis roll that the upper spine counter-rotates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float SpineCounterRollShare = 1.f;

	// Share of the pelvis roll that the head counter-rotates to keep its look direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Idle")
	float HeadStabilizeRollShare = 0.f;

	// Inertia: smoothing of the planar acceleration and turn rate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float InertiaSmoothingSpeed = 12.f;

	// Planar acceleration in cm/s^2 that counts as a full lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float InertiaAccelerationScaleCm = 380.f;

	// Forward lean in degrees under full forward acceleration, and backward lean under full braking.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float InertiaLeanPitchDegrees = 9.f;

	// Roll lean in degrees under full lateral acceleration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float InertiaLeanRollDegrees = 5.f;

	// Extra roll lean in degrees at a full turn rate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float YawRateLeanDegrees = 4.f;

	// Turn rate in degrees per second that counts as a full turn lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float YawRateScaleDegreesPerSecond = 180.f;

	// Limit in degrees on each inertia lean component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Inertia")
	float InertiaLeanLimitDegrees = 18.f;

	// Stumble step: the foot on the side of a falling centre of mass swings faster and may reach further.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Recovery")
	float RecoveryStepSeconds = 0.4f;

	// Distance in cm beyond the centre of mass projection, along the fall direction, where the recovery step lands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Recovery")
	float RecoveryStepLandingCm = 12.f;

	// Longest recovery step in multiples of the normal stride half length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Recovery")
	float RecoveryStepMaxStrideMultiplier = 1.6f;

	// Lateral distance in cm of each hand from the protective ground target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Reflex")
	float ProtectiveArmSpreadCm = 18.f;

	// Forward torso pitch in degrees at a full protective reflex.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Reflex")
	float ProtectiveSpineLeanDegrees = 10.f;

	// Backward torso lean in degrees at a full kick pose.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait|Kick")
	float KickSpineLeanDegrees = 6.f;

private:
	bool SampleGround(const FVector& WorldXY, float BottomZ, FVector& OutLocation, FVector& OutNormal) const;
	void ResetForRagdoll();
	void AdvancePhase(float DeltaTime, bool bMoving, bool bTurning, float Speed2D, float StrideLength);
	FRotator ComputeFootTilt(const FVector& GroundNormal, const FVector& ActorUp, const FRotator& CurrentTilt, float DeltaTime) const;
	void BeginRecoverySwing(int32 Foot, const FVector& CatchWorld, float MaxStepCm, float Urgency, float BottomZ);
	void ResolveArmReach(EAedirHand Hand, const UAedirCombatComponent* Combat, const UAedirInteractionComponent* Interaction, const FTransform& ActorTransform, const FVector& ProtectiveTargetWorld, float ProtectiveAlpha, FVector& OutTargetWorld, float& OutAlpha) const;

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> CreatureOwner;

	UPROPERTY(Transient)
	TObjectPtr<UAedirCharacterMovementComponent> MovementComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAedirBalanceComponent> BalanceComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	FAedirGaitState GaitState;

	FVector PlantedContact[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector PlantedNormal[2] = { FVector::UpVector, FVector::UpVector };
	FVector SwingStartContact[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bSwinging[2] = { false, false };
	float PreviousFootFraction[2] = { 0.f, 0.5f };

	// Stumble step state per foot. A recovery swing lands on RecoveryLandingWorld instead of the phase-driven landing.
	float CrouchAlpha = 0.f;
	float RecoveryCooldownRemaining = 0.f;
	float SettleCooldownRemaining = 0.f;
	float AirFallSpeed = 0.f;
	float LandingDipCm = 0.f;
	float LandingDipVelocity = 0.f;
	bool bRecoverySwinging[2] = { false, false };
	float RecoverySwingElapsed[2] = { 0.f, 0.f };
	float RecoverySwingDuration[2] = { 0.f, 0.f };
	FVector RecoveryLandingWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector RecoveryLandingNormal[2] = { FVector::UpVector, FVector::UpVector };

	float Phase = 0.45f;
	float StairFactor = 1.f;
	float RunBlend = 0.f;
	float ArmBlend = 0.f;
	float DynamicHipOffset = 0.f;
	float SmoothedSpineTwist = 0.f;
	bool bFeetInitialized = false;

	// Seconds of motion time that drive the idle sway and the weight shift.
	float MotionClock = 0.f;
	// 0 while walking, turning or in stance, 1 while standing relaxed.
	float IdleBlend = 0.f;
	float SmoothedYawRate = 0.f;
	float PreviousActorYaw = 0.f;
	FVector PreviousPlanarVelocity = FVector::ZeroVector;
	FVector SmoothedAcceleration = FVector::ZeroVector;
	bool bMotionInitialized = false;
};
