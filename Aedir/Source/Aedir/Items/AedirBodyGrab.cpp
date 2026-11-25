#include "Items/AedirInteractionComponent.h"

#include "Aedir.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/AedirCombatComponent.h"
#include "Fists/AedirFistArms.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AedirItemActor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace
{
	// A free hand closes on whatever body or loose part of one is in front of it: an arm, a collar, a cut-off leg.
	constexpr float BodyGrabForwardCm = 55.f;
	constexpr float BodyGrabHeightCm = 20.f;
	constexpr float BodyGrabRadiusCm = 50.f;
	constexpr float BodyGrabLowCm = -80.f;
	constexpr float BodyGrabLowRadiusCm = 75.f;
	constexpr float BodyGrabWideForwardCm = 70.f;
	constexpr float BodyGrabWideHeightCm = -40.f;
	constexpr float BodyGrabWideRadiusCm = 140.f;
	// The hand is kept on what it holds by no more than this: it is the arm as a whole that carries it (see the carry), and a
	// hand that cannot come up with what it holds, held hard to it, held it down with all the arm's muscles.
	constexpr float BodyGrabHandStiffness = 150.f;
	constexpr float BodyGrabHandDamping = 25.f;
	// Holding on: how far what is held must be behind before the holder is held back by it, and by how much all the way.
	constexpr float BodyGrabDragFromCm = 8.f;
	constexpr float BodyGrabDragFullCm = 30.f;
	constexpr float BodyGrabGravityCm = 981.f;
	// A living body pulled by the hand: how hard it is drawn after the hand, and how much of its balance that costs.
	constexpr float BodyGrabLivePullPerCm = 22.f;
	constexpr float BodyGrabLivePullMax = 420.f;
	constexpr float BodyGrabLiveBalancePerSecond = 0.35f;
	// How much of its going away from the hand a living body held loses, all of the hold's share.
	constexpr float BodyGrabHoldBackShare = 0.9f;
	constexpr float BodyGrabCloseCm = 28.f;
	constexpr float BodyGrabReachSeconds = 2.f;
	constexpr float BodyGrabAimRangeCm = 230.f;
	constexpr float BodyGrabAimRadiusCm = 20.f;
	// A throat within this reach is taken first; one hand on it takes this many points of the senses a second from an even
	// match, so it takes several seconds to choke a man senseless and as long again to kill him.
	constexpr float BodyGrabThroatCm = 26.f;
	constexpr float BodyGrabChokePerSecond = 14.f;
	constexpr float BodyGrabChokeKillPerSecond = 9.f;
}

bool UAedirInteractionComponent::FindAimedBody(FVector& OutPoint) const
{
	// What the eyes are on, if it is a body or a piece of one within arm's reach of a step.
	const AAedirCreature* Creature = GetCreature();
	const UWorld* World = GetWorld();
	if (!Creature || !World)
	{
		return false;
	}
	const FVector From = Creature->GetPawnViewLocation();
	const FVector Aim = (Creature->Controller ? Creature->GetControlRotation() : Creature->GetActorRotation()).Vector();
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirBodyAim), false, Creature);
	// The eyes stop at the ground or a wall where they meet it, but the hand is not as narrow as a look: a head lying on
	// the ground looked at counts, though a sweep the width of a hand meets the ground round it first. Only what is
	// behind the wall the look itself ends on is out of reach.
	FHitResult Wall;
	const double WallAt = World->LineTraceSingleByObjectType(Wall, From, From + Aim * BodyGrabAimRangeCm, FCollisionObjectQueryParams(ECC_WorldStatic), Params) ? Wall.Distance : BodyGrabAimRangeCm;
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, From + Aim * BodyGrabAimRangeCm, FQuat::Identity, Objects, FCollisionShape::MakeSphere(BodyGrabAimRadiusCm), Params);
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Component || Hit.Distance > WallAt + BodyGrabAimRadiusCm)
		{
			continue;
		}
		// A body's capsule stands for the body inside it.
		const AActor* Owner = Component->GetOwner();
		if (Owner && !Cast<AAedirItemActor>(Owner) && (Component->IsAnySimulatingPhysics() || Cast<AAedirCreature>(Owner)))
		{
			OutPoint = Hit.ImpactPoint;
			return true;
		}
	}
	return false;
}

AAedirCreature* UAedirInteractionComponent::GetGrabbedCreature(EAedirHand Hand, FName* OutBone) const
{
	const FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	const UPrimitiveComponent* Target = Grab.Target.Get();
	if (!(Grab.Joint.IsValid() || Grab.bReaching) || !Target)
	{
		return nullptr;
	}
	if (OutBone)
	{
		*OutBone = Grab.Bone;
	}
	return Cast<AAedirCreature>(Target->GetOwner());
}

AAedirItemActor* UAedirInteractionComponent::FindWornOn(FName Bone) const
{
	const EAedirBodyZone Zone = UAedirCombatComponent::ClassifyBodyZone(Bone);
	for (const TObjectPtr<AAedirItemActor>& Worn : WornItems)
	{
		if (Worn && Worn->ArmorZone == Zone)
		{
			return Worn;
		}
	}
	return nullptr;
}

void UAedirInteractionComponent::TakeOffWornLoose(AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	const int32 WornIndex = WornItems.IndexOfByKey(Item);
	if (!IsAuthoritative() || !Creature || !Item || WornIndex == INDEX_NONE)
	{
		return;
	}
	WornItems.RemoveAt(WornIndex);
	Item->TakeOff();
	Item->Detach(FVector::ZeroVector);
	if (Creature->Combat)
	{
		Creature->Combat->SetArmorProtection(Item->ArmorZone, GetWornArmorProtection(Item->ArmorZone));
	}
	UpdateCarriedWeight();
}

