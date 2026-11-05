#include "AedirInteractionComponent.h"

#include "Aedir.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Items/AedirItemActor.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 WaistSlotsPerSide = 3;
	constexpr float ReachBlendInSeconds = 0.15f;
	constexpr float ReachBlendOutSeconds = 0.2f;
	constexpr float PelvisLowerInterpSpeed = 8.f;
	constexpr float PelvisLowerFactor = 0.7f;
	constexpr float ReachRangeMultiplier = 1.5f;
	constexpr float HighlightInterval = 0.1f;

	EAedirHand GetOtherHand(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	}

	bool RequiresBothHands(const AAedirItemActor* Item)
	{
		return Item && (Item->bTwoHanded || Item->ItemClass == EAedirItemClass::Object);
	}

	int32 GetWaistSideBegin(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? 0 : WaistSlotsPerSide;
	}

	int32 GetWaistSideIndex(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? 0 : 1;
	}

	void ResetSlot(FAedirHandSlot& Slot)
	{
		Slot.State = EAedirHandState::Idle;
		Slot.Item = nullptr;
		Slot.ReachElapsed = 0.f;
		Slot.ReachHandWorld = FVector::ZeroVector;
	}
}

UAedirInteractionComponent::UAedirInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f;
	SetIsReplicatedByDefault(true);

	WaistItems.SetNum(WaistSlotsPerSide * 2);
}

void UAedirInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AAedirCreature* Creature = GetCreature();
	if (!Creature)
	{
		return;
	}

	if (IsAuthoritative())
	{
		UpdateReach(EAedirHand::Left, DeltaTime);
		UpdateReach(EAedirHand::Right, DeltaTime);
	}

	UpdateReachBlend(DeltaTime);
	UpdatePelvisLower(DeltaTime);

	if (Creature->IsLocallyControlled())
	{
		HighlightTimer -= DeltaTime;
		if (HighlightTimer <= 0.f)
		{
			HighlightTimer = HighlightInterval;
			UpdateHighlight();
		}
	}
}

void UAedirInteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirInteractionComponent, LeftSlot);
	DOREPLIFETIME(UAedirInteractionComponent, RightSlot);
	DOREPLIFETIME(UAedirInteractionComponent, WaistItems);
}

void UAedirInteractionComponent::StartReach(EAedirHand Hand)
{
	const EAedirHandState State = GetHandState(Hand);
	if (State == EAedirHandState::Holding)
	{
		DropHeld(Hand);
		return;
	}

	if (State != EAedirHandState::Idle)
	{
		return;
	}

	StartReachForItem(Hand, FindBestItem());
}

void UAedirInteractionComponent::StartReachForItem(EAedirHand Hand, AAedirItemActor* Item)
{
	if (!Item)
	{
		return;
	}

	if (IsAuthoritative())
	{
		BeginReach(Hand, Item);
	}
	else
	{
		ServerStartReachForItem(Hand, Item);
	}
}

void UAedirInteractionComponent::CancelReach(EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerCancelReach(Hand);
		return;
	}

	FAedirHandSlot& Slot = GetSlot(Hand);
	if (Slot.State == EAedirHandState::Reaching)
	{
		ResetSlot(Slot);
	}
}

void UAedirInteractionComponent::DropHeld(EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerDropHeld(Hand);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Creature || !Item)
	{
		return;
	}

	ReleaseHeldItem(Item);

	const FVector ThrowVelocity = Creature->GetActorForwardVector() * ThrowSpeed + Creature->GetVelocity();
	Item->Detach(ThrowVelocity);

	UpdateCarriedWeight();
}

void UAedirInteractionComponent::UseHeld(EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerUseHeld(Hand);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = GetHeldItem(Hand);
	if (Creature && Item)
	{
		Item->Use(Creature);
	}
}

void UAedirInteractionComponent::HolsterToWaist(EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerHolsterToWaist(Hand);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	if (!Creature)
	{
		return;
	}

	switch (GetHandState(Hand))
	{
	case EAedirHandState::Holding:
		StoreHeldToWaist(Hand, Creature);
		break;
	case EAedirHandState::Idle:
		DrawFromWaist(Hand, Creature);
		break;
	default:
		break;
	}
}

EAedirHandState UAedirInteractionComponent::GetHandState(EAedirHand Hand) const
{
	return GetSlot(Hand).State;
}

