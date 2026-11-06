#include "Travel/AedirTravelLogic.h"

bool AedirTravelLogic::UsesDiscovery(EAedirMarkerType Type)
{
	return Type != EAedirMarkerType::Custom;
}

bool AedirTravelLogic::UpdateDiscovery(EAedirMarkerType Type, float DistanceCm, bool& bInOutRevealed, bool& bInOutDiscovered)
{
	if (!UsesDiscovery(Type))
	{
		bInOutRevealed = true;
		bInOutDiscovered = true;
		return false;
	}

	if (bInOutDiscovered)
	{
		bInOutRevealed = true;
		return false;
	}

	if (DistanceCm <= DiscoverDistanceCm)
	{
		bInOutRevealed = true;
		bInOutDiscovered = true;
		return true;
	}

	if (DistanceCm <= RevealDistanceCm)
	{
		bInOutRevealed = true;
	}
	return false;
}

bool AedirTravelLogic::CanFastTravelTo(EAedirMarkerType Type, bool bDiscovered)
{
	return UsesDiscovery(Type) && bDiscovered;
}

float AedirTravelLogic::GetTravelHours(float DistanceCm)
{
	const float Kilometers = FMath::Max(0.f, DistanceCm) * 0.00001f;
	return Kilometers / TravelSpeedKmh;
}

bool AedirTravelLogic::ShouldEncounter(float Roll)
{
	return Roll < EncounterChance;
}

float AedirTravelLogic::GetEncounterFraction(float Roll)
{
	return FMath::Lerp(EncounterMinFraction, EncounterMaxFraction, FMath::Clamp(Roll, 0.f, 1.f));
}

FVector AedirTravelLogic::GetEncounterPoint(const FVector& From, const FVector& To, float Fraction)
{
	return FMath::Lerp(From, To, FMath::Clamp(Fraction, EncounterMinFraction, EncounterMaxFraction));
}