bool UAedirInteractionComponent::StripGrabbedArmor(EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerStripGrabbedArmor(Hand);
		return true;
	}
	FName Bone;
	AAedirCreature* Body = GetGrabbedCreature(Hand, &Bone);
	AAedirCreature* Creature = GetCreature();
	if (!Body || !Creature || !Body->Interaction || !(Body->Stats && Body->Stats->IsDead()))
	{
		return false;
	}
	// Holding it, or with the hand on it as it closes.
	const FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	if (!Grab.Joint.IsValid() && Creature->GetMesh() && Body->GetMesh())
	{
		const float Off = static_cast<float>(FVector::Dist(Creature->GetMesh()->GetBoneLocation(Hand == EAedirHand::Left ? LeftHandBone : RightHandBone), Body->GetMesh()->GetBoneLocation(Bone)));
		if (Off > StripReachCm)
		{
			UE_LOG(LogAedir, Log, TEXT("[Strip] %s's hand is still %.0f cm from %s's %s"), *Creature->GetName(), Off, *Body->GetName(), *Bone.ToString());
			return false;
		}
	}
	AAedirItemActor* Piece = Body->Interaction->FindWornOn(Bone);
	if (!Piece)
	{
		UE_LOG(LogAedir, Log, TEXT("[Strip] %s finds nothing worn on %s's %s"), *Creature->GetName(), *Body->GetName(), *Bone.ToString());
		return false;
	}
	// Let go of the body, the piece comes off it into that hand.
	ReleaseBodyGrab(Hand);
	Body->Interaction->TakeOffWornLoose(Piece);
	const bool bTaken = EquipImmediate(Hand, Piece);
	UE_LOG(LogAedir, Log, TEXT("[Strip] %s takes %s off %s's %s%s"), *Creature->GetName(), *Piece->GetName(), *Body->GetName(), *Bone.ToString(), bTaken ? TEXT("") : TEXT(", but cannot hold it"));
	return bTaken;
}

void UAedirInteractionComponent::ServerStripGrabbedArmor_Implementation(EAedirHand Hand)
{
	StripGrabbedArmor(Hand);
}

bool UAedirInteractionComponent::IsGrabbingBody(EAedirHand Hand) const
{
	const FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	return Grab.Joint.IsValid() || Grab.bReaching;
}

bool UAedirInteractionComponent::IsHoldingBody(EAedirHand Hand) const
{
	return BodyGrabs[static_cast<int32>(Hand)].Joint.IsValid();
}

bool UAedirInteractionComponent::TryGrabBody(EAedirHand Hand, const FVector* AimedPoint)
{
	AAedirCreature* Creature = GetCreature();
	UWorld* World = GetWorld();
	USkeletalMeshComponent* Own = Creature ? Creature->GetMesh() : nullptr;
	if (!Creature || !World || !Own || !IsAuthoritative() || IsGrabbingBody(Hand))
	{
		return false;
	}

	const FName HandBone = Hand == EAedirHand::Left ? LeftHandBone : RightHandBone;
	const FBodyInstance* HandBody = Own->GetBodyInstance(HandBone);
	if (!HandBody)
	{
		UE_LOG(LogAedir, Log, TEXT("%s has no physical hand to take hold with: %s %s"), *Creature->GetName(), *HandBone.ToString(), HandBody ? TEXT("not simulating") : TEXT("has no body"));
		return false;
	}

	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	// Without anything looked at, the hand feels in front of the chest, and then low in front of the feet for whoever lies there.
	FVector Center = AimedPoint ? *AimedPoint : Creature->GetActorTransform().TransformPositionNoScale(FVector(BodyGrabForwardCm, 0.f, BodyGrabHeightCm) * Scale);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirBodyGrab), false, Creature);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere((AimedPoint ? BodyGrabRadiusCm * 0.6f : BodyGrabRadiusCm) * Scale), Params);
	if (Overlaps.IsEmpty() && !AimedPoint)
	{
		Center = Creature->GetActorTransform().TransformPositionNoScale(FVector(BodyGrabForwardCm, 0.f, BodyGrabLowCm) * Scale);
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(BodyGrabLowRadiusCm * Scale), Params);
	}
	if (Overlaps.IsEmpty() && !AimedPoint)
	{
		// Anything within a step and a bend of the body, the one nearest to where the eyes are turned first.
		Center = Creature->GetActorTransform().TransformPositionNoScale(FVector(BodyGrabWideForwardCm, 0.f, BodyGrabWideHeightCm) * Scale);
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(BodyGrabWideRadiusCm * Scale), Params);
	}
	const FVector Eyes = Creature->GetPawnViewLocation();
	const FVector Look = (Creature->Controller ? Creature->GetControlRotation() : Creature->GetActorRotation()).Vector();

	// The nearest simulated part that is not an item: a body's bone, or a piece cut from one.
	UPrimitiveComponent* Best = nullptr;
	FName BestBone = NAME_None;
	FVector BestPoint = FVector::ZeroVector;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		// A body's root may be held still by its pose while its limbs move freely, so a body counts by its bones.
		if (!Component || Component->GetOwner() == Creature || Cast<AAedirItemActor>(Component->GetOwner()) || (!Cast<USkeletalMeshComponent>(Component) && !Component->IsSimulatingPhysics()))
		{
			continue;
		}

		// A loose part or a prop is taken at the point of its surface nearest to the hand's way, not at its middle.
		FName Bone = NAME_None;
		FVector Point = Component->Bounds.Origin;
		FVector Surface;
		if (!Cast<USkeletalMeshComponent>(Component) && Component->GetClosestPointOnCollision(Center, Surface) >= 0.f)
		{
			Point = Surface;
		}
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			// The closest bone that has a body of its own, walking up from fingers and twist bones.
			Bone = Skeletal->FindClosestBone(Center);
			while (!Bone.IsNone() && !Skeletal->GetBodyInstance(Bone))
			{
				Bone = Skeletal->GetParentBone(Bone);
			}
			// A hand that comes near a living throat takes the throat before anything else.
			const FName Throat(TEXT("neck_01"));
			if (Cast<AAedirCreature>(Skeletal->GetOwner()) && Skeletal->GetBodyInstance(Throat) && FVector::Dist(Skeletal->GetBoneLocation(Throat), Center) < BodyGrabThroatCm * Scale)
			{
				Bone = Throat;
			}
			const FBodyInstance* Part = Bone.IsNone() ? nullptr : Skeletal->GetBodyInstance(Bone);
			// A living body's part may be carried by its pose just now rather than loose; it can be held all the same.
			const AAedirCreature* Living = Cast<AAedirCreature>(Skeletal->GetOwner());
			if (!Part || (!Part->IsInstanceSimulatingPhysics() && !(Living && !(Living->Stats && Living->Stats->IsDead()))))
			{
				continue;
			}
			Point = Skeletal->GetBoneLocation(Bone);
		}

		const double Off = 1.0 - FVector::DotProduct((Point - Eyes).GetSafeNormal(), Look);
		// Bodies and the parts cut from them come before props lying about.
		const bool bBodily = Cast<USkeletalMeshComponent>(Component) || Cast<AAedirCreature>(Component->GetOwner()) || Component->GetOwner()->GetClass() == AActor::StaticClass();
		const double Distance = FVector::DistSquared(Point, Center) * (1.0 + 4.0 * Off) * (bBodily ? 1.0 : 4.0);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Component;
			BestBone = Bone;
			BestPoint = Point;
		}
	}
	if (!Best)
	{
		UE_LOG(LogAedir, Log, TEXT("%s found no body to take hold of among %d nearby"), *Creature->GetName(), Overlaps.Num());
		return false;
	}

	// The hand reaches for it first; it closes only when it gets there.
	const FTransform PartWorld = BestBone.IsNone() ? Best->GetComponentTransform() : Cast<USkeletalMeshComponent>(Best)->GetBoneTransform(Cast<USkeletalMeshComponent>(Best)->GetBoneIndex(BestBone));
	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	Grab = FAedirBodyGrab();
	Grab.Target = Best;
	Grab.Bone = BestBone;
	Grab.LocalPoint = PartWorld.InverseTransformPosition(BestPoint);
	Grab.bReaching = true;
	UE_LOG(LogAedir, Log, TEXT("%s reaches for %s %s"), *Creature->GetName(), *GetNameSafe(Best->GetOwner()), *BestBone.ToString());
	return true;
}

