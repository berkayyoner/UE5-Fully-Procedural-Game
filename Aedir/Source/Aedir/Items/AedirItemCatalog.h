#pragma once

#include "CoreMinimal.h"
#include "Combat/AedirCombatTypes.h"
#include "Items/AedirItemTypes.h"
#include "UObject/Object.h"
#include "AedirItemCatalog.generated.h"

// One code-defined item. Masses are in kg, lengths in cm, from the hand to the striking tip for weapons.
USTRUCT(BlueprintType)
struct AEDIR_API FAedirItemDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	EAedirItemClass ItemClass = EAedirItemClass::Unusable;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	float WeightKg = 1.f;

	// A scanned model instead of the built shape, turned so its longest side runs along the item.
	FString MeshPath;

	// Food and water a use gives, out of 100, and how many uses are in it.
	float FoodValue = 0.f;
	float DrinkValue = 0.f;
	int32 Uses = 1;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	float WeaponLengthCm = 0.f;

	// Where the hand closes on the item, in cm along its long axis from the centre. Negative is towards the pommel.
	float GripCm = 0.f;

	// Distance between the two hands on the handle when held in both.
	float GripSpanCm = 13.f;

	// Hangs from the belt diagonally like a sword ready to be drawn, rather than straight up and down.
	bool bHangsDiagonal = false;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	EAedirDamageType DamageType = EAedirDamageType::Blunt;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	float FlatDamage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	bool bTwoHanded = false;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	EAedirBodyZone ArmorZone = EAedirBodyZone::Torso;

	// Damage reduction while worn, range 0..0.9.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	float ArmorProtection = 0.f;

	// Manny bone the armor is attached to while worn.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	FName ArmorAttachBone;

	// Worn armour: a model on the body's own skeleton that moves with it, instead of a shape on one bone.
	FString WornMeshPath;

	// A model longer than the item's blade: its full length and where its middle lies on the item.
	float ModelLengthCm = 0.f;
	float ModelCenterCm = 0.f;

	// The model's top is the working end, as on an axe standing on its handle. Swords stand on their point instead.
	bool bModelTopForward = false;

	// A model that came in several parts sharing one frame (blade, guard, grip): the other parts, placed as the main one.
	TArray<FString> ExtraMeshPaths;

	// Worn on a bone as it was modelled: the model is not turned to lie along its length, only scaled to ModelLengthCm by its
	// largest side; worn, it is turned and set off on the bone by these.
	bool bModelAsIs = false;
	FRotator WornRotation = FRotator::ZeroRotator;
	FVector WornOffsetCm = FVector::ZeroVector;

	// A blade that comes with a scabbard: the scabbard's model and length. Put away, the blade sits in it on the belt; drawn,
	// the scabbard stays on the belt.
	FString ScabbardMeshPath;
	float ScabbardLengthCm = 0.f;

	// The model's working end lies the other way along its length than the item's: it is turned end for end.
	bool bModelReversed = false;

	// The model stands as it was made, open side up, instead of being laid along its longest side: a bucket, a pot.
	bool bStandsUpright = false;

	// How thick the model is made against how long, as a share of its own proportions: below 1 a stout model is slimmed
	// to the shaft of the real weapon.
	float ModelThickShare = 1.f;

	// Measured from each model's own shape: turned about the item's middle so the edge or the head lies across the fingers
	// and the handle along the item, then set off so the handle's middle is on the item's axis, where the palm closes.
	FRotator ModelFixRotation = FRotator::ZeroRotator;
	FVector ModelShiftCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	FLinearColor Color = FLinearColor::Gray;

	// Scale of the root cube (or cylinder or sphere) in engine units, so a 100 cm basic shape is scaled by 1.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Catalog")
	FVector BodyScale = FVector::OneVector;
};

UCLASS(Abstract)
class AEDIR_API UAedirItemCatalog : public UObject
{
	GENERATED_BODY()

public:
	static const TArray<FAedirItemDef>& GetAll();

	// Returns null when no definition has the id.
	static const FAedirItemDef* Find(FName Id);

	static TArray<FName> GetIds();
};
