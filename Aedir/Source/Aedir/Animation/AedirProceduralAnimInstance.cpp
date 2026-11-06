#include "Animation/AedirProceduralAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "GameFramework/Actor.h"
#include "TwoBoneIK.h"

namespace
{
	constexpr float AedirKneePoleDistance = 100.f;
	constexpr float AedirLegMinReachShare = 0.38f;
	constexpr float AedirLegMaxReachShare = 0.995f;
	constexpr float AedirWristFollowShare = 0.25f;
	constexpr float AedirClavicleFollowShare = 0.25f;
	constexpr float AedirReachAlphaThreshold = 0.001f;
	constexpr float AedirElbowOutwardDistance = 60.f;
	constexpr float AedirElbowDropDistance = 40.f;
	constexpr float AedirElbowBackDistance = 90.f;
	constexpr float AedirFistAlphaThreshold = 0.001f;
	constexpr int32 AedirFingerCount = 5;
	constexpr int32 AedirFingerJointCount = 3;
	// The chest pitch grows by this factor times the exertion, so a fully exerted breath is three times as deep.
	constexpr float AedirBreathExertionScale = 2.f;
	// The upper spine, spine_03 to spine_05 in the default rig, carries the chest breath and the counter roll.
	constexpr int32 AedirUpperSpineCount = 3;
	// Curl in degrees at a full fist, per finger (thumb first) and joint from the base.
	constexpr float AedirFistCurlDegrees[AedirFingerCount][AedirFingerJointCount] = {
		{ 35.f, 45.f, 30.f },
		{ 70.f, 90.f, 60.f },
		{ 70.f, 90.f, 60.f },
		{ 70.f, 90.f, 60.f },
		{ 70.f, 90.f, 60.f }
	};

	FCompactPoseBoneIndex ResolveCompactBone(const FBoneContainer& Bones, const FName& BoneName)
	{
		if (BoneName.IsNone())
		{
			return FCompactPoseBoneIndex(INDEX_NONE);
		}

		const int32 MeshBoneIndex = Bones.GetPoseBoneIndexForBoneName(BoneName);
		if (MeshBoneIndex == INDEX_NONE)
		{
			return FCompactPoseBoneIndex(INDEX_NONE);
		}

		return Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBoneIndex));
	}

	FQuat SafeDeltaRotation(const FVector& From, const FVector& To)
	{
		const FVector FromDirection = From.GetSafeNormal();
		const FVector ToDirection = To.GetSafeNormal();
		if (FromDirection.IsNearlyZero() || ToDirection.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		return FQuat::FindBetweenNormals(FromDirection, ToDirection);
	}

	void RotateBone(FCSPose<FCompactPose>& CSPose, const FCompactPoseBoneIndex& Bone, const FQuat& MeshSpaceDelta)
	{
		FTransform BoneTransform = CSPose.GetComponentSpaceTransform(Bone);
		BoneTransform.SetRotation(MeshSpaceDelta * BoneTransform.GetRotation());
		CSPose.SetComponentSpaceTransform(Bone, BoneTransform);
	}

	FTransform BlendBoneTransform(const FTransform& From, const FTransform& To, float Alpha)
	{
		FTransform Result = From;
		Result.SetTranslation(FMath::Lerp(From.GetLocation(), To.GetLocation(), Alpha));
		Result.SetRotation(FQuat::Slerp(From.GetRotation(), To.GetRotation(), Alpha));
		return Result;
	}
}

