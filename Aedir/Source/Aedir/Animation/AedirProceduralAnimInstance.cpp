#include "Animation/AedirProceduralAnimInstance.h"

#include "Items/AedirItemActor.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Actor.h"
#include "TwoBoneIK.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAedirBodyBulk(TEXT("aedir.BodyBulk"), 1, TEXT("Thicken the trunk and thighs of strong bodies by scaling their bones (1) or not (0)."));
	constexpr float AedirObjectPalmShare = 0.55f;
	constexpr float AedirObjectContactDownShare = 0.6f;
	constexpr float AedirObjectPalmClearanceCm = 2.5f;
	constexpr float AedirKneePoleDistance = 100.f;
	constexpr float AedirKneePoleDropDistance = 30.f;
	constexpr float AedirLegMinReachShare = 0.3f;
	constexpr float AedirLegMaxReachShare = 0.998f;
	constexpr float AedirHeelLiftStartShare = 0.2f;
	constexpr float AedirHeelLiftRangeShare = 0.2f;
	constexpr float AedirHeelRaiseCm = 7.f;
	constexpr float AedirFootLooseGain = 3.f;
	constexpr float AedirWristFollowShare = 0.25f;
	// At a full run (the run spread at its most, 0.6) the free hand is turned about 20 degrees in.
	constexpr float AedirRunWristInwardDegrees = 34.f;
	// A bent elbow carries the forearm in across the belly, as a runner's does.
	constexpr float AedirForearmInwardShare = 0.22f;
	constexpr float AedirClavicleFollowShare = 0.25f;
	constexpr float AedirReachAlphaThreshold = 0.001f;
	constexpr float AedirElbowOutwardDistance = 60.f;
	constexpr float AedirElbowDropDistance = 40.f;
	constexpr float AedirElbowBackDistance = 90.f;
	constexpr float AedirElbowGuardOutShare = 0.3f;
	constexpr float AedirElbowGuardDropShare = 1.8f;
	constexpr float AedirElbowOutGain = 2.5f;
	// A hand above the shoulder bends its elbow out to the side and a little forward.
	constexpr float AedirElbowRaiseRangeCm = 25.f;
	constexpr float AedirElbowRaisedOutCm = 60.f;
	constexpr float AedirElbowRaisedForwardCm = 18.f;
	// The handle crosses the palm, so it never lies closer to the line of the forearm than this.
	constexpr float AedirWristMinBladeDegrees = 28.f;
	// A weapon in a fist comes out of it at about this angle to the forearm, a little towards the hand; in a fight it stays
	// there and the arm does the aiming.
	constexpr double AedirBladeForearmDegrees = 80.0;
	constexpr int32 AedirArmLedSteps = 48;
	// How much a far move of the elbow, and an elbow raised over the shoulder or tucked in behind it, count against a pose.
	constexpr double AedirArmLedStayShare = 0.15;
	constexpr double AedirArmLedHighElbowCost = 2.0;
	constexpr double AedirArmLedInsideElbowCost = 1.0;
	constexpr double AedirArmLedBehindElbowCost = 1.5;
	// The wrist gives that angle up to this far either way rather than throw the elbow somewhere no arm goes; every degree of
	// it counts against a pose like this many cm of elbow.
	constexpr double AedirArmLedWristGiveDegrees = 30.0;
	constexpr double AedirArmLedWristGiveCost = 0.12;
	// However the blade and the forearm come to lie, a hand turns no faster than this.
	constexpr float AedirHandMaxTurnDegrees = 1000.f;
	constexpr float AedirElbowHintDistanceCm = 50.f;
	// How far a hand on a long shaft may stay from the handle's line, and how close it may come to the other hand when it has to slide nearer.
	constexpr float AedirGripPalmReachCm = 4.f;
	constexpr float AedirGripSlideStepCm = 2.f;
	constexpr float AedirGripMinHandsApartCm = 12.f;
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
	const AAedirCreature* Creature = Cast<AAedirCreature>(Owner);
	BulkScale = Creature && Creature->Stats ? Creature->Stats->GetBulkScale() * Creature->GetAppearanceBulk() : 1.f;
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
	ApplyArmReach(CSPose, Bones, BoneNameSet.LeftHumerus, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, GaitData.LeftReachTargetWorld, GaitData.LeftArmReachAlpha, LeftOutwardMesh, DownMesh, ArmForwardMesh, GaitData.LeftElbowHint, GaitData.LeftElbowHintAlpha);
	ApplyArmReach(CSPose, Bones, BoneNameSet.RightHumerus, BoneNameSet.RightRadius, BoneNameSet.RightHand, GaitData.RightReachTargetWorld, GaitData.RightArmReachAlpha, RightOutwardMesh, DownMesh, ArmForwardMesh, GaitData.RightElbowHint, GaitData.RightElbowHintAlpha);

	// A weapon in a fight is pointed by the arm: the elbow and shoulder turn the forearm and the blade keeps its angle in the hand.
	const FVector LeftBladeWorld = ApplyArmLedBlade(CSPose, Bones, BoneNameSet.LeftHumerus, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, GaitData.LeftWeaponDirWorld, GaitData.LeftArmLedShare * GaitData.LeftWeaponAimAlpha, GaitData.LeftArmReachAlpha, LeftOutwardMesh, DownMesh, ArmForwardMesh);
	const FVector RightBladeWorld = ApplyArmLedBlade(CSPose, Bones, BoneNameSet.RightHumerus, BoneNameSet.RightRadius, BoneNameSet.RightHand, GaitData.RightWeaponDirWorld, GaitData.RightArmLedShare * GaitData.RightWeaponAimAlpha, GaitData.RightArmReachAlpha, RightOutwardMesh, DownMesh, ArmForwardMesh);
	ApplyHandAim(CSPose, RefCSPose, Bones, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, TEXT("_l"), LeftBladeWorld, GaitData.LeftWeaponAimAlpha, GaitData.LeftWristFrontShare, GaitData.LeftWristMinDegrees, GaitData.LeftHangDegrees);
	ApplyHandAim(CSPose, RefCSPose, Bones, BoneNameSet.RightRadius, BoneNameSet.RightHand, TEXT("_r"), RightBladeWorld, GaitData.RightWeaponAimAlpha, GaitData.RightWristFrontShare, GaitData.RightWristMinDegrees, GaitData.RightHangDegrees);

	if (GaitData.ObjectAlpha > AedirReachAlphaThreshold)
	{
		ApplyObjectHand(CSPose, RefCSPose, Bones, true, LeftOutwardMesh, DownMesh, ArmForwardMesh);
		ApplyObjectHand(CSPose, RefCSPose, Bones, false, RightOutwardMesh, DownMesh, ArmForwardMesh);
	}

	const bool bGripLeft = GaitData.GripSecondaryHand == static_cast<int32>(EAedirHand::Left);
	ApplyGripHand(CSPose, RefCSPose, Bones, bGripLeft ? LeftOutwardMesh : RightOutwardMesh, DownMesh, ArmForwardMesh);

	ApplyForearmTwist(CSPose, RefCSPose, Bones, BoneNameSet.LeftRadius, BoneNameSet.LeftHand, TEXT("_l"));
	ApplyForearmTwist(CSPose, RefCSPose, Bones, BoneNameSet.RightRadius, BoneNameSet.RightHand, TEXT("_r"));

	// Fingers are children of the final hand pose, so they are curled after the arm IK.
	ApplyFingers(CSPose, Bones, LeftFingerBoneNames, GaitData.FistAlphaLeft, FistCurlSignLeft);
	ApplyFingers(CSPose, Bones, RightFingerBoneNames, GaitData.FistAlphaRight, FistCurlSignRight);

	const FVector ForwardMesh = ActorToMesh.RotateVector(FVector::ForwardVector);
	ApplyLeg(CSPose, RefCSPose, Bones, BoneNameSet.LeftFemur, BoneNameSet.LeftTibia, BoneNameSet.LeftFoot, GaitData.LeftFootWorld, GaitData.LeftFootRot, ForwardMesh, GaitData.LeftFootLift, GaitData.KickLeftLeg ? GaitData.KickSoleAlpha : 0.f);
	ApplyLeg(CSPose, RefCSPose, Bones, BoneNameSet.RightFemur, BoneNameSet.RightTibia, BoneNameSet.RightFoot, GaitData.RightFootWorld, GaitData.RightFootRot, ForwardMesh, GaitData.RightFootLift, GaitData.KickLeftLeg ? 0.f : GaitData.KickSoleAlpha);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, Output.Pose);
	Output.Pose.NormalizeRotations();
	ApplyBulk(Output.Pose, Bones);

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