void UAedirInteractionComponent::CloseHandOnBody(EAedirHand Hand)
{
	AAedirCreature* Creature = GetCreature();
	USkeletalMeshComponent* Own = Creature ? Creature->GetMesh() : nullptr;
	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	UPrimitiveComponent* Target = Grab.Target.Get();
	FVector Point;
	if (!Own || !Target || !GetBodyGrabPoint(Hand, Point))
	{
		ReleaseBodyGrab(Hand);
		return;
	}

	const FName HandBone = Hand == EAedirHand::Left ? LeftHandBone : RightHandBone;
	UPhysicsConstraintComponent* Joint = NewObject<UPhysicsConstraintComponent>(Creature);
	Joint->SetWorldLocation(Point);
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
	Joint->SetConstrainedComponents(Own, HandBone, Target, Grab.Bone);
	// The hand's own frame is its palm, the part's is where it was taken hold of.
	Joint->SetConstraintReferencePosition(EConstraintFrame::Frame1, FVector::ZeroVector);
	Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2, Grab.LocalPoint);
	Joint->SetLinearPositionTarget(FVector::ZeroVector);
	Joint->SetLinearVelocityTarget(FVector::ZeroVector);
	// In newtons, whatever the part weighs.
	Joint->SetLinearDriveAccelerationMode(false);
	Joint->SetLinearDriveParams(BodyGrabHandStiffness, BodyGrabHandDamping, 0.f);

	// The arm as a whole carries it: a joint from the holder's own body to where the part was taken hold of, pulled to where
	// the hand would have it.
	UCapsuleComponent* Capsule = Creature->GetCapsuleComponent();
	UPhysicsConstraintComponent* Carry = NewObject<UPhysicsConstraintComponent>(Creature);
	Carry->SetupAttachment(Capsule);
	Carry->SetDisableCollision(true);
	Carry->SetLinearXLimit(LCM_Free, 0.f);
	Carry->SetLinearYLimit(LCM_Free, 0.f);
	Carry->SetLinearZLimit(LCM_Free, 0.f);
	Carry->SetAngularSwing1Limit(ACM_Free, 0.f);
	Carry->SetAngularSwing2Limit(ACM_Free, 0.f);
	Carry->SetAngularTwistLimit(ACM_Free, 0.f);
	Carry->SetLinearPositionDrive(true, true, true);
	Carry->SetLinearVelocityDrive(true, true, true);
	Carry->RegisterComponent();
	Carry->SetConstrainedComponents(Capsule, NAME_None, Target, Grab.Bone);
	Carry->ConstraintInstance.SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
	Carry->ConstraintInstance.SetRefFrame(EConstraintFrame::Frame2, FTransform(Grab.LocalPoint));
	Carry->SetLinearDriveAccelerationMode(false);
	Carry->SetLinearVelocityTarget(FVector::ZeroVector);
	Grab.Carry = Carry;
	Grab.Where = Creature->GetActorTransform().InverseTransformPositionNoScale(Point);
	{
		const bool bLeft = Hand == EAedirHand::Left;
		const FVector Shoulder = Own->GetBoneLocation(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		const FVector Elbow = Own->GetBoneLocation(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
		Grab.BeyondReachCm = FMath::Clamp(static_cast<float>(FVector::Dist(Shoulder, Point) - FVector::Dist(Shoulder, Elbow) - FVector::Dist(Elbow, Own->GetBoneLocation(HandBone))), 0.f, BodyGrabCloseCm);
	}
	Grab.LagCm = 0.f;
	Grab.bLifting = false;
	Grab.LiftSeconds = 0.f;
	Grab.Joint = Joint;
	Grab.bReaching = false;
	Grab.TearSeconds = 0.f;
	UE_LOG(LogAedir, Log, TEXT("%s took hold of %s %s"), *Creature->GetName(), *GetNameSafe(Target->GetOwner()), *Grab.Bone.ToString());
}

void UAedirInteractionComponent::ReleaseBodyGrab(EAedirHand Hand)
{
	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	if (const USkeletalMeshComponent* Let = Grab.Joint.IsValid() ? Cast<USkeletalMeshComponent>(Grab.Target.Get()) : nullptr)
	{
		UE_LOG(LogAedir, Log, TEXT("[Carry] %s lets go of %s by the %s, its middle going %s"), *GetNameSafe(GetOwner()), *GetNameSafe(Let->GetOwner()), *Grab.Bone.ToString(), *Let->GetPhysicsLinearVelocity(TEXT("pelvis")).ToCompactString());
	}
	for (UPhysicsConstraintComponent* Joint : { Grab.Joint.Get(), Grab.Carry.Get() })
	{
		if (Joint)
		{
			Joint->BreakConstraint();
			Joint->DestroyComponent();
		}
	}
	Grab = FAedirBodyGrab();
}

bool UAedirInteractionComponent::GetBodyGrabPoint(EAedirHand Hand, FVector& OutWorld) const
{
	const FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	const UPrimitiveComponent* Target = Grab.Target.Get();
	if ((!Grab.Joint.IsValid() && !Grab.bReaching) || !Target)
	{
		return false;
	}
	const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Target);
	const FTransform PartWorld = Skeletal && !Grab.Bone.IsNone() ? Skeletal->GetBoneTransform(Skeletal->GetBoneIndex(Grab.Bone)) : Target->GetComponentTransform();
	OutWorld = PartWorld.TransformPosition(Grab.LocalPoint);
	return true;
}

