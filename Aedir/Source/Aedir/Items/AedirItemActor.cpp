#include "AedirItemActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Items/AedirInteractionComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 AedirHighlightStencilValue = 250;
}

namespace
{
	constexpr float HangSwingStiffness = 45.f;
	constexpr float HangSwingDamping = 5.f;
	constexpr float HangSwingGain = 0.05f;
	constexpr float HangSwingMaxRadians = 0.7f;
	constexpr float HangMaxAcceleration = 1800.f;
	constexpr double ReleaseGraceSeconds = 1.5;
	// An item let go inside the capsule is pushed out of it gently, not shot away.
	constexpr float ItemMaxDepenetrationSpeed = 80.f;
	constexpr float ItemLinearDamping = 0.6f;
	constexpr float ItemAngularDamping = 2.f;
}

AAedirItemActor::AAedirItemActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);
	Mesh->SetUseCCD(true);
	Mesh->SetLinearDamping(ItemLinearDamping);
	Mesh->SetAngularDamping(ItemAngularDamping);
}

void AAedirItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyItemMass();
}

void AAedirItemActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyItemMass();
}

void AAedirItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirItemActor, Holder);
	DOREPLIFETIME(AAedirItemActor, HeldHand);
	DOREPLIFETIME(AAedirItemActor, HeldGripShiftCm);
	DOREPLIFETIME(AAedirItemActor, bHolstered);
	DOREPLIFETIME(AAedirItemActor, bWorn);
	DOREPLIFETIME(AAedirItemActor, bStowed);
}

void AAedirItemActor::Use_Implementation(AAedirCreature* User)
{
	if (!HasAuthority() || !User || !User->Stats)
	{
		return;
	}

	if (ItemClass == EAedirItemClass::Poison)
	{
		User->Stats->ApplyDamage(PoisonDamage);
	}

	OnUsed(User);
}

void AAedirItemActor::AttachToCreatureHand(AAedirCreature* Creature, EAedirHand Hand, float GripShiftCm)
{
	if (!HasAuthority() || !Creature || !Creature->Interaction)
	{
		return;
	}

	HeldGripShiftCm = GripShiftCm;
	const FName BoneName = Hand == EAedirHand::Left ? Creature->Interaction->LeftHandBone : Creature->Interaction->RightHandBone;
	AttachToBone(Creature, BoneName, bUseHandGrip ? ComputeHandGrip(Creature, Hand) : HeldTransform);

	Holder = Creature;
	HeldHand = Hand;
	bHolstered = false;
}

void AAedirItemActor::AttachToHandInPlace(AAedirCreature* Creature, EAedirHand Hand)
{
	if (!HasAuthority() || !Creature || !Creature->Interaction)
	{
		return;
	}

	SetFreePhysics(false);
	const FName BoneName = Hand == EAedirHand::Left ? Creature->Interaction->LeftHandBone : Creature->Interaction->RightHandBone;
	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::KeepWorldTransform, BoneName);

	Holder = Creature;
	HeldHand = Hand;
	bHolstered = false;
}

