#pragma once

#include "CoreMinimal.h"

class UWorld;

// Sets the people and things of the test map where they belong, once, at the start of a fresh play.
namespace AedirWorldDirector
{
	void PopulateTestMap(UWorld* World);

	// The natural test level: a camp with food, drink, weapons and armour laid out, people, a meadow, a forest, rocks and ruins.
	void PopulateNatureMap(UWorld* World);
}
