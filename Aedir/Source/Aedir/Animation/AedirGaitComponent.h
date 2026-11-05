#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirGaitComponent.generated.h"

class AAedirCreature;
class UAedirBalanceComponent;
class UAedirCharacterMovementComponent;
class USkeletalMeshComponent;

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
	FName PelvisBoneName = FName(TEXT("hip"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	TArray<FName> SpineBoneNames = { FName(TEXT("l3")), FName(TEXT("t12")), FName(TEXT("t8")), FName(TEXT("t4")), FName(TEXT("t1")) };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName NeckBoneName = FName(TEXT("neck"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName HeadBoneName = FName(TEXT("head"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftClavicleBoneName = FName(TEXT("lClavicle"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftHumerusBoneName = FName(TEXT("lHumerus"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftRadiusBoneName = FName(TEXT("lRadius"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftHandBoneName = FName(TEXT("lHand"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftFemurBoneName = FName(TEXT("lFemur"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftTibiaBoneName = FName(TEXT("lTibia"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName LeftFootBoneName = FName(TEXT("lFoot"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightClavicleBoneName = FName(TEXT("rClavicle"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightHumerusBoneName = FName(TEXT("rHumerus"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightRadiusBoneName = FName(TEXT("rRadius"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightHandBoneName = FName(TEXT("rHand"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightFemurBoneName = FName(TEXT("rFemur"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightTibiaBoneName = FName(TEXT("rTibia"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Bones")
	FName RightFootBoneName = FName(TEXT("rFoot"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrideWalkCm = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StrideRunCm = 230.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SideStrideScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float BackStrideScale = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float AthleticsStrideDivisor = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MovingSpeedThreshold = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float TurnStepCyclesPerSecond = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SwingFractionWalk = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float SwingFractionRun = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float BaseStepHeightCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float StepHeightSpeedBonusCm = 6.f;

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
	float HipBounceCm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipDynamicMinCm = -25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipDynamicMaxCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float HipOffsetInterpSpeed = 10.f;

	// Pelvis drop in cm that counts as a full reach-down; scales the knee bend and torso lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ReachLowerMaxCm = 40.f;

	// Forward torso lean in degrees at a full reach-down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ReachTorsoLeanMaxDegrees = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxArmSwingWalkDegrees = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxArmSwingRunDegrees = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmSideScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmBackScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ArmBlendInterpSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ElbowBendWalkDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float ElbowBendRunDegrees = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float KneeBendMaxDegrees = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float MaxRunForwardLeanDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gait")
	float PelvisYawSwingDegrees = 4.f;

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

private:
	bool SampleGround(const FVector& WorldXY, float BottomZ, FVector& OutLocation, FVector& OutNormal) const;
	void ResetForRagdoll();
	void AdvancePhase(float DeltaTime, bool bMoving, bool bTurning, float Speed2D, float StrideLength);
	FRotator ComputeFootTilt(const FVector& GroundNormal, const FVector& ActorUp, const FRotator& CurrentTilt, float DeltaTime) const;

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

	float Phase = 0.45f;
	float StairFactor = 1.f;
	float RunBlend = 0.f;
	float ArmBlend = 0.f;
	float DynamicHipOffset = 0.f;
	float SmoothedSpineTwist = 0.f;
	bool bFeetInitialized = false;
};
