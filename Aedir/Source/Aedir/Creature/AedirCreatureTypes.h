#pragma once

#include "CoreMinimal.h"
#include "AedirCreatureTypes.generated.h"

UENUM(BlueprintType)
enum class EAedirRelation : uint8
{
	Enemy,
	Neutral,
	Ally
};

UENUM(BlueprintType)
enum class EAedirSkill : uint8
{
	SingleHandWeapon,
	TwoHandedWeapon,
	RangedWeapon,
	DestructionMagic,
	RegenerationMagic,
	IllusionMagic,
	MetamorphosisMagic,
	SummonMagic,
	HeavyArmor,
	LightArmor,
	Smelting,
	Alchemy,
	Enchanting,
	Count UMETA(Hidden)
};

// What a hurt does to a body. Knock: a fist, a kick, a bump; it takes the senses and never the life. Blow: a weapon, a long
// fall; it hurts inside as well. Life: poison, hunger, lightning, a hand at the throat; it goes at the life.
UENUM(BlueprintType)
enum class EAedirHarm : uint8
{
	Knock,
	Blow,
	Life
};

USTRUCT(BlueprintType)
struct FAedirGroupRelationEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Groups")
	FName GroupA;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Groups")
	FName GroupB;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Groups")
	EAedirRelation Relation = EAedirRelation::Neutral;
};