void UAedirInteractionComponent::UpdateBodyGrabs(float DeltaTime)
{
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* Own = Creature ? Creature->GetMesh() : nullptr;
	const float Strength = Creature && Creature->Stats ? Creature->Stats->Strength : 10.f;
	const float StaminaShare = Creature && Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAedirBodyGrab& Grab = BodyGrabs[Index];
		if (!Grab.Joint.IsValid() && !Grab.bReaching)
		{
			continue;
		}

		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FVector Point;
		if (!Own || !GetBodyGrabPoint(Hand, Point) || GetHeldItem(Hand))
		{
			ReleaseBodyGrab(Hand);
			continue;
		}

		const FVector HandWorld = Own->GetBoneLocation(Hand == EAedirHand::Left ? LeftHandBone : RightHandBone);
		const float Gap = static_cast<float>(FVector::Dist(Point, HandWorld));

		// Still on its way: it closes on the part when it touches it, and gives up when it cannot get there.
		if (Grab.bReaching)
		{
			Grab.ReachSeconds += DeltaTime;
			if (Gap <= BodyGrabCloseCm)
			{
				CloseHandOnBody(Hand);
			}
			else if (Grab.ReachSeconds >= BodyGrabReachSeconds)
			{
				UE_LOG(LogAedir, Log, TEXT("%s's hand does not get to %s, %.0f cm short"), *Creature->GetName(), *Grab.Bone.ToString(), Gap);
				ReleaseBodyGrab(Hand);
			}
			continue;
		}

		if (Creature->Balance)
		{
			Creature->Balance->LetArmThrough(Index, 0.5f);
			// What lies is taken up over the holder's own feet and held close before the chest.
			if (!IsStandingHeld(Grab))
			{
				Creature->Balance->LetBodyThrough(BodyThroughSeconds);
			}
		}
		// The arm pulls as hard as a body of that strength can, and no harder: a cut-off hand comes up with it, a dead man is
		// dragged along the ground, and with the hand's button held it strains to lift whoever it holds.
		const FVector Place = UpdateBodyCarryPlace(Hand, Point, DeltaTime);
		const float Breath = FMath::Lerp(BodyForceTiredShare, 1.f, StaminaShare);
		const float MostN = (Grab.bLifting ? BodyLiftForceN + Strength * BodyLiftForcePerStrengthN : BodyHoldForceN + Strength * BodyHoldForcePerStrengthN) * Breath;
		const FVector Lag = Place - Point;
		Grab.LagCm = static_cast<float>(Lag.Size());
		Grab.WhereWorld = Place;
		const float Stiffness = MostN * 100.f / FMath::Max(BodyHoldFullCm, 1.f);
		Grab.PullN = MostN * FMath::Min(Grab.LagCm / FMath::Max(BodyHoldFullCm, 1.f), 1.f);
		if (UPhysicsConstraintComponent* Carry = Grab.Carry.Get())
		{
			const FTransform Frame = Creature->GetCapsuleComponent()->GetComponentTransform();
			// Somebody on their feet is held by it. A body lying, hanging or dead, and a piece of one, is carried by the arm's own
			// pull, its whole weight at once: the joint alone, strong enough to bear that, would throw it about.
			const bool bStands = IsStandingHeld(Grab);
			const float Hold = bStands ? Stiffness : 0.f;
			Carry->SetLinearPositionDrive(bStands, bStands, bStands);
			Carry->SetLinearVelocityDrive(bStands, bStands, bStands);
			Carry->SetLinearDriveParams(Hold, Hold * BodyHoldDampingShare, 0.f);
			Carry->SetLinearPositionTarget(Frame.InverseTransformPositionNoScale(Point + Lag.GetClampedToMaxSize(BodyHoldFullCm)));
			if (!bStands)
			{
				Grab.PullN = PullBody(Grab, Point, Lag, MostN);
			}
		}
		if (Grab.bLifting && Creature->Stats && Grab.PullN > 0.f)
		{
			Creature->Stats->ConsumeStamina(FMath::Min(BodyLiftStaminaPerSecond * Grab.PullN / FMath::Max(MostN, 1.f) * DeltaTime, Creature->Stats->GetStamina()));
		}
		static const IConsoleVariable* CarryLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
		Grab.NextLog -= DeltaTime;
		if (CarryLog && CarryLog->GetInt() != 0 && Grab.NextLog <= 0.f)
		{
			Grab.NextLog = 0.25f;
			const USkeletalMeshComponent* HeldMesh = Cast<USkeletalMeshComponent>(Grab.Target.Get());
			const FVector Feet = Creature->GetActorLocation() - FVector(0.f, 0.f, Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			double Lowest = UE_BIG_NUMBER;
			FName LowestPart = NAME_None;
			double Rising = 0.0;
			double Kilos = 0.0;
			for (const FBodyInstance* Body : HeldMesh ? HeldMesh->Bodies : TArray<FBodyInstance*>())
			{
				if (Body && Body->IsInstanceSimulatingPhysics())
				{
					Rising += Body->GetUnrealWorldVelocity().Z * Body->GetBodyMass();
					Kilos += Body->GetBodyMass();
					if (Body->GetBodyBounds().Min.Z < Lowest)
					{
						Lowest = Body->GetBodyBounds().Min.Z;
						LowestPart = Body->BodySetup->BoneName;
					}
				}
			}
			FVector BodyAt = FVector::ZeroVector;
			FVector BodyGoing = FVector::ZeroVector;
			for (const FBodyInstance* Body : HeldMesh ? HeldMesh->Bodies : TArray<FBodyInstance*>())
			{
				if (Body && Body->IsInstanceSimulatingPhysics() && Kilos > 0.0)
				{
					BodyAt += Body->GetCOMPosition() * (Body->GetBodyMass() / Kilos);
					BodyGoing += Body->GetUnrealWorldVelocity() * (Body->GetBodyMass() / Kilos);
				}
			}
			const FVector BodyIn = Creature->GetActorTransform().InverseTransformPositionNoScale(BodyAt);
			const FVector PointIn = Creature->GetActorTransform().InverseTransformPositionNoScale(Point);
			UE_LOG(LogAedir, Log, TEXT("[Lift] aim %.2f %.2f, the place %.0f deg to the right and %.0f cm up, the held part %.0f deg and %.0f cm, the body's middle %.0f deg, %.0f cm ahead of and %.0f cm over the feet, going %.0f cm/s (%.0f up), the holder's view %.0f"), LiftAim.X, LiftAim.Y,
				FMath::RadiansToDegrees(FMath::Atan2(Grab.Where.Y, Grab.Where.X)), Place.Z - Feet.Z, FMath::RadiansToDegrees(FMath::Atan2(PointIn.Y, PointIn.X)), Point.Z - Feet.Z,
				FMath::RadiansToDegrees(FMath::Atan2(BodyIn.Y, BodyIn.X)), BodyIn.Size2D(), BodyAt.Z - Feet.Z, BodyGoing.Size(), BodyGoing.Z, Creature->GetControlRotation().Yaw);
			UE_LOG(LogAedir, Log, TEXT("[Carry] %s's lowest part %s is %.0f cm up, its %.0f kg rise at %.0f cm/s"), *GetNameSafe(Grab.Target->GetOwner()), *LowestPart.ToString(), Lowest - Feet.Z, Kilos, Kilos > 0.0 ? Rising / Kilos : 0.0);
			UE_LOG(LogAedir, Log, TEXT("[Carry] %s's %s hand has %s %s %.0f cm up, %.0f cm behind where it would (%.0f up), lifting %d, pulls %.0f of %.0f N, the hand %.0f cm off it, its hips %.0f cm up, head %.0f, feet %.0f and %.0f, breath %.0f%%"), *Creature->GetName(), Index == 0 ? TEXT("left") : TEXT("right"),
				*GetNameSafe(Grab.Target->GetOwner()), *Grab.Bone.ToString(), Point.Z - Feet.Z, Grab.LagCm, Lag.Z, Grab.bLifting ? 1 : 0, Grab.PullN, MostN, Gap,
				HeldMesh ? HeldMesh->GetBoneLocation(TEXT("pelvis")).Z - Feet.Z : 0.0, HeldMesh ? HeldMesh->GetBoneLocation(TEXT("head")).Z - Feet.Z : 0.0,
				HeldMesh ? HeldMesh->GetBoneLocation(TEXT("foot_l")).Z - Feet.Z : 0.0, HeldMesh ? HeldMesh->GetBoneLocation(TEXT("foot_r")).Z - Feet.Z : 0.0, StaminaShare * 100.f);
		}

		// What is held holds back: walking away from it, the body only gets as far as the arm can drag it.
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement(); Movement && Grab.LagCm > BodyGrabDragFromCm)
		{
			const FVector Away = Lag.GetSafeNormal2D();
			const double Outward = FVector::DotProduct(Movement->Velocity, Away);
			if (Outward > 0.0)
			{
				const float Hold = FMath::Clamp((Grab.LagCm - BodyGrabDragFromCm) / (BodyGrabDragFullCm - BodyGrabDragFromCm), 0.f, 1.f);
				Movement->Velocity -= Away * (Outward * Hold);
			}
		}
		// A living body standing on its feet is not dragged by its limbs alone: the pull reaches its whole body, which gives
		// a little, the more the stronger the hand and the lighter the body, and is thrown off balance by it.
		AAedirCreature* Held = Cast<AAedirCreature>(Grab.Target.IsValid() ? Grab.Target->GetOwner() : nullptr);
		if (Held && Held != Creature && Held->Stats && !Held->Stats->IsDead() && Held->Balance && !Held->Balance->IsRagdoll())
		{
			// Held, a living body is held back: going away from the hand it gets only as far as it outmatches the arm, by
			// strength against strength and the holder's breath.
			const float HeldStrengthNow = FMath::Max(Held->Stats->Strength, 1.f);
			const float Hold = FMath::Clamp(Strength / (Strength + HeldStrengthNow), 0.1f, 0.9f) * FMath::Lerp(0.5f, 1.f, StaminaShare);
			if (UCharacterMovementComponent* HeldMove = Held->GetCharacterMovement())
			{
				const FVector FromHand = (Point - HandWorld).GetSafeNormal2D();
				const FVector Off = FromHand.IsNearlyZero() ? (Held->GetActorLocation() - Creature->GetActorLocation()).GetSafeNormal2D() : FromHand;
				const double Leaving = FVector::DotProduct(HeldMove->Velocity, Off);
				if (Leaving > 0.0)
				{
					HeldMove->Velocity -= Off * (Leaving * Hold * BodyGrabHoldBackShare);
				}
				static const IConsoleVariable* HoldLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
				if (HoldLog && HoldLog->GetInt() != 0 && GFrameCounter % 15 == 0)
				{
					UE_LOG(LogAedir, Log, TEXT("[Hold] %s holds %s by the %s (%.0f%% of the struggle): it was going away at %.0f cm/s, held back by %.0f"), *Creature->GetName(), *Held->GetName(), *Grab.Bone.ToString(), Hold * 100.f,
						FMath::Max(Leaving, 0.0), FMath::Max(Leaving, 0.0) * Hold * BodyGrabHoldBackShare);
				}
			}
			// An arm or a hand held cannot strike freely: its fist is kept near the holding hand and thrown the slower.
			const FString HeldBone = Grab.Bone.ToString();
			const bool bArm = HeldBone.StartsWith(TEXT("hand")) || HeldBone.StartsWith(TEXT("lowerarm")) || HeldBone.StartsWith(TEXT("upperarm"))
				|| HeldBone.StartsWith(TEXT("thumb")) || HeldBone.StartsWith(TEXT("index")) || HeldBone.StartsWith(TEXT("middle")) || HeldBone.StartsWith(TEXT("ring")) || HeldBone.StartsWith(TEXT("pinky"));
			if (bArm)
			{
				if (UAedirFistArms* HeldArms = Held->FindComponentByClass<UAedirFistArms>())
				{
					// Held by the hand the fist is held all but still; by the upper arm, less.
					const float ByPart = HeldBone.StartsWith(TEXT("upperarm")) ? 0.5f : HeldBone.StartsWith(TEXT("lowerarm")) ? 0.8f : 1.f;
					HeldArms->SetHeldBy(HeldBone.EndsWith(TEXT("_l")) ? EAedirHand::Left : EAedirHand::Right, HandWorld, FMath::Clamp(Hold * 1.6f, 0.f, 1.f) * ByPart);
				}
			}
		}
		if (Held && Held != Creature && Held->Balance && !Held->Balance->IsRagdoll() && Grab.LagCm > BodyGrabCloseCm * 0.5f)
		{
			const FVector Pull = Lag.GetSafeNormal2D();
			const float HeldStrength = Held->Stats ? FMath::Max(Held->Stats->Strength, 1.f) : 10.f;
			const float Share = FMath::Clamp(Strength / (Strength + HeldStrength), 0.2f, 0.8f) * FMath::Lerp(0.5f, 1.f, StaminaShare);
			const float Accel = FMath::Min((Grab.LagCm - BodyGrabCloseCm * 0.5f) * BodyGrabLivePullPerCm, BodyGrabLivePullMax) * Share;
			if (UCharacterMovementComponent* HeldMovement = Held->GetCharacterMovement())
			{
				HeldMovement->AddImpulse(Pull * Accel * DeltaTime, true);
			}
			if (IsAuthoritative())
			{
				Held->Balance->ApplyBalanceImpact(Pull, BodyGrabLiveBalancePerSecond * Share * FMath::Clamp(Grab.LagCm / BodyGrabDragFullCm, 0.f, 1.f) * DeltaTime);
			}
		}

		// A hand closed on a living throat chokes it: the senses go the faster the stronger the hand against the neck, and
		// with them the strength to fight it.
		AAedirCreature* Choked = Cast<AAedirCreature>(Grab.Target.IsValid() ? Grab.Target->GetOwner() : nullptr);
		if (Choked && Choked != Creature && Choked->Stats && !Choked->Stats->IsDead() && Grab.Bone.ToString().StartsWith(TEXT("neck")) && Gap < BodyGrabCloseCm * 1.5f && IsAuthoritative())
		{
			const float ChokedStrength = FMath::Max(Choked->Stats->Strength, 1.f);
			const float Grip = FMath::Clamp(Strength / ChokedStrength, 0.4f, 2.f) * FMath::Lerp(0.5f, 1.f, StaminaShare);
			const bool bWasOut = Choked->Stats->IsKnockedOut();
			Choked->Stats->LoseConsciousness(BodyGrabChokePerSecond * Grip * DeltaTime);
			// Held on past senselessness, the choking kills.
			if (bWasOut)
			{
				Choked->ReceiveCreatureDamage(BodyGrabChokeKillPerSecond * Grip * DeltaTime, EAedirHarm::Life);
			}
		}

		// Lifted, it does not get up meanwhile: there is nothing under it to get up from.
		if (Grab.bLifting && Held && Held->Balance)
		{
			Held->Balance->SetHeldUp(0.6f);
		}

		// Whatever goes further from the shoulder than the arm reaches tears out of the hand.
		const bool bLeft = Hand == EAedirHand::Left;
		const FVector Shoulder = Own->GetBoneLocation(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"));
		const FVector Elbow = Own->GetBoneLocation(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
		const float ArmCm = static_cast<float>(FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, HandWorld));
		const float Beyond = static_cast<float>(FVector::Dist(Shoulder, Point)) - ArmCm;
		Grab.TearSeconds = Beyond > BodyHoldTearCm ? Grab.TearSeconds + DeltaTime : 0.f;
		if (Grab.TearSeconds >= BodyHoldTearSeconds)
		{
			UE_LOG(LogAedir, Log, TEXT("%s lost hold of %s, %.0f cm out of its arm's reach"), *Creature->GetName(), *GetNameSafe(Grab.Target.IsValid() ? Grab.Target->GetOwner() : nullptr), Beyond);
			ReleaseBodyGrab(Hand);
		}
	}
	UpdateBodyLiftOff();
}

