#include "Combat/AedirCombatComponent.h"

#include "Aedir.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Physics/AedirBalanceComponent.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAedirHeldWeight(TEXT("aedir.HeldWeight"), 1, TEXT("Let the weight of a held weapon pull on the hand."));
	TAutoConsoleVariable<int32> CVarAedirGripJoint(TEXT("aedir.GripJoint"), 1, TEXT("Hold the second hand on a weapon with a physical joint."));

	// A held weapon is not part of the arm: its weight and the force it takes to swing it pull on the hand that holds it.
	constexpr float HeldAccelSmoothing = 25.f;
	constexpr float HeldMaxAccel = 6000.f;
	// What a grip can hold against, in N m of twist at the wrist: a weak hand a little, a strong one much more, two hands more still.
	constexpr float HeldGripBaseNm = 40.f;
	constexpr float HeldGripPerStrengthNm = 0.45f;
	constexpr float HeldGripTiredShare = 0.6f;
	constexpr float HeldGripTwoHandShare = 1.8f;
	constexpr float HeldStrainSeconds = 0.25f;
	constexpr float HeldOneHandedTwoHanderShare = 1.8f;
	constexpr float HeldStrainDecay = 2.f;
	// The second hand on a weapon is pulled onto its grip and held there, and lets go when it is torn off.
	constexpr float GripJointMinAlpha = 0.3f;
	constexpr float GripJointPalmShare = 0.7f;
	// The same reach rule the pose uses for the second hand (see the procedural anim instance): the palm reaches this much
	// past the wrist, the hands stay at least this far apart, and the hand moves back this far at a time.
	constexpr double GripJointPalmReachCm = 4.0;
	constexpr double GripJointMinHandsApartCm = 12.0;
	constexpr double GripJointSlideStepCm = 2.0;
	constexpr float GripJointStiffness = 300000.f;
	constexpr float GripJointDamping = 9000.f;
	constexpr float GripJointTearCm = 14.f;
	constexpr float GripJointBreakSeconds = 0.3f;
	constexpr float GripLostSecondsAfterTear = 0.6f;
}

void UAedirCombatComponent::UpdateHeldWeight(float DeltaTime)
{
	USkeletalMeshComponent* Body = Creature ? Creature->GetMesh() : nullptr;
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	if (!Body || !Interaction || DeltaTime <= KINDA_SMALL_NUMBER || CVarAedirHeldWeight.GetValueOnGameThread() == 0)
	{
		return;
	}

	const UAedirStatsComponent* Stats = Creature->Stats.Get();
	const float Strength = Stats ? Stats->Strength : 10.f;
	const float StaminaShare = Stats ? Stats->GetStaminaFraction() : 1.f;
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.f;

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirHeldWeight& State = HeldWeight[Index];
		AAedirItemActor* Item = GetHeldItem(Hand);
		const FName HandBone = Hand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
		FBodyInstance* HandBody = Body->GetBodyInstance(HandBone);
		// The second hand of a two-handed hold takes its share from the first; a big object in both arms is carried, not gripped.
		if (!Item || Item->ItemClass == EAedirItemClass::Object || Item->bWorn || Item->bHolstered || IsSecondaryGripHand(Hand) || !HandBody || !HandBody->IsInstanceSimulatingPhysics())
		{
			State = FAedirHeldWeight();
			continue;
		}

		const FVector Center = Item->GetBoundsCenterWorld();
		if (State.Item.Get() != Item)
		{
			State = FAedirHeldWeight();
			State.Item = Item;
			State.LastCenter = Center;
			continue;
		}

		// How the arm means to move it, not how the body happened to wobble this frame: reading the wobble back would feed it.
		const FVector Velocity = GetArm(Hand).HandVelocity;
		const FVector Accel = State.bHasVelocity ? (Velocity - State.LastVelocity) / DeltaTime : FVector::ZeroVector;
		State.Accel = FMath::Lerp(State.Accel, Accel.GetClampedToMaxSize(HeldMaxAccel), 1.f - FMath::Exp(-HeldAccelSmoothing * DeltaTime));
		State.LastCenter = Center;
		State.LastVelocity = Velocity;
		State.bHasVelocity = true;

		// What it takes to hold it up and to move it, applied where its weight is, so a long blade twists the wrist as well.
		const bool bShared = bTwoHandGrip && GetHeldItem(Hand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left) == Item;
		// A weapon made for two hands, held out in one, hangs far from the wrist and pulls it down much harder.
		const float Leverage = Item->bTwoHanded && !bShared ? HeldOneHandedTwoHanderShare : 1.f;
		const FVector Force = (FVector(0.f, 0.f, GravityZ) - State.Accel) * (Item->Weight * Leverage);
		const float Share = bShared ? 0.5f : 1.f;
		// A weapon made part of the hand already weighs on it through the physics; only a loose one is added by hand here.
		const UPrimitiveComponent* ItemBody = Cast<UPrimitiveComponent>(Item->GetRootComponent());
		const bool bWelded = ItemBody && ItemBody->IsWelded();
		if (!bWelded)
		{
			Body->AddForceAtLocation(Force * Share, Center, HandBone);
		}
		if (bShared && !bWelded)
		{
			const FName OtherBone = Hand == EAedirHand::Left ? Interaction->RightHandBone : Interaction->LeftHandBone;
			Body->AddForceAtLocation(Force * (1.f - Share), Center, OtherBone);
		}

		// A grip that cannot take the twist for a moment lets the weapon go.
		const FVector Lever = Center - Body->GetBoneLocation(HandBone);
		const float TwistNm = static_cast<float>(FVector::CrossProduct(Lever, Force).Size()) / 10000.f;
		const float HoldNm = (HeldGripBaseNm + Strength * HeldGripPerStrengthNm) * FMath::Lerp(HeldGripTiredShare, 1.f, StaminaShare) * (bShared ? HeldGripTwoHandShare : 1.f);
		State.StrainSeconds = TwistNm > HoldNm ? State.StrainSeconds + DeltaTime : FMath::Max(State.StrainSeconds - HeldStrainDecay * DeltaTime, 0.f);
		if (State.StrainSeconds >= HeldStrainSeconds && IsAuthoritative())
		{
			UE_LOG(LogAedir, Log, TEXT("%s could not hold %s: %.0f N m against a grip of %.0f"), *Creature->GetName(), *Item->GetName(), TwistNm, HoldNm);
			State = FAedirHeldWeight();
			Creature->Interaction->DropHeld(Hand);
		}
	}
}