void FAedirProceduralAnimProxy::ApplyBulk(FCompactPose& Pose, const FBoneContainer& Bones) const
{
	if (CVarAedirBodyBulk.GetValueOnAnyThread() == 0 || FMath::IsNearlyEqual(BulkScale, 1.f, 0.005f))
	{
		return;
	}
	// The bones run along their own X: thickening the trunk and limbs scales their other two axes. The parts hung on them,
	// the head, the hands and the feet, keep their own size; the shoulders, set out to the sides of the chest, sit wider.
	const FVector Thick(1.f, BulkScale, BulkScale);
	const FVector Kept(1.f, 1.f / BulkScale, 1.f / BulkScale);
	const auto Scale = [&Pose, &Bones](const TCHAR* Name, const FVector& By)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		if (MeshIndex == INDEX_NONE)
		{
			return;
		}
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Index != INDEX_NONE)
		{
			Pose[Index].SetScale3D(Pose[Index].GetScale3D() * By);
		}
	};
	Scale(TEXT("spine_01"), Thick);
	Scale(TEXT("neck_01"), Kept);
	for (const TCHAR* Side : { TEXT("_l"), TEXT("_r") })
	{
		Scale(*(FString(TEXT("thigh")) + Side), Thick);
		Scale(*(FString(TEXT("foot")) + Side), Kept);
		Scale(*(FString(TEXT("hand")) + Side), Kept);
	}
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
		RotateBone(CSPose, Humerus, ActorRotationToMesh(FRotator(ArmSwingDegrees + ArmRestForwardDegrees * RestAlpha, 0.f, RestRollDegrees * RestAlpha * (1.f - 3.f * GaitData.ArmBalanceSpread - GaitData.ArmRunSpread)).Quaternion()));
	}

	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	if (Radius.IsValid())
	{
		// The left arm hangs on the actor's -Y side, so in is towards +Y, a positive yaw.
		const float InwardSign = ClavicleName == BoneNameSet.LeftClavicle ? 1.f : -1.f;
		const float InwardDegrees = InwardSign * ElbowDegrees * AedirForearmInwardShare * FMath::Clamp(1.f - ReachAlpha, 0.f, 1.f);
		RotateBone(CSPose, Radius, ActorRotationToMesh(FRotator(RestElbowDegrees, InwardDegrees, 0.f).Quaternion()));
	}

	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	if (Hand.IsValid())
	{
		// Running, a free hand turns in towards the body at the wrist, as a runner's does.
		const float InwardSign = ClavicleName == BoneNameSet.LeftClavicle ? 1.f : -1.f;
		const float RunWristDegrees = InwardSign * AedirRunWristInwardDegrees * GaitData.ArmRunSpread * RestAlpha;
		RotateBone(CSPose, Hand, ActorRotationToMesh(FRotator(RestElbowDegrees * AedirWristFollowShare, RunWristDegrees, 0.f).Quaternion()));
	}
}

