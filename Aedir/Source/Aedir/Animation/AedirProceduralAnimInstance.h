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

private:
	FQuat ActorRotationToMesh(const FQuat& ActorLocalRotation) const;
	FQuat WorldRotationToMesh(const FQuat& WorldRotation) const;
	void ApplyPelvis(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplySpine(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplyNeckAndHead(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const;
	void ApplyArm(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& ClavicleName, const FName& HumerusName, const FName& RadiusName, const FName& HandName, float ArmSwingDegrees, float ElbowDegrees) const;
	void ApplyArmReach(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& HandWorld, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh) const;
	void ApplyFingers(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const TArray<FName>& FingerBoneNames, float FistAlpha, float CurlSign) const;
	void ApplyLeg(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh) const;

	FAedirGaitState GaitData;
	FAedirGaitBoneNames BoneNameSet;
	FTransform MeshToWorld = FTransform::Identity;
	FQuat ActorToMesh = FQuat::Identity;
	TArray<FName> LeftFingerBoneNames;
	TArray<FName> RightFingerBoneNames;
	FVector FistCurlAxis = FVector::UpVector;
	float FistCurlSignLeft = 1.f;
	float FistCurlSignRight = -1.f;
};

UCLASS()
class AEDIR_API UAedirProceduralAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UAedirGaitComponent* GetGaitComponent() const;

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
	float FistCurlSignLeft = 1.f;

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
