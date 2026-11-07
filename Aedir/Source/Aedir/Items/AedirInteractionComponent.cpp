#include "AedirInteractionComponent.h"

#include "Aedir.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Items/AedirItemActor.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr int32 WaistSlotsPerSide = 3;
	constexpr float ReachBlendInSeconds = 0.15f;
	constexpr float ReachBlendOutSeconds = 0.2f;
	constexpr float PelvisLowerInterpSpeed = 2.6f;
	constexpr float PelvisRiseInterpSpeed = 1.8f;
	// Standing proportions of the body in unscaled cm, used to work out how far it has to bend for a low item.
	constexpr float ReachPelvisHeightCm = 95.f;
	constexpr float ReachTorsoLengthCm = 48.f;
	constexpr float ReachArmLengthCm = 44.f;
	constexpr float ReachShoulderSideCm = 18.f;
	constexpr float ReachMaxLowerCm = 54.f;
	constexpr float ReachMaxHipBackCm = 30.f;
	constexpr float ReachMaxHipForwardCm = 25.f;
	constexpr float ReachMinLeanDegrees = 15.f;
	constexpr float ReachMaxLeanDegrees = 90.f;
	constexpr float ReachSpineChordShare = 0.8f;
	constexpr float ReachHipSideShare = 0.5f;
	constexpr float ReachMaxHipSideCm = 22.f;
	constexpr float ReachMaxTwistDegrees = 50.f;
	constexpr float ReachLeanStepDegrees = 5.f;
	constexpr float HolsterHandSeconds = 0.85f;
	constexpr float HolsterBlendInSeconds = 0.3f;
	// The arm goes down in front of the belly, not straight through the body.
	constexpr float HolsterFrontForwardCm = 34.f;
	constexpr float HolsterFrontSideCm = 8.f;
	constexpr float HolsterFrontUpCm = 6.f;
	constexpr float HolsterAimStart = 0.2f;
	constexpr float HolsterAimEnd = 0.85f;
	// The blade goes over the top on its way to and from the belt, with the thumb up: below, the wrist would have to turn all the way round.
	constexpr float HolsterOverForward = 0.2f;
	constexpr float HolsterOverSide = 0.35f;
	constexpr float HolsterDrawSettleSeconds = 0.6f;
	// Passing a weapon: both hands come together before the chest and the other hand takes it there.
	constexpr float PassHandSeconds = 0.95f;
	constexpr float PassGiveUpSeconds = 1.5f;
	constexpr float PassBlendInSeconds = 0.55f;
	constexpr float PassMeetForwardCm = 32.f;
	constexpr float PassMeetUpCm = 18.f;
	constexpr float WaistSlotRadiusCm = 22.f;
	constexpr float WaistSlotUpCm = -4.f;
	constexpr float WaistSlotYawDegrees[3] = { 95.f, 130.f, 160.f };
	constexpr float SwordHangBackShare = 1.15f;
	constexpr float BigObjectHandClearanceCm = 7.f;
	constexpr float BigObjectHandSpanShare = 0.9f;
	constexpr float TwoHandLiftMultiplier = 2.5f;
	constexpr float OneHandObjectLiftShare = 0.6f;
	constexpr float ObjectStrainSeconds = 1.5f;
	constexpr float ObjectThrowWeightKg = 1.f;
	constexpr float ReachRangeMultiplier = 1.5f;
	constexpr float HighlightInterval = 0.1f;
	constexpr float BackpackDropDistance = 60.f;
	constexpr float BackpackDropHeight = 30.f;
	// Food in the hand is held before the belly; at the mouth the hand stops short so that what it holds touches the lips.
	constexpr float ConsumableBlendInSeconds = 0.35f;
	constexpr float ConsumableRestForwardCm = 30.f;
	constexpr float ConsumableRestSideCm = 18.f;
	constexpr float ConsumableRestUpCm = 8.f;
	constexpr float MouthHeadUpCm = 3.f;
	constexpr float MouthHeadForwardCm = 7.f;
	constexpr float MouthHandSideCm = 3.f;
	constexpr float MouthHandDownCm = 14.f;
	constexpr float MouthSeconds = 0.7f;
	constexpr float BiteSeconds = 1.1f;
	constexpr float MouthDrinkTipUp = 0.45f;
	constexpr float MouthDrinkTipBack = 0.9f;

	EAedirHand GetOtherHand(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	}

	float HolsterSmoothStep(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	bool RequiresBothHands(const AAedirItemActor* Item)
	{
		return Item && Item->bTwoHanded;
	}

	// A hand wears its items on the opposite hip, where it can draw them across the body.
	int32 GetWaistSideBegin(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? WaistSlotsPerSide : 0;
	}

	int32 GetWaistSideIndex(EAedirHand Hand)
	{
		return Hand == EAedirHand::Left ? 1 : 0;
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
		UpdateObjectStrain(DeltaTime);
		UpdateBodyGrabs(DeltaTime);
	}

	UpdateHolsterMotion(EAedirHand::Left, DeltaTime);
	UpdateHolsterMotion(EAedirHand::Right, DeltaTime);
	UpdateMouth(EAedirHand::Left, DeltaTime);
	UpdateMouth(EAedirHand::Right, DeltaTime);
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
	DOREPLIFETIME(UAedirInteractionComponent, WornItems);
	DOREPLIFETIME(UAedirInteractionComponent, Backpack);
	DOREPLIFETIME(UAedirInteractionComponent, MouthHands);
}

void UAedirInteractionComponent::StartReach(EAedirHand Hand)
{
	const EAedirHandState State = GetHandState(Hand);
	if (State == EAedirHandState::Holding)
	{
		DropHeld(Hand);
		return;
	}

	// A hand on a body lets go.
	if (IsGrabbingBody(Hand))
	{
		ReleaseBodyGrab(Hand);
		return;
	}

	if (State != EAedirHandState::Idle)
	{
		return;
	}

	// Looking at a body, the hand goes for it before anything lying about.
	FVector AimedBody;
	if (FindAimedBody(AimedBody) && TryGrabBody(Hand, &AimedBody))
	{
		return;
	}

	// The free hand joins the other one on a big object it is holding.
	AAedirItemActor* OtherHeld = GetHeldItem(GetOtherHand(Hand));
	if (OtherHeld && OtherHeld->ItemClass == EAedirItemClass::Object)
	{
		StartReachForItem(Hand, OtherHeld);
		return;
	}

	// With nothing else in reach, the free hand joins the other one on a weapon made for two hands.
	AAedirItemActor* Target = FindBestItem();
	// With no item to take, the hand takes hold of a body or a piece of one.
	if (!Target && !(OtherHeld && OtherHeld->bTwoHanded) && TryGrabBody(Hand))
	{
		return;
	}
	StartReachForItem(Hand, !Target && OtherHeld && OtherHeld->bTwoHanded ? OtherHeld : Target);
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

	// A two-handed weapon gripped by both hands is let go by one hand only; the other hand keeps holding it.
	const EAedirHand OtherHand = GetOtherHand(Hand);
	if (GetHeldItem(OtherHand) == Item)
	{
		ResetSlot(GetSlot(Hand));
		if (Item->ItemClass == EAedirItemClass::Object)
		{
			Item->AttachToHandInPlace(Creature, OtherHand);
		}
		else if (Item->HeldHand == Hand)
		{
			Item->AttachToCreatureHand(Creature, OtherHand);
		}
		UpdateCarriedWeight();
		return;
	}

	ReleaseHeldItem(Item);

	Item->Detach(GetTossVelocity(Item, 1.f));

	UpdateCarriedWeight();
}

