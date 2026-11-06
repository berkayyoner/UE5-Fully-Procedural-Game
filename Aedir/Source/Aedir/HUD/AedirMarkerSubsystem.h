#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirMarkerSubsystem.generated.h"

UENUM(BlueprintType)
enum class EAedirMarkerType : uint8
{
	Mission,
	Area,
	Custom
};

USTRUCT(BlueprintType)
struct AEDIR_API FAedirMapMarker
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	EAedirMarkerType Type = EAedirMarkerType::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	bool bShowDistance = true;

	// Visible on the map as a "?" once the player came within the reveal distance. Defaults to true so quest markers always show.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	bool bRevealed = true;

	// Fast travel is only possible to discovered markers. Locations placed in the world start with both flags false.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Marker")
	bool bDiscovered = true;
};

// Id of the single marker the player places on the map.
inline const FName AedirCustomMarkerId(TEXT("PlayerCustomMarker"));

// Local to each machine, not replicated.
UCLASS()
class AEDIR_API UAedirMarkerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Replaces an existing marker with the same Id.
	UFUNCTION(BlueprintCallable, Category="Aedir|Marker")
	void AddMarker(const FAedirMapMarker& Marker);

	UFUNCTION(BlueprintCallable, Category="Aedir|Marker")
	void RemoveMarker(FName Id);

	UFUNCTION(BlueprintCallable, Category="Aedir|Marker")
	void ClearMarkers(EAedirMarkerType Type);

	const TArray<FAedirMapMarker>& GetMarkers() const;

	const FAedirMapMarker* FindMarker(FName Id) const;

	// Reveals and discovers markers near PlayerLocation. Returns the labels of the markers discovered by this call.
	TArray<FText> UpdateDiscovery(const FVector& PlayerLocation);

	void DiscoverAll();

	// Registers an undiscovered, unrevealed location. Returns its id.
	FName AddLocation(const FText& Label, const FVector& Location);

	// Replaces the player's previous flag. Custom markers are always discovered.
	void SetCustomMarker(const FVector& Location);

private:
	TArray<FAedirMapMarker> Markers;
};