void AAedirItemActor::AttachCarried(AAedirCreature* Creature)
{
	if (HasAuthority() && Creature)
	{
		AttachToComponent(Creature->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
}

bool AAedirItemActor::WasJustReleasedBy(const AActor* Creature) const
{
	const UWorld* World = GetWorld();
	return World && LastHolder.Get() == Creature && World->GetTimeSeconds() - LastReleaseTime < ReleaseGraceSeconds;
}

FTransform AAedirItemActor::ComputeHandGrip(const AAedirCreature* Creature, EAedirHand Hand) const
{
	const USkeletalMesh* BodyAsset = Creature->GetMesh() ? Creature->GetMesh()->GetSkeletalMeshAsset() : nullptr;
	if (!BodyAsset)
	{
		return HeldTransform;
	}

	// The axes of a closed hand come from the skeleton: a handle runs across the knuckles, its blade out on the thumb side.
	const TCHAR* Suffix = Hand == EAedirHand::Left ? TEXT("_l") : TEXT("_r");
	const auto RefLocation = [&](const TCHAR* Bone)
	{
		return BodyAsset->GetComposedRefPoseMatrix(FName(*FString::Printf(TEXT("%s%s"), Bone, Suffix))).GetOrigin();
	};

	const FMatrix HandMatrix = BodyAsset->GetComposedRefPoseMatrix(FName(*FString::Printf(TEXT("hand%s"), Suffix)));
	const FQuat HandRotation = HandMatrix.ToQuat();
	const FVector HandLocation = HandMatrix.GetOrigin();
	const FVector KnuckleAxis = (RefLocation(TEXT("index_01")) - RefLocation(TEXT("pinky_01"))).GetSafeNormal();
	const FVector FingerAxis = (RefLocation(TEXT("middle_01")) - HandLocation).GetSafeNormal();
	// The handle lies in the hollow of the palm: part way to the knuckles and off the back of the hand, on the side the thumb is on.
	FVector PalmNormal = FVector::CrossProduct(KnuckleAxis, FingerAxis).GetSafeNormal();
	if (FVector::DotProduct(PalmNormal, RefLocation(TEXT("thumb_01")) - HandLocation) < 0.0)
	{
		PalmNormal = -PalmNormal;
	}
	const FVector Palm = HandLocation + (RefLocation(TEXT("middle_01")) - HandLocation) * HandGripPalmShare + PalmNormal * HandGripPalmDepthCm;

	const FQuat ItemRotation = FRotationMatrix::MakeFromXY(KnuckleAxis, FingerAxis).ToQuat();
	const FQuat RelativeRotation = HandRotation.Inverse() * ItemRotation;
	const FVector RelativePalm = HandRotation.Inverse().RotateVector(Palm - HandLocation);
	return FTransform(RelativeRotation, RelativePalm - RelativeRotation.RotateVector(FVector(GripLocalCm + HeldGripShiftCm, 0.f, 0.f)));
}

void AAedirItemActor::AttachToWaist(AAedirCreature* Creature, FName BoneName, const FVector& SlotOnBone, const FQuat& RotationOnBone, float PivotCm)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	HeldGripShiftCm = 0.f;
	AttachToBone(Creature, BoneName, FTransform(RotationOnBone, SlotOnBone - RotationOnBone.RotateVector(FVector(PivotCm, 0.f, 0.f))));

	Holder = Creature;
	bHolstered = true;

	HangSlot = SlotOnBone;
	HangRotation = RotationOnBone;
	HangPivotCm = PivotCm;
	HangSwing = FVector::ZeroVector;
	HangSwingSpeed = FVector::ZeroVector;
	bHangTracking = false;
	SetActorTickEnabled(true);
}

void AAedirItemActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const USceneComponent* Parent = GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr;
	if (!bHolstered || !Parent || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		SetActorTickEnabled(false);
		return;
	}

	// A hanging item is a small pendulum: the acceleration of the bone it hangs from throws it around its pivot.
	const FTransform BoneWorld = Parent->GetSocketTransform(GetRootComponent()->GetAttachSocketName());
	const FVector BoneLocation = BoneWorld.GetLocation();
	FVector Acceleration = FVector::ZeroVector;
	if (bHangTracking)
	{
		const FVector Velocity = (BoneLocation - HangPreviousBone) / DeltaSeconds;
		Acceleration = ((Velocity - HangPreviousVelocity) / DeltaSeconds).GetClampedToMaxSize(HangMaxAcceleration);
		HangPreviousVelocity = Velocity;
	}
	HangPreviousBone = BoneLocation;
	bHangTracking = true;

	// The lever runs from the pivot to the middle of the item.
	const FVector Lever = HangRotation.RotateVector(FVector(-HangPivotCm >= 0.f ? 1.f : -1.f, 0.f, 0.f));
	const FVector Inertial = BoneWorld.GetRotation().Inverse().RotateVector(-Acceleration);
	const FVector Torque = FVector::CrossProduct(Lever, Inertial) * HangSwingGain;
	HangSwingSpeed += (Torque - HangSwing * HangSwingStiffness - HangSwingSpeed * HangSwingDamping) * DeltaSeconds;
	HangSwing = (HangSwing + HangSwingSpeed * DeltaSeconds).GetClampedToMaxSize(HangSwingMaxRadians);

	const float Angle = static_cast<float>(HangSwing.Size());
	const FQuat Swing = Angle > KINDA_SMALL_NUMBER ? FQuat(HangSwing / Angle, Angle) : FQuat::Identity;
	const FQuat Rotation = Swing * HangRotation;
	SetActorRelativeTransform(FTransform(Rotation, HangSlot - Rotation.RotateVector(FVector(HangPivotCm, 0.f, 0.f)), BodyScale));
}

FVector AAedirItemActor::GetGripWorld() const
{
	return GetActorLocation() + GetActorForwardVector() * GripLocalCm;
}

void AAedirItemActor::AttachToBackpack(AAedirCreature* Creature)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	SetFreePhysics(false);
	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	Holder = Creature;
	bHolstered = false;
	bStowed = true;
	ApplyStowed(true);
}

