#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AedirGaitComponent.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "AedirProceduralAnimInstance.generated.h"

class UAedirGaitComponent;

// Worker-thread side of the procedural pose. Only copied data is read here; no UObject is touched during Evaluate.
struct FAedirProceduralAnimProxy : public FAnimInstanceProxy
{
	FAedirProceduralAnimProxy() = default;
	explicit FAedirProceduralAnimProxy(UAnimInstance* InAnimInstance);

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	// For the dev tools: the way each hand's blade was asked to point (0), what the arm brought it to (1) and what the
	// wrist then let it point (2), in world space, left hand first.
	mutable FVector DevBladeWorld[3][2];
	// For the dev tools: each hand's final pose rotation in world space, left hand first.
	mutable FQuat DevHandWorld[2] = { FQuat::Identity, FQuat::Identity };
	// The angle the blade was wanted at off the forearm, and whether the wrist's reach cut it (-1 too near the line, 1 too far back).
	mutable float DevWristWanted[2] = { 0.f, 0.f };
	mutable int32 DevWristClamped[2] = { 0, 0 };
	// The last pose's upper arm, forearm and hand in world space, left arm first.
	mutable FTransform PoseArmWorld[2][3];

private:
	FQuat ActorRotationToMesh(const FQuat& ActorLocalRotation) const;
	FQuat WorldRotationToMesh(const FQuat& WorldRotation) const;
	void ApplyPelvis(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplySpine(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplyNeckAndHead(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplyBulk(FCompactPose& Pose, const FBoneContainer& Bones) const;
	void ApplyArm(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& ClavicleName, const FName& HumerusName, const FName& RadiusName, const FName& HandName, float ArmSwingDegrees, float ElbowDegrees, float ReachAlpha, float RestRollDegrees, float ClavicleRollDegrees) const;
	// Breathing wave in [-1, 1], zero while dead.
	float GetBreathWave() const;
	void ApplyArmReach(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& HandWorld, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh, const FVector& ElbowHint = FVector::ZeroVector, float ElbowHintAlpha = 0.f) const;
	void ApplyFingers(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const TArray<FName>& FingerBoneNames, float FistAlpha, float CurlSign) const;
	// Turns the forearm, by the elbow and shoulder, so a blade held at one fixed angle in the hand points where it should,
	// and gives back the way the blade then points.
	// The weapon arm placed straight from where the hand and blade are wanted: the forearm runs across the blade as a fist
	// holds a handle, the elbow goes where the upper arm then reaches, and the same aim always gives the same arm.
	// The rebuilt fists: the arm laid from the shoulder to the fist through the elbow where they put it, the hand kept as it
	// sits on the forearm.
	void ApplyFistArm(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& FistWorld, const FVector& ElbowWorld, float Alpha, float WristTurnDegrees) const;
	FVector ApplyWeaponArm(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, int32 Slot, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& HandWorld, const FVector& DirectionWorld, float Share, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const;
	mutable FVector LastWeaponForearm[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector ApplyArmLedBlade(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& DirectionWorld, float Share, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const;
	void ApplyHandAim(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& RadiusName, const FName& HandName, const TCHAR* Suffix, const FVector& DirectionWorld, float Alpha, float FrontShare, float MinBladeDegrees, float HangDegrees) const;

	// Where each hand was turned to in the last pose, so it can only turn so fast from there.
	mutable FQuat LastHandRotation[2] = { FQuat::Identity, FQuat::Identity };
	mutable bool bLastHandValid[2] = { false, false };
	// Each arm joint's turn in the last pose given (collarbone, upper arm, forearm, hand of each side): no joint turns faster
	// than a living one can, so a target that jumps from one frame to the next is followed round smoothly instead.
	mutable FQuat LastArmJoint[2][4];
	mutable bool bLastArmJointValid[2][4] = { { false, false, false, false }, { false, false, false, false } };
	void LimitArmJointSpeed(FCompactPose& Pose, const FBoneContainer& Bones) const;
	// Whether the second hand holds its grip the other way round, left hand first.
	mutable bool bGripReversed[2] = { false, false };
	void ApplyForearmTwist(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& RadiusName, const FName& HandName, const TCHAR* Suffix) const;
	void ApplyObjectHand(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, bool bLeft, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const;
	void ApplyGripHand(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const;
	void ApplyLeg(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh, float FootLift, float KickSoleAlpha) const;

	FAedirGaitState GaitData;
	FAedirGaitBoneNames BoneNameSet;
	FTransform MeshToWorld = FTransform::Identity;
	FQuat ActorToMesh = FQuat::Identity;
	TArray<FName> LeftFingerBoneNames;
	TArray<FName> RightFingerBoneNames;
	FVector FistCurlAxis = FVector::UpVector;
	float FistCurlSignLeft = -1.f;
	float FistCurlSignRight = -1.f;

	// Copied from the gait component in PreUpdate.
	float ArmRestAdductionLeft = 0.f;
	float ArmRestAdductionRight = 0.f;
	float ArmRestForwardDegrees = 0.f;
	float ElbowRestBendDegrees = 0.f;
	float BreathChestDegrees = 0.f;
	// Thickness of trunk and limbs from strength.
	float BulkScale = 1.f;
	float BreathClavicleDegrees = 0.f;
	float BreathNeckCounterDegrees = 0.f;
};

UCLASS()
class AEDIR_API UAedirProceduralAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UAedirGaitComponent* GetGaitComponent() const;
	// Dev tools: see FAedirProceduralAnimProxy::DevBladeWorld.
	FVector DevGetPoseBlade(int32 Stage, int32 HandSlot);
	FQuat DevGetPoseHand(int32 HandSlot);
	// Where the last pose has an arm's part (0 upper arm, 1 forearm, 2 hand), in world space; slot 0 is the left arm.
	FTransform GetPoseArmWorld(int32 HandSlot, int32 Part);
	FString DevWrists();

	// Finger bones per hand, ordered thumb, index, middle, ring, pinky with three joints each from the base.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	TArray<FName> LeftFingerBoneNames = {
		FName(TEXT("thumb_01_l")), FName(TEXT("thumb_02_l")), FName(TEXT("thumb_03_l")),
		FName(TEXT("index_01_l")), FName(TEXT("index_02_l")), FName(TEXT("index_03_l")),
		FName(TEXT("middle_01_l")), FName(TEXT("middle_02_l")), FName(TEXT("middle_03_l")),
		FName(TEXT("ring_01_l")), FName(TEXT("ring_02_l")), FName(TEXT("ring_03_l")),
		FName(TEXT("pinky_01_l")), FName(TEXT("pinky_02_l")), FName(TEXT("pinky_03_l"))
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	TArray<FName> RightFingerBoneNames = {
		FName(TEXT("thumb_01_r")), FName(TEXT("thumb_02_r")), FName(TEXT("thumb_03_r")),
		FName(TEXT("index_01_r")), FName(TEXT("index_02_r")), FName(TEXT("index_03_r")),
		FName(TEXT("middle_01_r")), FName(TEXT("middle_02_r")), FName(TEXT("middle_03_r")),
		FName(TEXT("ring_01_r")), FName(TEXT("ring_02_r")), FName(TEXT("ring_03_r")),
		FName(TEXT("pinky_01_r")), FName(TEXT("pinky_02_r")), FName(TEXT("pinky_03_r"))
	};

	// Local axis the finger joints curl around, in the bone's own space.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	FVector FistCurlAxis = FVector::UpVector;

	// Mirrors the curl direction per hand, since the mirrored rig flips the local axis.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	float FistCurlSignLeft = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	float FistCurlSignRight = -1.f;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	void RefreshGaitComponent();

	UPROPERTY(Transient)
	TObjectPtr<UAedirGaitComponent> GaitComponent;
};
