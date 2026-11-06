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
	AttachToBone(Creature, BoneName, HeldTransform);

	Holder = Creature;
	HeldHand = Hand;
	bHolstered = false;
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
