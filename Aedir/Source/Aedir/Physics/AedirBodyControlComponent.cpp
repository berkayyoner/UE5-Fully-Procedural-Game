#include "Physics/AedirBodyControlComponent.h"

#include "Aedir.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"

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
	HeadControls.Reset();
	LeftArmControls.Reset();
	RightArmControls.Reset();
	LegControls.Reset();

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
		const FName Name = Control->CreateControl(Mesh, Parent, Mesh, Bone, Muscle, FPhysicsControlTarget(), TEXT("Muscles"));
		switch (Part)
		{
		case EAedirBodyPart::Head: HeadControls.Add(Name); break;
		case EAedirBodyPart::LeftArm: LeftArmControls.Add(Name); break;
		case EAedirBodyPart::RightArm: RightArmControls.Add(Name); break;
		case EAedirBodyPart::Leg: LegControls.Add(Name); break;
		default: SpineControls.Add(Name); break;
		}
	}

	// The hips are held where the pose has them on the capsule, in place and in turn.
	FPhysicsControlData Hold = Muscle;
	Hold.LinearStrength = 1.f;
	Hold.MaxForce = 0.f;
	HipControl = Control->CreateControl(InCapsule, NAME_None, Mesh, TEXT("pelvis"), Hold, FPhysicsControlTarget(), TEXT("Hips"));

	bReady = !HipControl.IsNone();
	AppliedKey = -1.f;
	ApplyMultipliers();
	UE_LOG(LogAedir, Log, TEXT("%s: physical body with %d spine, %d head, %d+%d arm and %d leg muscles."), *GetNameSafe(GetOwner()),
		SpineControls.Num(), HeadControls.Num(), LeftArmControls.Num(), RightArmControls.Num(), LegControls.Num());
}

void UAedirBodyControlComponent::SetStrengths(float InUpright, float LeftArm, float RightArm)
{
	Upright = FMath::Clamp(InUpright, 0.f, 1.f);
	LeftArmShare = FMath::Clamp(LeftArm, 0.f, 1.f);
	RightArmShare = FMath::Clamp(RightArm, 0.f, 1.f);
	ApplyMultipliers();
}

void UAedirBodyControlComponent::SetLimp(bool bInLimp)
{
	if (bLimp != bInLimp)
	{
		bLimp = bInLimp;
		AppliedKey = -1.f;
		ApplyMultipliers();
	}
}

void UAedirBodyControlComponent::SetHipsCarried(bool bCarried)
{
	if (bHipsCarried != bCarried && Control && !HipModifier.IsNone())
	{
		bHipsCarried = bCarried;
		Control->SetBodyModifierMovementType(HipModifier, bCarried ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
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
	const bool bWantLeft = bLeft && !bLimp;
	const bool bWantRight = bRight && !bLimp;
	if (bWantLeft != bLeftPlanted)
	{
		bLeftPlanted = bWantLeft;
		for (const FName& Modifier : LeftFootModifiers)
		{
			if (!Modifier.IsNone())
			{
				Control->SetBodyModifierMovementType(Modifier, bWantLeft ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
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
				Control->SetBodyModifierMovementType(Modifier, bWantRight ? EPhysicsMovementType::Kinematic : EPhysicsMovementType::Simulated);
			}
		}
	}
}

void UAedirBodyControlComponent::Teleported()
{
	if (Control && bReady)
	{
		Control->UpdateTargetCaches(0.f);
		Control->SetCachedBoneVelocitiesToZero();
		Control->ResetBodyModifiersInSetToCachedBoneTransforms(TEXT("All"));
	}
}

void UAedirBodyControlComponent::ApplyMultipliers()
{
	if (!Control || !bReady)
	{
		return;
	}
	// Only when something changed enough to matter.
	const float Key = (bLimp ? 1000.f : 0.f) + (bHipsCarried ? 2000.f : 0.f) + FMath::RoundToFloat(Upright * 50.f) + FMath::RoundToFloat(LeftArmShare * 50.f) * 0.01f + FMath::RoundToFloat(RightArmShare * 50.f) * 0.0001f;
	if (FMath::IsNearlyEqual(Key, AppliedKey))
	{
		return;
	}
	AppliedKey = Key;

	const float Body = bLimp ? LimpShare : FMath::Lerp(0.35f, 1.f, Upright);
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
	Apply(SpineControls, SpineStrength * Body, 0.f);
	Apply(HeadControls, HeadStrength * Body, 0.f);
	Apply(LegControls, LegStrength * Body, 0.f);
	Apply(LeftArmControls, ArmStrength * (bLimp ? LimpShare : FMath::Lerp(0.3f, 1.f, LeftArmShare)), 0.f);
	Apply(RightArmControls, ArmStrength * (bLimp ? LimpShare : FMath::Lerp(0.3f, 1.f, RightArmShare)), 0.f);
	// Down, the hips are free; carried, the pose places them and the control rests.
	const bool bHold = !bLimp && !bHipsCarried;
	Apply({ HipControl }, bHold ? HipAngularStrength * Body : 0.f, bHold ? HipLinearStrength * Body : 0.f);
	Control->SetControlEnabled(HipControl, bHold);
}
