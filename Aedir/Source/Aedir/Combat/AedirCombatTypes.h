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
