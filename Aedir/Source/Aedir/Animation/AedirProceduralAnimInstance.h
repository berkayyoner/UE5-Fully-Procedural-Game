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
	void ApplyLeg(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh) const;

	FAedirGaitState GaitData;
	FAedirGaitBoneNames BoneNameSet;
	FTransform MeshToWorld = FTransform::Identity;
	FQuat ActorToMesh = FQuat::Identity;
};

UCLASS()
class AEDIR_API UAedirProceduralAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UAedirGaitComponent* GetGaitComponent() const;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	void RefreshGaitComponent();

	UPROPERTY(Transient)
	TObjectPtr<UAedirGaitComponent> GaitComponent;
};
