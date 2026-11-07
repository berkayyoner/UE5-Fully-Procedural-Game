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