void FAedirProceduralAnimProxy::ApplyArmReach(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& HandWorld, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh, const FVector& ElbowHint, float ElbowHintAlpha) const
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
	const FVector LowJointTarget = Effector + OutwardMesh * (AedirElbowOutwardDistance * ElbowOutShare) + DownMesh * (AedirElbowDropDistance * ElbowDropShare) - ForwardMesh * (AedirElbowBackDistance * Sideways);
	// Over the shoulder a pole below the hand would lie on the line of the arm and flip the elbow, so the raised elbow has its own.
	const float RaisedShare = FMath::SmoothStep(0.f, 1.f, static_cast<float>(-FVector::DotProduct(Effector - RootPosition, DownMesh)) / AedirElbowRaiseRangeCm);
	const FVector RaisedJointTarget = (RootPosition + Effector) * 0.5 + OutwardMesh * AedirElbowRaisedOutCm + ForwardMesh * AedirElbowRaisedForwardCm;
	// In a weapon stance the pose says where the elbow goes.
	const FVector HintJointTarget = (RootPosition + Effector) * 0.5 + ActorToMesh.RotateVector(ElbowHint) * AedirElbowHintDistanceCm;
	const FVector JointTarget = FMath::Lerp(FMath::Lerp(LowJointTarget, RaisedJointTarget, RaisedShare), HintJointTarget, FMath::Clamp(ElbowHintAlpha, 0.f, 1.f));

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

