#pragma once

#include "CoreMinimal.h"
#include "AedirCombatTypes.generated.h"

UENUM(BlueprintType)
enum class EAedirSwingType : uint8
{
	Jab,
	Hook,
	Uppercut,
	Overhand
};

UENUM(BlueprintType)
enum class EAedirBodyZone : uint8
{
	Head,
	Torso,
	Arm,
	Leg
};

UENUM(BlueprintType)
enum class EAedirLimb : uint8
{
	LeftArm,
	RightArm,
	LeftLeg,
	RightLeg
};

UENUM(BlueprintType)
enum class EAedirDamageType : uint8
{
	Blunt,
	Slash,
	Pierce
};

// One physical contact between an attacker and a creature, used by the victim's hit reaction model.
USTRUCT(BlueprintType)
struct AEDIR_API FAedirImpactInfo
{
	GENERATED_BODY()

	// Physics bone that was struck. NAME_None means no bone was resolved.
	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	FName Bone = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	EAedirBodyZone Zone = EAedirBodyZone::Torso;

	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	float EnergyJoules = 0.f;

	// Unit vector from the attacker towards the victim.
	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	FVector Direction = FVector::ZeroVector;

	// World location of the contact.
	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	FVector Location = FVector::ZeroVector;

	// Effective moving mass of the attacking weapon chain in kg.
	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	float MassKg = 0.f;

	// Closing speed between the attacking tip and the victim in cm/s.
	UPROPERTY(BlueprintReadWrite, Category="Aedir|Combat")
	float RelativeSpeed = 0.f;
};