FAedirProceduralAnimProxy::FAedirProceduralAnimProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void FAedirProceduralAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const UAedirProceduralAnimInstance* Instance = Cast<UAedirProceduralAnimInstance>(InAnimInstance);
	const UAedirGaitComponent* Gait = Instance ? Instance->GetGaitComponent() : nullptr;
	const USkeletalMeshComponent* Mesh = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	const AActor* Owner = InAnimInstance ? InAnimInstance->GetOwningActor() : nullptr;

	if (!Gait || !Mesh || !Owner)
	{
		GaitData = FAedirGaitState();
		return;
	}

	GaitData = Gait->GetGaitState();
	Gait->FillBoneNames(BoneNameSet);

	LeftFingerBoneNames = Instance->LeftFingerBoneNames;
	RightFingerBoneNames = Instance->RightFingerBoneNames;
	FistCurlAxis = Instance->FistCurlAxis;
	FistCurlSignLeft = Instance->FistCurlSignLeft;
	FistCurlSignRight = Instance->FistCurlSignRight;

	ArmRestAdductionLeft = Gait->ArmRestAdductionDegrees * Gait->ArmRestSignLeft;
	ArmRestAdductionRight = Gait->ArmRestAdductionDegrees * Gait->ArmRestSignRight;
	ArmRestForwardDegrees = Gait->ArmRestForwardDegrees;
	ElbowRestBendDegrees = Gait->ElbowRestBendDegrees;
	BreathChestDegrees = Gait->BreathChestDegrees;
	BreathClavicleDegrees = Gait->BreathClavicleDegrees;
	BreathNeckCounterDegrees = Gait->BreathNeckCounterDegrees;

	MeshToWorld = Mesh->GetComponentTransform();
	const FQuat ActorToWorld = Owner->GetActorQuat();
	// Maps actor-local directions into mesh space: MeshToWorld^-1 * ActorToWorld.
	ActorToMesh = MeshToWorld.GetRotation().Inverse() * ActorToWorld;
}

bool FAedirProceduralAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	if (!GaitData.bProceduralActive)
	{
		return true;
	}

	const FBoneContainer& Bones = GetRequiredBones();

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	// Order matters: every bone is modified only after its parents, and children are read only after their parents are final.
	ApplyPelvis(CSPose, Bones);
	ApplySpine(CSPose, Bones);
	ApplyNeckAndHead(CSPose, Bones);
	// Positive roll lowers the right side, so the left clavicle lifts with a positive roll and the right with a negative one.
	const float ClavicleLift = BreathClavicleDegrees * GetBreathWave();
	ApplyArm(CSPose, Bones, BoneNameSet.LeftClavicle, BoneNameSet.LeftHumerus, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, GaitData.ArmSwingLeft, GaitData.ElbowBend, GaitData.LeftArmReachAlpha, ArmRestAdductionLeft, ClavicleLift);
	ApplyArm(CSPose, Bones, BoneNameSet.RightClavicle, BoneNameSet.RightHumerus, BoneNameSet.RightRadius, BoneNameSet.RightHand, GaitData.ArmSwingRight, GaitData.ElbowBend, GaitData.RightArmReachAlpha, ArmRestAdductionRight, -ClavicleLift);

	// Left is the actor's -Y side (matches the gait foot placement), right is +Y.
	const FVector LeftOutwardMesh = ActorToMesh.RotateVector(-FVector::RightVector);
	const FVector RightOutwardMesh = ActorToMesh.RotateVector(FVector::RightVector);
	const FVector DownMesh = ActorToMesh.RotateVector(-FVector::UpVector);
	const FVector ArmForwardMesh = ActorToMesh.RotateVector(FVector::ForwardVector);
	ApplyArmReach(CSPose, Bones, BoneNameSet.LeftHumerus, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, GaitData.LeftReachTargetWorld, GaitData.LeftArmReachAlpha, LeftOutwardMesh, DownMesh, ArmForwardMesh);
	ApplyArmReach(CSPose, Bones, BoneNameSet.RightHumerus, BoneNameSet.RightRadius, BoneNameSet.RightHand, GaitData.RightReachTargetWorld, GaitData.RightArmReachAlpha, RightOutwardMesh, DownMesh, ArmForwardMesh);

	// Fingers are children of the final hand pose, so they are curled after the arm IK.
	ApplyFingers(CSPose, Bones, LeftFingerBoneNames, GaitData.FistAlphaLeft, FistCurlSignLeft);
	ApplyFingers(CSPose, Bones, RightFingerBoneNames, GaitData.FistAlphaRight, FistCurlSignRight);

	const FVector ForwardMesh = ActorToMesh.RotateVector(FVector::ForwardVector);
	ApplyLeg(CSPose, Bones, BoneNameSet.LeftFemur, BoneNameSet.LeftTibia, BoneNameSet.LeftFoot, GaitData.LeftFootWorld, GaitData.LeftFootRot, ForwardMesh);
	ApplyLeg(CSPose, Bones, BoneNameSet.RightFemur, BoneNameSet.RightTibia, BoneNameSet.RightFoot, GaitData.RightFootWorld, GaitData.RightFootRot, ForwardMesh);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, Output.Pose);
	Output.Pose.NormalizeRotations();

	return true;
}

