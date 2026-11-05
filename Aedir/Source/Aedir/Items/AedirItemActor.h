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
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Item")
	bool bWorn = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	TObjectPtr<UMaterialInterface> HighlightOverlayMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_Holder, BlueprintReadOnly, Category="Aedir|Item")
	TObjectPtr<AAedirCreature> Holder;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Item")
	EAedirHand HeldHand = EAedirHand::Right;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Item")
	bool bHolstered = false;

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
	void AttachToCreatureHand(AAedirCreature* Creature, EAedirHand Hand);

	// Server only.
	void AttachToWaist(AAedirCreature* Creature, FName BoneName, const FTransform& SlotRelative);

	// Server only.
	void Detach(const FVector& ThrowVelocity);

	// Server only. Attaches the item to ArmorAttachBone and marks it worn.
	void AttachToWornBone(AAedirCreature* Creature);

	// Client-local visual only.
	void SetHighlighted(bool bNewHighlighted);

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

	void AttachToBone(AAedirCreature* Creature, FName BoneName, const FTransform& HoldOffset);
	void SetFreePhysics(bool bFree);
	void ApplyItemMass();
	FTransform ComputeHoldTransform(const FTransform& HoldOffset) const;
	FVector GetStaticBoundsCenter() const;
};
