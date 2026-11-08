#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/AedirCombatTypes.h"
#include "Items/AedirItemTypes.h"
#include "AedirItemActor.generated.h"

class AAedirCreature;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class AEDIR_API AAedirItemActor : public AActor
{
	GENERATED_BODY()

public:
	AAedirItemActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	FText ItemName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	EAedirItemClass ItemClass = EAedirItemClass::Unusable;

	// Mass in kg, also applied as the mesh mass override.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	float Weight = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	bool bTwoHanded = false;

	// Hold offset relative to the hand bone. The item bounds center is placed at the translation of this transform.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Hold")
	FTransform HeldTransform;

	// Item-specific offset applied before the waist slot transform.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Hold")
	FTransform WaistTransform;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	float PoisonDamage = 10.f;

	// Damage dealt per swing while this item is held.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	float Damage = 0.f;

	// Length in cm from the hand to the striking tip. 0 means a short object, treated as 12 cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	float WeaponLength = 0.f;

	// Spears, staves and the like: held with the lower hand beside the waist.
	bool bPolearm = false;

	// Spears, staves and big objects are too long to hang from a belt.
	bool FitsOnBelt() const { return ItemClass != EAedirItemClass::Object && !bPolearm; }

	// Food, drink and potions go to the mouth. Drinks and potions are held upright and tipped there.
	bool bDrinkable = false;
	bool IsConsumable() const { return ItemClass == EAedirItemClass::Food || ItemClass == EAedirItemClass::Poison; }

	// Grip point along the long axis of the item, used when the item is held in the closed hand.
	float GripLocalCm = 0.f;
	float GripSpanCm = 13.f;

	// The hollow of a closed hand: this share of the way from the wrist to the knuckles and this far off the back of the hand.
	static constexpr float HandGripPalmShare = 0.62f;
	static constexpr float HandGripPalmDepthCm = 3.2f;

	// How far above GripLocalCm the hand it is attached to holds the item. Set while half-swording, where the upper hand carries it.
	UPROPERTY(Replicated)
	float HeldGripShiftCm = 0.f;
	bool bUseHandGrip = false;
	bool bHangsDiagonal = false;

	// Where the hand closes on the item in the world.
	FVector GetGripWorld() const;

	// How the item injures: blunt force, cutting edge or point.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	EAedirDamageType DamageType = EAedirDamageType::Blunt;

	// Intrinsic scale of the root mesh. It is kept when the item is attached to a hand, a waist slot or a body bone.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Hold")
	FVector BodyScale = FVector::OneVector;

	// Armor only. Body zone that the worn armor protects.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Armor")
	EAedirBodyZone ArmorZone = EAedirBodyZone::Torso;

	// Armor only. Damage reduction for ArmorZone while worn, range 0..0.9.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Armor")
	float ArmorProtection = 0.f;

	// Armor only. Manny bone that the armor is attached to while worn.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item|Armor")
	FName ArmorAttachBone = NAME_None;

	// Set on the server while the item is worn. Worn items are not pickable.
	UPROPERTY(ReplicatedUsing=OnRep_Worn, BlueprintReadOnly, Category="Aedir|Item")
	bool bWorn = false;

	// How the item looks once it is on the body, and how it looks again once taken off.
	virtual void ApplyWornLook() {}
	virtual void ClearWornLook() {}