void UAedirCombatComponent::LoseGrip(float Seconds)
{
	GripLostSeconds = FMath::Max(GripLostSeconds, Seconds);
	ReleaseGripJoint();
}

void UAedirCombatComponent::ReleaseGripJoint()
{
	if (UPhysicsConstraintComponent* Joint = GripJoint.Get())
	{
		Joint->BreakConstraint();
		Joint->DestroyComponent();
	}
	GripJoint.Reset();
	GripJointStrain = 0.f;
}

void UAedirCombatComponent::UpdateGripJoint(float DeltaTime)
{
	GripLostSeconds = FMath::Max(GripLostSeconds - DeltaTime, 0.f);

	USkeletalMeshComponent* Body = Creature ? Creature->GetMesh() : nullptr;
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	EAedirHand OtherHand = EAedirHand::Left;
	FVector PointInPrimary;
	FVector BladeInPrimary;
	float Alpha = 0.f;
	const bool bRagdoll = Creature && Creature->Balance && Creature->Balance->IsRagdoll();
	const bool bWanted = CVarAedirGripJoint.GetValueOnGameThread() != 0 && Body && Interaction && !bRagdoll && GetSecondaryGrip(OtherHand, PointInPrimary, BladeInPrimary, Alpha) && Alpha >= GripJointMinAlpha;
	const EAedirHand PrimaryHand = OtherHand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	const AAedirItemActor* Item = bWanted ? GetHeldItem(PrimaryHand) : nullptr;
	if (!bWanted || !Item || Item->ItemClass != EAedirItemClass::Weapon)
	{
		if (GripJoint.IsValid() || GripJointStrain > 0.f)
		{
			ReleaseGripJoint();
		}
		return;
	}

	const FName PrimaryBone = PrimaryHand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
	const FName OtherBone = OtherHand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
	const FBodyInstance* PrimaryBody = Body->GetBodyInstance(PrimaryBone);
	const FBodyInstance* OtherBody = Body->GetBodyInstance(OtherBone);
	if (!PrimaryBody || !OtherBody || !PrimaryBody->IsInstanceSimulatingPhysics() || !OtherBody->IsInstanceSimulatingPhysics())
	{
		return;
	}

	// The palm, not the wrist, is what closes on the weapon.
	const TCHAR* Suffix = OtherHand == EAedirHand::Left ? TEXT("_l") : TEXT("_r");
	const FTransform OtherWorld = Body->GetBoneTransform(Body->GetBoneIndex(OtherBone));
	const FVector PalmWorld = FMath::Lerp(OtherWorld.GetLocation(), Body->GetBoneLocation(FName(*FString::Printf(TEXT("middle_01%s"), Suffix))), GripJointPalmShare);
	const FTransform PrimaryWorld = Body->GetBoneTransform(Body->GetBoneIndex(PrimaryBone));
	FVector TargetWorld = PrimaryWorld.TransformPosition(PointInPrimary);

	// The pose takes hold nearer the carrying hand where the arm cannot reach the place on the weapon; the spring pulls to
	// that same nearer place, or the two would pull the hand back and forth between them and set both arms shaking.
	{
		const FVector Shoulder = Body->GetBoneLocation(FName(*FString::Printf(TEXT("upperarm%s"), Suffix)));
		const FVector Elbow = Body->GetBoneLocation(FName(*FString::Printf(TEXT("lowerarm%s"), Suffix)));
		const double ArmCm = FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, OtherWorld.GetLocation()) + GripJointPalmReachCm;
		const FVector BladeWorld = PrimaryWorld.TransformVectorNoScale(BladeInPrimary).GetSafeNormal();
		double Slide = FVector::DotProduct(TargetWorld - PrimaryWorld.GetLocation(), BladeWorld) - GripJointMinHandsApartCm;
		while (Slide > 0.0 && FVector::Dist(TargetWorld, Shoulder) > ArmCm)
		{
			TargetWorld -= BladeWorld * GripJointSlideStepCm;
			PointInPrimary -= BladeInPrimary.GetSafeNormal() * GripJointSlideStepCm;
			Slide -= GripJointSlideStepCm;
		}
	}

	// A spring from the palm to its place on the weapon: the hand is pulled onto the grip and kept there wherever the arms go,
	// as the place itself moves when the hand slides up the blade.
	if (!GripJoint.IsValid())
	{
		UPhysicsConstraintComponent* Joint = NewObject<UPhysicsConstraintComponent>(Creature);
		Joint->SetWorldLocation(TargetWorld);
		Joint->SetDisableCollision(true);
		Joint->SetLinearXLimit(LCM_Free, 0.f);
		Joint->SetLinearYLimit(LCM_Free, 0.f);
		Joint->SetLinearZLimit(LCM_Free, 0.f);
		Joint->SetAngularSwing1Limit(ACM_Free, 0.f);
		Joint->SetAngularSwing2Limit(ACM_Free, 0.f);
		Joint->SetAngularTwistLimit(ACM_Free, 0.f);
		Joint->SetLinearPositionDrive(true, true, true);
		Joint->SetLinearVelocityDrive(true, true, true);
		Joint->RegisterComponent();
		Joint->SetConstrainedComponents(Body, PrimaryBone, Body, OtherBone);
		Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2, OtherWorld.InverseTransformPosition(PalmWorld));
		GripJoint = Joint;
		GripJointStrain = 0.f;
	}
	GripJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1, PointInPrimary);
	GripJoint->SetLinearPositionTarget(FVector::ZeroVector);
	GripJoint->SetLinearVelocityTarget(FVector::ZeroVector);
	GripJoint->SetLinearDriveParams(GripJointStiffness * Alpha, GripJointDamping * Alpha, 0.f);

	// Torn away by a blow or by the arms pulling apart: the hand lets go for a moment before it takes hold again.
	const float Gap = static_cast<float>(FVector::Dist(PalmWorld, TargetWorld));
	GripJointStrain = Gap > GripJointTearCm && Alpha >= 1.f ? GripJointStrain + DeltaTime : 0.f;
	if (GripJointStrain >= GripJointBreakSeconds)
	{
		UE_LOG(LogAedir, Log, TEXT("%s lost the second hand on %s, %.0f cm off"), *Creature->GetName(), *Item->GetName(), Gap);
		LoseGrip(GripLostSecondsAfterTear);
	}
}

float UAedirCombatComponent::GetGripJointForce() const
{
	if (!GripJoint.IsValid())
	{
		return -1.f;
	}
	FVector LinearForce;
	FVector AngularForce;
	GripJoint->GetConstraintForce(LinearForce, AngularForce);
	return static_cast<float>(LinearForce.Size());
}