void FAedirProceduralAnimProxy::ApplyHandAim(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& RadiusName, const FName& HandName, const TCHAR* Suffix, const FVector& DirectionWorld, float Alpha, float FrontShare, float MinBladeDegrees, float HangDegrees) const
{
	const int32 HandSlot = HandName == BoneNameSet.LeftHand ? 0 : 1;
	if (Alpha <= AedirReachAlphaThreshold)
	{
		bLastHandValid[HandSlot] = false;
		return;
	}

	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	const FCompactPoseBoneIndex Index = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("index_01%s"), Suffix)));
	const FCompactPoseBoneIndex Pinky = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("pinky_01%s"), Suffix)));
	const FCompactPoseBoneIndex Middle = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("middle_01%s"), Suffix)));
	if (!Radius.IsValid() || !Hand.IsValid() || !Index.IsValid() || !Pinky.IsValid() || !Middle.IsValid())
	{
		return;
	}

	const FTransform RefHand = RefCSPose.GetComponentSpaceTransform(Hand);
	const FQuat RefHandInverse = RefHand.GetRotation().Inverse();
	const FVector RefKnuckles = RefCSPose.GetComponentSpaceTransform(Index).GetLocation() - RefCSPose.GetComponentSpaceTransform(Pinky).GetLocation();
	const FVector RefToMiddle = RefCSPose.GetComponentSpaceTransform(Middle).GetLocation() - RefHand.GetLocation();
	const FQuat ClosedHandInverse = FRotationMatrix::MakeFromXY(RefHandInverse.RotateVector(RefKnuckles), RefHandInverse.RotateVector(RefToMiddle)).ToQuat().Inverse();

	FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);
	const FVector Forearm = (HandTransform.GetLocation() - CSPose.GetComponentSpaceTransform(Radius).GetLocation()).GetSafeNormal();
	FVector Blade = MeshToWorld.InverseTransformVectorNoScale(DirectionWorld).GetSafeNormal();

	// A weapon that is only carried hangs the way it is carried, down and a little forward of the hand, however the elbow
	// is bent to hold it.
	(void)FrontShare;
	(void)HangDegrees;

	// The handle crosses the palm: the wrist bends only so far, and a blade pointing along the forearm stops short of its line.
	const double Along = FVector::DotProduct(Blade, Forearm);
	const double LimitCos = FMath::Cos(FMath::DegreesToRadians(MinBladeDegrees));
	if (FMath::Abs(Along) > LimitCos)
	{
		// Close to that line the front of the body decides the side, or the hand would turn over as the blade crosses it.
		const double Depth = (FMath::Abs(Along) - LimitCos) / (1.0 - LimitCos);
		FVector Across = FVector::VectorPlaneProject(Blade, Forearm) + FVector::VectorPlaneProject(ActorToMesh.RotateVector(FVector::ForwardVector), Forearm) * Depth;
		if (Across.IsNearlyZero(0.01))
		{
			Across = FVector::VectorPlaneProject(ActorToMesh.RotateVector(FVector::UpVector), Forearm);
		}
		Blade = Forearm * (Along > 0.0 ? LimitCos : -LimitCos) + Across.GetSafeNormal() * FMath::Sin(FMath::DegreesToRadians(MinBladeDegrees));
	}

	// The blade runs across the knuckles and the fingers run on from the forearm, so a change of direction comes from the arm.
	FQuat HandRotation = FRotationMatrix::MakeFromXY(Blade, FVector::VectorPlaneProject(Forearm, Blade)).ToQuat() * ClosedHandInverse;
	if (bLastHandValid[HandSlot])
	{
		const float Turn = static_cast<float>(LastHandRotation[HandSlot].AngularDistance(HandRotation));
		const float MaxTurn = FMath::DegreesToRadians(AedirHandMaxTurnDegrees) * GetDeltaSeconds();
		if (Turn > MaxTurn && MaxTurn > 0.f)
		{
			HandRotation = FQuat::Slerp(LastHandRotation[HandSlot], HandRotation, MaxTurn / Turn).GetNormalized();
		}
	}
	LastHandRotation[HandSlot] = HandRotation;
	bLastHandValid[HandSlot] = true;
	HandTransform.SetRotation(FQuat::Slerp(HandTransform.GetRotation(), HandRotation, FMath::Clamp(Alpha, 0.f, 1.f)).GetNormalized());
	CSPose.SetComponentSpaceTransform(Hand, HandTransform);
}

