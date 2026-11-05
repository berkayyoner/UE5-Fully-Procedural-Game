#pragma once

#include "CoreMinimal.h"
#include "AedirWeatherTypes.generated.h"

UENUM(BlueprintType)
enum class EAedirWeather : uint8
{
	Sunny,
	ExtraSunny,
	Cloudy,
	Rainy,
	Stormy,
	Thunderstorm,
	Snowy
};

USTRUCT(BlueprintType)
struct AEDIR_API FAedirWeatherProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0, ClampMax=1))
	float CloudCoverage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0, ClampMax=1))
	float RainIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0, ClampMax=1))
	float SnowIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0))
	float FogDensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0))
	float SunIntensityLux = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0))
	float SkyLightIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0))
	float WindSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0))
	float LightningPerMinute = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0, ClampMax=1))
	float WetnessTarget = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather", meta=(ClampMin=0, ClampMax=1))
	float SnowTarget = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Weather")
	FLinearColor SunColor = FLinearColor::White;

	static FAedirWeatherProfile MakeDefaultProfile(EAedirWeather Weather)
	{
		FAedirWeatherProfile Profile;

		switch (Weather)
		{
		case EAedirWeather::Sunny:
			Profile.CloudCoverage = 0.1f;
			Profile.FogDensity = 0.01f;
			Profile.SunIntensityLux = 10.f;
			Profile.SkyLightIntensity = 1.f;
			Profile.WindSpeed = 2.f;
			Profile.SunColor = FLinearColor(1.f, 0.95f, 0.85f);
			break;

		case EAedirWeather::ExtraSunny:
			Profile.CloudCoverage = 0.f;
			Profile.FogDensity = 0.002f;
			Profile.SunIntensityLux = 14.f;
			Profile.SkyLightIntensity = 1.2f;
			Profile.WindSpeed = 1.f;
			Profile.SunColor = FLinearColor(1.f, 0.97f, 0.9f);
			break;

		case EAedirWeather::Cloudy:
			Profile.CloudCoverage = 0.7f;
			Profile.FogDensity = 0.02f;
			Profile.SunIntensityLux = 4.f;
			Profile.SkyLightIntensity = 0.7f;
			Profile.WindSpeed = 4.f;
			Profile.WetnessTarget = 0.05f;
			Profile.SunColor = FLinearColor(0.8f, 0.85f, 0.9f);
			break;

		case EAedirWeather::Rainy:
			Profile.CloudCoverage = 0.9f;
			Profile.RainIntensity = 0.6f;
			Profile.FogDensity = 0.03f;
			Profile.SunIntensityLux = 2.f;
			Profile.SkyLightIntensity = 0.5f;
			Profile.WindSpeed = 6.f;
			Profile.WetnessTarget = 1.f;
			Profile.SunColor = FLinearColor(0.6f, 0.66f, 0.74f);
			break;

		case EAedirWeather::Stormy:
			Profile.CloudCoverage = 1.f;
			Profile.RainIntensity = 0.9f;
			Profile.FogDensity = 0.04f;
			Profile.SunIntensityLux = 1.2f;
			Profile.SkyLightIntensity = 0.35f;
			Profile.WindSpeed = 14.f;
			Profile.LightningPerMinute = 1.5f;
			Profile.WetnessTarget = 1.f;
			Profile.SunColor = FLinearColor(0.5f, 0.55f, 0.62f);
			break;

		case EAedirWeather::Thunderstorm:
			Profile.CloudCoverage = 1.f;
			Profile.RainIntensity = 1.f;
			Profile.FogDensity = 0.05f;
			Profile.SunIntensityLux = 1.f;
			Profile.SkyLightIntensity = 0.3f;
			Profile.WindSpeed = 18.f;
			Profile.LightningPerMinute = 6.f;
			Profile.WetnessTarget = 1.f;
			Profile.SunColor = FLinearColor(0.45f, 0.5f, 0.6f);
			break;

		case EAedirWeather::Snowy:
			Profile.CloudCoverage = 0.85f;
			Profile.SnowIntensity = 0.8f;
			Profile.FogDensity = 0.03f;
			Profile.SunIntensityLux = 2.5f;
			Profile.SkyLightIntensity = 0.8f;
			Profile.WindSpeed = 7.f;
			Profile.WetnessTarget = 0.2f;
			Profile.SnowTarget = 1.f;
			Profile.SunColor = FLinearColor(0.75f, 0.8f, 0.9f);
			break;
		}

		return Profile;
	}
};
