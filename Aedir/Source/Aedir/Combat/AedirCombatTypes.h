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
