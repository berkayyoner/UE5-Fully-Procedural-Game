#include "Animation/AedirProceduralAnimInstance.h"

#include "Items/AedirItemActor.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "GameFramework/Actor.h"
#include "TwoBoneIK.h"

namespace
{
	constexpr float AedirObjectPalmShare = 0.55f;
	constexpr float AedirObjectContactDownShare = 0.35f;
	constexpr float AedirObjectPalmClearanceCm = 1.f;
	constexpr float AedirKneePoleDistance = 100.f;
	constexpr float AedirKneePoleDropDistance = 30.f;
	constexpr float AedirLegMinReachShare = 0.3f;
	constexpr float AedirLegMaxReachShare = 0.995f;
	constexpr float AedirHeelLiftStartShare = 0.2f;
	constexpr float AedirHeelLiftRangeShare = 0.2f;
	constexpr float AedirHeelRaiseCm = 7.f;
	constexpr float AedirFootLooseGain = 3.f;
	constexpr float AedirWristFollowShare = 0.25f;
	constexpr float AedirClavicleFollowShare = 0.25f;
	constexpr float AedirReachAlphaThreshold = 0.001f;
	constexpr float AedirElbowOutwardDistance = 60.f;
	constexpr float AedirElbowDropDistance = 40.f;
	constexpr float AedirElbowBackDistance = 90.f;
	constexpr float AedirElbowGuardOutShare = 0.3f;
	constexpr float AedirElbowGuardDropShare = 1.8f;
	constexpr float AedirElbowOutGain = 2.5f;
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

	// The untouched reference pose gives the orientation of a foot that stands flat, which the feet are rebuilt from.
	FCompactPose RefPoseCopy = Output.Pose;
	FCSPose<FCompactPose> RefCSPose;
	RefCSPose.InitPose(RefPoseCopy);

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

	ApplyHandAim(CSPose, RefCSPose, Bones, BoneNameSet.LeftHand, TEXT("_l"), GaitData.LeftWeaponDirWorld, GaitData.LeftWeaponAimAlpha);
	ApplyHandAim(CSPose, RefCSPose, Bones, BoneNameSet.RightHand, TEXT("_r"), GaitData.RightWeaponDirWorld, GaitData.RightWeaponAimAlpha);

	if (GaitData.ObjectAlpha > AedirReachAlphaThreshold)
	{
		ApplyObjectHand(CSPose, RefCSPose, Bones, true, LeftOutwardMesh, DownMesh, ArmForwardMesh);
		ApplyObjectHand(CSPose, RefCSPose, Bones, false, RightOutwardMesh, DownMesh, ArmForwardMesh);
	}

	const bool bGripLeft = GaitData.GripSecondaryHand == static_cast<int32>(EAedirHand::Left);
	ApplyGripHand(CSPose, RefCSPose, Bones, bGripLeft ? LeftOutwardMesh : RightOutwardMesh, DownMesh, ArmForwardMesh);

	// Fingers are children of the final hand pose, so they are curled after the arm IK.
	ApplyFingers(CSPose, Bones, LeftFingerBoneNames, GaitData.FistAlphaLeft, FistCurlSignLeft);
	ApplyFingers(CSPose, Bones, RightFingerBoneNames, GaitData.FistAlphaRight, FistCurlSignRight);

