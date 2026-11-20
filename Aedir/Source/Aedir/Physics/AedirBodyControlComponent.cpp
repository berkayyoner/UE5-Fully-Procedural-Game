#include "Physics/AedirBodyControlComponent.h"
#include "Engine/SkinnedAsset.h"
#include "ReferenceSkeleton.h"

#include "Aedir.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"

static TAutoConsoleVariable<float> CVarAedirBodyPosture(TEXT("aedir.BodyPosture"), -1.f, TEXT("Hz of the trunk's upright hold in the world; below 0 uses the component's own."));
static TAutoConsoleVariable<float> CVarAedirBodyGravity(TEXT("aedir.BodyGravity"), -1.f, TEXT("Share of its weight a standing body carries; below 0 uses the component's own."));

static TAutoConsoleVariable<int32> CVarAedirBodyPlant(TEXT("aedir.BodyPlant"), 0, TEXT("Whether a planted foot is held by the pose rather than its muscles."));

static TAutoConsoleVariable<float> CVarAedirBodyFoot(TEXT("aedir.BodyFoot"), -1.f, TEXT("Hz of the feet's hold on where the pose sets them; below 0 uses the component's own."));

static TAutoConsoleVariable<float> CVarAedirBodyHips(TEXT("aedir.BodyHips"), -1.f, TEXT("Hz of the hips' hold on the capsule; below 0 uses the component's own."));

static TAutoConsoleVariable<float> CVarAedirBodyArm(TEXT("aedir.BodyArm"), -1.f, TEXT("Hz of the arms' joints; below 0 uses the component's own."));
static TAutoConsoleVariable<float> CVarAedirBodySpine(TEXT("aedir.BodySpine"), -1.f, TEXT("Hz of the spine's joints; below 0 uses the component's own."));

static TAutoConsoleVariable<float> CVarAedirBodyRise(TEXT("aedir.BodyRise"), -1.f, TEXT("How hard the hips are lifted getting up, against standing; below 0 uses the component's own."));

namespace
{
	// Which part of the body a bone belongs to, by its name on the standard skeleton.
	enum class EAedirBodyPart : uint8 { Hips, Spine, Head, LeftArm, RightArm, Leg };

	EAedirBodyPart BodyPartOf(const FName Bone)
	{
		const FString Name = Bone.ToString();
		if (Name == TEXT("pelvis"))
		{
			return EAedirBodyPart::Hips;
		}
		if (Name.StartsWith(TEXT("neck")) || Name.StartsWith(TEXT("head")))
		{
			return EAedirBodyPart::Head;
		}
		if (Name.StartsWith(TEXT("thigh")) || Name.StartsWith(TEXT("calf")) || Name.StartsWith(TEXT("foot")) || Name.StartsWith(TEXT("ball")))
		{
			return EAedirBodyPart::Leg;
		}
		const bool bArm = Name.StartsWith(TEXT("clavicle")) || Name.StartsWith(TEXT("upperarm")) || Name.StartsWith(TEXT("lowerarm")) || Name.StartsWith(TEXT("hand"))
			|| Name.StartsWith(TEXT("thumb")) || Name.StartsWith(TEXT("index")) || Name.StartsWith(TEXT("middle")) || Name.StartsWith(TEXT("ring")) || Name.StartsWith(TEXT("pinky"));
		if (bArm)
		{
			return Name.EndsWith(TEXT("_l")) ? EAedirBodyPart::LeftArm : EAedirBodyPart::RightArm;
		}
		return EAedirBodyPart::Spine;
	}
}