AAedirItemActor* UAedirInteractionComponent::GetHeldItem(EAedirHand Hand) const
{
	const FAedirHandSlot& Slot = GetSlot(Hand);
	return Slot.State == EAedirHandState::Holding ? Slot.Item.Get() : nullptr;
}

float UAedirInteractionComponent::GetCarriedWeight() const
{
	float Total = 0.f;

	const AAedirItemActor* LeftItem = GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* RightItem = GetHeldItem(EAedirHand::Right);
	if (LeftItem)
	{
		Total += LeftItem->Weight;
	}
	if (RightItem && RightItem != LeftItem)
	{
		Total += RightItem->Weight;
	}

	for (const TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		if (WaistItem)
		{
			Total += WaistItem->Weight;
		}
	}

	return Total;
}

AAedirItemActor* UAedirInteractionComponent::GetHighlightedItem() const
{
	return HighlightedItem.Get();
}

bool UAedirInteractionComponent::GetReachPose(EAedirHand Hand, FVector& OutTargetWorld, float& OutAlpha) const
{
	const int32 Index = static_cast<int32>(Hand);
	OutTargetWorld = ReachTargetCache[Index];
	OutAlpha = ReachBlend[Index];
	return OutAlpha > KINDA_SMALL_NUMBER;
}

float UAedirInteractionComponent::GetPelvisLowerZ() const
{
	return PelvisLowerZ;
}

void UAedirInteractionComponent::BeginReach(EAedirHand Hand, AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	if (!Creature || !Item || !AAedirItemActor::IsPickable(Item))
	{
		return;
	}

	FAedirHandSlot& Slot = GetSlot(Hand);
	const FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	if (Slot.State != EAedirHandState::Idle)
	{
		return;
	}

	if (Item->Weight > Creature->GetCarryCapacity())
	{
		UE_LOG(LogAedir, Verbose, TEXT("%s cannot reach %s: weight %.1f exceeds carry capacity"), *GetNameSafe(Creature), *GetNameSafe(Item), Item->Weight);
		return;
	}

	if (Other.Item.Get() == Item)
	{
		return;
	}

	if (RequiresBothHands(Item) && Other.State != EAedirHandState::Idle)
	{
		return;
	}

	Slot.State = EAedirHandState::Reaching;
	Slot.Item = Item;
	Slot.ReachElapsed = 0.f;
	Slot.ReachHandWorld = GetHandWorld(Hand);
}

void UAedirInteractionComponent::GrabItem(EAedirHand Hand, AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	FAedirHandSlot& Slot = GetSlot(Hand);
	FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	if (!Creature || !Item)
	{
		ResetSlot(Slot);
		return;
	}

	const bool bBothHands = RequiresBothHands(Item);
	if (Item->Weight > Creature->GetCarryCapacity() || (bBothHands && Other.State != EAedirHandState::Idle))
	{
		ResetSlot(Slot);
		return;
	}

	Item->AttachToCreatureHand(Creature, Hand);

	Slot.State = EAedirHandState::Holding;
	Slot.Item = Item;
	Slot.ReachElapsed = 0.f;

	if (bBothHands)
	{
		Other.State = EAedirHandState::Holding;
		Other.Item = Item;
		Other.ReachElapsed = 0.f;
	}

	UpdateCarriedWeight();
}

void UAedirInteractionComponent::UpdateReach(EAedirHand Hand, float DeltaTime)
{
	FAedirHandSlot& Slot = GetSlot(Hand);
	if (Slot.State != EAedirHandState::Reaching)
	{
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = Slot.Item.Get();
	if (!Creature || !Item || !AAedirItemActor::IsPickable(Item))
	{
		ResetSlot(Slot);
		return;
	}

	Slot.ReachElapsed += DeltaTime;

	const FVector ItemCenter = Item->GetBoundsCenterWorld();
	const FVector ToItem = ItemCenter - Slot.ReachHandWorld;
	const float Step = ReachSpeed * DeltaTime;
	if (ToItem.SizeSquared() <= FMath::Square(Step))
	{
		Slot.ReachHandWorld = ItemCenter;
	}
	else
	{
		Slot.ReachHandWorld += ToItem.GetSafeNormal() * Step;
	}

	if (FVector::Dist(GetHandWorld(Hand), ItemCenter) < GrabDistance)
	{
		GrabItem(Hand, Item);
		return;
	}

	const bool bTimedOut = Slot.ReachElapsed >= ReachTimeout;
	const bool bOutOfRange = FVector::Dist(Creature->GetActorLocation(), ItemCenter) > PickupRange * ReachRangeMultiplier;
	if (bTimedOut || bOutOfRange)
	{
		ResetSlot(Slot);
	}
}

void UAedirInteractionComponent::UpdateReachBlend(float DeltaTime)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FAedirHandSlot& Slot = GetSlot(static_cast<EAedirHand>(Index));

		if (Slot.State == EAedirHandState::Reaching)
		{
			ReachBlend[Index] = FMath::Min(1.f, ReachBlend[Index] + DeltaTime / ReachBlendInSeconds);
		}
		else
		{
			ReachBlend[Index] = FMath::Max(0.f, ReachBlend[Index] - DeltaTime / ReachBlendOutSeconds);
		}

		if (Slot.Item)
		{
			ReachTargetCache[Index] = Slot.Item->GetBoundsCenterWorld();
		}
	}
}

