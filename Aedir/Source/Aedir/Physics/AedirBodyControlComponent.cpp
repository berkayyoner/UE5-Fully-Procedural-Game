#include "Physics/AedirBodyControlComponent.h"

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
		// Standing, the legs are set on the ground by the steps: the floor, a step's edge or a weapon hanging at the side
		// would only push them out of place. Down, they lie on the ground like everything else.
		FPhysicsControlModifierData PartData = Simulated;
		if (Part == EAedirBodyPart::Leg)
		{
			PartData.CollisionType = bLimp || bRising ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly;
		}
		const FName Modifier = Control->CreateBodyModifier(Mesh, Bone, TEXT("Body"), PartData);
		if (Part == EAedirBodyPart::Leg)
		{
			LegModifiers.Add(Modifier);
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
		const FName Name = Control->CreateControl(Mesh, Parent, Mesh, Bone, Muscle, FPhysicsControlTarget(), TEXT("Muscles"));
		if (BoneText == TEXT("foot_l") || BoneText == TEXT("foot_r"))
		{
			// A foot finds where the pose sets it down, in the world: the leg's joints alone carry it too late.
			FPhysicsControlData Step = Muscle;
			Step.LinearStrength = 1.f;
			FootControls.Add(Control->CreateControl(nullptr, NAME_None, Mesh, Bone, Step, FPhysicsControlTarget(), TEXT("Feet")));
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
		case EAedirBodyPart::LeftArm: LeftArmControls.Add(Name); break;
		case EAedirBodyPart::RightArm: RightArmControls.Add(Name); break;
		case EAedirBodyPart::Leg: LegControls.Add(Name); break;
		default: SpineControls.Add(Name); break;
		}
	}

	HipData = Muscle;
	HipData.LinearStrength = 1.f;
	HipData.MaxForce = 0.f;
	HipControl = NAME_None;
	RebindHips(InCapsule);
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
	// Down, or pushing up off the ground on hands and knees, the legs bear on it; standing, the steps set them.
	const ECollisionEnabled::Type Contact = bLimp || bRising ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly;
	for (const FName& Modifier : LegModifiers)
	{
		Control->SetBodyModifierCollisionType(Modifier, Contact);
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
	const float Key = (bLimp ? 1000.f : 0.f) + (bHipsCarried ? 2000.f : 0.f) + (bAirborne ? 4000.f : 0.f) + (bRising ? 8000.f : 0.f) + FMath::RoundToFloat(Upright * 50.f) + FMath::RoundToFloat(LeftArmShare * 50.f) * 0.01f + FMath::RoundToFloat(RightArmShare * 50.f) * 0.0001f;
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
	const float Hips = CVarAedirBodyHips.GetValueOnGameThread() >= 0.f ? CVarAedirBodyHips.GetValueOnGameThread() : HipLinearStrength;
	// Rising, the hips are lifted along the moves by the muscles, as hard as the body has it in it, never carried.
	const float Rise = CVarAedirBodyRise.GetValueOnGameThread() >= 0.f ? CVarAedirBodyRise.GetValueOnGameThread() : RiseHipShare;
	const float HipShare = bRising ? Rise * Body : Body;
	Apply({ HipControl }, bHold ? HipAngularStrength * HipShare : 0.f, bHold ? Hips * HipShare : 0.f);
	Control->SetControlEnabled(HipControl, bHold);

	// The trunk is kept upright as a whole, and standing it carries only part of its weight.
	const float Posture = CVarAedirBodyPosture.GetValueOnGameThread() >= 0.f ? CVarAedirBodyPosture.GetValueOnGameThread() : PostureStrength;
	Apply(PostureControls, bLimp ? 0.f : Posture * Body, 0.f);
	for (const FName& Name : PostureControls)
	{
		Control->SetControlEnabled(Name, !bLimp && Posture > 0.f);
	}
	const float Foot = CVarAedirBodyFoot.GetValueOnGameThread() >= 0.f ? CVarAedirBodyFoot.GetValueOnGameThread() : FootStrength;
	Apply(FootControls, bLimp ? 0.f : Foot * Body, bLimp ? 0.f : Foot * Body);
	for (const FName& Name : FootControls)
	{
		Control->SetControlEnabled(Name, !bLimp && Foot > 0.f);
	}
	const float Weight = CVarAedirBodyGravity.GetValueOnGameThread() >= 0.f ? CVarAedirBodyGravity.GetValueOnGameThread() : StandingGravity;
	// The feet carry part of the weight only while they stand on something; in the air it all falls alike.
	Control->SetBodyModifiersInSetGravityMultiplier(TEXT("Body"), bLimp || bAirborne ? 1.f : FMath::Lerp(1.f, Weight, Upright));
}
