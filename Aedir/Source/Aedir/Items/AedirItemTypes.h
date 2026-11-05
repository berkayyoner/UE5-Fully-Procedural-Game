#pragma once

#include "CoreMinimal.h"
#include "AedirItemTypes.generated.h"

UENUM(BlueprintType)
enum class EAedirItemClass : uint8
{
	Weapon,
	Unusable,
	Poison,
	Book,
	Armor,
	Object
};

UENUM(BlueprintType)
enum class EAedirHand : uint8
{
	Left,
	Right
};

UENUM(BlueprintType)
enum class EAedirHandState : uint8
{
	Idle,
	Reaching,
	Holding
};
