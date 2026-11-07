#pragma once

#include "CoreMinimal.h"

class UWorld;

// Sets the people and things of the test map where they belong, once, at the start of a fresh play.
namespace AedirWorldDirector
{
	void PopulateTestMap(UWorld* World);
}