	// Server only. No longer worn; the caller decides where it goes next.
	void TakeOff();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	TObjectPtr<UMaterialInterface> HighlightOverlayMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_Holder, BlueprintReadOnly, Category="Aedir|Item")
	TObjectPtr<AAedirCreature> Holder;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Item")
	EAedirHand HeldHand = EAedirHand::Right;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Item")
	bool bHolstered = false;

	// Set while the item sits in a creature's backpack: hidden, without collision and attached to nothing but the body.
	UPROPERTY(ReplicatedUsing=OnRep_Stowed, BlueprintReadOnly, Category="Aedir|Item")
	bool bStowed = false;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server only. Poison damages the user; every use is reported through OnUsed.
	UFUNCTION(BlueprintNativeEvent, Category="Aedir|Item")
	void Use(AAedirCreature* User);
	virtual void Use_Implementation(AAedirCreature* User);

	UFUNCTION(BlueprintImplementableEvent, Category="Aedir|Item")
	void OnUsed(AAedirCreature* User);

	// Server only.
	void AttachToCreatureHand(AAedirCreature* Creature, EAedirHand Hand, float GripShiftCm = 0.f);

	// Takes it into the hand from where it is, then lets the handle slide into the grip, as when it is drawn from the belt.
	void SettleIntoCreatureHand(AAedirCreature* Creature, EAedirHand Hand);

	// Server only. Hangs the item from a point on the waist bone, rotated as given, pivoting at PivotCm along its long axis.
	// It keeps swinging on that pivot with the movement of the body.
	void AttachToWaist(AAedirCreature* Creature, FName BoneName, const FVector& SlotOnBone, const FQuat& RotationOnBone, float PivotCm);

	virtual void Tick(float DeltaSeconds) override;

private:
	// The grip being settled into, from the hold it was taken with.
	FTransform SettleFrom = FTransform::Identity;
	FTransform SettleTo = FTransform::Identity;
	float SettleAlpha = 1.f;

public:

	// Server only.
	void Detach(const FVector& ThrowVelocity);

	// Server only. A big object in both hands hangs on neither of them: it follows the body and the hands stay on its surface.
	void AttachCarried(AAedirCreature* Creature);

	// Takes the item in a hand exactly where the hand touches it, for big objects with no handle.
	void AttachToHandInPlace(AAedirCreature* Creature, EAedirHand Hand);

	// True for a short while after this creature let go of the item, so a dropped item does not count as hitting it.
	bool WasJustReleasedBy(const AActor* Creature) const;

	// Server only. Hides the item and keeps it with the creature without any bone attachment.
	void AttachToBackpack(AAedirCreature* Creature);

	// Server only. Attaches the item to ArmorAttachBone and marks it worn.
	void AttachToWornBone(AAedirCreature* Creature);

	// Client-local visual only.
	void SetHighlighted(bool bNewHighlighted);

	// How far the sides of a big object lie from its middle.
	float GetHoldRadiusCm() const;

	FVector GetBoundsCenterWorld() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Item")
	bool IsHeld() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Item")
	bool IsUsable() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Item")
	float GetDamage() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Item")
	float GetWeaponLength() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Item")
	EAedirDamageType GetDamageType() const;

	static bool IsPickable(const AAedirItemActor* Item);

private:
	UFUNCTION()
	void OnRep_Holder();

	UFUNCTION()
	void OnRep_Stowed();

	UFUNCTION()
	void OnRep_Worn()
	{
		if (bWorn)
		{
			ApplyWornLook();
		}
		else
		{
			ClearWornLook();
		}
	}

	void ApplyStowed(bool bNewStowed);

	void AttachToBone(AAedirCreature* Creature, FName BoneName, const FTransform& HoldOffset);
	void SetFreePhysics(bool bFree);

	// Solid in the hand of a creature, pushing what it meets, but not against the body that holds it.
	void ApplyHeldCollision(const AAedirCreature* Creature);
	bool bHeldCollision = false;
	TEnumAsByte<ECollisionResponse> FreePawnResponse = ECR_Block;
	void ApplyItemMass();
	FTransform ComputeHoldTransform(const FTransform& HoldOffset) const;
	FTransform ComputeHandGrip(const AAedirCreature* Creature, EAedirHand Hand) const;

	// Hanging from the belt.
	FVector HangSlot = FVector::ZeroVector;
	FQuat HangRotation = FQuat::Identity;
	float HangPivotCm = 0.f;
	FVector HangSwing = FVector::ZeroVector;
	FVector HangSwingSpeed = FVector::ZeroVector;
	FVector HangPreviousBone = FVector::ZeroVector;
	FVector HangPreviousVelocity = FVector::ZeroVector;
	float HangContactHold = 0.f;
	bool bHangTracking = false;
	void UpdateHangContact(const FTransform& BoneWorld, float DeltaSeconds);

	TWeakObjectPtr<const AActor> LastHolder;
	double LastReleaseTime = -1000.0;
	FVector GetStaticBoundsCenter() const;
};
