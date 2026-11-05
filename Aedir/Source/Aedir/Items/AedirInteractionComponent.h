#pragma once

#include "CoreMinimal.h"
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

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float PickupRange = 220.f;

	// Distance in cm from the hand bone to the item bounds center that counts as a grab.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float GrabDistance = 18.f;

	// Speed in cm/s of the hand moving toward the reach target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachTimeout = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float UseHoldTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ThrowSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName LeftHandBone = FName(TEXT("lHand"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName RightHandBone = FName(TEXT("rHand"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName WaistBone = FName(TEXT("hip"));

	// Three slots per side, relative to the waist bone. Rotation points the item down.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<FTransform> LeftWaistSlots = {
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, -20.f, 0.f)),
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, -20.f, -14.f)),
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, -20.f, -28.f))
	};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<FTransform> RightWaistSlots = {
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, 20.f, 0.f)),
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, 20.f, -14.f)),
		FTransform(FRotator(-80.f, 0.f, 0.f), FVector(-12.f, 20.f, -28.f))
	};

	// Maximum pelvis lowering in cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float KneeBendMaxLower = 45.f;

	// Depth below the hip bone where lowering starts; items above this depth are reached with the arm alone.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachLowerStartHeight = 60.f;

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReach(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReachForItem(EAedirHand Hand, AAedirItemActor* Item);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void CancelReach(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropHeld(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void UseHeld(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void HolsterToWaist(EAedirHand Hand);

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

	// Server only. Equips a freshly spawned item into a hand without a reach phase.
	bool EquipImmediate(EAedirHand Hand, AAedirItemActor* Item);

	// Server only. Stores a freshly spawned item in waist slot 0-5 without a reach phase.
	bool HolsterImmediate(int32 SlotIndex, AAedirItemActor* Item);

	// Server only. Clears both hands and every waist slot. The items themselves are left untouched.
	void ReleaseAllItems();

	TArray<AAedirItemActor*> GetWaistItemsCopy() const;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot LeftSlot;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot RightSlot;

	// Left side entries are indices 0-2, right side entries are indices 3-5.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> WaistItems;

private:
	void BeginReach(EAedirHand Hand, AAedirItemActor* Item);
	void GrabItem(EAedirHand Hand, AAedirItemActor* Item);
	void UpdateReach(EAedirHand Hand, float DeltaTime);
	void UpdateReachBlend(float DeltaTime);
	void UpdatePelvisLower(float DeltaTime);
	void UpdateHighlight();
	void UpdateCarriedWeight();
	void StoreHeldToWaist(EAedirHand Hand, AAedirCreature* Creature);
	void DrawFromWaist(EAedirHand Hand, AAedirCreature* Creature);
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
	void ServerUseHeld(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerHolsterToWaist(EAedirHand Hand);

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> HighlightedItem;

	float HighlightTimer = 0.f;
	float ReachBlend[2] = { 0.f, 0.f };
	FVector ReachTargetCache[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float PelvisLowerZ = 0.f;
	int32 LastStoredWaistIndex[2] = { INDEX_NONE, INDEX_NONE };
};
