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
