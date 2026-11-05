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
		FName(TEXT("lThumb1")), FName(TEXT("lThumb2")), FName(TEXT("lThumb3")),
		FName(TEXT("lIndex1")), FName(TEXT("lIndex2")), FName(TEXT("lIndex3")),
		FName(TEXT("lMid1")), FName(TEXT("lMid2")), FName(TEXT("lMid3")),
		FName(TEXT("lRing1")), FName(TEXT("lRing2")), FName(TEXT("lRing3")),
		FName(TEXT("lPinky1")), FName(TEXT("lPinky2")), FName(TEXT("lPinky3"))
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Gait|Fist")
	TArray<FName> RightFingerBoneNames = {
		FName(TEXT("rThumb1")), FName(TEXT("rThumb2")), FName(TEXT("rThumb3")),
		FName(TEXT("rIndex1")), FName(TEXT("rIndex2")), FName(TEXT("rIndex3")),
		FName(TEXT("rMid1")), FName(TEXT("rMid2")), FName(TEXT("rMid3")),
		FName(TEXT("rRing1")), FName(TEXT("rRing2")), FName(TEXT("rRing3")),
		FName(TEXT("rPinky1")), FName(TEXT("rPinky2")), FName(TEXT("rPinky3"))
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