FVector FAedirProceduralAnimProxy::ApplyArmLedBlade(FCSPose<FCompactPose>& CSPose, const FBoneContainer& Bones, const FName& HumerusName, const FName& RadiusName, const FName& HandName, const FVector& DirectionWorld, float Share, float ReachAlpha, const FVector& OutwardMesh, const FVector& DownMesh, const FVector& ForwardMesh) const
{
	const FCompactPoseBoneIndex Humerus = ResolveCompactBone(Bones, HumerusName);
	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	if (Share <= AedirReachAlphaThreshold || ReachAlpha <= AedirReachAlphaThreshold || !Humerus.IsValid() || !Radius.IsValid() || !Hand.IsValid() || DirectionWorld.IsNearlyZero())
	{
		return DirectionWorld;
	}

	const FVector Shoulder = CSPose.GetComponentSpaceTransform(Humerus).GetLocation();
	const FVector Elbow = CSPose.GetComponentSpaceTransform(Radius).GetLocation();
	const FVector Wrist = CSPose.GetComponentSpaceTransform(Hand).GetLocation();
	const double UpperCm = (Elbow - Shoulder).Size();
	const double ForeCm = (Wrist - Elbow).Size();
	const FVector Blade = MeshToWorld.InverseTransformVectorNoScale(DirectionWorld).GetSafeNormal();
	const double Fixed = FMath::DegreesToRadians(AedirBladeForearmDegrees);

	// The forearms that hold the blade at that angle make a cone around it. Of those, the one whose elbow the upper arm can
	// reach, nearest to where the elbow is now so the arm does not flip from one side to the other.
	FVector Across = FVector::VectorPlaneProject(Wrist - Elbow, Blade).GetSafeNormal();
	if (Across.IsNearlyZero())
	{
		Across = FVector::VectorPlaneProject(-DownMesh, Blade).GetSafeNormal();
	}
	const FVector Third = FVector::CrossProduct(Blade, Across);
	FVector BestElbow = Elbow;
	double BestError = UE_BIG_NUMBER;
	double BestAngle = Fixed;
	for (const double Give : { 0.0, -0.5, 0.5, -1.0, 1.0 })
	{
		const double Angle = Fixed + FMath::DegreesToRadians(Give * AedirArmLedWristGiveDegrees);
		for (int32 Step = 0; Step < AedirArmLedSteps; ++Step)
		{
			const double Turn = UE_TWO_PI * Step / AedirArmLedSteps;
			const FVector Forearm = Blade * FMath::Cos(Angle) + (Across * FMath::Cos(Turn) + Third * FMath::Sin(Turn)) * FMath::Sin(Angle);
			const FVector Candidate = Wrist - Forearm * ForeCm;
			// An elbow up above the shoulder, tucked in across the chest or thrown back behind the body is not how anyone swings.
			const double Awkward = FMath::Max(FVector::DotProduct(Candidate - Shoulder, -DownMesh), 0.0) * AedirArmLedHighElbowCost
				+ FMath::Max(FVector::DotProduct(Candidate - Shoulder, -OutwardMesh), 0.0) * AedirArmLedInsideElbowCost
				+ FMath::Max(FVector::DotProduct(Candidate - Shoulder, -ForwardMesh) - ForeCm * 0.5, 0.0) * AedirArmLedBehindElbowCost;
			const double Error = FMath::Abs((Candidate - Shoulder).Size() - UpperCm) + (Candidate - Elbow).Size() * AedirArmLedStayShare + Awkward
				+ FMath::Abs(Give) * AedirArmLedWristGiveDegrees * AedirArmLedWristGiveCost;
			if (Error < BestError)
			{
				BestError = Error;
				BestElbow = Candidate;
				BestAngle = Angle;
			}
		}
	}

	// The arm is solved again with its elbow drawn to that place; the hand stays where it was.
	const FVector Middle = (Shoulder + Wrist) * 0.5;
	const FVector HintLocal = ActorToMesh.Inverse().RotateVector((BestElbow - Middle).GetSafeNormal());
	ApplyArmReach(CSPose, Bones, HumerusName, RadiusName, HandName, MeshToWorld.TransformPosition(Wrist), ReachAlpha, OutwardMesh, DownMesh, ForwardMesh, HintLocal, Share);

	// Wherever the arm could bring the forearm, the blade comes out of the hand at the same angle: what the arm cannot reach,
	// the blade does not point at.
	const FVector Forearm = (CSPose.GetComponentSpaceTransform(Hand).GetLocation() - CSPose.GetComponentSpaceTransform(Radius).GetLocation()).GetSafeNormal();
	FVector Out = FVector::VectorPlaneProject(Blade, Forearm).GetSafeNormal();
	if (Out.IsNearlyZero())
	{
		return DirectionWorld;
	}
	const FVector Held = Forearm * FMath::Cos(BestAngle) + Out * FMath::Sin(BestAngle);
	return MeshToWorld.TransformVectorNoScale(FMath::Lerp(Blade, Held, static_cast<double>(FMath::Clamp(Share, 0.f, 1.f))).GetSafeNormal());
}