UAedirBodyControlComponent::UAedirBodyControlComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAedirBodyControlComponent::Setup(USkeletalMeshComponent* InMesh, UCapsuleComponent* InCapsule)
{
	if (!InMesh || !InCapsule || !InMesh->GetPhysicsAsset())
	{
		return;
	}
	Mesh = InMesh;
	if (Control)
	{
		Control->DestroyAllControlsAndBodyModifiers();
	}
	else
	{
		Control = NewObject<UPhysicsControlComponent>(GetOwner(), TEXT("AedirBodyControl"));
		Control->RegisterComponent();
	}
	SpineControls.Reset();
	PostureControls.Reset();
	FootControls.Reset();
	LegModifiers.Reset();
	ArmModifiers[0].Reset();
	ArmModifiers[1].Reset();
	bArmPosed[0] = bArmPosed[1] = false;
	bArmLoose[0] = bArmLoose[1] = false;
	LegBones.Reset();
	HeadControls.Reset();
	LeftArmControls.Reset();
	RightArmControls.Reset();
	MuscleBones.Reset();
	ShoulderControls[0] = ShoulderControls[1] = NAME_None;
	LeftHandControls.Reset();
	RightHandControls.Reset();
	ForearmControls[0].Reset();
	ForearmControls[1].Reset();
	LegControls.Reset();
	LeftLegControls.Reset();
	RightLegControls.Reset();

	// The mesh stays where the capsule has it; its bones take where the simulated bodies are.
	Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
	Mesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);
	Mesh->SetAllBodiesPhysicsBlendWeight(1.f);

	FPhysicsControlData Muscle;
	Muscle.LinearStrength = 0.f;
	Muscle.AngularStrength = 1.f;
	Muscle.AngularDampingRatio = DampingRatio;
	Muscle.LinearDampingRatio = DampingRatio;
	Muscle.MaxTorque = 0.f;
	Muscle.bUseSkeletalAnimation = true;
	Muscle.AngularTargetVelocityMultiplier = 1.f;
	Muscle.LinearTargetVelocityMultiplier = 1.f;
	Muscle.bUseAccelerationDriveMode = true;
	Muscle.bDisableCollision = true;

	FPhysicsControlModifierData Simulated;
	Simulated.MovementType = EPhysicsMovementType::Simulated;
	Simulated.CollisionType = ECollisionEnabled::QueryAndPhysics;
	Simulated.GravityMultiplier = 1.f;
	Simulated.PhysicsBlendWeight = 1.f;

	for (int32 Index = 0; Index < Mesh->Bodies.Num(); ++Index)
	{
		const FBodyInstance* Body = Mesh->Bodies[Index];
		const FName Bone = Body && Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
		if (Bone.IsNone())
		{
			continue;
		}
		const EAedirBodyPart Part = BodyPartOf(Bone);
		const FName Modifier = Control->CreateBodyModifier(Mesh, Bone, TEXT("Body"), Simulated);
		if (Part == EAedirBodyPart::Leg)
		{
			LegModifiers.Add(Modifier);
			LegBones.Add(Bone);
		}
		if ((Part == EAedirBodyPart::LeftArm || Part == EAedirBodyPart::RightArm) && !Bone.ToString().StartsWith(TEXT("clavicle")))
		{
			ArmModifiers[Part == EAedirBodyPart::LeftArm ? 0 : 1].Add(Modifier);
		}
		const FString BoneText = Bone.ToString();
		if (Part == EAedirBodyPart::Hips)
		{
			HipModifier = Modifier;
			continue;
		}
		if (BoneText == TEXT("foot_l") || BoneText == TEXT("ball_l"))
		{
			LeftFootModifiers[BoneText.StartsWith(TEXT("foot")) ? 0 : 1] = Modifier;
		}
		if (BoneText == TEXT("foot_r") || BoneText == TEXT("ball_r"))
		{
			RightFootModifiers[BoneText.StartsWith(TEXT("foot")) ? 0 : 1] = Modifier;
		}

		// Held to the pose by its joint to the nearest part above it that has a body of its own.
		FName Parent = Mesh->GetParentBone(Bone);
		while (!Parent.IsNone() && !Mesh->GetBodyInstance(Parent))
		{
			Parent = Mesh->GetParentBone(Parent);
		}
		if (Parent.IsNone())
		{
			continue;
		}
		const bool bArmPart = Part == EAedirBodyPart::LeftArm || Part == EAedirBodyPart::RightArm;
		FPhysicsControlData ArmMuscle = Muscle;
		ArmMuscle.AngularDampingRatio = ArmDampingRatio;
		ArmMuscle.LinearDampingRatio = ArmDampingRatio;
		const FName Name = Control->CreateControl(Mesh, Parent, Mesh, Bone, bArmPart ? ArmMuscle : Muscle, FPhysicsControlTarget(), TEXT("Muscles"));
		MuscleBones.Add(Name, TPair<FName, FName>(Parent, Bone));
		if (BoneText == TEXT("upperarm_l") || BoneText == TEXT("upperarm_r"))
		{
			ShoulderControls[BoneText.EndsWith(TEXT("_l")) ? 0 : 1] = Name;
		}
		const bool bWrist = bArmPart && !BoneText.StartsWith(TEXT("clavicle")) && !BoneText.StartsWith(TEXT("upperarm")) && !BoneText.StartsWith(TEXT("lowerarm"));
		if (BoneText == TEXT("foot_l") || BoneText == TEXT("foot_r"))
		{
			// A foot finds where the pose sets it down, in the world: the leg's joints alone carry it too late.
			FPhysicsControlData Step = Muscle;
			Step.LinearStrength = 1.f;
			const FName Foot = Control->CreateControl(nullptr, NAME_None, Mesh, Bone, Step, FPhysicsControlTarget(), TEXT("Feet"));
			FootControls.Add(Foot);
			(BoneText == TEXT("foot_l") ? LeftFootControl : RightFootControl) = Foot;
		}
		if (Part == EAedirBodyPart::Spine || Part == EAedirBodyPart::Head)
		{
			// The trunk's sense of up: each part turned towards the pose in the world, which the joints alone cannot do
			// against the weight of everything above them.
			PostureControls.Add(Control->CreateControl(nullptr, NAME_None, Mesh, Bone, Muscle, FPhysicsControlTarget(), TEXT("Posture")));
		}
		switch (Part)
		{
		case EAedirBodyPart::Head: HeadControls.Add(Name); break;
		case EAedirBodyPart::LeftArm:
			(bWrist ? LeftHandControls : LeftArmControls).Add(Name);
			if (BoneText.StartsWith(TEXT("lowerarm")))
			{
				ForearmControls[0].Add(Name);
			}
			break;
		case EAedirBodyPart::RightArm:
			(bWrist ? RightHandControls : RightArmControls).Add(Name);
			if (BoneText.StartsWith(TEXT("lowerarm")))
			{
				ForearmControls[1].Add(Name);
			}
			break;
		case EAedirBodyPart::Leg:
			LegControls.Add(Name);
			(BoneText.EndsWith(TEXT("_l")) ? LeftLegControls : RightLegControls).Add(Name);
			break;
		default: SpineControls.Add(Name); break;
		}
	}

	HipData = Muscle;
	HipData.LinearStrength = 1.f;
	HipData.MaxForce = 0.f;
	HipControl = NAME_None;
	ApplyLegContacts();
	RebindHips(InCapsule);
	AppliedKey = -1.f;
	ApplyMultipliers();
	UE_LOG(LogAedir, Log, TEXT("%s: physical body with %d spine, %d head, %d+%d arm, %d+%d wrist and %d leg muscles."), *GetNameSafe(GetOwner()),
		SpineControls.Num(), HeadControls.Num(), LeftArmControls.Num(), RightArmControls.Num(), LeftHandControls.Num(), RightHandControls.Num(), LegControls.Num());
}