	const FVector ForwardMesh = ActorToMesh.RotateVector(FVector::ForwardVector);
	ApplyLeg(CSPose, RefCSPose, Bones, BoneNameSet.LeftFemur, BoneNameSet.LeftTibia, BoneNameSet.LeftFoot, GaitData.LeftFootWorld, GaitData.LeftFootRot, ForwardMesh, GaitData.LeftFootLift);
	ApplyLeg(CSPose, RefCSPose, Bones, BoneNameSet.RightFemur, BoneNameSet.RightTibia, BoneNameSet.RightFoot, GaitData.RightFootWorld, GaitData.RightFootRot, ForwardMesh, GaitData.RightFootLift);

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
	const FVector ForwardMeshDirection = ActorToMesh.RotateVector(FVector::ForwardVector);
	PelvisTransform.AddToTranslation(UpMesh * GaitData.HipOffsetZ + RightMesh * GaitData.HipLateralOffset + ForwardMeshDirection * GaitData.HipForwardOffset);

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
	// A hand held in front of the body (the guard) keeps its elbow close to the ribs, pointing down; the further out the hand
	// goes, the further out the elbow is allowed.
	const float ElbowOutShare = FMath::Lerp(AedirElbowGuardOutShare, 1.f, FMath::Clamp(Sideways * AedirElbowOutGain, 0.f, 1.f));
	const float ElbowDropShare = FMath::Lerp(AedirElbowGuardDropShare, 1.f, FMath::Clamp(Sideways * AedirElbowOutGain, 0.f, 1.f));
	const FVector JointTarget = Effector + OutwardMesh * (AedirElbowOutwardDistance * ElbowOutShare) + DownMesh * (AedirElbowDropDistance * ElbowDropShare) - ForwardMesh * (AedirElbowBackDistance * Sideways);

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

void FAedirProceduralAnimProxy::ApplyHandAim(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& HandName, const TCHAR* Suffix, const FVector& DirectionWorld, float Alpha) const
{
	if (Alpha <= AedirReachAlphaThreshold)
	{
		return;
	}

	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	const FCompactPoseBoneIndex Index = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("index_01%s"), Suffix)));
	const FCompactPoseBoneIndex Pinky = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("pinky_01%s"), Suffix)));
	if (!Hand.IsValid() || !Index.IsValid() || !Pinky.IsValid())
	{
		return;
	}

	// The blade runs across the knuckles. The wrist turns the shortest way to bring that line onto the wanted direction.
	const FQuat RefHandRotation = RefCSPose.GetComponentSpaceTransform(Hand).GetRotation();
	const FVector RefKnuckles = RefCSPose.GetComponentSpaceTransform(Index).GetLocation() - RefCSPose.GetComponentSpaceTransform(Pinky).GetLocation();
	const FVector KnucklesInHand = RefHandRotation.Inverse().RotateVector(RefKnuckles.GetSafeNormal());

	FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);
	const FVector KnucklesNow = HandTransform.GetRotation().RotateVector(KnucklesInHand);
	const FVector Target = MeshToWorld.InverseTransformVectorNoScale(DirectionWorld).GetSafeNormal();
	const FQuat Turn = FQuat::Slerp(FQuat::Identity, SafeDeltaRotation(KnucklesNow, Target), FMath::Clamp(Alpha, 0.f, 1.f));
	HandTransform.SetRotation((Turn * HandTransform.GetRotation()).GetNormalized());
	CSPose.SetComponentSpaceTransform(Hand, HandTransform);
}

void FAedirProceduralAnimProxy::ApplyObjectHand(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, bool bLeft, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const
{
	const TCHAR* Suffix = bLeft ? TEXT("_l") : TEXT("_r");
	const FName& HandName = bLeft ? BoneNameSet.LeftHand : BoneNameSet.RightHand;
	const FName& HumerusName = bLeft ? BoneNameSet.LeftHumerus : BoneNameSet.RightHumerus;
	const FName& RadiusName = bLeft ? BoneNameSet.LeftRadius : BoneNameSet.RightRadius;
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Middle = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("middle_01%s"), Suffix)));
	const FCompactPoseBoneIndex Index = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("index_01%s"), Suffix)));
	const FCompactPoseBoneIndex Pinky = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("pinky_01%s"), Suffix)));
	const FCompactPoseBoneIndex Thumb = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("thumb_01%s"), Suffix)));
	if (!Hand.IsValid() || !Radius.IsValid() || !Middle.IsValid() || !Index.IsValid() || !Pinky.IsValid() || !Thumb.IsValid())
	{
		return;
	}

	const FTransform RefHand = RefCSPose.GetComponentSpaceTransform(Hand);
	const FQuat RefHandInverse = RefHand.GetRotation().Inverse();
	const FVector RefToMiddle = RefCSPose.GetComponentSpaceTransform(Middle).GetLocation() - RefHand.GetLocation();
	const FVector RefKnuckles = RefCSPose.GetComponentSpaceTransform(Index).GetLocation() - RefCSPose.GetComponentSpaceTransform(Pinky).GetLocation();
	FVector RefPalmNormal = FVector::CrossProduct(RefKnuckles, RefToMiddle).GetSafeNormal();
	if (FVector::DotProduct(RefPalmNormal, RefCSPose.GetComponentSpaceTransform(Thumb).GetLocation() - RefHand.GetLocation()) < 0.0)
	{
		RefPalmNormal = -RefPalmNormal;
	}
	const FVector PalmInHand = RefHandInverse.RotateVector(RefToMiddle * AedirObjectPalmShare);
	const FQuat OpenHandInverse = FRotationMatrix::MakeFromXZ(RefHandInverse.RotateVector(RefToMiddle), RefHandInverse.RotateVector(RefPalmNormal)).ToQuat().Inverse();

	// The palm lies on the side of the object, a little below its middle where it carries the weight, and the fingers run on from the forearm.
	const FVector CenterMesh = MeshToWorld.InverseTransformPosition(GaitData.ObjectCenterWorld);
	const FVector Contact = (OutwardMesh + DownMesh * AedirObjectContactDownShare).GetSafeNormal();
	const FVector ContactMesh = CenterMesh + Contact * (GaitData.ObjectRadiusCm + AedirObjectPalmClearanceCm);

	FQuat HandRotation = CSPose.GetComponentSpaceTransform(Hand).GetRotation();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FVector Fingers = FVector::VectorPlaneProject(ContactMesh - CSPose.GetComponentSpaceTransform(Radius).GetLocation(), Contact);
		if (Fingers.IsNearlyZero(1.f))
		{
			Fingers = ForwardMesh;
		}
		HandRotation = FRotationMatrix::MakeFromXZ(Fingers, -Contact).ToQuat() * OpenHandInverse;
		const FVector Wrist = ContactMesh - HandRotation.RotateVector(PalmInHand);
		ApplyArmReach(CSPose, Bones, HumerusName, RadiusName, HandName, MeshToWorld.TransformPosition(Wrist), GaitData.ObjectAlpha, OutwardMesh, DownMesh, ForwardMesh);
	}

	FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);
	HandTransform.SetRotation(FQuat::Slerp(HandTransform.GetRotation(), HandRotation, FMath::Clamp(GaitData.ObjectAlpha, 0.f, 1.f)).GetNormalized());
	CSPose.SetComponentSpaceTransform(Hand, HandTransform);
}