void UAedirInteractionComponent::AddLiftMouse(const FVector2D& Delta)
{
	if (!IsLiftingBody())
	{
		return;
	}
	const double Across = LiftAim.X + Delta.X / FMath::Max(LiftMouseAcross, 1.f);
	LiftAim.X = FMath::Clamp(Across, -1.0, 1.0);
	LiftAim.Y = FMath::Clamp(LiftAim.Y + Delta.Y / FMath::Max(LiftMouseUpDown, 1.f), -1.0, 1.0);
	// Past the end of the arms' way the whole body is turned round with what it holds.
	const double Over = (Across - LiftAim.X) * LiftMouseAcross;
	if (AAedirCreature* Creature = GetCreature(); Creature && !FMath::IsNearlyZero(Over))
	{
		Creature->AddControllerYawInput(static_cast<float>(Over) * LiftEdgeTurnShare);
	}
}

FVector UAedirInteractionComponent::UpdateBodyCarryPlace(EAedirHand Hand, const FVector& Point, float DeltaTime)
{
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* Own = Creature->GetMesh();
	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	FAedirBodyGrab& With = BodyGrabs[1 - static_cast<int32>(Hand)];
	const FTransform Frame = Creature->GetActorTransform();
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const bool bLeft = Hand == EAedirHand::Left;
	const FVector LeftShoulder = Own->GetBoneLocation(TEXT("upperarm_l"));
	const FVector RightShoulder = Own->GetBoneLocation(TEXT("upperarm_r"));
	const FVector Shoulder = bLeft ? LeftShoulder : RightShoulder;
	const FVector Elbow = Own->GetBoneLocation(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
	const FVector Wrist = Own->GetBoneLocation(bLeft ? LeftHandBone : RightHandBone);
	const double ArmCm = FVector::Dist(Shoulder, Elbow) + FVector::Dist(Elbow, Wrist);
	const bool bWasLifting = Grab.bLifting;

	// A hand's button held, it lifts, and the other one with it if both hold the same: what they hold goes where the mouse
	// takes it, up, down and round.
	const UAedirFistArms* Fists = Creature->IsPlayerControlled() ? Creature->FindComponentByClass<UAedirFistArms>() : nullptr;
	const bool bTogether = With.Joint.IsValid() && With.Target == Grab.Target;
	Grab.bLifting = Fists && (Fists->IsHeld(Hand) || (bTogether && Fists->IsHeld(bLeft ? EAedirHand::Right : EAedirHand::Left)));
	FVector Place;
	const FVector Before = Grab.Where;
	if (Grab.bLifting)
	{
		// About where the shoulders are standing straight, however the body bends down to what it holds.
		const float HalfHeight = Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Middle(LiftShoulderAheadCm * Scale, 0.f, HalfHeight * (2.f * LiftShoulderShare - 1.f));
		if (!bWasLifting)
		{
			Grab.Where = Frame.InverseTransformPositionNoScale(Point);
			FVector Other;
			const bool bOther = bTogether && GetBodyGrabPoint(bLeft ? EAedirHand::Right : EAedirHand::Left, Other);
			const FVector Centre = bOther ? (Grab.Where + Frame.InverseTransformPositionNoScale(Other)) * 0.5 : Grab.Where;
			// From where it is held: the first hand to lift sets where the mouse starts.
			if (!With.bLifting || !bTogether)
			{
				const FVector Way = (Centre - Middle).GetSafeNormal();
				const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Centre.Y, FMath::Max(Centre.X, 15.0)));
				const double Pitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Way.Z, -1.0, 1.0)));
				LiftAim.X = FMath::Clamp(Yaw / FMath::Max(LiftSideDegrees, 1.f), -1.0, 1.0);
				LiftAim.Y = FMath::Clamp(Pitch / FMath::Max(Pitch > 0.0 ? LiftUpDegrees : LiftDownDegrees, 1.f), static_cast<double>(LiftStartLeast), 1.0);
			}
			const FVector Off = FRotator(0.f, LiftAim.X * LiftSideDegrees, 0.f).UnrotateVector(Grab.Where - Centre);
			Grab.LiftOffset = Off.GetClampedToMaxSize(BodyLiftHandsApartCm * Scale);
		}
		const FRotator Aim(static_cast<float>(LiftAim.Y) * (LiftAim.Y > 0.0 ? LiftUpDegrees : LiftDownDegrees), static_cast<float>(LiftAim.X) * LiftSideDegrees, 0.f);
		// Low down the body bends after it, to set it on the ground.
		const double Low = FMath::Square(FMath::Max(-LiftAim.Y, 0.0));
		FVector Wanted = Middle + Aim.Vector() * (ArmCm * 0.95 + FMath::Clamp(Grab.BeyondReachCm, 0.f, LiftBeyondMostCm) + LiftLowBendCm * Scale * Low) + FRotator(0.f, Aim.Yaw, 0.f).RotateVector(Grab.LiftOffset);
		Wanted.Z = FMath::Max(Wanted.Z, -HalfHeight + LiftLowestCm * Scale);
		Grab.Where = FMath::VInterpConstantTo(Grab.Where, Wanted, DeltaTime, LiftSpeedCm * Scale);
	}
	else
	{
		if (bWasLifting)
		{
			Grab.Where = Frame.InverseTransformPositionNoScale(Point);
		}
		// Let hang, what lies on the ground is brought up to the hand at the side of the hip; somebody standing is held where
		// they were taken hold of.
		if (!IsStandingHeld(Grab))
		{
			const float FeetZ = -Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector Hang(BodyCarryPlaceCm.X * Scale, BodyCarryPlaceCm.Y * Scale * (bLeft ? -1.f : 1.f), FeetZ + BodyCarryPlaceCm.Z * Scale);
			Grab.Where = FMath::VInterpConstantTo(Grab.Where, Hang, DeltaTime, BodyCarrySpeedCm);
		}
	}
	// How fast the place goes, turned and carried along with the holder as it is.
	const FVector Last = Grab.WhereWorld;
	Place = Frame.TransformPositionNoScale(Grab.Where);

	// No further from the shoulder than the arm reaches.
	const double Reach = ArmCm * 0.96 + (Grab.bLifting ? FMath::Clamp(Grab.BeyondReachCm, 0.f, LiftBeyondMostCm) : Grab.BeyondReachCm);
	Place = Shoulder + (Place - Shoulder).GetClampedToMaxSize(Reach);
	const FVector Speed = DeltaTime > KINDA_SMALL_NUMBER && Grab.bLifting && bWasLifting && !Last.IsNearlyZero() ? (Place - Last) / DeltaTime : FVector::ZeroVector;
	Grab.WhereVelocity = FMath::VInterpTo(Grab.WhereVelocity, Speed.GetClampedToMaxSize(LiftSpeedCm * Scale * 1.5f), DeltaTime, 18.f);
	return Place;
}

