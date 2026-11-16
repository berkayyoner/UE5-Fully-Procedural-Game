#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Weather/AedirWeatherTypes.h"
#include "AedirWeatherSettings.generated.h"

class UNiagaraSystem;
class USoundBase;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Aedir Weather"))
class AEDIR_API UAedirWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAedirWeatherSettings();

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Profiles")
	TMap<EAedirWeather, FAedirWeatherProfile> Profiles;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Automatic")
	TMap<EAedirWeather, float> AutoWeights;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Automatic")
	FVector2D WeatherChangeIntervalMinMax = FVector2D(300.f, 900.f);

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Automatic", meta=(ClampMin=0))
	float DefaultTransitionSeconds = 45.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Automatic")
	bool bAutoChangeWeather = true;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Automatic")
	EAedirWeather StartWeather = EAedirWeather::Sunny;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float LightningStrikeRadius = 60.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float LightningDamage = 60.f;

	// How far from a player a bolt comes down (cm), and how much the far end is favoured: 1 spreads them evenly by distance
	// scale, lower keeps more of them far off, so a strike close by is rare.
	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning")
	FVector2D LightningStrikeDistanceMinMaxCm = FVector2D(6000.f, 500000.f);

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0.05, ClampMax=1))
	float LightningNearness = 0.5f;

	// A bolt lights the land by up to this many lux when it comes down within LightningFlashFullWithinCm, less as the square
	// of the distance past it.
	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float LightningFlashLux = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=1))
	float LightningFlashFullWithinCm = 25000.f;

	// Thunder is heard only from bolts within this distance, at once with the flash, at full loudness up to
	// ThunderFullWithinCm and down to ThunderFarShare at the edge. Farther ones are seen and not heard.
	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float ThunderHeardWithinCm = 60000.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float ThunderFullWithinCm = 10000.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0, ClampMax=1))
	float ThunderFarShare = 0.45f;

	// Flashes inside the clouds that never reach the ground: so many for each bolt that does, how bright (candelas) and how
	// far round their light spreads in the cloud (cm), and how much of it falls on the land below.
	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float CloudFlashesPerStrike = 1.2f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float CloudFlashCandelas = 1.2e7f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float CloudFlashRadiusCm = 200000.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Lightning", meta=(ClampMin=0))
	float CloudFlashLandLux = 0.4f;

	// Rain-soaked: everything within WetReachCm of the eye darkens by up to WetDarkenShare and shines with a roughness of
	// WetRoughness over up to WetGlossShare of its own, as wet as the ground is.
	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Wet", meta=(ClampMin=0, ClampMax=1))
	float WetDarkenShare = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Wet", meta=(ClampMin=0, ClampMax=1))
	float WetGlossShare = 0.8f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Wet", meta=(ClampMin=0, ClampMax=1))
	float WetRoughness = 0.08f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Wet", meta=(ClampMin=1000))
	float WetReachCm = 12000.f;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Effects")
	TSoftObjectPtr<UNiagaraSystem> RainSystem;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Effects")
	TSoftObjectPtr<UNiagaraSystem> SnowSystem;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Effects")
	TSoftObjectPtr<UNiagaraSystem> LightningBoltSystem;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Effects")
	TSoftObjectPtr<UNiagaraSystem> RainSplashSystem;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Audio")
	TSoftObjectPtr<USoundBase> RainLoop;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Audio")
	TSoftObjectPtr<USoundBase> WindLoop;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Weather|Audio")
	TSoftObjectPtr<USoundBase> ThunderSound;

	FAedirWeatherProfile GetProfile(EAedirWeather Weather) const;
};
