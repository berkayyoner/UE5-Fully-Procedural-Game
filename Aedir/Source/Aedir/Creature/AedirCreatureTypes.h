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