void UAedirBodyControlComponent::SetStrengths(float InUpright, float LeftArm, float RightArm)
{
	Upright = FMath::Clamp(InUpright, 0.f, 1.f);
	LeftArmShare = FMath::Clamp(LeftArm, 0.f, 1.f);
	RightArmShare = FMath::Clamp(RightArm, 0.f, 1.f);
	ApplyMultipliers();
}

void UAedirBodyControlComponent::SetBlowSoftness(float Share)
{
	const float Clamped = FMath::Clamp(Share, 0.f, 1.f);
	if (FMath::Abs(Clamped - BlowSoftness) > 0.04f || (Clamped != BlowSoftness && Clamped == 0.f))
	{
		BlowSoftness = Clamped;
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetTwoHandGrip(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
	if (FMath::Abs(Clamped - TwoHandGrip) > 0.05f || (Clamped != TwoHandGrip && (Clamped == 0.f || Clamped == 1.f)))
	{
		TwoHandGrip = Clamped;
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetArmLoads(float Left, float Right)
{
	LeftLoadShare = FMath::Clamp(Left, 0.1f, 1.f);
	RightLoadShare = FMath::Clamp(Right, 0.1f, 1.f);
	ApplyMultipliers();
}

void UAedirBodyControlComponent::SetKickLeg(int32 Leg)
{
	if (KickLeg != Leg)
	{
		KickLeg = Leg;
		AppliedKey = -1.0;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetAirborne(bool bInAirborne)
{
	if (bAirborne != bInAirborne)
	{
		bAirborne = bInAirborne;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetLimp(bool bInLimp)
{
	if (bLimp != bInLimp)
	{
		bLimp = bInLimp;
		ApplyLegContacts();
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetHoist(float Share)
{
	Hoist = FMath::Clamp(Share, 0.f, 1.f);
	ApplyMultipliers();
}

void UAedirBodyControlComponent::SetDead(bool bInDead)
{
	if (bDead != bInDead)
	{
		bDead = bInDead;
		DeadTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		// Dead, the muscles hold what shape the body had, more and more weakly; not the pose the living one was asked.
		TArray<FName> All;
		All.Append(SpineControls);
		All.Append(HeadControls);
		All.Append(LeftArmControls);
		All.Append(RightArmControls);
		All.Append(LeftHandControls);
		All.Append(RightHandControls);
		All.Append(LegControls);
		if (bDead)
		{
			HoldShapeNow(All);
		}
		else
		{
			FollowAnimation(All);
			bArmLoose[0] = bArmLoose[1] = false;
			ApplyBrokenArms();
		}
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

float UAedirBodyControlComponent::GetDeadShareNow() const
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : DeadTime;
	const float Share = DeadFadeSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(static_cast<float>(Now - DeadTime) / DeadFadeSeconds, 0.f, 1.f) : 1.f;
	return FMath::Lerp(DeadStartShare, DeadShare, Share);
}

void UAedirBodyControlComponent::HoldShapeNow(const TArray<FName>& Names)
{
	if (!Control || !Mesh)
	{
		return;
	}
	for (const FName& Name : Names)
	{
		const TPair<FName, FName>* Bones = MuscleBones.Find(Name);
		if (!Bones)
		{
			continue;
		}
		const FQuat Parent = Mesh->GetBoneQuaternion(Bones->Key);
		const FQuat Child = Mesh->GetBoneQuaternion(Bones->Value);
		Control->SetControlUseSkeletalAnimation(Name, false);
		Control->SetControlTargetOrientation(Name, (Parent.Inverse() * Child).Rotator(), 0.f, true, false);
	}
}

void UAedirBodyControlComponent::FollowAnimation(const TArray<FName>& Names)
{
	if (!Control)
	{
		return;
	}
	for (const FName& Name : Names)
	{
		Control->SetControlUseSkeletalAnimation(Name, true);
		Control->SetControlTargetOrientation(Name, FRotator::ZeroRotator, 0.f, true, false);
	}
}

void UAedirBodyControlComponent::HangBrokenArm(int32 Side)
{
	const FName Shoulder = ShoulderControls[Side];
	const TPair<FName, FName>* Bones = MuscleBones.Find(Shoulder);
	if (!Control || !Mesh || !Bones)
	{
		return;
	}
	const FName Elbow = Side == 0 ? FName(TEXT("lowerarm_l")) : FName(TEXT("lowerarm_r"));
	// Worked out on the body standing as it was made, not as it lies or rises now: hanging down the side of an upright
	// trunk, a little out, whatever way the trunk is turned when the bone breaks or the body gets up.
	const USkinnedAsset* Asset = Mesh->GetSkinnedAsset();
	if (!Asset)
	{
		return;
	}
	const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
	const auto Standing = [&Skeleton](FName Bone)
	{
		FTransform Pose = FTransform::Identity;
		for (int32 Index = Skeleton.FindBoneIndex(Bone); Index != INDEX_NONE; Index = Skeleton.GetParentIndex(Index))
		{
			Pose = Pose * Skeleton.GetRefBonePose()[Index];
		}
		return Pose;
	};
	const FTransform Clavicle = Standing(Bones->Key);
	const FTransform Upper = Standing(Bones->Value);
	const FVector Along = (Standing(Elbow).GetLocation() - Upper.GetLocation()).GetSafeNormal();
	// In the model's own frame up is +Z, and the left side +X.
	const FVector Out = FVector(Side == 0 ? 1.f : -1.f, 0.f, 0.f);
	const float OutRadians = FMath::DegreesToRadians(BrokenArmOutDegrees);
	const FVector Down = (FVector(0.f, 0.f, -1.f) * FMath::Cos(OutRadians) + Out * FMath::Sin(OutRadians)).GetSafeNormal();
	const FQuat Hanging = FQuat::FindBetweenNormals(Along, Down) * Upper.GetRotation();
	Control->SetControlUseSkeletalAnimation(Shoulder, false);
	Control->SetControlTargetOrientation(Shoulder, (Clavicle.GetRotation().Inverse() * Hanging).Rotator(), 0.f, true, false);
	// Below the shoulder the arm is held weakly as it hangs at rest, nearly straight, not in whatever bend it had.
	TArray<FName> Below = Side == 0 ? LeftArmControls : RightArmControls;
	Below.Remove(Shoulder);
	Below.Append(Side == 0 ? LeftHandControls : RightHandControls);
	for (const FName& Name : Below)
	{
		if (const TPair<FName, FName>* Joint = MuscleBones.Find(Name))
		{
			Control->SetControlUseSkeletalAnimation(Name, false);
			Control->SetControlTargetOrientation(Name, (Standing(Joint->Key).GetRotation().Inverse() * Standing(Joint->Value).GetRotation()).Rotator(), 0.f, true, false);
		}
	}
	// Every joint of it is dragged on by the body it hangs from, so running it is carried along and does not stream out
	// behind like a flag.
	Below.Add(Shoulder);
	for (const FName& Name : Below)
	{
		Control->SetControlAngularData(Name, 1.f, ArmDampingRatio * BrokenArmDampingShare, BrokenArmDrag, 0.f, true);
	}
}

void UAedirBodyControlComponent::SetArmBoost(bool bRight, bool bBoost)
{
	if (bArmBoost[bRight ? 1 : 0] != bBoost)
	{
		bArmBoost[bRight ? 1 : 0] = bBoost;
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetBrokenLimbs(bool bLeftArm, bool bRightArm, bool bLeftLeg, bool bRightLeg)
{
	const bool Now[4] = { bLeftArm, bRightArm, bLeftLeg, bRightLeg };
	if (FMemory::Memcmp(Now, bBroken, sizeof(Now)) != 0)
	{
		FMemory::Memcpy(bBroken, Now, sizeof(Now));
		AppliedKey = -1.f;
		ApplyMultipliers();
		ApplyBrokenArms();
	}
}

void UAedirBodyControlComponent::ApplyBrokenArms()
{
	if (!Control)
	{
		return;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLoose = bBroken[Side];
		if (!bLoose && !bArmLoose[Side])
		{
			continue;
		}
		bArmLoose[Side] = bLoose;
		if (!bDead)
		{
			if (bLoose)
			{
				HangBrokenArm(Side);
			}
			else
			{
				TArray<FName> Arm = Side == 0 ? LeftArmControls : RightArmControls;
				Arm.Append(Side == 0 ? LeftHandControls : RightHandControls);
				FollowAnimation(Arm);
				for (const FName& Name : Arm)
				{
					Control->SetControlAngularData(Name, 1.f, ArmDampingRatio, 0.f, 0.f, true);
				}
			}
		}
		for (const FName& Modifier : ArmModifiers[Side])
		{
			const bool bKinematic = !bLoose && bWholePosed;
			Control->SetBodyModifierMovementType(Modifier, bKinematic ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
			Control->SetBodyModifierPhysicsBlendWeight(Modifier, bLoose ? 1.f : bWholePosed || bArmPosed[Side] ? 0.f : 1.f);
			// It carries all its weight: no foot under it takes any of it.
			if (bLoose)
			{
				Control->SetBodyModifierGravityMultiplier(Modifier, 1.f);
			}
		}
	}
}

void UAedirBodyControlComponent::SetRising(bool bInRising)
{
	if (bRising != bInRising)
	{
		bRising = bInRising;
		ApplyLegContacts();
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::ApplyLegContacts()
{
	if (!Control)
	{
		return;
	}
	// Down, or pushing up off the ground on hands and knees, the legs bear on the ground. Standing, the steps set them on it:
	// the floor or a step's edge would only push them out of place, so they pass the fixed world, but they stay solid to other
	// bodies, the dead, weapons and loose things, which can no more pass through a standing man's legs than through his trunk.
	const ECollisionResponse World = bLimp || bRising ? ECR_Block : ECR_Ignore;
	for (const FName& Bone : LegBones)
	{
		if (FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(Bone) : nullptr)
		{
			Body->SetResponseToChannel(ECC_WorldStatic, World);
			Body->SetResponseToChannel(ECC_WorldDynamic, World);
		}
	}
}

void UAedirBodyControlComponent::SetHipsCarried(bool bCarried)
{
	if (bHipsCarried != bCarried && Control && !HipModifier.IsNone())
	{
		bHipsCarried = bCarried;
		Control->SetBodyModifierMovementType(HipModifier, bCarried || bWholePosed ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetFeetPlanted(bool bLeft, bool bRight)
{
	if (!Control)
	{
		return;
	}
	// Down, no foot holds anything.
	const bool bPlanting = CVarAedirBodyPlant.GetValueOnGameThread() != 0;
	const bool bWantLeft = bLeft && !bLimp && bPlanting;
	const bool bWantRight = bRight && !bLimp && bPlanting;
	if (bWantLeft != bLeftPlanted)
	{
		bLeftPlanted = bWantLeft;
		for (const FName& Modifier : LeftFootModifiers)
		{
			if (!Modifier.IsNone())
			{
				Control->SetBodyModifierMovementType(Modifier, bWantLeft || bWholePosed ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
			}
		}
	}
	if (bWantRight != bRightPlanted)
	{
		bRightPlanted = bWantRight;
		for (const FName& Modifier : RightFootModifiers)
		{
			if (!Modifier.IsNone())
			{
				Control->SetBodyModifierMovementType(Modifier, bWantRight || bWholePosed ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
			}
		}
	}
}

FVector UAedirBodyControlComponent::GetPoseLocation(FName Bone) const
{
	return Control && Mesh ? Control->GetCachedBoneTransform(Mesh, Bone).GetLocation() : FVector::ZeroVector;
}

void UAedirBodyControlComponent::RebindHips(UCapsuleComponent* InCapsule)
{
	if (!Control || !Mesh || !InCapsule)
	{
		return;
	}
	if (!HipControl.IsNone())
	{
		Control->DestroyControl(HipControl);
	}
	// The hips are held where the pose has them on the capsule, in place and in turn.
	HipControl = Control->CreateControl(InCapsule, NAME_None, Mesh, TEXT("pelvis"), HipData, FPhysicsControlTarget(), TEXT("Hips"));
	bReady = !HipControl.IsNone();
	UE_LOG(LogAedir, Verbose, TEXT("[Body] %s: hips tied to the capsule as %s."), *GetNameSafe(GetOwner()), *HipControl.ToString());
	AppliedKey = -1.f;
	ApplyMultipliers();
}

void UAedirBodyControlComponent::Teleported(const FVector& Offset)
{
	if (!Control || !bReady || !Mesh)
	{
		return;
	}
	// The whole body is carried over as it is, every part by the same move, and set at rest: whatever speed the parts had
	// from being torn after the capsule goes with it.
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			FTransform Moved = Body->GetUnrealWorldTransform();
			Moved.SetScale3D(FVector::OneVector);
			Moved.AddToTranslation(Offset);
			Body->SetBodyTransform(Moved, ETeleportType::TeleportPhysics);
		}
	}
	Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	Mesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	Control->SetCachedBoneVelocitiesToZero();
}

void UAedirBodyControlComponent::ApplyMultipliers()
{
	if (!Control || !bReady)
	{
		return;
	}
	// Only when something changed enough to matter.
	const auto Step = [](float Share) { return static_cast<double>(FMath::RoundToInt(Share * 50.f)); };
	const float DeadNow = bDead ? GetDeadShareNow() : 0.f;
	const double Key = (bLimp ? 1.0 : 0.0) + (bHipsCarried ? 2.0 : 0.0) + (bAirborne ? 4.0 : 0.0) + (bRising ? 8.0 : 0.0)
		+ 16.0 * (Step(Upright) + 64.0 * (Step(LeftArmShare) + 64.0 * (Step(RightArmShare) + 64.0 * (Step(LeftLoadShare) + 64.0 * (Step(RightLoadShare) + 64.0 * Step(DeadNow * 10.f)))))) + 1.0e13 * Step(bLimp ? Hoist : 0.f);
	// A tuning console variable changed counts as a change too.
	const float Tuning = CVarAedirBodyPosture.GetValueOnGameThread() + 3.f * CVarAedirBodyGravity.GetValueOnGameThread() + 7.f * CVarAedirBodyFoot.GetValueOnGameThread()
		+ 13.f * CVarAedirBodyHips.GetValueOnGameThread() + 17.f * CVarAedirBodyRise.GetValueOnGameThread() + 19.f * CVarAedirBodySpine.GetValueOnGameThread();
	if (Key == AppliedKey && Tuning == AppliedTuning)
	{
		return;
	}
	AppliedKey = Key;
	AppliedTuning = Tuning;
	const float SpineHz = CVarAedirBodySpine.GetValueOnGameThread() >= 0.f ? CVarAedirBodySpine.GetValueOnGameThread() : SpineStrength;

	// A dead body keeps less of its muscles than a living one that is down.
	const float Limp = bDead ? DeadNow : LimpShare;
	const float Hauled = bLimp && !bDead ? Hoist : 0.f;
	const float Body = bLimp ? FMath::Lerp(Limp, HoistMuscleShare, Hauled) : FMath::Lerp(0.35f, 1.f, Upright);
	const float LegBody = bLimp ? FMath::Lerp(Limp, HoistLegShare, Hauled) : Body;
	const auto Apply = [this](const TArray<FName>& Names, float AngularHz, float LinearHz)
	{
		FPhysicsControlMultiplier Multiplier;
		Multiplier.AngularStrengthMultiplier = AngularHz;
		Multiplier.LinearStrengthMultiplier = FVector(LinearHz);
		for (const FName& Name : Names)
		{
			Control->SetControlMultiplier(Name, Multiplier, true);
		}
	};
	// Getting up the trunk, the head and the arms give more.
	// A blow just now slackens the trunk, the head and the arms for a moment, and the trunk and head always give a little.
	const float Blow = bLimp ? 1.f : 1.f - BlowSoftness;
	const float Soft = (bRising && !bLimp ? RisingMuscleShare : 1.f) * Blow;
	const float Trunk = Soft * (bLimp ? 1.f : TrunkMuscleShare);
	Apply(SpineControls, SpineHz * Body * Trunk, 0.f);
	Apply(HeadControls, HeadStrength * Body * Trunk, 0.f);
	Apply(LeftLegControls, LegStrength * LegBody * (bBroken[2] && !bLimp ? BrokenShare : 1.f), 0.f);
	Apply(RightLegControls, LegStrength * LegBody * (bBroken[3] && !bLimp ? BrokenShare : 1.f), 0.f);
	const float ArmHz = CVarAedirBodyArm.GetValueOnGameThread() >= 0.f ? CVarAedirBodyArm.GetValueOnGameThread() : ArmStrength;
	// A broken arm hangs as a limp body's arm does, the muscle in it still there (LimpShare, its forearm and wrist slacker
	// as a limp one's); a hurt one keeps at least some of its strength.
	const float LimpArm = FMath::Lerp(Limp, HoistArmShare, Hauled);
	const float LeftArmHz = ArmHz * (bLimp ? LimpArm : bBroken[0] ? LimpShare : FMath::Lerp(0.3f, 1.f, LeftArmShare) * LeftLoadShare) * (bArmBoost[0] ? ArmFreeBoost : 1.f);
	const float RightArmHz = ArmHz * (bLimp ? LimpArm : bBroken[1] ? LimpShare : FMath::Lerp(0.3f, 1.f, RightArmShare) * RightLoadShare) * (bArmBoost[1] ? ArmFreeBoost : 1.f);
	Apply(LeftArmControls, LeftArmHz * Soft, 0.f);
	Apply(RightArmControls, RightArmHz * Soft, 0.f);
	// A broken arm's shoulder still holds it hanging.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (bBroken[Side] && !bLimp && !ShoulderControls[Side].IsNone())
		{
			Apply({ ShoulderControls[Side] }, ArmHz * BrokenShoulderShare, 0.f);
		}
	}
	// The wrists are looser than the arm above them, less so with both hands on one thing.
	const float Wrist = FMath::Lerp(WristShare, TwoHandWristShare, TwoHandGrip);
	// Down or dead, the elbows and the wrists go slacker still than the rest of the arm.
	const float Slack[2] = { bLimp || bBroken[0] ? LimpForearmShare : 1.f, bLimp || bBroken[1] ? LimpForearmShare : 1.f };
	Apply(LeftHandControls, LeftArmHz * Wrist * Slack[0], 0.f);
	Apply(RightHandControls, RightArmHz * Wrist * Slack[1], 0.f);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (bLimp || bBroken[Side])
		{
			Apply(ForearmControls[Side], (Side == 0 ? LeftArmHz : RightArmHz) * Soft * Slack[Side], 0.f);
		}
	}
	// Down, the hips are free; carried, the pose places them and the control rests.
	const bool bHold = !bLimp && !bHipsCarried;
	const float Hips = CVarAedirBodyHips.GetValueOnGameThread() >= 0.f ? CVarAedirBodyHips.GetValueOnGameThread() : HipLinearStrength;
	// Rising, the hips are lifted along the moves by the muscles, as hard as the body has it in it, never carried.
	const float Rise = CVarAedirBodyRise.GetValueOnGameThread() >= 0.f ? CVarAedirBodyRise.GetValueOnGameThread() : RiseHipShare;
	const float HipShare = bRising ? Rise * Body : Body;
	Apply({ HipControl }, bHold ? HipAngularStrength * HipShare : 0.f, bHold ? Hips * HipShare : 0.f);
	Control->SetControlEnabled(HipControl, bHold);

	// The trunk is kept upright as a whole, and standing it carries only part of its weight.
	const float Posture = CVarAedirBodyPosture.GetValueOnGameThread() >= 0.f ? CVarAedirBodyPosture.GetValueOnGameThread() : PostureStrength;
	Apply(PostureControls, bLimp ? 0.f : Posture * Body * Soft, 0.f);
	for (const FName& Name : PostureControls)
	{
		Control->SetControlEnabled(Name, !bLimp && Posture > 0.f);
	}
	const float Foot = CVarAedirBodyFoot.GetValueOnGameThread() >= 0.f ? CVarAedirBodyFoot.GetValueOnGameThread() : FootStrength;
	Apply(FootControls, bLimp ? 0.f : Foot * Body, bLimp ? 0.f : Foot * Body);
	// A kicking foot is thrown along its arc by the whole leg, much harder than a step sets a foot down.
	if (!bLimp && KickLeg >= 0)
	{
		Apply({ KickLeg == 0 ? LeftFootControl : RightFootControl }, Foot * Body * KickFootShare, Foot * Body * KickFootShare);
	}
	for (const FName& Name : FootControls)
	{
		Control->SetControlEnabled(Name, !bLimp && Foot > 0.f);
	}
	const float Weight = CVarAedirBodyGravity.GetValueOnGameThread() >= 0.f ? CVarAedirBodyGravity.GetValueOnGameThread() : StandingGravity;
	// The feet carry part of the weight only while they stand on something; in the air it all falls alike.
	// Getting up the body has the weight of a body lying down to lift: the muscles bring up all of it, not a lightened one.
	const float Carried = bRising ? FMath::Lerp(1.f, Weight, Upright * (1.f - RisingWeightShare)) : FMath::Lerp(1.f, Weight, Upright);
	Control->SetBodyModifiersInSetGravityMultiplier(TEXT("Body"), bLimp || bAirborne ? 1.f : Carried);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (bArmLoose[Side])
		{
			Control->SetBodyModifiersGravityMultiplier(ArmModifiers[Side], 1.f);
		}
	}
}

void UAedirBodyControlComponent::SetArmsPosed(bool bLeft, bool bRight)
{
	const bool Wanted[2] = { bLeft, bRight };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!Control || bWholePosed || bArmPosed[Side] == Wanted[Side] || bArmLoose[Side])
		{
			continue;
		}
		bArmPosed[Side] = Wanted[Side];
		// The arm is shown as the pose has it; its bodies still simulate under it, held to the pose by their muscles, for
		// what they strike and are struck by. Made kinematic instead they were moved by two systems at once and ran off.
		for (const FName& Modifier : ArmModifiers[Side])
		{
			Control->SetBodyModifierPhysicsBlendWeight(Modifier, Wanted[Side] ? 0.f : 1.f);
		}
	}
}


void UAedirBodyControlComponent::SetWholePosed(bool bPosed)
{
	if (!Control || bWholePosed == bPosed)
	{
		return;
	}
	bWholePosed = bPosed;
	Control->SetBodyModifiersInSetMovementType(TEXT("Body"), bPosed ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
	Control->SetBodyModifiersInSetPhysicsBlendWeight(TEXT("Body"), bPosed ? 0.f : 1.f);
	// The hips and the feet have modifiers of their own, held kinematic by their own states when physics comes back.
	TArray<FName> Own = { HipModifier, LeftFootModifiers[0], LeftFootModifiers[1], RightFootModifiers[0], RightFootModifiers[1] };
	for (const FName& Modifier : Own)
	{
		if (Modifier.IsNone())
		{
			continue;
		}
		const bool bOwnKinematic = Modifier == HipModifier ? bHipsCarried : (Modifier == LeftFootModifiers[0] || Modifier == LeftFootModifiers[1] ? bLeftPlanted : bRightPlanted);
		Control->SetBodyModifierMovementType(Modifier, bPosed || bOwnKinematic ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
		Control->SetBodyModifierPhysicsBlendWeight(Modifier, bPosed ? 0.f : 1.f);
	}
	// Back to physics, the arms take up their own posing again.
	bArmPosed[0] = bArmPosed[1] = false;
	// A broken arm still hangs loose.
	bArmLoose[0] = bArmLoose[1] = false;
	ApplyBrokenArms();
}