FQuat FAedirProceduralAnimProxy::ActorRotationToMesh(const FQuat& ActorLocalRotation) const
{
	return ActorToMesh * ActorLocalRotation * ActorToMesh.Inverse();
}

FQuat FAedirProceduralAnimProxy::WorldRotationToMesh(const FQuat& WorldRotation) const
{
	const FQuat MeshRotation = MeshToWorld.GetRotation();
	return MeshRotation.Inverse() * WorldRotation * MeshRotation;
}

void FAedirProceduralAnimProxy::ApplyPelvis(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const
{
	const FCompactPoseBoneIndex Pelvis = ResolveCompactBone(Bones, BoneNameSet.Pelvis);
	if (!Pelvis.IsValid())
	{
		return;
	}

	const FVector UpMesh = ActorToMesh.RotateVector(FVector::UpVector);
	const FVector RightMesh = ActorToMesh.RotateVector(FVector::RightVector);
	FTransform PelvisTransform = CSPose.GetComponentSpaceTransform(Pelvis);
	PelvisTransform.AddToTranslation(UpMesh * GaitData.HipOffsetZ + RightMesh * GaitData.HipLateralOffset);

	FRotator PelvisRotation = GaitData.PelvisRotation;
	PelvisRotation.Roll += GaitData.PelvisRollDegrees;
	PelvisTransform.SetRotation(ActorRotationToMesh(PelvisRotation.Quaternion()) * PelvisTransform.GetRotation());
	CSPose.SetComponentSpaceTransform(Pelvis, PelvisTransform);
}

void FAedirProceduralAnimProxy::ApplySpine(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const
{
	TArray<FCompactPoseBoneIndex> SpineBones;
	SpineBones.Reserve(BoneNameSet.Spine.Num());
	for (const FName& SpineName : BoneNameSet.Spine)
	{
		const FCompactPoseBoneIndex Bone = ResolveCompactBone(Bones, SpineName);
		if (Bone.IsValid())
		{
			SpineBones.Add(Bone);
		}
	}

	if (SpineBones.IsEmpty())
	{
		return;
	}

	// Lean, twist and inertia roll are shared evenly over the chain, low to high, so each bone inherits the accumulated rotation of its parents.
	const int32 BoneTotal = SpineBones.Num();
	const float BoneCount = static_cast<float>(BoneTotal);
	const int32 UpperFirst = FMath::Max(BoneTotal - AedirUpperSpineCount, 0);
	const float UpperCount = static_cast<float>(BoneTotal - UpperFirst);

	// The chest breath and the counter roll act on the upper spine only.
	const float Breath = GetBreathWave();
	const float ChestPitch = Breath * BreathChestDegrees * (1.f + AedirBreathExertionScale * GaitData.BreathExertion);

	for (int32 BoneIndex = 0; BoneIndex < BoneTotal; ++BoneIndex)
	{
		float PitchDegrees = GaitData.SpineLeanPitch / BoneCount;
		float RollDegrees = GaitData.SpineLeanRoll / BoneCount;
		const float YawDegrees = GaitData.SpineTwistYaw / BoneCount;
		if (BoneIndex >= UpperFirst)
		{
			PitchDegrees += ChestPitch;
		}

		// The pelvis roll is cancelled at the lowest spine bone, so the motion stays in the hips and the upper body does not sway.
		if (BoneIndex == 0)
		{
			RollDegrees += GaitData.SpineCounterRollDegrees;
		}

		RotateBone(CSPose, SpineBones[BoneIndex], ActorRotationToMesh(FRotator(PitchDegrees, YawDegrees, RollDegrees).Quaternion()));
	}
}

void FAedirProceduralAnimProxy::ApplyNeckAndHead(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones) const
{
	const FCompactPoseBoneIndex Neck = ResolveCompactBone(Bones, BoneNameSet.Neck);
	if (Neck.IsValid())
	{
		// The neck leans against the chest breath so the head stays steady.
		FRotator NeckRotation = GaitData.NeckLookRotation;
		NeckRotation.Pitch -= BreathNeckCounterDegrees * GetBreathWave();
		RotateBone(CSPose, Neck, ActorRotationToMesh(NeckRotation.Quaternion()));
	}

	const FCompactPoseBoneIndex Head = ResolveCompactBone(Bones, BoneNameSet.Head);
	if (Head.IsValid())
	{
		FRotator HeadRotation = GaitData.HeadLookRotation;
		HeadRotation.Roll += GaitData.HeadStabilizeRollDegrees;
		RotateBone(CSPose, Head, ActorRotationToMesh(HeadRotation.Quaternion()));
	}
}

float FAedirProceduralAnimProxy::GetBreathWave() const
{
	return FMath::Sin(2.f * UE_PI * GaitData.BreathPhase) * GaitData.MotionAlpha;
}

void FAedirProceduralAnimProxy::ApplyArm(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& ClavicleName, const FName& HumerusName, const FName& RadiusName, const FName& HandName, float ArmSwingDegrees, float ElbowDegrees, float ReachAlpha, float RestRollDegrees, float ClavicleRollDegrees) const
{
	// The rest pose fades out as reach, swing or guard targets take the arm, and is off while dead.
	const float RestAlpha = FMath::Clamp(1.f - ReachAlpha, 0.f, 1.f) * GaitData.MotionAlpha;
	const float RestElbowDegrees = ElbowDegrees + ElbowRestBendDegrees * RestAlpha;

	// Positive pitch swings the hanging arm forward, so the same sign drives shoulder, elbow and wrist.
	const FCompactPoseBoneIndex Clavicle = ResolveCompactBone(Bones, ClavicleName);
	if (Clavicle.IsValid())
	{
		RotateBone(CSPose, Clavicle, ActorRotationToMesh(FRotator(ArmSwingDegrees * AedirClavicleFollowShare, 0.f, ClavicleRollDegrees).Quaternion()));
	}

	const FCompactPoseBoneIndex Humerus = ResolveCompactBone(Bones, HumerusName);
	if (Humerus.IsValid())
	{
		RotateBone(CSPose, Humerus, ActorRotationToMesh(FRotator(ArmSwingDegrees + ArmRestForwardDegrees * RestAlpha, 0.f, RestRollDegrees * RestAlpha * (1.f - 3.f * GaitData.ArmBalanceSpread)).Quaternion()));
	}

	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	if (Radius.IsValid())
	{
		RotateBone(CSPose, Radius, ActorRotationToMesh(FRotator(RestElbowDegrees, 0.f, 0.f).Quaternion()));
	}

	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	if (Hand.IsValid())
	{
		RotateBone(CSPose, Hand, ActorRotationToMesh(FRotator(RestElbowDegrees * AedirWristFollowShare, 0.f, 0.f).Quaternion()));
	}
}

void FAedirProceduralAnimProxy::ApplyArmReach(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& HandWorld, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const
{
	if (ReachAlpha <= AedirReachAlphaThreshold)
	{
		return;
	}

	const FCompactPoseBoneIndex Humerus = ResolveCompactBone(Bones, HumerusName);
	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	if (!Humerus.IsValid() || !Radius.IsValid() || !Hand.IsValid())
	{
		return;
	}

	const FTransform HumerusTransform = CSPose.GetComponentSpaceTransform(Humerus);
	const FTransform RadiusTransform = CSPose.GetComponentSpaceTransform(Radius);
	const FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);

	const FVector RootPosition = HumerusTransform.GetLocation();
	const FVector JointPosition = RadiusTransform.GetLocation();
	const FVector EndPosition = HandTransform.GetLocation();

	const FVector Effector = MeshToWorld.InverseTransformPosition(HandWorld);
	// Elbow pole outward from the body and slightly down so the elbow stays out of the torso.
	// A hand pulled out to the side takes the elbow back with it, like a hook or a drawn bow.
	const float Sideways = FMath::Clamp(static_cast<float>(FVector::DotProduct((Effector - RootPosition).GetSafeNormal(), OutwardMesh)), 0.f, 1.f);
	const FVector JointTarget = Effector + OutwardMesh * AedirElbowOutwardDistance + DownMesh * AedirElbowDropDistance - ForwardMesh * (AedirElbowBackDistance * Sideways);

	FVector SolvedJointPosition;
	FVector SolvedEndPosition;
	AnimationCore::SolveTwoBoneIK(RootPosition, JointPosition, EndPosition, JointTarget, Effector, SolvedJointPosition, SolvedEndPosition, false, 1.0, 1.0);

	const FQuat HumerusDelta = SafeDeltaRotation(JointPosition - RootPosition, SolvedJointPosition - RootPosition);
	const FQuat RadiusDelta = SafeDeltaRotation(HumerusDelta.RotateVector(EndPosition - JointPosition), SolvedEndPosition - SolvedJointPosition);

	FTransform SolvedHumerus = HumerusTransform;
	SolvedHumerus.SetRotation(HumerusDelta * HumerusTransform.GetRotation());

	FTransform SolvedRadius = RadiusTransform;
	SolvedRadius.SetTranslation(SolvedJointPosition);
	SolvedRadius.SetRotation(RadiusDelta * HumerusDelta * RadiusTransform.GetRotation());

	FTransform SolvedHand = HandTransform;
	SolvedHand.SetTranslation(SolvedEndPosition);
	SolvedHand.SetRotation(RadiusDelta * HumerusDelta * HandTransform.GetRotation());

	const float Alpha = FMath::Clamp(ReachAlpha, 0.f, 1.f);
	CSPose.SetComponentSpaceTransform(Humerus, BlendBoneTransform(HumerusTransform, SolvedHumerus, Alpha));
	CSPose.SetComponentSpaceTransform(Radius, BlendBoneTransform(RadiusTransform, SolvedRadius, Alpha));
	CSPose.SetComponentSpaceTransform(Hand, BlendBoneTransform(HandTransform, SolvedHand, Alpha));
}

void FAedirProceduralAnimProxy::ApplyFingers(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const TArray<FName>& FingerBoneNames, float FistAlpha, float CurlSign) const
{
	const float Alpha = FMath::Clamp(FistAlpha, 0.f, 1.f);
	if (Alpha <= AedirFistAlphaThreshold || FingerBoneNames.IsEmpty() || FistCurlAxis.IsNearlyZero())
	{
		return;
	}

	const FVector CurlAxis = FistCurlAxis.GetSafeNormal();
	// Bones are listed base to tip per finger, so each joint is curled after its parent joint.
	for (int32 BoneIndex = 0; BoneIndex < FingerBoneNames.Num(); ++BoneIndex)
	{
		const FCompactPoseBoneIndex Bone = ResolveCompactBone(Bones, FingerBoneNames[BoneIndex]);
		if (!Bone.IsValid())
		{
			continue;
		}

		const int32 Finger = FMath::Min(BoneIndex / AedirFingerJointCount, AedirFingerCount - 1);
		const int32 Joint = BoneIndex % AedirFingerJointCount;
		const FQuat LocalCurl(CurlAxis, FMath::DegreesToRadians(AedirFistCurlDegrees[Finger][Joint] * Alpha * CurlSign));

		// Post-multiplying applies the curl in the bone's own frame on top of its component space rotation.
		FTransform BoneTransform = CSPose.GetComponentSpaceTransform(Bone);
		BoneTransform.SetRotation(BoneTransform.GetRotation() * LocalCurl);
		CSPose.SetComponentSpaceTransform(Bone, BoneTransform);
	}
}

void FAedirProceduralAnimProxy::ApplyLeg(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh) const
{
	const FCompactPoseBoneIndex Femur = ResolveCompactBone(Bones, FemurName);
	const FCompactPoseBoneIndex Tibia = ResolveCompactBone(Bones, TibiaName);
	const FCompactPoseBoneIndex Foot = ResolveCompactBone(Bones, FootName);
	if (!Femur.IsValid() || !Tibia.IsValid() || !Foot.IsValid())
	{
		return;
	}

	const FTransform FemurTransform = CSPose.GetComponentSpaceTransform(Femur);
	const FTransform TibiaTransform = CSPose.GetComponentSpaceTransform(Tibia);
	const FTransform FootTransform = CSPose.GetComponentSpaceTransform(Foot);

	const FVector RootPosition = FemurTransform.GetLocation();
	const FVector JointPosition = TibiaTransform.GetLocation();
	const FVector EndPosition = FootTransform.GetLocation();

	// The foot target stays inside the reach of the leg: never stretched past straight, never folded tighter than the knee can bend.
	const float LegLength = static_cast<float>((JointPosition - RootPosition).Size() + (EndPosition - JointPosition).Size());
	FVector Effector = MeshToWorld.InverseTransformPosition(FootWorld);
	const FVector ToEffector = Effector - RootPosition;
	const float EffectorDistance = static_cast<float>(ToEffector.Size());
	if (EffectorDistance > KINDA_SMALL_NUMBER)
	{
		const float ClampedDistance = FMath::Clamp(EffectorDistance, LegLength * AedirLegMinReachShare, LegLength * AedirLegMaxReachShare);
		Effector = RootPosition + ToEffector * (ClampedDistance / EffectorDistance);
	}

	// Knee pole in front of the creature so the knee bends forward.
	const FVector JointTarget = Effector + ForwardMesh * AedirKneePoleDistance;

	FVector SolvedJointPosition;
	FVector SolvedEndPosition;
	AnimationCore::SolveTwoBoneIK(RootPosition, JointPosition, EndPosition, JointTarget, Effector, SolvedJointPosition, SolvedEndPosition, false, 1.0, 1.0);

	const FQuat FemurDelta = SafeDeltaRotation(JointPosition - RootPosition, SolvedJointPosition - RootPosition);
	const FQuat TibiaDelta = SafeDeltaRotation(FemurDelta.RotateVector(EndPosition - JointPosition), SolvedEndPosition - SolvedJointPosition);
	const FQuat FootTiltMesh = WorldRotationToMesh(FootTiltWorld.Quaternion());

	FTransform NewFemur = FemurTransform;
	NewFemur.SetRotation(FemurDelta * FemurTransform.GetRotation());
	CSPose.SetComponentSpaceTransform(Femur, NewFemur);

	FTransform NewTibia = TibiaTransform;
	NewTibia.SetTranslation(SolvedJointPosition);
	NewTibia.SetRotation(TibiaDelta * FemurDelta * TibiaTransform.GetRotation());
	CSPose.SetComponentSpaceTransform(Tibia, NewTibia);

	FTransform NewFoot = FootTransform;
	NewFoot.SetTranslation(SolvedEndPosition);
	NewFoot.SetRotation(FootTiltMesh * TibiaDelta * FemurDelta * FootTransform.GetRotation());
	CSPose.SetComponentSpaceTransform(Foot, NewFoot);
}

UAedirGaitComponent* UAedirProceduralAnimInstance::GetGaitComponent() const
{
	return GaitComponent;
}

void UAedirProceduralAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	RefreshGaitComponent();
}

void UAedirProceduralAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!GaitComponent)
	{
		RefreshGaitComponent();
	}
}

FAnimInstanceProxy* UAedirProceduralAnimInstance::CreateAnimInstanceProxy()
{
	return new FAedirProceduralAnimProxy(this);
}

void UAedirProceduralAnimInstance::RefreshGaitComponent()
{
	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwningActor());
	GaitComponent = Creature ? Creature->Gait.Get() : nullptr;
}
