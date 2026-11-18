#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirAmbienceSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;

// The sound of the land around the one who listens: the wildlife of the day, the insects and owls of the night and the
// birds of dawn and dusk under them, and now and then a single bird, a crow, an owl, a cricket, leaves or a twig somewhere
// nearby. Rain and snow hush the animals.
UCLASS()
class AEDIR_API UAedirAmbienceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	// How loud the lasting sound of the day, the night and the hours between is at its fullest.
	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float DayVolume = 0.45f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float NightVolume = 0.4f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float TwilightVolume = 0.35f;

	// The day sound comes in from DayFromHour to DayFullHour and goes from NightFromHour to NightFullHour; the birds of the
	// hours between are loudest TwilightSpanHours either side of the middle of each change.
	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float DayFromHour = 5.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float DayFullHour = 7.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float NightFromHour = 19.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float NightFullHour = 21.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float TwilightSpanHours = 1.5f;

	// Full rain or snow takes this share of the animals' sound away.
	UPROPERTY(EditAnywhere, Category="Aedir|Ambience", meta=(ClampMin="0", ClampMax="1"))
	float WeatherHushShare = 0.75f;

	// A single sound somewhere about every SpotMinSeconds to SpotMaxSeconds, SpotNearCm to SpotFarCm away, as loud as
	// SpotVolume at most.
	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float SpotMinSeconds = 3.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float SpotMaxSeconds = 11.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float SpotNearCm = 600.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float SpotFarCm = 3000.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float SpotVolume = 0.6f;

	// How fast the lasting sounds follow the hour and the weather (share of the way a second).
	UPROPERTY(EditAnywhere, Category="Aedir|Ambience")
	float FadeRate = 0.5f;

private:
	enum class EBed : uint8 { Day, Night, Twilight, Count };
	enum class ESpot : uint8 { Day, Night, Twilight, Any, Count };

	void UpdateBed(EBed Bed, float Wanted, float DeltaTime);
	void PlaySpot(ESpot Kind, const FVector& Listener, float Loudness);
	USoundBase* LoadSound(const TCHAR* Path);
	const TArray<TObjectPtr<USoundBase>>& GetSpots(ESpot Kind);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Beds;
	float BedLevel[static_cast<int32>(EBed::Count)] = {};

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Loaded;
	TArray<TObjectPtr<USoundBase>> Spots[static_cast<int32>(ESpot::Count)];
	bool bSpotsLoaded[static_cast<int32>(ESpot::Count)] = {};

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SpotAttenuation;

	float NextSpotIn = 4.f;
};