void UAedirInteractionComponent::UpdatePelvisLower(float DeltaTime)
{
	float Target = 0.f;

	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* MeshComponent = Creature ? Creature->GetMesh() : nullptr;
	if (MeshComponent)
	{
		const float HipZ = MeshComponent->GetBoneLocation(WaistBone).Z;

		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FAedirHandSlot& Slot = GetSlot(static_cast<EAedirHand>(Index));
			if (Slot.State != EAedirHandState::Reaching || !Slot.Item)
			{
				continue;
			}

			const float Depth = HipZ - Slot.Item->GetBoundsCenterWorld().Z - ReachLowerStartHeight;
			Target = FMath::Max(Target, Depth * PelvisLowerFactor);
		}
	}

	Target = FMath::Clamp(Target, 0.f, KneeBendMaxLower);
	PelvisLowerZ = FMath::FInterpTo(PelvisLowerZ, Target, DeltaTime, PelvisLowerInterpSpeed);
}

void UAedirInteractionComponent::UpdateHighlight()
{
	AAedirItemActor* Best = FindBestItem();
	if (Best == HighlightedItem.Get())
	{
		return;
	}

	if (HighlightedItem)
	{
		HighlightedItem->SetHighlighted(false);
	}

	HighlightedItem = Best;

	if (HighlightedItem)
	{
		HighlightedItem->SetHighlighted(true);
	}
}

void UAedirInteractionComponent::UpdateCarriedWeight()
{
	if (AAedirCreature* Creature = GetCreature())
	{
		Creature->SetCarriedWeight(GetCarriedWeight());
	}
}

void UAedirInteractionComponent::StoreHeldToWaist(EAedirHand Hand, AAedirCreature* Creature)
{
	AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Item || Item->ItemClass == EAedirItemClass::Object)
	{
		return;
	}

	const int32 SideBegin = GetWaistSideBegin(Hand);
	int32 FreeIndex = INDEX_NONE;
	for (int32 Index = SideBegin; Index < SideBegin + WaistSlotsPerSide; ++Index)
	{
		if (!WaistItems[Index])
		{
			FreeIndex = Index;
			break;
		}
	}

	if (FreeIndex == INDEX_NONE)
	{
		return;
	}

	ReleaseHeldItem(Item);

	WaistItems[FreeIndex] = Item;
	LastStoredWaistIndex[GetWaistSideIndex(Hand)] = FreeIndex;

	const TArray<FTransform>& Slots = Hand == EAedirHand::Left ? LeftWaistSlots : RightWaistSlots;
	const int32 SlotIndex = FreeIndex - SideBegin;
	const FTransform SlotTransform = Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex] : FTransform::Identity;

	Item->AttachToWaist(Creature, WaistBone, SlotTransform);
	UpdateCarriedWeight();
}

