#include "HUD/AedirMarkerSubsystem.h"

#include "Travel/AedirTravelLogic.h"

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

const FAedirMapMarker* UAedirMarkerSubsystem::FindMarker(FName Id) const
{
	return Markers.FindByPredicate([Id](const FAedirMapMarker& Existing)
	{
		return Existing.Id == Id;
	});
}

TArray<FText> UAedirMarkerSubsystem::UpdateDiscovery(const FVector& PlayerLocation)
{
	TArray<FText> Discovered;
	for (FAedirMapMarker& Marker : Markers)
	{
		const float Distance = static_cast<float>((Marker.Location - PlayerLocation).Size2D());
		if (AedirTravelLogic::UpdateDiscovery(Marker.Type, Distance, Marker.bRevealed, Marker.bDiscovered))
		{
			Discovered.Add(Marker.Label);
		}
	}
	return Discovered;
}

void UAedirMarkerSubsystem::DiscoverAll()
{
	for (FAedirMapMarker& Marker : Markers)
	{
		Marker.bRevealed = true;
		Marker.bDiscovered = true;
	}
}

void UAedirMarkerSubsystem::SetCustomMarker(const FVector& Location)
{
	FAedirMapMarker Marker;
	Marker.Id = AedirCustomMarkerId;
	Marker.Label = NSLOCTEXT("Aedir", "CustomMarker", "Marker");
	Marker.Location = Location;
	Marker.Type = EAedirMarkerType::Custom;
	Marker.Color = FLinearColor(0.35f, 0.8f, 1.f, 1.f);
	AddMarker(Marker);
}

FName UAedirMarkerSubsystem::AddLocation(const FText& Label, const FVector& Location)
{
	FAedirMapMarker Marker;
	Marker.Id = FName(*FString::Printf(TEXT("Location_%s"), *Label.ToString().Replace(TEXT(" "), TEXT("_"))));
	Marker.Label = Label;
	Marker.Location = Location;
	Marker.Type = EAedirMarkerType::Area;
	Marker.Color = FLinearColor(0.92f, 0.86f, 0.7f, 1.f);
	Marker.bRevealed = false;
	Marker.bDiscovered = false;
	AddMarker(Marker);
	return Marker.Id;
}