void FAedirProceduralAnimProxy::ApplyGripHand(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const
{
	if (GaitData.GripSecondaryHand < 0 || GaitData.GripAlpha <= AedirReachAlphaThreshold)
	{
		return;
	}

	const bool bLeft = GaitData.GripSecondaryHand == static_cast<int32>(EAedirHand::Left);
	const TCHAR* Suffix = bLeft ? TEXT("_l") : TEXT("_r");
	const FName& HandName = bLeft ? BoneNameSet.LeftHand : BoneNameSet.RightHand;
	const FName& HumerusName = bLeft ? BoneNameSet.LeftHumerus : BoneNameSet.RightHumerus;
	const FName& RadiusName = bLeft ? BoneNameSet.LeftRadius : BoneNameSet.RightRadius;
	const FCompactPoseBoneIndex PrimaryHand = ResolveCompactBone(Bones, bLeft ? BoneNameSet.RightHand : BoneNameSet.LeftHand);
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Index = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("index_01%s"), Suffix)));
	const FCompactPoseBoneIndex Pinky = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("pinky_01%s"), Suffix)));
	const FCompactPoseBoneIndex Middle = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("middle_01%s"), Suffix)));
	const FCompactPoseBoneIndex Thumb = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("thumb_01%s"), Suffix)));
	if (!PrimaryHand.IsValid() || !Hand.IsValid() || !Radius.IsValid() || !Index.IsValid() || !Pinky.IsValid() || !Middle.IsValid() || !Thumb.IsValid())
	{
		return;
	}

	// The closed hand as the item grip sees it, read from the reference pose in the space of the hand.
	const FTransform RefHand = RefCSPose.GetComponentSpaceTransform(Hand);
	const FVector RefToMiddle = RefCSPose.GetComponentSpaceTransform(Middle).GetLocation() - RefHand.GetLocation();
	const FVector RefKnuckles = (RefCSPose.GetComponentSpaceTransform(Index).GetLocation() - RefCSPose.GetComponentSpaceTransform(Pinky).GetLocation()).GetSafeNormal();
	FVector RefPalmNormal = FVector::CrossProduct(RefKnuckles, RefToMiddle.GetSafeNormal()).GetSafeNormal();
	if (FVector::DotProduct(RefPalmNormal, RefCSPose.GetComponentSpaceTransform(Thumb).GetLocation() - RefHand.GetLocation()) < 0.0)
	{
		RefPalmNormal = -RefPalmNormal;
	}
	const FQuat RefHandInverse = RefHand.GetRotation().Inverse();
	const FVector PalmInHand = RefHandInverse.RotateVector(RefToMiddle * AAedirItemActor::HandGripPalmShare + RefPalmNormal * AAedirItemActor::HandGripPalmDepthCm);
	const FQuat ClosedHandInverse = FRotationMatrix::MakeFromXY(RefHandInverse.RotateVector(RefKnuckles), RefHandInverse.RotateVector(RefToMiddle)).ToQuat().Inverse();

	// The point on the weapon comes from the final pose of the carrying hand, so the two hands cannot drift apart.
	const FTransform PrimaryTransform = CSPose.GetComponentSpaceTransform(PrimaryHand);
	const FVector HandleMesh = PrimaryTransform.TransformPositionNoScale(GaitData.GripPointInPrimaryHand);
	const FVector BladeMesh = PrimaryTransform.GetRotation().RotateVector(GaitData.GripBladeInPrimaryHand).GetSafeNormal();

	// The blade runs across the knuckles, the fingers point away from the elbow, and the palm goes on the handle. The elbow
	// moves with the solve and the wrist with the elbow, so a second pass settles both.
	FQuat HandRotation = CSPose.GetComponentSpaceTransform(Hand).GetRotation();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FVector FromElbow = HandleMesh - CSPose.GetComponentSpaceTransform(Radius).GetLocation();
		FVector Fingers = FVector::VectorPlaneProject(FromElbow, BladeMesh);
		if (Fingers.IsNearlyZero(1.f))
		{
			Fingers = HandRotation.RotateVector(RefHandInverse.RotateVector(RefToMiddle));
		}
		HandRotation = FRotationMatrix::MakeFromXY(BladeMesh, Fingers).ToQuat() * ClosedHandInverse;
		const FVector Wrist = HandleMesh - HandRotation.RotateVector(PalmInHand);
		ApplyArmReach(CSPose, Bones, HumerusName, RadiusName, HandName, MeshToWorld.TransformPosition(Wrist), GaitData.GripAlpha, OutwardMesh, DownMesh, ForwardMesh);
	}

	FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);
	HandTransform.SetRotation(FQuat::Slerp(HandTransform.GetRotation(), HandRotation, FMath::Clamp(GaitData.GripAlpha, 0.f, 1.f)).GetNormalized());
	CSPose.SetComponentSpaceTransform(Hand, HandTransform);
}

