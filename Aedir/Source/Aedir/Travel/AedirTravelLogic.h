#pragma once

#include "CoreMinimal.h"
#include "HUD/AedirMarkerSubsystem.h"

// Pure rules for map discovery and fast travel, kept free of world state so they can be tested.
namespace AedirTravelLogic
{
	// Distances are measured on the ground plane from the local player and checked once per interval.
	constexpr float RevealDistanceCm = 12000.f;
	constexpr float DiscoverDistanceCm = 3500.f;
	constexpr float DiscoveryIntervalSeconds = 1.f;

	// Chance that a fast travel is cut short by an ambush, and where along the way it happens.
	constexpr float EncounterChance = 0.3f;
	constexpr float EncounterMinFraction = 0.25f;
	constexpr float EncounterMaxFraction = 0.75f;
	constexpr int32 EncounterMinBandits = 2;
	constexpr int32 EncounterMaxBandits = 3;

	// Walking pace used to turn a distance into the in-game hours a fast travel takes.
	constexpr float TravelSpeedKmh = 4.f;

	// A living hostile creature closer than this blocks fast travel.
	constexpr float CombatRadiusCm = 2500.f;

	// Only markers other than the player's own flag are gated behind discovery.
	bool UsesDiscovery(EAedirMarkerType Type);

	// Moves a marker to the state its distance to the player allows. Never goes backwards. Returns true when it became discovered.
	bool UpdateDiscovery(EAedirMarkerType Type, float DistanceCm, bool& bInOutRevealed, bool& bInOutDiscovered);

	bool CanFastTravelTo(EAedirMarkerType Type, bool bDiscovered);

	float GetTravelHours(float DistanceCm);

	// Roll is a random value in 0..1.
	bool ShouldEncounter(float Roll);
	float GetEncounterFraction(float Roll);
	FVector GetEncounterPoint(const FVector& From, const FVector& To, float Fraction);
}
