#pragma once

#include "CoreMinimal.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/ActorComponent.h"
#include "Items/AedirItemTypes.h"
#include "AedirInteractionComponent.generated.h"

class AAedirCreature;
class AAedirItemActor;

USTRUCT(BlueprintType)
struct AEDIR_API FAedirHandSlot
{
	GENERATED_BODY()

	// Reaching: Item is the reach target. Holding: Item is the held item (shared by both hands for two-handed items).
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	EAedirHandState State = EAedirHandState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	TObjectPtr<AAedirItemActor> Item;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachElapsed = 0.f;

	// Simulated hand position moving from the hand bone toward the reach target at ReachSpeed.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	FVector ReachHandWorld = FVector::ZeroVector;
};

// How far the body bends down for something low.
struct FAedirReachPosture
{
	float LowerCm = 0.f;
	float LeanDegrees = 0.f;
	float HipForwardCm = 0.f;
	// Sideways shift of the pelvis and twist of the spine towards an item off to the side. Positive is to the right.
	float HipSideCm = 0.f;
	float TwistDegrees = 0.f;
};

class UPrimitiveComponent;

// A hand closed on a body or a piece of one, held to it by a spring.
struct FAedirBodyGrab
{
	TWeakObjectPtr<class UPhysicsConstraintComponent> Joint;
	TWeakObjectPtr<UPrimitiveComponent> Target;
	FName Bone = NAME_None;
	FVector LocalPoint = FVector::ZeroVector;
	float TearSeconds = 0.f;
	// Reaching for it, not yet holding it.
	bool bReaching = false;
	float ReachSeconds = 0.f;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// For the dev tools: where the hand is being taken by a reach, a draw or a stow, and how far that has taken over.
	FVector DevGetReachTarget(EAedirHand Hand) const { return ReachTargetCache[Hand == EAedirHand::Left ? 0 : 1]; }
	float DevGetReachBlend(EAedirHand Hand) const { return ReachBlend[Hand == EAedirHand::Left ? 0 : 1]; }
	UAedirInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float PickupRange = 150.f;

	// Distance in cm from the hand bone to the item bounds center that counts as a grab.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float GrabDistance = 22.f;

	// Speed in cm/s of the hand moving toward the reach target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachSpeed = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachTimeout = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float UseHoldTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ThrowSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName LeftHandBone = FName(TEXT("hand_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName RightHandBone = FName(TEXT("hand_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName WaistBone = FName(TEXT("pelvis"));

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReach(EAedirHand Hand);

	// A free hand takes hold of the nearest body or loose part of one in front of it, living, dead or cut off. False when there is none.
	bool TryGrabBody(EAedirHand Hand, const FVector* AimedPoint = nullptr);