void FAedirProceduralAnimProxy::ApplyLeg(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh, float FootLift) const
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

	// A foot trailing behind the hip peels off the ground: the heel rises and the toes stay down, as at the end of a real step.
	const FVector UpMeshDirection = ActorToMesh.RotateVector(FVector::UpVector);
	const float BehindShare = static_cast<float>(-FVector::DotProduct(Effector - RootPosition, ForwardMesh)) / FMath::Max(LegLength, 1.f);
	const float HeelAlpha = FMath::Clamp((BehindShare - AedirHeelLiftStartShare) / AedirHeelLiftRangeShare, 0.f, 1.f);
	Effector += UpMeshDirection * (AedirHeelRaiseCm * HeelAlpha);

	const FVector ToEffector = Effector - RootPosition;
	const float EffectorDistance = static_cast<float>(ToEffector.Size());
	if (EffectorDistance > KINDA_SMALL_NUMBER)
	{
		const float ClampedDistance = FMath::Clamp(EffectorDistance, LegLength * AedirLegMinReachShare, LegLength * AedirLegMaxReachShare);
		Effector = RootPosition + ToEffector * (ClampedDistance / EffectorDistance);
	}

	// Knee pole in front of the creature so the knee bends forward.
	// The pole sits beside the middle of the leg, in front of it and slightly down. A foot flicked up behind the body keeps a
	// well defined bending plane this way, where a pole beyond the foot would flip the knee.
	const FVector DownMeshDirection = ActorToMesh.RotateVector(-FVector::UpVector);
	const FVector JointTarget = (RootPosition + Effector) * 0.5 + ForwardMesh * AedirKneePoleDistance + DownMeshDirection * AedirKneePoleDropDistance;

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
	// A foot on the ground lies flat on it: the flat reference orientation tilted to the ground normal.
	// A foot in the air knows nothing of the ground and keeps its rest angle to the shin, as does a heel peeling off behind the body.
	const FQuat FlatFootRotation = FootTiltMesh * RefCSPose.GetComponentSpaceTransform(Foot).GetRotation();
	const FQuat ShinFootRotation = TibiaDelta * FemurDelta * FootTransform.GetRotation();
	const float LooseAlpha = FMath::Max(FMath::Clamp(FootLift * AedirFootLooseGain, 0.f, 1.f), HeelAlpha);
	NewFoot.SetRotation(FQuat::Slerp(FlatFootRotation, ShinFootRotation, LooseAlpha).GetNormalized());
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
