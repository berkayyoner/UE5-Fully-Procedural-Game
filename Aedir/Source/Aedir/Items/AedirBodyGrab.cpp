#include "Items/AedirInteractionComponent.h"

#include "Aedir.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AedirItemActor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace
{
	// A free hand closes on whatever body or loose part of one is in front of it: an arm, a collar, a cut-off leg.
	constexpr float BodyGrabForwardCm = 55.f;
	constexpr float BodyGrabHeightCm = 20.f;
	constexpr float BodyGrabRadiusCm = 50.f;
	constexpr float BodyGrabLowCm = -70.f;
	// The hand pulls the part to itself and the part pulls back with all its weight; a strong arm pulls harder.
	constexpr float BodyGrabStiffnessBase = 20000.f;
	constexpr float BodyGrabStiffnessPerStrength = 400.f;
	constexpr float BodyGrabDampingShare = 0.08f;
	constexpr float BodyGrabTearCm = 100.f;
	constexpr float BodyGrabDragFromCm = 45.f;
	constexpr float BodyGrabTearSeconds = 0.35f;
	constexpr float BodyGrabAimRangeCm = 230.f;
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
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirBodyAim), false, Creature);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, From, From + Aim * BodyGrabAimRangeCm, Objects, Params);
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Component || Component->GetCollisionObjectType() == ECC_WorldStatic)
		{
			return false;
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

bool UAedirInteractionComponent::IsGrabbingBody(EAedirHand Hand) const
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
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeSphere(BodyGrabRadiusCm * Scale), Params);
	}

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

		FName Bone = NAME_None;
		FVector Point = Component->GetComponentLocation();
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
		{
			// The closest bone that has a body of its own, walking up from fingers and twist bones.
			Bone = Skeletal->FindClosestBone(Center);
			while (!Bone.IsNone() && !Skeletal->GetBodyInstance(Bone))
			{
				Bone = Skeletal->GetParentBone(Bone);
			}
			const FBodyInstance* Part = Bone.IsNone() ? nullptr : Skeletal->GetBodyInstance(Bone);
			if (!Part || !Part->IsInstanceSimulatingPhysics())
			{
				continue;
			}
			Point = Skeletal->GetBoneLocation(Bone);
		}

		const double Distance = FVector::DistSquared(Point, Center);
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

	UPhysicsConstraintComponent* Joint = NewObject<UPhysicsConstraintComponent>(Creature);
	Joint->SetWorldLocation(BestPoint);
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
	Joint->SetConstrainedComponents(Own, HandBone, Best, BestBone);
	// The hand's own frame is its palm, the part's is where it was taken hold of.
	Joint->SetConstraintReferencePosition(EConstraintFrame::Frame1, FVector::ZeroVector);
	const FTransform PartWorld = BestBone.IsNone() ? Best->GetComponentTransform() : Cast<USkeletalMeshComponent>(Best)->GetBoneTransform(Cast<USkeletalMeshComponent>(Best)->GetBoneIndex(BestBone));
	Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2, PartWorld.InverseTransformPosition(BestPoint));
	Joint->SetLinearPositionTarget(FVector::ZeroVector);
	Joint->SetLinearVelocityTarget(FVector::ZeroVector);

	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	Grab.Joint = Joint;
	Grab.Target = Best;
	Grab.Bone = BestBone;
	Grab.LocalPoint = PartWorld.InverseTransformPosition(BestPoint);
	Grab.TearSeconds = 0.f;
	UE_LOG(LogAedir, Log, TEXT("%s took hold of %s %s"), *Creature->GetName(), *GetNameSafe(Best->GetOwner()), *BestBone.ToString());
	return true;
}

void UAedirInteractionComponent::ReleaseBodyGrab(EAedirHand Hand)
{
	FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	if (UPhysicsConstraintComponent* Joint = Grab.Joint.Get())
	{
		Joint->BreakConstraint();
		Joint->DestroyComponent();
	}
	Grab = FAedirBodyGrab();
}

bool UAedirInteractionComponent::GetBodyGrabPoint(EAedirHand Hand, FVector& OutWorld) const
{
	const FAedirBodyGrab& Grab = BodyGrabs[static_cast<int32>(Hand)];
	const UPrimitiveComponent* Target = Grab.Target.Get();
	if (!Grab.Joint.IsValid() || !Target)
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
		if (!Grab.Joint.IsValid())
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

		const float Stiffness = (BodyGrabStiffnessBase + Strength * BodyGrabStiffnessPerStrength) * FMath::Lerp(0.5f, 1.f, StaminaShare);
		Grab.Joint->SetLinearDriveParams(Stiffness, Stiffness * BodyGrabDampingShare, 0.f);

		// Pulled out of the hand by weight or by the other body tearing itself away.
		const FVector HandWorld = Own->GetBoneLocation(Hand == EAedirHand::Left ? LeftHandBone : RightHandBone);
		const float Gap = static_cast<float>(FVector::Dist(Point, HandWorld));

		// What is held holds back: walking away from it, the body only gets as far as the arm can drag it.
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement(); Movement && Gap > BodyGrabDragFromCm)
		{
			const FVector Away = (HandWorld - Point).GetSafeNormal2D();
			const double Outward = FVector::DotProduct(Movement->Velocity, Away);
			if (Outward > 0.0)
			{
				const float Hold = FMath::Clamp((Gap - BodyGrabDragFromCm) / (BodyGrabTearCm - BodyGrabDragFromCm), 0.f, 1.f);
				Movement->Velocity -= Away * (Outward * Hold);
			}
		}
		Grab.TearSeconds = Gap > BodyGrabTearCm ? Grab.TearSeconds + DeltaTime : 0.f;
		if (Grab.TearSeconds >= BodyGrabTearSeconds)
		{
			UE_LOG(LogAedir, Log, TEXT("%s lost hold of %s, %.0f cm away"), *Creature->GetName(), *GetNameSafe(Grab.Target.IsValid() ? Grab.Target->GetOwner() : nullptr), Gap);
			ReleaseBodyGrab(Hand);
		}
	}
}