FVector UAedirInteractionComponent::GetTossVelocity(const AAedirItemActor* Item, float SpeedShare) const
{
	// A light item is tossed a little, a heavy one just falls.
	const AAedirCreature* Creature = GetCreature();
	const float TossShare = FMath::Min(ObjectThrowWeightKg / FMath::Max(Item->Weight, KINDA_SMALL_NUMBER), 1.f);
	return Creature->GetActorForwardVector() * (ThrowSpeed * TossShare * SpeedShare) + Creature->GetVelocity();
}

void UAedirInteractionComponent::SwapGripOrder()
{
	if (!IsAuthoritative())
	{
		ServerSwapGripOrder();
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = GetHeldItem(EAedirHand::Right);
	if (!Creature || !Item || Item != GetHeldItem(EAedirHand::Left) || Item->ItemClass == EAedirItemClass::Object || Item->HeldGripShiftCm > 0.f)
	{
		return;
	}

	// The hand that followed the weapon at the pommel moves up towards the blade, the other one takes the pommel.
	Item->AttachToCreatureHand(Creature, GetOtherHand(Item->HeldHand));
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

	BeginHolsterMotion(Hand);
}

int32 UAedirInteractionComponent::FindWaistWeaponSlot() const
{
	const auto IsWeaponAt = [this](int32 Index)
	{
		const AAedirItemActor* Item = WaistItems.IsValidIndex(Index) ? WaistItems[Index].Get() : nullptr;
		return Item && Item->ItemClass == EAedirItemClass::Weapon;
	};
	for (const int32 Last : LastStoredWaistIndex)
	{
		if (IsWeaponAt(Last))
		{
			return Last;
		}
	}
	for (int32 Index = 0; Index < WaistItems.Num(); ++Index)
	{
		if (IsWeaponAt(Index))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UAedirInteractionComponent::CanDrawWaistSlot(EAedirHand Hand, int32 SlotIndex) const
{
	const AAedirItemActor* Item = WaistItems.IsValidIndex(SlotIndex) ? WaistItems[SlotIndex].Get() : nullptr;
	return Item && GetHandState(Hand) == EAedirHandState::Idle && HolsterMotion[static_cast<int32>(Hand)] == EAedirHolsterMotion::None;
}

void UAedirInteractionComponent::DrawWaistSlot(EAedirHand Hand, int32 SlotIndex)
{
	if (!IsAuthoritative())
	{
		ServerDrawWaistSlot(Hand, SlotIndex);
		return;
	}

	if (!CanDrawWaistSlot(Hand, SlotIndex))
	{
		return;
	}

	const int32 Index = static_cast<int32>(Hand);
	HolsterWaistIndex[Index] = SlotIndex;
	HolsterMotion[Index] = EAedirHolsterMotion::Draw;
	StartHolsterPath(Hand);
}

bool UAedirInteractionComponent::IsHolsterMoving() const
{
	return HolsterMotion[0] != EAedirHolsterMotion::None || HolsterMotion[1] != EAedirHolsterMotion::None;
}

void UAedirInteractionComponent::BeginHolsterMotion(EAedirHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	if (HolsterMotion[Index] != EAedirHolsterMotion::None)
	{
		return;
	}

	const AAedirItemActor* Held = GetHeldItem(Hand);
	if (GetHandState(Hand) == EAedirHandState::Holding && Held && Held->FitsOnBelt())
	{
		// Only weapons hang on the belt. Anything else goes the same way, but into the pack behind it.
		const bool bWeapon = Held->ItemClass == EAedirItemClass::Weapon;
		HolsterWaistIndex[Index] = bWeapon ? FindFreeWaistIndex(Hand) : GetWaistSideBegin(Hand);
		HolsterMotion[Index] = HolsterWaistIndex[Index] != INDEX_NONE ? EAedirHolsterMotion::Store : EAedirHolsterMotion::None;
		MouthHands &= ~(1 << Index);
	}
	else if (GetHandState(Hand) == EAedirHandState::Idle)
	{
		HolsterWaistIndex[Index] = FindDrawWaistIndex(Hand);
		HolsterMotion[Index] = HolsterWaistIndex[Index] != INDEX_NONE ? EAedirHolsterMotion::Draw : EAedirHolsterMotion::None;
	}
	StartHolsterPath(Hand);
}

void UAedirInteractionComponent::StartHolsterPath(EAedirHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	HolsterElapsed[Index] = 0.f;
	if (const AAedirCreature* Creature = GetCreature())
	{
		HolsterStartLocal[Index] = Creature->GetActorTransform().InverseTransformPositionNoScale(GetHandWorld(Hand));
	}
}

bool UAedirInteractionComponent::GetHolsterAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutShare) const
{
	const int32 Index = static_cast<int32>(Hand);
	const bool bStoring = HolsterMotion[Index] == EAedirHolsterMotion::Store;
	const AAedirItemActor* Item = bStoring || HolsterDrawSettle[Index] > 0.f ? GetHeldItem(Hand) : nullptr;
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* BodyMesh = Creature ? Creature->GetMesh() : nullptr;
	if (!Item || !BodyMesh)
	{
		return false;
	}

	const float SlotSide = HolsterWaistIndex[Index] < WaistSlotsPerSide ? -1.f : 1.f;
	const FVector Over = (Creature->GetActorForwardVector() * HolsterOverForward + Creature->GetActorRightVector() * (SlotSide * HolsterOverSide) + FVector::UpVector).GetSafeNormal();

	// Just drawn, the blade goes back the way it came in: up out of the belt over the side of the body, then where the stance wants it.
	if (!bStoring)
	{
		const float Settled = 1.f - HolsterDrawSettle[Index] / HolsterDrawSettleSeconds;
		OutDirectionWorld = Settled < 0.5f ? FMath::Lerp(HolsterDrawFrom[Index], Over, HolsterSmoothStep(Settled * 2.f)).GetSafeNormal() : Over;
		OutShare = FMath::Min((1.f - Settled) * 2.f, 1.f);
		return true;
	}

	FVector SlotOnBone;
	FQuat RotationOnBone;
	float PivotCm = 0.f;
	GetWaistSlot(HolsterWaistIndex[Index], Item, SlotOnBone, RotationOnBone, PivotCm);
	const FVector HangDirection = BodyMesh->GetSocketTransform(WaistBone).TransformVectorNoScale(RotationOnBone.GetAxisX());

	// First up, then over the side of the body down into the slot.
	const float Share = HolsterSmoothStep((HolsterElapsed[Index] / HolsterHandSeconds - HolsterAimStart) / (HolsterAimEnd - HolsterAimStart));
	OutDirectionWorld = Share < 0.5f ? Over : FMath::Lerp(Over, HangDirection, (Share - 0.5f) * 2.f).GetSafeNormal();
	OutShare = FMath::Min(Share * 2.f, 1.f);
	return true;
}

FVector UAedirInteractionComponent::GetMouthWorld() const
{
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* BodyMesh = Creature ? Creature->GetMesh() : nullptr;
	return BodyMesh ? BodyMesh->GetBoneLocation(FName(TEXT("head"))) + Creature->GetActorForwardVector() * MouthHeadForwardCm + FVector(0.f, 0.f, MouthHeadUpCm) : FVector::ZeroVector;
}

float UAedirInteractionComponent::GetMouthShare(EAedirHand Hand) const
{
	return HolsterSmoothStep(MouthElapsed[static_cast<int32>(Hand)] / MouthSeconds);
}

FVector UAedirInteractionComponent::GetConsumableHandTarget(EAedirHand Hand, const AAedirItemActor* Item) const
{
	const AAedirCreature* Creature = GetCreature();
	if (!Creature || !Item)
	{
		return FVector::ZeroVector;
	}

	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	const FVector Rest = Creature->GetActorTransform().TransformPositionNoScale(FVector(ConsumableRestForwardCm, Side * ConsumableRestSideCm, ConsumableRestUpCm));
	const FVector Forward = Creature->GetActorForwardVector();
	const FVector Right = Creature->GetActorRightVector();
	// What the hand holds comes to the lips from below and from its own side, a drink with its top to them.
	const float Reach = Item->bDrinkable ? static_cast<float>(Item->BodyScale.X) * 50.f : 0.f;
	const FVector AtMouth = GetMouthWorld() + Right * (Side * MouthHandSideCm) + Forward * (MouthHandSideCm + Reach * MouthDrinkTipBack) - FVector(0.f, 0.f, MouthHandDownCm + Reach * MouthDrinkTipUp);
	return FMath::Lerp(Rest, AtMouth, static_cast<double>(GetMouthShare(Hand)));
}

bool UAedirInteractionComponent::GetConsumableAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const
{
	const AAedirCreature* Creature = GetCreature();
	const AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Creature || !Item || !Item->IsConsumable() || HolsterMotion[static_cast<int32>(Hand)] != EAedirHolsterMotion::None)
	{
		return false;
	}

	const FVector Forward = Creature->GetActorForwardVector();
	const FVector Right = Creature->GetActorRightVector();
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	// A drink stands upright and tips its top back towards the face; food lies across the hand and turns its end to the mouth.
	const FVector RestAim = Item->bDrinkable ? (FVector::UpVector + Forward * 0.1f).GetSafeNormal() : (Forward - Right * (Side * 0.5f) + FVector::UpVector * 0.25f).GetSafeNormal();
	const FVector MouthAim = Item->bDrinkable ? (FVector::UpVector * MouthDrinkTipUp - Forward * MouthDrinkTipBack).GetSafeNormal() : (FVector::UpVector * 0.4f - Forward * 0.5f - Right * (Side * 0.8f)).GetSafeNormal();
	OutDirectionWorld = FMath::Lerp(RestAim, MouthAim, static_cast<double>(GetMouthShare(Hand))).GetSafeNormal();
	OutAlpha = ReachBlend[static_cast<int32>(Hand)];
	return OutAlpha > 0.f;
}

void UAedirInteractionComponent::SetMouthHold(EAedirHand Hand, bool bHold)
{
	if (!IsAuthoritative())
	{
		ServerSetMouthHold(Hand, bHold);
		return;
	}

	const AAedirItemActor* Item = GetHeldItem(Hand);
	const int32 Bit = 1 << static_cast<int32>(Hand);
	MouthHands = bHold && Item && Item->IsConsumable() ? (MouthHands | Bit) : (MouthHands & ~Bit);
}

void UAedirInteractionComponent::ServerSetMouthHold_Implementation(EAedirHand Hand, bool bHold)
{
	SetMouthHold(Hand, bHold);
}

void UAedirInteractionComponent::UpdateMouth(EAedirHand Hand, float DeltaTime)
{
	const int32 Index = static_cast<int32>(Hand);
	AAedirItemActor* Item = GetHeldItem(Hand);
	const bool bHold = IsMouthHold(Hand) && Item && Item->IsConsumable();
	MouthElapsed[Index] = FMath::Clamp(MouthElapsed[Index] + (bHold ? DeltaTime : -DeltaTime), 0.f, MouthSeconds);
	if (!bHold || MouthElapsed[Index] < MouthSeconds)
	{
		BiteElapsed[Index] = 0.f;
		return;
	}

	BiteElapsed[Index] += DeltaTime;
	if (BiteElapsed[Index] < BiteSeconds || !IsAuthoritative())
	{
		return;
	}

	BiteElapsed[Index] = 0.f;
	if (AAedirCreature* Creature = GetCreature())
	{
		Item->Use(Creature);
		// The last bite leaves nothing to hold.
		if (!IsValid(Item) || Item->IsActorBeingDestroyed())
		{
			ReleaseHeldItem(Item);
			MouthHands &= ~(1 << Index);
			UpdateCarriedWeight();
		}
	}
}

void UAedirInteractionComponent::DrawFromBackpack(int32 Index, EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerDrawFromBackpack(Index, Hand);
		return;
	}

	const int32 HandIndex = static_cast<int32>(Hand);
	AAedirItemActor* Item = Backpack.IsValidIndex(Index) ? Backpack[Index].Get() : nullptr;
	if (!Item || GetHandState(Hand) != EAedirHandState::Idle || HolsterMotion[HandIndex] != EAedirHolsterMotion::None)
	{
		return;
	}

	BackpackDrawItem[HandIndex] = Item;
	HolsterWaistIndex[HandIndex] = GetWaistSideBegin(Hand);
	HolsterMotion[HandIndex] = EAedirHolsterMotion::Draw;
	StartHolsterPath(Hand);
}

