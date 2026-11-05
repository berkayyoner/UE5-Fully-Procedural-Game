#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreatureTypes.h"
#include "UObject/Object.h"
#include "AedirRaceCatalog.generated.h"

USTRUCT(BlueprintType)
struct AEDIR_API FAedirSkillBonus
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	EAedirSkill Skill = EAedirSkill::SingleHandWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float Bonus = 0.f;
};

// One playable race. Stats are the starting values after character creation.
USTRUCT(BlueprintType)
struct AEDIR_API FAedirRaceDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float MaxMana = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float MaxStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float Strength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float Athletism = 10.f;

	// Multiplies the strength-derived size of the creature.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	float HeightMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Race")
	TArray<FAedirSkillBonus> SkillBonuses;
};

// Code-defined race roster: Human, Elf, DarkElf and Dwarf.
UCLASS(Abstract)
class AEDIR_API UAedirRaceCatalog : public UObject
{
	GENERATED_BODY()

public:
	static const TArray<FAedirRaceDef>& GetAll();

	// Returns null when no race has the id.
	static const FAedirRaceDef* Find(FName Id);

	static TArray<FName> GetIds();

	// Strength-derived scale multiplied by the race height, clamped to 0.6..1.5.
	static float ComputeRaceSizeScale(float StrengthDerivedScale, float HeightMultiplier);

	// Sum of the race's bonuses for one skill.
	static float GetStartSkillValue(const FAedirRaceDef& Race, EAedirSkill Skill);
};
