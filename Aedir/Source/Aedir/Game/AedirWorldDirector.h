#pragma once

#include "CoreMinimal.h"

class UWorld;

// Sets the people and things of the test map where they belong, once, at the start of a fresh play.
namespace AedirWorldDirector
{
	// Worn by an actor of a level whose content was laid out in the editor: the game then places nothing more in it.
	inline const FName LaidOutTag(TEXT("AedirLaidOut"));

	AEDIR_API void PopulateTestMap(UWorld* World);

	// The natural test level: a camp with food, drink, weapons and armour laid out, people, a meadow, a forest, rocks and ruins.
	// Only when the level does not already hold them.
	AEDIR_API void PopulateNatureMap(UWorld* World);

	// Places all of it in the world, game or editor; the editor saves it into the level so it can be edited there.
	AEDIR_API void LayOutNatureMap(UWorld* World);

	// The landscape around the camp, at every start: water in the stream bed, a waterfall where it begins, a ruined tower
	// on the highest hill, a cave in a hillside, rock outcrops, fallen trees, a campfire and torches.
	AEDIR_API void AddScenery(UWorld* World);

	// The height of the water's surface at a point, if the point is in the water.
	AEDIR_API bool GetWaterSurface(const UWorld* World, const FVector& Point, float& OutZ);
}
