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
	constexpr float HandGripPalmShare = 0.8f;
	constexpr double ReleaseGraceSeconds = 1.5;
}

AAedirItemActor::AAedirItemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetSimulatePhysics(true);
	Mesh->SetUseCCD(true);
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

void AAedirItemActor::AttachToCreatureHand(AAedirCreature* Creature, EAedirHand Hand)
{
	if (!HasAuthority() || !Creature || !Creature->Interaction)
	{
		return;
	}

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
	const FVector Palm = HandLocation + (RefLocation(TEXT("middle_01")) - HandLocation) * HandGripPalmShare;

	const FQuat ItemRotation = FRotationMatrix::MakeFromXY(KnuckleAxis, FingerAxis).ToQuat();
	const FQuat RelativeRotation = HandRotation.Inverse() * ItemRotation;
	const FVector RelativePalm = HandRotation.Inverse().RotateVector(Palm - HandLocation);
	return FTransform(RelativeRotation, RelativePalm - RelativeRotation.RotateVector(FVector(GripLocalCm, 0.f, 0.f)));
}

void AAedirItemActor::AttachToWaist(AAedirCreature* Creature, FName BoneName, const FTransform& SlotRelative)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	AttachToBone(Creature, BoneName, WaistTransform * SlotRelative);

	Holder = Creature;
	bHolstered = true;
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