	// Where the eyes meet a body or a piece of one within reach, if they do.
	bool FindAimedBody(FVector& OutPoint) const;
	void ReleaseBodyGrab(EAedirHand Hand);
	bool IsGrabbingBody(EAedirHand Hand) const;
	bool GetBodyGrabPoint(EAedirHand Hand, FVector& OutWorld) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReachForItem(EAedirHand Hand, AAedirItemActor* Item);


	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void CancelReach(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropHeld(EAedirHand Hand);

	// Swaps which hand grips a two-handed weapon higher: the upper hand goes to the pommel and the lower one moves up.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void SwapGripOrder();

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void UseHeld(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void HolsterToWaist(EAedirHand Hand);

	// Takes the item of one belt slot in the given hand, whichever side of the belt it hangs on. At once while the game is paused.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DrawWaistSlot(EAedirHand Hand, int32 SlotIndex);

	// The belt slot of the weapon to draw first, the one put away last if it is still there. INDEX_NONE without a weapon on the belt.
	int32 FindWaistWeaponSlot() const;

	// True if that hand can take the item of that belt slot now.
	bool CanDrawWaistSlot(EAedirHand Hand, int32 SlotIndex) const;

	// Takes a stowed item: the hand goes to the belt as for a drawn weapon, and the item is in it when it comes back.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DrawFromBackpack(int32 Index, EAedirHand Hand);

	// Brings food or drink in that hand to the mouth while held. It is eaten or drunk there, a bite at a time.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void SetMouthHold(EAedirHand Hand, bool bHold);

	bool IsMouthHold(EAedirHand Hand) const { return (MouthHands & (1 << static_cast<int32>(Hand))) != 0; }

	// How the wrist holds food or drink, at rest or at the mouth.
	bool GetConsumableAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const;

	// Server only. A new item straight into the backpack.
	void StowNewItem(AAedirItemActor* Item);

	// Server only. Takes an item out of the hands, belt or backpack and destroys it.
	void RemoveOwnedItem(AAedirItemActor* Item);

	// A one-handed weapon goes to the empty other hand, which reaches for it first. False if there is nothing to pass.
	bool PassToOtherHand();

	// True while a hand is on its way to the belt.
	bool IsHolsterMoving() const;

	// How the wrist turns an item on its way to the belt.
	bool GetHolsterAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutShare) const;

	// On its way to take an item off the belt: how that item hangs there, and how far the hand has turned to it.
	bool GetDrawAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const;

	// True while the other hand is coming to take the weapon this hand holds.
	bool IsPassingHeld(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	EAedirHandState GetHandState(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	AAedirItemActor* GetHeldItem(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetCarriedWeight() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	AAedirItemActor* GetHighlightedItem() const;

	// True while the hand is reaching or still blending out after a grab or cancel.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	bool GetReachPose(EAedirHand Hand, FVector& OutTargetWorld, float& OutAlpha) const;

	// Positive number of cm the pelvis should go down, smoothed.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetPelvisLowerZ() const;

	// Forward bend of the spine in degrees while reaching low, smoothed.
	float GetReachLeanDegrees() const { return ReachLeanDegrees; }

	// Forward shift of the pelvis in unscaled cm while reaching low, negative when it sits back. Smoothed.
	float GetReachHipForward() const { return ReachHipForward; }

	// Sideways shift of the pelvis in unscaled cm and twist of the spine in degrees while reaching low. Smoothed.
	float GetReachHipSide() const { return ReachHipSide; }
	float GetReachTwistDegrees() const { return ReachTwistDegrees; }

	// The body posture that brings the shoulder within arm's reach of a point. Forward and lateral are relative to the shoulder's
	// side of the body, the height is above the floor. All in unscaled cm.
	static FAedirReachPosture ComputeReachPosture(float ForwardCm, float LateralCm, float ItemHeightCm);

	// Server only. Equips a freshly spawned item into a hand without a reach phase.
	bool EquipImmediate(EAedirHand Hand, AAedirItemActor* Item);

	// Server only. Stores a freshly spawned item in waist slot 0-5 without a reach phase.
	bool HolsterImmediate(int32 SlotIndex, AAedirItemActor* Item);

	// Server only. Clears both hands and every waist slot. The items themselves are left untouched.
	void ReleaseAllItems();

	TArray<AAedirItemActor*> GetWaistItemsCopy() const;

	// Server only. Takes an armor item off the hand or waist, attaches it to its bone and adds it to the worn list.
	// FromHand is the hand the item is taken from. Returns false when the item cannot be worn.
	bool WearItem(AAedirItemActor* Item, EAedirHand FromHand);

	// Stows a held or holstered item in the backpack. Clients forward the request to the server.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void AddToBackpack(AAedirItemActor* Item);

	// Takes the backpack entry into a free hand.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void TakeFromBackpack(int32 Index, EAedirHand Hand);

	// Drops the backpack entry on the ground in front of the creature.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropFromBackpack(int32 Index);

	// Drops the holstered item of waist slot 0-5.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropFromWaist(int32 SlotIndex);

	// Takes off a worn piece of armour, by its place among the worn items, and puts it in the backpack.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void TakeOffWorn(int32 WornIndex);

	TArray<AAedirItemActor*> GetWornItemsCopy() const;

	// Entries keep their backpack index, destroyed items show up as null.
	TArray<AAedirItemActor*> GetBackpackItemsCopy() const;

	// Highest protection of the worn items covering the zone, range 0..0.9.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetWornArmorProtection(EAedirBodyZone Zone) const;

	// What the worn armour does to a blow of this kind landing on this bone at this point: the piece that takes the most of it,
	// or nothing where the body is bare.
	FAedirArmorHit GetArmorAt(FName Bone, const FVector& Point, EAedirDamageType Type) const;
	bool HasWornArmor() const { return !WornItems.IsEmpty(); }

	// A blow knocks a worn piece off the body: it flies off with this speed and lies where it falls.
	void KnockOffWorn(AAedirItemActor* Item, const FVector& Velocity);

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot LeftSlot;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot RightSlot;

	// Left side entries are indices 0-2, right side entries are indices 3-5.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> WaistItems;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> WornItems;

	// Stowed items: hidden, without collision, counted in the carried weight.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> Backpack;

private:
	void UpdateBodyGrabs(float DeltaTime);
	void CloseHandOnBody(EAedirHand Hand);
	FAedirBodyGrab BodyGrabs[2];

	void BeginReach(EAedirHand Hand, AAedirItemActor* Item);
	void GrabItem(EAedirHand Hand, AAedirItemActor* Item);
	void UpdateReach(EAedirHand Hand, float DeltaTime);
	void UpdateReachBlend(float DeltaTime);
	void UpdatePelvisLower(float DeltaTime);
	void UpdateHighlight();
	void UpdateCarriedWeight();
	void StoreHeldToWaist(EAedirHand Hand, AAedirCreature* Creature);
	void DrawFromWaist(EAedirHand Hand, AAedirCreature* Creature, int32 SourceIndex);
	void ReleaseHeldItem(const AAedirItemActor* Item);
	void ClearSlotIfHolding(FAedirHandSlot& Slot, const AAedirItemActor* Item);

	FAedirHandSlot& GetSlot(EAedirHand Hand);
	const FAedirHandSlot& GetSlot(EAedirHand Hand) const;
	AAedirItemActor* FindBestItem() const;
	AAedirCreature* GetCreature() const;
	FVector GetHandWorld(EAedirHand Hand) const;
	bool IsAuthoritative() const;

	UFUNCTION(Server, Reliable)
	void ServerStartReachForItem(EAedirHand Hand, AAedirItemActor* Item);

	UFUNCTION(Server, Reliable)
	void ServerCancelReach(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDropHeld(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerSwapGripOrder();

	UFUNCTION(Server, Reliable)
	void ServerUseHeld(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerHolsterToWaist(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDrawWaistSlot(EAedirHand Hand, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerPassToOtherHand();

	UFUNCTION(Server, Reliable)
	void ServerDrawFromBackpack(int32 Index, EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerSetMouthHold(EAedirHand Hand, bool bHold);

	void UpdateMouth(EAedirHand Hand, float DeltaTime);
	FVector GetConsumableHandTarget(EAedirHand Hand, const AAedirItemActor* Item) const;
	FVector GetMouthWorld() const;
	float GetMouthShare(EAedirHand Hand) const;

	UPROPERTY(Replicated)
	uint8 MouthHands = 0;

	float MouthElapsed[2] = { 0.f, 0.f };

	// A piece of armour is put on by the hand that holds it: carried to where it is worn, and fastened there once it arrives.
	UPROPERTY(Replicated)
	uint8 DonHands = 0;
	float DonElapsed[2] = { 0.f, 0.f };
	bool IsDonning(EAedirHand Hand) const { return (DonHands & (1 << static_cast<int32>(Hand))) != 0; }
	void UpdateDon(EAedirHand Hand, float DeltaTime);
	FVector GetDonHandTarget(EAedirHand Hand, const AAedirItemActor* Item) const;
	float BiteElapsed[2] = { 0.f, 0.f };

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> BackpackDrawItem[2];

	UFUNCTION(Server, Reliable)
	void ServerAddToBackpack(AAedirItemActor* Item);

	UFUNCTION(Server, Reliable)
	void ServerTakeFromBackpack(int32 Index, EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDropFromBackpack(int32 Index);

	UFUNCTION(Server, Reliable)
	void ServerDropFromWaist(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerTakeOffWorn(int32 WornIndex);

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> HighlightedItem;

	float HighlightTimer = 0.f;
	float ReachBlend[2] = { 0.f, 0.f };
	// How close each reaching hand has come to its target, and how long it has gone without coming closer.
	float ReachBestGap[2] = { 0.f, 0.f };
	float ReachStallSeconds[2] = { 0.f, 0.f };
	FVector ReachTargetCache[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float PelvisLowerZ = 0.f;
	float ReachLeanDegrees = 0.f;
	float ReachHipForward = 0.f;
	float ReachHipSide = 0.f;
	float ReachTwistDegrees = 0.f;

	// The hand whose key started a reach. It takes the upper grip on a weapon held in both hands.
	EAedirHand ReachLeadHand = EAedirHand::Right;

	FVector GetReachTargetWorld(EAedirHand Hand, const AAedirItemActor* Item) const;
	bool CanLift(const AAedirItemActor* Item) const;
	void UpdateObjectStrain(float DeltaTime);

	// The hand goes to the belt first, and the item changes place when it gets there.
	void BeginHolsterMotion(EAedirHand Hand);
	void StartHolsterPath(EAedirHand Hand);
	void UpdateHolsterMotion(EAedirHand Hand, float DeltaTime);
	int32 FindFreeWaistIndex(EAedirHand Hand) const;
	int32 FindDrawWaistIndex(EAedirHand Hand) const;
	void GetWaistSlot(int32 WaistIndex, const AAedirItemActor* Item, FVector& OutSlotOnBone, FQuat& OutRotationOnBone, float& OutPivotCm) const;
	FVector GetWaistSlotWorld(int32 WaistIndex, const AAedirItemActor* Item) const;
	FVector GetTossVelocity(const AAedirItemActor* Item, float SpeedShare) const;

	enum class EAedirHolsterMotion : uint8 { None, Store, Draw };
	EAedirHolsterMotion HolsterMotion[2] = { EAedirHolsterMotion::None, EAedirHolsterMotion::None };
	float HolsterElapsed[2] = { 0.f, 0.f };
	float HolsterDrawSettle[2] = { 0.f, 0.f };
	FVector HolsterDrawFrom[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector HolsterStartLocal[2] = { FVector::ZeroVector, FVector::ZeroVector };
	int32 HolsterWaistIndex[2] = { INDEX_NONE, INDEX_NONE };

	// The hand is reaching for a weapon in the other hand, to take it over.
	bool bReachingToTakeOver[2] = { false, false };
	float HoldStrainSeconds = 0.f;
	int32 LastStoredWaistIndex[2] = { INDEX_NONE, INDEX_NONE };
};