bool UAedirInteractionComponent::IsStandingHeld(const FAedirBodyGrab& Grab) const
{
	const AAedirCreature* Held = Cast<AAedirCreature>(Grab.Target.IsValid() ? Grab.Target->GetOwner() : nullptr);
	return Held && Held->Stats && !Held->Stats->IsDead() && Held->Balance && !Held->Balance->IsRagdoll();
}

float UAedirInteractionComponent::PullBody(const FAedirBodyGrab& Grab, const FVector& Point, const FVector& Lag, float MostN) const
{
	UPrimitiveComponent* Target = Grab.Target.Get();
	const AAedirCreature* Creature = GetCreature();
	const UWorld* World = GetWorld();
	if (!Target || !Creature || !World)
	{
		return 0.f;
	}
	USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Target);
	// Both hands on the same body bear it between them.
	int32 Hands = 0;
	for (const FAedirBodyGrab& Any : BodyGrabs)
	{
		Hands += Any.Joint.IsValid() && Any.Target == Grab.Target ? 1 : 0;
	}
	float Kilos = 0.f;
	FVector Momentum = FVector::ZeroVector;
	if (Skeletal)
	{
		for (const FBodyInstance* Body : Skeletal->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				Kilos += Body->GetBodyMass();
				Momentum += Body->GetUnrealWorldVelocity() * Body->GetBodyMass();
			}
		}
	}
	else if (Target->IsSimulatingPhysics())
	{
		Kilos = Target->GetMass();
		Momentum = Target->GetPhysicsLinearVelocity() * Kilos;
	}
	if (Kilos <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	// Lying still it is asleep, and a push on a body asleep is dropped again and again.
	if (Skeletal)
	{
		Skeletal->WakeAllRigidBodies();
	}
	// As a weight on a spring: its whole weight borne as far as it is below where the hand would have it, and drawn there, the
	// more slowly the nearer; lifted, along with the place it is taken to, so it swings with the arms and keeps its way.
	const bool bLift = Grab.bLifting;
	const FVector Wanted = Lag.GetClampedToMaxSize(bLift ? LiftPullFullCm : BodyPullFullCm);
	const FVector Going = Momentum / Kilos - (bLift ? Grab.WhereVelocity * LiftFollowShare + Creature->GetVelocity() * (1.f - LiftFollowShare) : Creature->GetVelocity());
	const float Spring = bLift ? LiftPullPerSecondSq : BodyPullPerSecondSq;
	const float Damping = 2.f * FMath::Sqrt(Spring);
	const bool bMan = Cast<AAedirCreature>(Target->GetOwner()) != nullptr;
	const float Bear = FMath::Clamp(0.5f + static_cast<float>(Lag.Z) / FMath::Max(BodyPullBearCm, 1.f), 0.f, 1.f) * (bMan && !bLift ? BodyDragBearShare : 1.f);
	const float Mine = Kilos / FMath::Max(Hands, 1);
	// The weight is borne first; what strength is left moves it.
	const FVector Weight = FVector(0.f, 0.f, BodyGrabGravityCm * Bear) * Mine;
	const FVector Borne = Weight.GetClampedToMaxSize(MostN * 100.f);
	const FVector Moving = (Wanted * Spring - Going * Damping) * Mine;
	const double Ahead = FVector::DotProduct(Borne, Moving.GetSafeNormal());
	const double Spare = -Ahead + FMath::Sqrt(FMath::Max(Ahead * Ahead + FMath::Square(static_cast<double>(MostN) * 100.0) - Borne.SizeSquared(), 0.0));
	FVector Force = Borne + Moving.GetClampedToMaxSize(FMath::Max(Spare, 0.0) * (bLift ? LiftSwingForceShare : 1.f));
	const float PullN = static_cast<float>(Force.Size()) * 0.01f;
	if (!Skeletal)
	{
		Target->AddForceAtLocation(Force, Point);
		return PullN;
	}
	// A light part is pulled no harder than it can bear without being flung: what is left goes to what it hangs on.
	const FVector Way = Force.GetSafeNormal();
	// A share of it reaches the whole body at once, each part by its weight: handed from joint to joint alone, part of the
	// pull on a light hand or a thigh is lost on the way and the body stays on the ground under it.
	// Lifted, only the trunk: the head and the limbs hang from it.
	const float Spread = FMath::Clamp(bLift ? LiftSpreadShare : BodyPullSpreadShare, 0.f, 1.f);
	const auto Bears = [bLift](const FBodyInstance* Body)
	{
		if (!Body || !Body->IsInstanceSimulatingPhysics())
		{
			return false;
		}
		const FString Name = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName.ToString() : FString();
		return !bLift || Name.StartsWith(TEXT("pelvis")) || Name.StartsWith(TEXT("spine"));
	};
	float TrunkKilos = 0.f;
	for (const FBodyInstance* Body : Skeletal->Bodies)
	{
		TrunkKilos += Bears(Body) ? Body->GetBodyMass() : 0.f;
	}
	for (FBodyInstance* Body : Skeletal->Bodies)
	{
		if (Bears(Body))
		{
			Body->AddForce(Force * (Spread * Body->GetBodyMass() / FMath::Max(TrunkKilos, 1.f)), true, false);
		}
	}
	float Left = static_cast<float>(Force.Size()) * (1.f - Spread);
	const float Step = FMath::Clamp(World->GetDeltaSeconds(), 1.f / 240.f, 1.f / 20.f);
	FName Bone = Grab.Bone;
	while (Left > KINDA_SMALL_NUMBER && !Bone.IsNone())
	{
		FBodyInstance* Body = Skeletal->GetBodyInstance(Bone);
		if (Body && Body->IsInstanceSimulatingPhysics())
		{
			const float Here = FMath::Min(Left, Body->GetBodyMass() * BodyPullPartSpeedCm / Step);
			Body->AddForce(Way * Here, true, false);
			Left -= Here;
		}
		Bone = Skeletal->GetParentBone(Bone);
	}
	return PullN;
}