void AAedirItemActor::Detach(const FVector& ThrowVelocity)
{
	if (!HasAuthority())
	{
		return;
	}

	bStowed = false;
	ApplyStowed(false);
	HeldGripShiftCm = 0.f;

	SetActorTickEnabled(false);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (Holder)
	{
		LastHolder = Holder;
		LastReleaseTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}
	Holder = nullptr;
	bHolstered = false;

	SetFreePhysics(true);
	if (Mesh)
	{
		Mesh->SetPhysicsLinearVelocity(ThrowVelocity);
	}
}

void AAedirItemActor::SetHighlighted(bool bNewHighlighted)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->SetRenderCustomDepth(bNewHighlighted);
	Mesh->SetCustomDepthStencilValue(AedirHighlightStencilValue);

	if (HighlightOverlayMaterial)
	{
		Mesh->SetOverlayMaterial(bNewHighlighted ? HighlightOverlayMaterial.Get() : nullptr);
	}
}

float AAedirItemActor::GetHoldRadiusCm() const
{
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return 0.f;
	}

	// From the mesh itself: the box around a turned item in the world is larger than the item.
	const FVector Extent = Mesh->GetStaticMesh()->GetBounds().BoxExtent * Mesh->GetComponentScale().GetAbs();
	return static_cast<float>(FMath::Max(Extent.X, Extent.Y));
}

FVector AAedirItemActor::GetBoundsCenterWorld() const
{
	if (!Mesh)
	{
		return GetActorLocation();
	}

	return Mesh->GetComponentTransform().TransformPosition(GetStaticBoundsCenter());
}

bool AAedirItemActor::IsHeld() const
{
	return Holder != nullptr;
}

bool AAedirItemActor::IsUsable() const
{
	return ItemClass == EAedirItemClass::Poison || ItemClass == EAedirItemClass::Book || ItemClass == EAedirItemClass::Armor;
}

float AAedirItemActor::GetDamage() const
{
	return Damage;
}

float AAedirItemActor::GetWeaponLength() const
{
	return WeaponLength;
}

EAedirDamageType AAedirItemActor::GetDamageType() const
{
	return DamageType;
}

bool AAedirItemActor::IsPickable(const AAedirItemActor* Item)
{
	return Item && !Item->IsHeld() && !Item->bWorn;
}

void AAedirItemActor::OnRep_Holder()
{
	SetFreePhysics(Holder == nullptr);
}

void AAedirItemActor::OnRep_Stowed()
{
	ApplyStowed(bStowed);
}

void AAedirItemActor::ApplyStowed(bool bNewStowed)
{
	SetActorHiddenInGame(bNewStowed);
	if (Mesh && bNewStowed)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AAedirItemActor::AttachToBone(AAedirCreature* Creature, FName BoneName, const FTransform& HoldOffset)
{
	if (bStowed)
	{
		bStowed = false;
		ApplyStowed(false);
	}

	SetActorTickEnabled(false);
	SetFreePhysics(false);

	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, BoneName);
	SetActorRelativeTransform(ComputeHoldTransform(HoldOffset));
}

void AAedirItemActor::AttachToWornBone(AAedirCreature* Creature)
{
	if (!HasAuthority() || !Creature || ArmorAttachBone.IsNone())
	{
		return;
	}

	AttachToBone(Creature, ArmorAttachBone, FTransform::Identity);

	Holder = Creature;
	bHolstered = false;
	bWorn = true;
}

void AAedirItemActor::SetFreePhysics(bool bFree)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->SetCollisionEnabled(bFree ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetSimulatePhysics(bFree);
}

void AAedirItemActor::ApplyItemMass()
{
	if (Mesh)
	{
		Mesh->SetMassOverrideInKg(NAME_None, Weight, true);
		Mesh->BodyInstance.SetOverrideMaxDepenetrationVelocity(true);
		Mesh->BodyInstance.SetMaxDepenetrationVelocity(ItemMaxDepenetrationSpeed);
	}
}

FTransform AAedirItemActor::ComputeHoldTransform(const FTransform& HoldOffset) const
{
	// The root transform maps mesh-local space to the parent, so the bounds center lands on the hold offset translation.
	const FVector CenterOffset = GetStaticBoundsCenter();
	return FTransform(HoldOffset.GetRotation(), HoldOffset.TransformPosition(-CenterOffset), HoldOffset.GetScale3D() * BodyScale);
}

FVector AAedirItemActor::GetStaticBoundsCenter() const
{
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return FVector::ZeroVector;
	}

	return Mesh->GetStaticMesh()->GetBounds().Origin;
}