void UAedirInteractionComponent::ServerDrawFromBackpack_Implementation(int32 Index, EAedirHand Hand)
{
	DrawFromBackpack(Index, Hand);
}

void UAedirInteractionComponent::StowNewItem(AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	if (IsAuthoritative() && Creature && Item)
	{
		Item->AttachToBackpack(Creature);
		Backpack.Add(Item);
		UpdateCarriedWeight();
	}
}

void UAedirInteractionComponent::RemoveOwnedItem(AAedirItemActor* Item)
{
	if (!IsAuthoritative() || !Item)
	{
		return;
	}

	ReleaseHeldItem(Item);
	Backpack.Remove(Item);
	WornItems.Remove(Item);
	for (TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		if (WaistItem == Item)
		{
			WaistItem = nullptr;
		}
	}
	Item->Destroy();
	UpdateCarriedWeight();
}

bool UAedirInteractionComponent::GetDrawAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const
{
	const int32 Index = static_cast<int32>(Hand);
	const AAedirItemActor* Item = HolsterMotion[Index] == EAedirHolsterMotion::Draw && !BackpackDrawItem[Index] && WaistItems.IsValidIndex(HolsterWaistIndex[Index]) ? WaistItems[HolsterWaistIndex[Index]].Get() : nullptr;
	if (!Item || Item->ItemClass != EAedirItemClass::Weapon)
	{
		return false;
	}

	// The same turn as putting it away: thumb up first, then over the side of the body down to how it hangs.
	const AAedirCreature* Creature = GetCreature();
	if (!Creature)
	{
		return false;
	}
	const float SlotSide = HolsterWaistIndex[Index] < WaistSlotsPerSide ? -1.f : 1.f;
	const FVector Over = (Creature->GetActorForwardVector() * HolsterOverForward + Creature->GetActorRightVector() * (SlotSide * HolsterOverSide) + FVector::UpVector).GetSafeNormal();
	const float Share = HolsterSmoothStep((HolsterElapsed[Index] / HolsterHandSeconds - HolsterAimStart) / (HolsterAimEnd - HolsterAimStart));
	OutDirectionWorld = Share < 0.5f ? Over : FMath::Lerp(Over, Item->GetActorForwardVector(), (Share - 0.5f) * 2.f).GetSafeNormal();
	OutAlpha = FMath::Min(Share * 2.f, 1.f);
	return OutAlpha > 0.f;
}

