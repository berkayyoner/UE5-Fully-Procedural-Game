#pragma once

#include "CoreMinimal.h"

class UPackage;

namespace AedirTestMap
{
	// The landscape material: the scanned ground surfaces blended by slope, height and patches, at real scale and without
	// visible repeats.
	void BuildLandscapeMaterial(TArray<UPackage*>& ToSave);

	// The test level: rolling land around a flat clearing, a stream bed, the sky and light. What stands on it is placed by
	// the game when it starts.
	void BuildLevel();
}
