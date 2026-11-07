#include "Weather/AedirWeatherSettings.h"

UAedirWeatherSettings::UAedirWeatherSettings()
{
	// Wind and thunder come from the scanned sound library.
	WindLoop = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Wind/sfx_ambient_forest_WindTone_PGFX_F1.sfx_ambient_forest_WindTone_PGFX_F1")));
	ThunderSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/_Content/SFX/Thunder/wav/sfx_Ambient_Thunder_01.sfx_Ambient_Thunder_01")));

	if (Profiles.Num() == 0)
	{
		for (const EAedirWeather Weather : { EAedirWeather::Sunny, EAedirWeather::ExtraSunny, EAedirWeather::Cloudy, EAedirWeather::Rainy, EAedirWeather::Stormy, EAedirWeather::Thunderstorm, EAedirWeather::Snowy })
		{
			Profiles.Add(Weather, FAedirWeatherProfile::MakeDefaultProfile(Weather));
		}
	}

	if (AutoWeights.Num() == 0)
	{
		AutoWeights.Add(EAedirWeather::Sunny, 3.f);
		AutoWeights.Add(EAedirWeather::ExtraSunny, 1.5f);
		AutoWeights.Add(EAedirWeather::Cloudy, 3.f);
		AutoWeights.Add(EAedirWeather::Rainy, 2.f);
		AutoWeights.Add(EAedirWeather::Stormy, 1.f);
		AutoWeights.Add(EAedirWeather::Thunderstorm, 0.7f);
		AutoWeights.Add(EAedirWeather::Snowy, 1.f);
	}
}

FAedirWeatherProfile UAedirWeatherSettings::GetProfile(EAedirWeather Weather) const
{
	if (const FAedirWeatherProfile* Profile = Profiles.Find(Weather))
	{
		return *Profile;
	}

	return FAedirWeatherProfile::MakeDefaultProfile(Weather);
}
