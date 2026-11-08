#pragma once

#include "CoreMinimal.h"

class UPackage;

namespace AedirTestMap
{
	// The one landscape master material: six paint layers, each with its base colour, normal, roughness, displacement,
	// ambient occlusion and metallic maps as parameters, blended by their heights where painted, with Nanite displacement.
	void BuildLandscapeMaterial(TArray<UPackage*>& ToSave);

	// The test level: rolling land around a flat clearing and a stream bed, painted with a first pass of the layers, the sky
	// and light, and everything that stands on it saved in the level so it can be edited there.
	void BuildLevel();

	// How far a point, in metres from the middle of the land, is from the stream.
	float TestStreamDistance(float X, float Y);
}