void UAedirInteractionComponent::DrawFromWaist(EAedirHand Hand, AAedirCreature* Creature)
{
	FAedirHandSlot& Slot = GetSlot(Hand);
	const FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));

	const int32 Side = GetWaistSideIndex(Hand);
	const int32 SideBegin = GetWaistSideBegin(Hand);
	const int32 SideEnd = SideBegin + WaistSlotsPerSide;

	int32 SourceIndex = LastStoredWaistIndex[Side];
	const bool bLastValid = SourceIndex >= SideBegin && SourceIndex < SideEnd && WaistItems[SourceIndex];
	if (!bLastValid)
	{
		SourceIndex = INDEX_NONE;
		for (int32 Index = SideEnd - 1; Index >= SideBegin; --Index)
		{
			if (WaistItems[Index])
			{
				SourceIndex = Index;
				break;
			}
		}
	}

	if (SourceIndex == INDEX_NONE)
	{
		return;
	}

	AAedirItemActor* Item = WaistItems[SourceIndex].Get();
	if (RequiresBothHands(Item) && Other.State != EAedirHandState::Idle)
	{
		return;
	}

	WaistItems[SourceIndex] = nullptr;
	LastStoredWaistIndex[Side] = INDEX_NONE;

	Item->AttachToCreatureHand(Creature, Hand);

	Slot.State = EAedirHandState::Holding;
	Slot.Item = Item;

	if (RequiresBothHands(Item))
	{
		FAedirHandSlot& OtherSlot = GetSlot(GetOtherHand(Hand));
		OtherSlot.State = EAedirHandState::Holding;
		OtherSlot.Item = Item;
	}

	UpdateCarriedWeight();
}

void UAedirInteractionComponent::ReleaseHeldItem(const AAedirItemActor* Item)
{
	ClearSlotIfHolding(LeftSlot, Item);
	ClearSlotIfHolding(RightSlot, Item);
}

void UAedirInteractionComponent::ClearSlotIfHolding(FAedirHandSlot& Slot, const AAedirItemActor* Item)
{
	if (Slot.State == EAedirHandState::Holding && Slot.Item.Get() == Item)
	{
		ResetSlot(Slot);
	}
}

FAedirHandSlot& UAedirInteractionComponent::GetSlot(EAedirHand Hand)
{
	return Hand == EAedirHand::Left ? LeftSlot : RightSlot;
}

const FAedirHandSlot& UAedirInteractionComponent::GetSlot(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? LeftSlot : RightSlot;
}

AAedirItemActor* UAedirInteractionComponent::FindBestItem() const
{
	AAedirCreature* Creature = GetCreature();
	const UWorld* World = GetWorld();
	if (!Creature || !World)
	{
		return nullptr;
	}

	const FVector ViewLocation = Creature->GetPawnViewLocation();
	const FRotator AimRotation = Creature->Controller ? Creature->GetControlRotation() : Creature->GetActorRotation();
	const FVector AimDirection = AimRotation.Vector();
	const FVector CreatureLocation = Creature->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	const FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllDynamicObjects);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Creature);
	World->OverlapMultiByObjectType(Overlaps, CreatureLocation, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(PickupRange), QueryParams);

	AAedirItemActor* Best = nullptr;
	float BestDot = 0.f;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirItemActor* Item = Cast<AAedirItemActor>(Overlap.GetActor());
		if (!AAedirItemActor::IsPickable(Item))
		{
			continue;
		}

		const FVector ItemCenter = Item->GetBoundsCenterWorld();
		if (FVector::Dist(CreatureLocation, ItemCenter) > PickupRange)
		{
			continue;
		}

		const FVector ToItem = (ItemCenter - ViewLocation).GetSafeNormal();
		const float Dot = FVector::DotProduct(AimDirection, ToItem);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			Best = Item;
		}
	}

	return Best;
}

AAedirCreature* UAedirInteractionComponent::GetCreature() const
{
	return Cast<AAedirCreature>(GetOwner());
}

FVector UAedirInteractionComponent::GetHandWorld(EAedirHand Hand) const
{
	const AAedirCreature* Creature = GetCreature();
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const USkeletalMeshComponent* MeshComponent = Creature->GetMesh();
	if (!MeshComponent)
	{
		return Creature->GetActorLocation();
	}

	return MeshComponent->GetBoneLocation(Hand == EAedirHand::Left ? LeftHandBone : RightHandBone);
}

bool UAedirInteractionComponent::IsAuthoritative() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UAedirInteractionComponent::ServerStartReachForItem_Implementation(EAedirHand Hand, AAedirItemActor* Item)
{
	BeginReach(Hand, Item);
}

void UAedirInteractionComponent::ServerCancelReach_Implementation(EAedirHand Hand)
{
	CancelReach(Hand);
}

void UAedirInteractionComponent::ServerDropHeld_Implementation(EAedirHand Hand)
{
	DropHeld(Hand);
}

void UAedirInteractionComponent::ServerUseHeld_Implementation(EAedirHand Hand)
{
	UseHeld(Hand);
}

void UAedirInteractionComponent::ServerHolsterToWaist_Implementation(EAedirHand Hand)
{
	HolsterToWaist(Hand);
}