void FAedirProceduralAnimProxy::ApplyForearmTwist(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& RadiusName, const FName& HandName, const TCHAR* Suffix) const
{
	const FCompactPoseBoneIndex Radius = ResolveCompactBone(Bones, RadiusName);
	const FCompactPoseBoneIndex Hand = ResolveCompactBone(Bones, HandName);
	if (!Radius.IsValid() || !Hand.IsValid())
	{
		return;
	}

	// A forearm turns along its whole length, not at the wrist. The twist bones take the turn of the hand, more of it the nearer they are to the hand.
	const FTransform RadiusTransform = CSPose.GetComponentSpaceTransform(Radius);
	const FVector ToHand = CSPose.GetComponentSpaceTransform(Hand).GetLocation() - RadiusTransform.GetLocation();
	const double Length = ToHand.Size();
	if (Length < 1.0)
	{
		return;
	}

	const FVector Axis = ToHand / Length;
	const FQuat RestHandInForearm = RefCSPose.GetComponentSpaceTransform(Radius).GetRotation().Inverse() * RefCSPose.GetComponentSpaceTransform(Hand).GetRotation();
	const FQuat HandTurn = CSPose.GetComponentSpaceTransform(Hand).GetRotation() * (RadiusTransform.GetRotation() * RestHandInForearm).Inverse();
	FQuat Swing;
	FQuat Twist;
	HandTurn.ToSwingTwist(Axis, Swing, Twist);
	if (Twist.W < 0.0)
	{
		Twist = -Twist;
	}

	const TCHAR* const TwistNames[] = { TEXT("lowerarm_twist_01"), TEXT("lowerarm_twist_02") };
	for (const TCHAR* TwistName : TwistNames)
	{
		const FCompactPoseBoneIndex TwistBone = ResolveCompactBone(Bones, FName(*FString::Printf(TEXT("%s%s"), TwistName, Suffix)));
		if (TwistBone.IsValid())
		{
			FTransform TwistTransform = CSPose.GetComponentSpaceTransform(TwistBone);
			const float Share = FMath::Clamp(static_cast<float>(FVector::DotProduct(TwistTransform.GetLocation() - RadiusTransform.GetLocation(), Axis) / Length), 0.f, 1.f);
			TwistTransform.SetRotation((FQuat::Slerp(FQuat::Identity, Twist, Share) * TwistTransform.GetRotation()).GetNormalized());
			CSPose.SetComponentSpaceTransform(TwistBone, TwistTransform);
		}
	}
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
	FVector HandleMesh = PrimaryTransform.TransformPositionNoScale(GaitData.GripPointInPrimaryHand);
	const FVector BladeMesh = PrimaryTransform.GetRotation().RotateVector(GaitData.GripBladeInPrimaryHand).GetSafeNormal();

	// A hand cannot close on a point its arm does not reach, so on a long shaft it takes hold nearer the carrying hand.
	const FCompactPoseBoneIndex Humerus = ResolveCompactBone(Bones, HumerusName);
	if (Humerus.IsValid())
	{
		const FVector Shoulder = CSPose.GetComponentSpaceTransform(Humerus).GetLocation();
		const FVector Elbow = CSPose.GetComponentSpaceTransform(Radius).GetLocation();
		const double ArmCm = (Elbow - Shoulder).Size() + (CSPose.GetComponentSpaceTransform(Hand).GetLocation() - Elbow).Size() + AedirGripPalmReachCm;
		double Slide = FVector::DotProduct(HandleMesh - PrimaryTransform.GetLocation(), BladeMesh) - AedirGripMinHandsApartCm;
		while (Slide > 0.0 && (HandleMesh - Shoulder).Size() > ArmCm)
		{
			HandleMesh -= BladeMesh * AedirGripSlideStepCm;
			Slide -= AedirGripSlideStepCm;
		}
	}

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
		// In the half-sword the hand lies on top of the blade, so it turns over around its fingers, through thumb up, and its thumb points back along the weapon.
		FVector Knuckles = BladeMesh;
		if (GaitData.GripOverhand > 0.f && !Fingers.IsNearlyZero(1.f))
		{
			const FVector FingerAxis = Fingers.GetSafeNormal();
			const float ThumbUpSign = FVector::DotProduct(FVector::CrossProduct(FingerAxis, BladeMesh), -DownMesh) >= 0.0 ? 1.f : -1.f;
			Knuckles = FQuat(FingerAxis, ThumbUpSign * UE_PI * GaitData.GripOverhand).RotateVector(BladeMesh);
		}
		HandRotation = FRotationMatrix::MakeFromXY(Knuckles, Fingers).ToQuat() * ClosedHandInverse;
		const FVector Wrist = HandleMesh - HandRotation.RotateVector(PalmInHand);
		ApplyArmReach(CSPose, Bones, HumerusName, RadiusName, HandName, MeshToWorld.TransformPosition(Wrist), GaitData.GripAlpha, OutwardMesh, DownMesh, ForwardMesh, bLeft ? GaitData.LeftElbowHint : GaitData.RightElbowHint, bLeft ? GaitData.LeftElbowHintAlpha : GaitData.RightElbowHintAlpha);
	}

	FTransform HandTransform = CSPose.GetComponentSpaceTransform(Hand);
	HandTransform.SetRotation(FQuat::Slerp(HandTransform.GetRotation(), HandRotation, FMath::Clamp(GaitData.GripAlpha, 0.f, 1.f)).GetNormalized());
	CSPose.SetComponentSpaceTransform(Hand, HandTransform);
}

