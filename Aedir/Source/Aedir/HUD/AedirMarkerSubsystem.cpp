#include "HUD/AedirMarkerSubsystem.h"

void UAedirMarkerSubsystem::AddMarker(const FAedirMapMarker& Marker)
{
	const int32 Index = Markers.IndexOfByPredicate([&Marker](const FAedirMapMarker& Existing)
	{
		return Existing.Id == Marker.Id;
	});

	if (Index != INDEX_NONE)
	{
		Markers[Index] = Marker;
	}
	else
	{
		Markers.Add(Marker);
	}
}

void UAedirMarkerSubsystem::RemoveMarker(FName Id)
{
	Markers.RemoveAll([Id](const FAedirMapMarker& Existing)
	{
		return Existing.Id == Id;
	});
}

void UAedirMarkerSubsystem::ClearMarkers(EAedirMarkerType Type)
{
	Markers.RemoveAll([Type](const FAedirMapMarker& Existing)
	{
		return Existing.Type == Type;
	});
}

const TArray<FAedirMapMarker>& UAedirMarkerSubsystem::GetMarkers() const
{
	return Markers;
}