void UAedirInteractionComponent::UpdateBodyLiftOff()
{
	// Somebody standing, pulled up at by hands that together pull with more than their weight, leaves the ground.
	const UWorld* World = GetWorld();
	AAedirCreature* Creature = GetCreature();
	if (!World || !Creature)
	{
		return;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAedirBodyGrab& Grab = BodyGrabs[Index];
		AAedirCreature* Held = Grab.Joint.IsValid() && Grab.Target.IsValid() ? Cast<AAedirCreature>(Grab.Target->GetOwner()) : nullptr;
		FVector Point;
		if (!Held || Held == Creature || !Held->Balance || Held->Balance->IsRagdoll() || !Grab.bLifting || !GetBodyGrabPoint(static_cast<EAedirHand>(Index), Point))
		{
			Grab.LiftSeconds = 0.f;
			continue;
		}
		float UpN = 0.f;
		for (int32 Other = 0; Other < 2; ++Other)
		{
			const FAedirBodyGrab& With = BodyGrabs[Other];
			if (With.Joint.IsValid() && With.bLifting && With.Target == Grab.Target)
			{
				const float MostN = (BodyLiftForceN + (Creature->Stats ? Creature->Stats->Strength : 10.f) * BodyLiftForcePerStrengthN) * FMath::Lerp(BodyForceTiredShare, 1.f, Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f);
				UpN += MostN;
			}
		}
		const float WeighsN = Held->Balance->GetBodyKilos() * BodyGrabGravityCm * 0.01f;
		const bool bPullsUp = Grab.WhereWorld.Z - Point.Z > BodyLiftOffCm;
		Grab.LiftSeconds = bPullsUp && UpN > WeighsN ? Grab.LiftSeconds + World->GetDeltaSeconds() : 0.f;
		if (Grab.LiftSeconds >= BodyLiftOffSeconds)
		{
			UE_LOG(LogAedir, Log, TEXT("[Carry] %s lifts %s off the ground by the %s: %.0f N against its %.0f N"), *Creature->GetName(), *Held->GetName(), *Grab.Bone.ToString(), UpN, WeighsN);
			Held->Balance->LiftOffFeet();
			Held->Balance->SetHeldUp(0.3f);
			Grab.LiftSeconds = 0.f;
		}
	}
}