void FAedirProceduralAnimProxy::ApplyLeg(FCSPose<FCompactPose>& CSPose, FCSPose<FCompactPose>& RefCSPose, const FBoneContainer& Bones, const FName& FemurName, const FName& TibiaName, const FName& FootName, const FVector& FootWorld, const FRotator& FootTiltWorld, const FVector& ForwardMesh, float FootLift, float KickSoleAlpha) const
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
	const FVector ForwardJointTarget = (RootPosition + Effector) * 0.5 + ForwardMesh * AedirKneePoleDistance + DownMeshDirection * AedirKneePoleDropDistance;
	// Sat on the ground with the feet drawn in, the hip and the foot are level and the knees point up.
	const FVector UpJointTarget = (RootPosition + Effector) * 0.5 + UpMeshDirection * AedirKneePoleDistance + ForwardMesh * AedirKneePoleDropDistance;
	const FVector JointTarget = FMath::Lerp(ForwardJointTarget, UpJointTarget, FMath::Clamp(GaitData.KneeUpShare, 0.f, 1.f));

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
	FQuat FootRotation = FQuat::Slerp(FlatFootRotation, ShinFootRotation, LooseAlpha);
	// A kick lands on the sole: the flat foot turned toes up until its underside faces the target.
	if (KickSoleAlpha > AedirReachAlphaThreshold)
	{
		const FVector SoleDirection = MeshToWorld.InverseTransformVectorNoScale(GaitData.KickSoleDirWorld);
		const FQuat SoleRotation = SafeDeltaRotation(DownMeshDirection, SoleDirection) * RefCSPose.GetComponentSpaceTransform(Foot).GetRotation();
		FootRotation = FQuat::Slerp(FootRotation, SoleRotation, FMath::Clamp(KickSoleAlpha, 0.f, 1.f));
	}
	NewFoot.SetRotation(FootRotation.GetNormalized());
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