bool UAedirInteractionComponent::IsPassingHeld(EAedirHand Hand) const
{
	const FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	const AAedirItemActor* Item = GetHeldItem(Hand);
	return Item && !Item->bTwoHanded && Item->ItemClass == EAedirItemClass::Weapon && Other.State == EAedirHandState::Reaching && Other.Item.Get() == Item;
}

void UAedirInteractionComponent::UpdateHolsterMotion(EAedirHand Hand, float DeltaTime)
{
	const int32 Index = static_cast<int32>(Hand);
	const EAedirHolsterMotion Motion = HolsterMotion[Index];
	HolsterDrawSettle[Index] = FMath::Max(HolsterDrawSettle[Index] - DeltaTime, 0.f);
	if (Motion == EAedirHolsterMotion::None)
	{
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* FromPack = BackpackDrawItem[Index].Get();
	const AAedirItemActor* Item = Motion == EAedirHolsterMotion::Store ? GetHeldItem(Hand) : (FromPack ? FromPack : (WaistItems.IsValidIndex(HolsterWaistIndex[Index]) ? WaistItems[HolsterWaistIndex[Index]].Get() : nullptr));
	if (!Creature || !Item)
	{
		HolsterMotion[Index] = EAedirHolsterMotion::None;
		BackpackDrawItem[Index] = nullptr;
		return;
	}

	HolsterElapsed[Index] += DeltaTime;
	if (HolsterElapsed[Index] < HolsterHandSeconds)
	{
		return;
	}

	HolsterMotion[Index] = EAedirHolsterMotion::None;
	HolsterDrawSettle[Index] = Motion == EAedirHolsterMotion::Draw && !FromPack ? HolsterDrawSettleSeconds : 0.f;
	if (IsAuthoritative())
	{
		if (Motion == EAedirHolsterMotion::Store)
		{
			StoreHeldToWaist(Hand, Creature);
		}
		else if (FromPack)
		{
			BackpackDrawItem[Index] = nullptr;
			TakeFromBackpack(Backpack.IndexOfByKey(FromPack), Hand);
		}
		else
		{
			DrawFromWaist(Hand, Creature, HolsterWaistIndex[Index]);
		}
	}
}

int32 UAedirInteractionComponent::FindFreeWaistIndex(EAedirHand Hand) const
{
	const int32 SideBegin = GetWaistSideBegin(Hand);
	for (int32 Index = SideBegin; Index < SideBegin + WaistSlotsPerSide; ++Index)
	{
		if (!WaistItems[Index])
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UAedirInteractionComponent::FindDrawWaistIndex(EAedirHand Hand) const
{
	const int32 SideBegin = GetWaistSideBegin(Hand);
	const int32 SideEnd = SideBegin + WaistSlotsPerSide;
	const int32 Last = LastStoredWaistIndex[GetWaistSideIndex(Hand)];
	if (Last >= SideBegin && Last < SideEnd && WaistItems[Last])
	{
		return Last;
	}
	for (int32 Index = SideEnd - 1; Index >= SideBegin; --Index)
	{
		if (WaistItems[Index])
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UAedirInteractionComponent::GetWaistSlot(int32 WaistIndex, const AAedirItemActor* Item, FVector& OutSlotOnBone, FQuat& OutRotationOnBone, float& OutPivotCm) const
{
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* BodyMesh = Creature ? Creature->GetMesh() : nullptr;
	const USkeletalMesh* BodyAsset = BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	if (!BodyAsset)
	{
		OutSlotOnBone = FVector::ZeroVector;
		OutRotationOnBone = FQuat::Identity;
		OutPivotCm = 0.f;
		return;
	}

	// The slots run around the hip from the side towards the back. They are worked out in the frame of the body and
	// converted to the waist bone, whose own axes point anywhere.
	const float Side = WaistIndex < WaistSlotsPerSide ? -1.f : 1.f;
	const int32 Row = WaistIndex % WaistSlotsPerSide;
	const float YawRadians = FMath::DegreesToRadians(WaistSlotYawDegrees[FMath::Clamp(Row, 0, 2)]);
	const FVector Outward(FMath::Cos(YawRadians), Side * FMath::Sin(YawRadians), 0.f);
	const FVector SlotInBody = Outward * WaistSlotRadiusCm + FVector(0.f, 0.f, WaistSlotUpCm);

	// Swords hang point down and back with the hilt forward ready to draw. Tools hang by the head, smaller items straight down.
	const bool bWeapon = Item && Item->ItemClass == EAedirItemClass::Weapon;
	const bool bDiagonal = Item && Item->bHangsDiagonal;
	FVector LongAxis = FVector(0.f, 0.f, -1.f);
	OutPivotCm = 0.f;
	if (bDiagonal)
	{
		LongAxis = FVector(-SwordHangBackShare, 0.f, -1.f).GetSafeNormal();
		OutPivotCm = Item->GripLocalCm + Item->GripSpanCm;
	}
	else if (bWeapon)
	{
		LongAxis = FVector::UpVector;
		OutPivotCm = -Item->GripLocalCm * 0.5f;
	}
	// The flat of the item lies against the body, its thin side facing out.
	const FQuat RotationInBody = FRotationMatrix::MakeFromXZ(LongAxis, Outward).ToQuat();

	const FQuat BoneInMesh = BodyAsset->GetComposedRefPoseMatrix(WaistBone).ToQuat();
	const FQuat BodyToBone = BoneInMesh.Inverse() * BodyMesh->GetRelativeRotation().Quaternion().Inverse();
	OutSlotOnBone = BodyToBone.RotateVector(SlotInBody);
	OutRotationOnBone = BodyToBone * RotationInBody;
}

FVector UAedirInteractionComponent::GetWaistSlotWorld(int32 WaistIndex, const AAedirItemActor* Item) const
{
	const AAedirCreature* Creature = GetCreature();
	const USkeletalMeshComponent* BodyMesh = Creature ? Creature->GetMesh() : nullptr;
	if (!BodyMesh)
	{
		return FVector::ZeroVector;
	}

	FVector SlotOnBone;
	FQuat RotationOnBone;
	float PivotCm = 0.f;
	GetWaistSlot(WaistIndex, Item, SlotOnBone, RotationOnBone, PivotCm);
	return BodyMesh->GetSocketTransform(WaistBone).TransformPosition(SlotOnBone);
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

	for (const TObjectPtr<AAedirItemActor>& WornItem : WornItems)
	{
		if (WornItem)
		{
			Total += WornItem->Weight;
		}
	}

	for (const TObjectPtr<AAedirItemActor>& StowedItem : Backpack)
	{
		if (StowedItem)
		{
			Total += StowedItem->Weight;
		}
	}

	return Total;
}

float UAedirInteractionComponent::GetWornArmorProtection(EAedirBodyZone Zone) const
{
	float Best = 0.f;
	for (const TObjectPtr<AAedirItemActor>& WornItem : WornItems)
	{
		if (WornItem && WornItem->ArmorZone == Zone)
		{
			Best = FMath::Max(Best, WornItem->ArmorProtection);
		}
	}

	return Best;
}

AAedirItemActor* UAedirInteractionComponent::GetHighlightedItem() const
{
	return HighlightedItem.Get();
}

bool UAedirInteractionComponent::GetReachPose(EAedirHand Hand, FVector& OutTargetWorld, float& OutAlpha) const
{
	const int32 Index = static_cast<int32>(Hand);
	// A hand on a body stays reached out to where it holds it.
	if (GetBodyGrabPoint(Hand, OutTargetWorld))
	{
		OutAlpha = 1.f;
		return true;
	}
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
	FAedirHandSlot& Slot = GetSlot(Hand);
	FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	const bool bJoiningOtherHand = Item && Other.State == EAedirHandState::Holding && Other.Item.Get() == Item;
	if (!Creature || !Item || (!AAedirItemActor::IsPickable(Item) && !bJoiningOtherHand))
	{
		return;
	}

	if (Slot.State != EAedirHandState::Idle)
	{
		return;
	}

	if (!CanLift(Item))
	{
		UE_LOG(LogAedir, Verbose, TEXT("%s cannot reach %s: weight %.1f exceeds what it can lift"), *GetNameSafe(Creature), *GetNameSafe(Item), Item->Weight);
		return;
	}

	// The second key on the same item sends the second hand after the first.
	const bool bOtherOnThisItem = Other.Item.Get() == Item;

	if (!bOtherOnThisItem || bJoiningOtherHand)
	{
		ReachLeadHand = Hand;
	}
	HoldStrainSeconds = 0.f;
	bReachingToTakeOver[static_cast<int32>(Hand)] = false;

	Slot.State = EAedirHandState::Reaching;
	Slot.Item = Item;
	Slot.ReachElapsed = 0.f;
	Slot.ReachHandWorld = GetHandWorld(Hand);

	// Something that takes two hands is reached for with both if the other one is free, whichever key was pressed. With the other hand busy one has to do.
	if (RequiresBothHands(Item) && Other.State == EAedirHandState::Idle)
	{
		Other.State = EAedirHandState::Reaching;
		Other.Item = Item;
		Other.ReachElapsed = 0.f;
		Other.ReachHandWorld = GetHandWorld(GetOtherHand(Hand));
	}
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

	// Both hands close on the item if it needs them or if both are already on their way to it.
	const bool bOtherOnThisItem = Other.State == EAedirHandState::Reaching && Other.Item.Get() == Item;
	const bool bOtherHolds = Other.State == EAedirHandState::Holding && Other.Item.Get() == Item;
	const bool bBothHands = (RequiresBothHands(Item) && Other.State == EAedirHandState::Idle) || bOtherOnThisItem || (bOtherHolds && RequiresBothHands(Item));
	if (!CanLift(Item))
	{
		ResetSlot(Slot);
		return;
	}

	const int32 HandIndex = static_cast<int32>(Hand);
	if (bReachingToTakeOver[HandIndex])
	{
		bReachingToTakeOver[HandIndex] = false;
		ResetSlot(Other);
		Item->AttachToCreatureHand(Creature, Hand);
		if (Creature->Combat)
		{
			Creature->Combat->SetWeaponDirNow(Hand, Item->GetActorForwardVector());
		}
		Slot.State = EAedirHandState::Holding;
		Slot.Item = Item;
		Slot.ReachElapsed = 0.f;
		UpdateCarriedWeight();
		return;
	}

	if (Item->ItemClass == EAedirItemClass::Object)
	{
		// A big object has no handle: one hand closes where it touches, and with the second hand it comes to the middle of the body.
		if (Other.State == EAedirHandState::Holding)
		{
			Item->AttachCarried(Creature);
		}
		else
		{
			Item->AttachToHandInPlace(Creature, Hand);
		}

		Slot.State = EAedirHandState::Holding;
		Slot.Item = Item;
		Slot.ReachElapsed = 0.f;
		UpdateCarriedWeight();
		return;
	}

	// A weapon gripped in both hands is gripped higher by the hand whose key started the reach: the weapon follows the other hand
	// at the pommel and the lead hand sits above it towards the blade.
	Item->AttachToCreatureHand(Creature, bBothHands ? GetOtherHand(ReachLeadHand) : Hand);

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
	const bool bOtherHolds = GetHeldItem(GetOtherHand(Hand)) == Item;
	if (!Creature || !Item || (!AAedirItemActor::IsPickable(Item) && !bOtherHolds))
	{
		ResetSlot(Slot);
		return;
	}

	Slot.ReachElapsed += DeltaTime;

	const FVector ItemCenter = GetReachTargetWorld(Hand, Item);
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

	// A pass takes its time, and happens once the hands have met.
	const bool bPassing = IsPassingHeld(GetOtherHand(Hand));
	const bool bClose = FVector::Dist(GetHandWorld(Hand), ItemCenter) < GrabDistance;
	if (bPassing ? (Slot.ReachElapsed >= PassGiveUpSeconds || (bClose && Slot.ReachElapsed >= PassHandSeconds)) : bClose)
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
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		const FAedirHandSlot& Slot = GetSlot(Hand);

		// A big object held in both hands is carried up in front of the body.
		const bool bCarryingObject = Slot.State == EAedirHandState::Holding && Slot.Item && Slot.Item->ItemClass == EAedirItemClass::Object
			&& GetSlot(GetOtherHand(Hand)).State == EAedirHandState::Holding && GetSlot(GetOtherHand(Hand)).Item == Slot.Item;
		const bool bHolsterMotion = HolsterMotion[Index] != EAedirHolsterMotion::None;
		const bool bPassGiver = IsPassingHeld(Hand);
		const bool bPassTaker = IsPassingHeld(GetOtherHand(Hand));
		const bool bConsumable = Slot.State == EAedirHandState::Holding && Slot.Item && Slot.Item->IsConsumable() && !bHolsterMotion;
		if (Slot.State == EAedirHandState::Reaching || bCarryingObject || bHolsterMotion || bPassGiver || bConsumable)
		{
			const float BlendIn = bPassGiver || bPassTaker ? PassBlendInSeconds : (bConsumable ? ConsumableBlendInSeconds : (bHolsterMotion ? HolsterBlendInSeconds : ReachBlendInSeconds));
			ReachBlend[Index] = FMath::Min(1.f, ReachBlend[Index] + DeltaTime / BlendIn);
		}
		else
		{
			ReachBlend[Index] = FMath::Max(0.f, ReachBlend[Index] - DeltaTime / ReachBlendOutSeconds);
		}

		if (bHolsterMotion)
		{
			const AAedirItemActor* HolsterItem = HolsterMotion[Index] == EAedirHolsterMotion::Store ? Slot.Item.Get() : (WaistItems.IsValidIndex(HolsterWaistIndex[Index]) ? WaistItems[HolsterWaistIndex[Index]].Get() : nullptr);
			const AAedirCreature* Creature = GetCreature();
			const FVector SlotWorld = GetWaistSlotWorld(HolsterWaistIndex[Index], HolsterItem);
			const FTransform ActorTransform = Creature ? Creature->GetActorTransform() : FTransform::Identity;
			const float SlotSide = HolsterWaistIndex[Index] < WaistSlotsPerSide ? -1.f : 1.f;
			const FVector Front = ActorTransform.TransformPositionNoScale(FVector(HolsterFrontForwardCm, SlotSide * HolsterFrontSideCm, HolsterFrontUpCm));
			const float T = HolsterSmoothStep(HolsterElapsed[Index] / HolsterHandSeconds);
			const FVector Start = ActorTransform.TransformPositionNoScale(HolsterStartLocal[Index]);
			ReachTargetCache[Index] = FMath::Lerp(FMath::Lerp(Start, Front, T), FMath::Lerp(Front, SlotWorld, T), T);
		}
		else if (bConsumable)
		{
			ReachTargetCache[Index] = GetConsumableHandTarget(Hand, Slot.Item.Get());
		}
		else if (bPassGiver)
		{
			const AAedirCreature* Creature = GetCreature();
			ReachTargetCache[Index] = Creature ? Creature->GetActorTransform().TransformPositionNoScale(FVector(PassMeetForwardCm, 0.f, PassMeetUpCm)) : ReachTargetCache[Index];
		}
		else if (Slot.Item)
		{
			ReachTargetCache[Index] = GetReachTargetWorld(Hand, Slot.Item);
		}
	}
}

FAedirReachPosture UAedirInteractionComponent::ComputeReachPosture(float ForwardCm, float LateralCm, float ItemHeightCm)
{
	FAedirReachPosture Posture;

	const float StandingShoulderCm = ReachPelvisHeightCm + ReachTorsoLengthCm;
	const float StandingDistance = FMath::Sqrt(ForwardCm * ForwardCm + LateralCm * LateralCm + FMath::Square(StandingShoulderCm - ItemHeightCm));
	if (StandingDistance <= ReachArmLengthCm)
	{
		return Posture;
	}

	// The gentlest bend of the spine that lets the pelvis stay within its squat wins. The pelvis sits back to keep the shoulder
	// over an item next to the feet, and moves forward for one further away.
	for (float Lean = ReachMinLeanDegrees; Lean <= ReachMaxLeanDegrees; Lean += ReachLeanStepDegrees)
	{
		// The spine curves, so the shoulder ends up on a chord flatter than the bend itself.
		const float ChordRadians = FMath::DegreesToRadians(Lean * ReachSpineChordShare);
		const float LeanSin = FMath::Sin(ChordRadians);
		const float LeanCos = FMath::Cos(ChordRadians);
		const float HipForward = FMath::Clamp(ForwardCm - ReachTorsoLengthCm * LeanSin, -ReachMaxHipBackCm, ReachMaxHipForwardCm);
		// Off to the side the pelvis shifts over and the bent torso twists around towards the item.
		const float HipSide = FMath::Clamp(LateralCm * ReachHipSideShare, -ReachMaxHipSideCm, ReachMaxHipSideCm);
		const float SideLeft = LateralCm - HipSide;
		const float TorsoReach = ReachTorsoLengthCm * LeanSin;
		const float TwistRadians = FMath::Clamp(FMath::Atan2(SideLeft, FMath::Max(ForwardCm - HipForward, 10.f)), -FMath::DegreesToRadians(ReachMaxTwistDegrees), FMath::DegreesToRadians(ReachMaxTwistDegrees));
		const float ShoulderForward = HipForward + TorsoReach * FMath::Cos(TwistRadians);
		const float SideRemaining = SideLeft - TorsoReach * FMath::Sin(TwistRadians);
		const float Remaining = FMath::Sqrt(FMath::Square(ForwardCm - ShoulderForward) + SideRemaining * SideRemaining);
		const float ArmDown = FMath::Sqrt(FMath::Max(ReachArmLengthCm * ReachArmLengthCm - Remaining * Remaining, 0.f));
		const float Lower = ReachPelvisHeightCm + ReachTorsoLengthCm * LeanCos - (ItemHeightCm + ArmDown);

		Posture.LowerCm = FMath::Clamp(Lower, 0.f, ReachMaxLowerCm);
		Posture.LeanDegrees = Lean;
		Posture.HipForwardCm = HipForward;
		Posture.HipSideCm = HipSide;
		Posture.TwistDegrees = FMath::RadiansToDegrees(TwistRadians);
		if (Lower <= ReachMaxLowerCm)
		{
			break;
		}
	}

	return Posture;
}

void UAedirInteractionComponent::UpdatePelvisLower(float DeltaTime)
{
	FAedirReachPosture Target;
	float Scale = 1.f;

	const AAedirCreature* Creature = GetCreature();
	if (Creature)
	{
		Scale = FMath::Max(FMath::Abs(static_cast<float>(Creature->GetActorScale3D().Z)), KINDA_SMALL_NUMBER);
		const FTransform ActorTransform = Creature->GetActorTransform();
		const float FloorZ = static_cast<float>(Creature->GetActorLocation().Z) - Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		for (int32 Index = 0; Index < 2; ++Index)
		{
			const EAedirHand Hand = static_cast<EAedirHand>(Index);
			const FAedirHandSlot& Slot = GetSlot(Hand);
			if (Slot.State != EAedirHandState::Reaching || !Slot.Item || IsPassingHeld(GetOtherHand(Hand)))
			{
				continue;
			}

			const FVector ItemWorld = GetReachTargetWorld(Hand, Slot.Item);
			const FVector ItemLocal = ActorTransform.InverseTransformPositionNoScale(ItemWorld);
			const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
			const FAedirReachPosture Posture = ComputeReachPosture(
				static_cast<float>(ItemLocal.X) / Scale,
				static_cast<float>(ItemLocal.Y) / Scale - Side * ReachShoulderSideCm,
				(static_cast<float>(ItemWorld.Z) - FloorZ) / Scale);
			if (Posture.LowerCm + Posture.LeanDegrees > Target.LowerCm + Target.LeanDegrees)
			{
				Target = Posture;
			}
		}
	}

	// The body straightens up again more slowly than it bent down.
	const float PostureSpeed = Target.LeanDegrees >= ReachLeanDegrees ? PelvisLowerInterpSpeed : PelvisRiseInterpSpeed;
	PelvisLowerZ = FMath::FInterpTo(PelvisLowerZ, Target.LowerCm * Scale, DeltaTime, PostureSpeed);
	ReachLeanDegrees = FMath::FInterpTo(ReachLeanDegrees, Target.LeanDegrees, DeltaTime, PostureSpeed);
	ReachHipForward = FMath::FInterpTo(ReachHipForward, Target.HipForwardCm, DeltaTime, PostureSpeed);
	ReachHipSide = FMath::FInterpTo(ReachHipSide, Target.HipSideCm, DeltaTime, PostureSpeed);
	ReachTwistDegrees = FMath::FInterpTo(ReachTwistDegrees, Target.TwistDegrees, DeltaTime, PostureSpeed);
}

FVector UAedirInteractionComponent::GetReachTargetWorld(EAedirHand Hand, const AAedirItemActor* Item) const
{
	const FVector Center = Item->GetBoundsCenterWorld();
	const AAedirCreature* Creature = GetCreature();
	if (!Creature)
	{
		return Center;
	}

	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	if (Item->ItemClass == EAedirItemClass::Object)
	{
		// The hand goes to the near surface of the object, or to its far side when the other hand already has it.
		// The hand bone is the wrist, so it stops short of the surface for the palm to lie on it, not in it.
		const float Radius = Item->GetHoldRadiusCm() * BigObjectHandSpanShare + BigObjectHandClearanceCm;
		const FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
		if (Other.Item.Get() == Item)
		{
			return Center + Creature->GetActorRightVector() * (Side * Radius);
		}
		const FVector TowardsHand = (GetHandWorld(Hand) - Center).GetSafeNormal();
		return Center + TowardsHand * Radius;
	}

	// The hand goes to the handle, not to the middle of the item. Two hands take it a grip apart, the lead hand above.
	const FVector Grip = Item->GetGripWorld();
	if (GetSlot(GetOtherHand(Hand)).Item.Get() != Item)
	{
		return Grip;
	}
	return Hand == ReachLeadHand ? Grip + Item->GetActorForwardVector() * Item->GripSpanCm : Grip;
}

bool UAedirInteractionComponent::CanLift(const AAedirItemActor* Item) const
{
	const AAedirCreature* Creature = GetCreature();
	if (!Creature || !Item)
	{
		return false;
	}

	// A big object can always be tried: whether it can be held is found out with it in the hands.
	if (Item->ItemClass == EAedirItemClass::Object)
	{
		return true;
	}

	// Two hands lift far more than can be carried comfortably; the excess only slows the body down.
	return Item->Weight <= Creature->GetCarryCapacity() * (RequiresBothHands(Item) ? TwoHandLiftMultiplier : 1.f);
}

void UAedirInteractionComponent::UpdateObjectStrain(float DeltaTime)
{
	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* LeftItem = GetHeldItem(EAedirHand::Left);
	AAedirItemActor* RightItem = GetHeldItem(EAedirHand::Right);
	AAedirItemActor* Object = (LeftItem && LeftItem->ItemClass == EAedirItemClass::Object) ? LeftItem : ((RightItem && RightItem->ItemClass == EAedirItemClass::Object) ? RightItem : nullptr);
	if (!Creature || !Object)
	{
		HoldStrainSeconds = 0.f;
		return;
	}

	// More than the hands can bear slips out after a moment, the sooner the heavier it is.
	const bool bBothHands = LeftItem == RightItem;
	const float Limit = Creature->GetCarryCapacity() * (bBothHands ? TwoHandLiftMultiplier : OneHandObjectLiftShare);
	if (Object->Weight <= Limit)
	{
		HoldStrainSeconds = FMath::Max(HoldStrainSeconds - DeltaTime, 0.f);
		return;
	}

	HoldStrainSeconds += DeltaTime * Object->Weight / FMath::Max(Limit, 1.f);
	if (HoldStrainSeconds >= ObjectStrainSeconds)
	{
		HoldStrainSeconds = 0.f;
		ReleaseHeldItem(Object);
		Object->Detach(Creature->GetVelocity());
		UpdateCarriedWeight();
	}
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
	if (!Item || !Item->FitsOnBelt())
	{
		return;
	}
	if (Item->ItemClass != EAedirItemClass::Weapon)
	{
		AddToBackpack(Item);
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

	FVector SlotOnBone;
	FQuat RotationOnBone;
	float PivotCm = 0.f;
	GetWaistSlot(FreeIndex, Item, SlotOnBone, RotationOnBone, PivotCm);
	Item->AttachToWaist(Creature, WaistBone, SlotOnBone, RotationOnBone, PivotCm);
	UpdateCarriedWeight();
}

void UAedirInteractionComponent::DrawFromWaist(EAedirHand Hand, AAedirCreature* Creature, int32 SourceIndex)
{
	FAedirHandSlot& Slot = GetSlot(Hand);
	const FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));

	AAedirItemActor* Item = WaistItems.IsValidIndex(SourceIndex) ? WaistItems[SourceIndex].Get() : nullptr;
	if (!Item || Slot.State != EAedirHandState::Idle)
	{
		return;
	}
	const bool bBothHands = RequiresBothHands(Item) && Other.State == EAedirHandState::Idle;

	WaistItems[SourceIndex] = nullptr;
	const int32 Side = SourceIndex < WaistSlotsPerSide ? 0 : 1;
	if (LastStoredWaistIndex[Side] == SourceIndex)
	{
		LastStoredWaistIndex[Side] = INDEX_NONE;
	}

	const FVector HangingDir = Item->GetActorForwardVector();
	HolsterDrawFrom[static_cast<int32>(Hand)] = HangingDir;
	Item->SettleIntoCreatureHand(Creature, Hand);
	if (Creature->Combat)
	{
		Creature->Combat->SetWeaponDirNow(Hand, HangingDir);
	}

	Slot.State = EAedirHandState::Holding;
	Slot.Item = Item;

	if (bBothHands)
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

void UAedirInteractionComponent::ServerSwapGripOrder_Implementation()
{
	SwapGripOrder();
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

bool UAedirInteractionComponent::PassToOtherHand()
{
	const AAedirItemActor* LeftItem = GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* RightItem = GetHeldItem(EAedirHand::Right);
	const EAedirHand FromHand = RightItem ? EAedirHand::Right : EAedirHand::Left;
	AAedirItemActor* Item = GetHeldItem(FromHand);
	const EAedirHand ToHand = GetOtherHand(FromHand);
	if ((LeftItem && RightItem) || !Item || Item->ItemClass != EAedirItemClass::Weapon || Item->bTwoHanded || GetHandState(ToHand) != EAedirHandState::Idle)
	{
		return false;
	}

	if (!IsAuthoritative())
	{
		ServerPassToOtherHand();
		return true;
	}

	BeginReach(ToHand, Item);
	bReachingToTakeOver[static_cast<int32>(ToHand)] = true;
	return true;
}

void UAedirInteractionComponent::ServerPassToOtherHand_Implementation()
{
	PassToOtherHand();
}

void UAedirInteractionComponent::ServerDrawWaistSlot_Implementation(EAedirHand Hand, int32 SlotIndex)
{
	DrawWaistSlot(Hand, SlotIndex);
}

bool UAedirInteractionComponent::EquipImmediate(EAedirHand Hand, AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	if (!IsAuthoritative() || !Creature || !Item || Item->IsHeld())
	{
		return false;
	}

	FAedirHandSlot& Slot = GetSlot(Hand);
	FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	const bool bBothHands = RequiresBothHands(Item) && Other.State == EAedirHandState::Idle;
	if (Slot.State != EAedirHandState::Idle)
	{
		return false;
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
	return true;
}

bool UAedirInteractionComponent::HolsterImmediate(int32 SlotIndex, AAedirItemActor* Item)
{
	AAedirCreature* Creature = GetCreature();
	if (!IsAuthoritative() || !Creature || !Item || Item->IsHeld() || !Item->FitsOnBelt() || !WaistItems.IsValidIndex(SlotIndex) || WaistItems[SlotIndex] != nullptr)
	{
		return false;
	}

	const EAedirHand Hand = SlotIndex < WaistSlotsPerSide ? EAedirHand::Right : EAedirHand::Left;

	WaistItems[SlotIndex] = Item;
	LastStoredWaistIndex[GetWaistSideIndex(Hand)] = SlotIndex;

	FVector SlotOnBone;
	FQuat RotationOnBone;
	float PivotCm = 0.f;
	GetWaistSlot(SlotIndex, Item, SlotOnBone, RotationOnBone, PivotCm);
	Item->AttachToWaist(Creature, WaistBone, SlotOnBone, RotationOnBone, PivotCm);
	UpdateCarriedWeight();
	return true;
}

void UAedirInteractionComponent::ReleaseAllItems()
{
	if (!IsAuthoritative())
	{
		return;
	}

	ResetSlot(LeftSlot);
	ResetSlot(RightSlot);

	for (TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		WaistItem = nullptr;
	}

	Backpack.Reset();

	LastStoredWaistIndex[0] = INDEX_NONE;
	LastStoredWaistIndex[1] = INDEX_NONE;
	UpdateCarriedWeight();
}

bool UAedirInteractionComponent::WearItem(AAedirItemActor* Item, EAedirHand FromHand)
{
	AAedirCreature* Creature = GetCreature();
	if (!IsAuthoritative() || !Creature || !Item || Item->bWorn || Item->ArmorAttachBone.IsNone() || WornItems.Contains(Item))
	{
		return false;
	}

	ClearSlotIfHolding(GetSlot(FromHand), Item);
	ClearSlotIfHolding(GetSlot(GetOtherHand(FromHand)), Item);

	for (TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		if (WaistItem == Item)
		{
			WaistItem = nullptr;
		}
	}

	Item->AttachToWornBone(Creature);
	WornItems.Add(Item);
	UpdateCarriedWeight();
	return true;
}

TArray<AAedirItemActor*> UAedirInteractionComponent::GetWaistItemsCopy() const
{
	TArray<AAedirItemActor*> Copy;
	Copy.Reserve(WaistItems.Num());
	for (const TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		Copy.Add(WaistItem.Get());
	}
	return Copy;
}

void UAedirInteractionComponent::AddToBackpack(AAedirItemActor* Item)
{
	if (!IsAuthoritative())
	{
		ServerAddToBackpack(Item);
		return;
	}

	// Big objects are carried in the arms and never fit in a backpack.
	AAedirCreature* Creature = GetCreature();
	if (!Creature || !Item || Item->ItemClass == EAedirItemClass::Object || Item->bWorn || Item->bStowed || Item->Holder != Creature || Backpack.Contains(Item))
	{
		return;
	}

	const bool bInHand = GetHeldItem(EAedirHand::Left) == Item || GetHeldItem(EAedirHand::Right) == Item;
	if (!bInHand && !WaistItems.Contains(Item))
	{
		return;
	}

	ReleaseHeldItem(Item);
	for (TObjectPtr<AAedirItemActor>& WaistItem : WaistItems)
	{
		if (WaistItem == Item)
		{
			WaistItem = nullptr;
		}
	}

	Item->AttachToBackpack(Creature);
	Backpack.Add(Item);
	UpdateCarriedWeight();
}

void UAedirInteractionComponent::TakeFromBackpack(int32 Index, EAedirHand Hand)
{
	if (!IsAuthoritative())
	{
		ServerTakeFromBackpack(Index, Hand);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = Backpack.IsValidIndex(Index) ? Backpack[Index].Get() : nullptr;
	if (!Creature || !Item)
	{
		return;
	}

	FAedirHandSlot& Slot = GetSlot(Hand);
	FAedirHandSlot& Other = GetSlot(GetOtherHand(Hand));
	const bool bBothHands = RequiresBothHands(Item) && Other.State == EAedirHandState::Idle;
	if (Slot.State != EAedirHandState::Idle || Item->Weight > Creature->GetCarryCapacity())
	{
		return;
	}

	Backpack.RemoveAt(Index);
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

void UAedirInteractionComponent::DropFromBackpack(int32 Index)
{
	if (!IsAuthoritative())
	{
		ServerDropFromBackpack(Index);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = Backpack.IsValidIndex(Index) ? Backpack[Index].Get() : nullptr;
	if (!Creature || !Item)
	{
		return;
	}

	Backpack.RemoveAt(Index);

	const FVector Forward = Creature->GetActorForwardVector();
	Item->SetActorLocation(Creature->GetActorLocation() + Forward * BackpackDropDistance + FVector(0.f, 0.f, BackpackDropHeight), false, nullptr, ETeleportType::TeleportPhysics);
	Item->Detach(GetTossVelocity(Item, 0.5f));

	UpdateCarriedWeight();
}

void UAedirInteractionComponent::DropFromWaist(int32 SlotIndex)
{
	if (!IsAuthoritative())
	{
		ServerDropFromWaist(SlotIndex);
		return;
	}

	AAedirCreature* Creature = GetCreature();
	AAedirItemActor* Item = WaistItems.IsValidIndex(SlotIndex) ? WaistItems[SlotIndex].Get() : nullptr;
	if (!Creature || !Item)
	{
		return;
	}

	WaistItems[SlotIndex] = nullptr;
	Item->Detach(GetTossVelocity(Item, 1.f));
	UpdateCarriedWeight();
}

TArray<AAedirItemActor*> UAedirInteractionComponent::GetBackpackItemsCopy() const
{
	TArray<AAedirItemActor*> Copy;
	Copy.Reserve(Backpack.Num());
	for (const TObjectPtr<AAedirItemActor>& StowedItem : Backpack)
	{
		Copy.Add(StowedItem.Get());
	}
	return Copy;
}

void UAedirInteractionComponent::ServerAddToBackpack_Implementation(AAedirItemActor* Item)
{
	AddToBackpack(Item);
}

void UAedirInteractionComponent::ServerTakeFromBackpack_Implementation(int32 Index, EAedirHand Hand)
{
	TakeFromBackpack(Index, Hand);
}

void UAedirInteractionComponent::ServerDropFromBackpack_Implementation(int32 Index)
{
	DropFromBackpack(Index);
}

void UAedirInteractionComponent::ServerDropFromWaist_Implementation(int32 SlotIndex)
{
	DropFromWaist(SlotIndex);
}
