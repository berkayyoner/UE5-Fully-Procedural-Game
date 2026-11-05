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
};

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

private:
	TArray<FAedirMapMarker> Markers;
};
