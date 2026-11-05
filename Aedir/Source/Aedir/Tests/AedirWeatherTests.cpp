#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Weather/AedirWeatherTypes.h"

namespace
{
	const EAedirWeather AedirWeatherTestAllTypes[] =
	{
		EAedirWeather::Sunny,
		EAedirWeather::ExtraSunny,
		EAedirWeather::Cloudy,
		EAedirWeather::Rainy,
		EAedirWeather::Stormy,
		EAedirWeather::Thunderstorm,
		EAedirWeather::Snowy
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWeatherPrecipitationTest, "Aedir.Weather.Precipitation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWeatherPrecipitationTest::RunTest(const FString& Parameters)
{
	const FAedirWeatherProfile Rainy = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Rainy);
	const FAedirWeatherProfile Stormy = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Stormy);
	const FAedirWeatherProfile Thunderstorm = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Thunderstorm);
	const FAedirWeatherProfile Snowy = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Snowy);
	const FAedirWeatherProfile Sunny = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Sunny);
	const FAedirWeatherProfile ExtraSunny = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::ExtraSunny);
	const FAedirWeatherProfile Cloudy = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Cloudy);

	TestTrue(TEXT("Rainy has rain"), Rainy.RainIntensity > 0.f);
	TestTrue(TEXT("Rainy has no snow"), Rainy.SnowIntensity == 0.f);
	TestTrue(TEXT("Stormy has rain"), Stormy.RainIntensity > 0.f);
	TestTrue(TEXT("Stormy has no snow"), Stormy.SnowIntensity == 0.f);
	TestTrue(TEXT("Thunderstorm has rain"), Thunderstorm.RainIntensity > 0.f);
	TestTrue(TEXT("Thunderstorm has no snow"), Thunderstorm.SnowIntensity == 0.f);
	TestTrue(TEXT("Snowy has snow"), Snowy.SnowIntensity > 0.f);
	TestTrue(TEXT("Snowy has no rain"), Snowy.RainIntensity == 0.f);
	TestTrue(TEXT("Sunny has no rain"), Sunny.RainIntensity == 0.f);
	TestTrue(TEXT("Sunny has no snow"), Sunny.SnowIntensity == 0.f);
	TestTrue(TEXT("ExtraSunny has no rain"), ExtraSunny.RainIntensity == 0.f);
	TestTrue(TEXT("ExtraSunny has no snow"), ExtraSunny.SnowIntensity == 0.f);
	TestTrue(TEXT("Cloudy has no rain"), Cloudy.RainIntensity == 0.f);
	TestTrue(TEXT("Cloudy has no snow"), Cloudy.SnowIntensity == 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWeatherSunTest, "Aedir.Weather.Sun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWeatherSunTest::RunTest(const FString& Parameters)
{
	const FAedirWeatherProfile Sunny = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Sunny);
	const FAedirWeatherProfile ExtraSunny = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::ExtraSunny);

	TestTrue(TEXT("ExtraSunny is at least as bright as Sunny"), ExtraSunny.SunIntensityLux >= Sunny.SunIntensityLux);
	TestTrue(TEXT("Sunny sun intensity is positive"), Sunny.SunIntensityLux > 0.f);
	TestTrue(TEXT("ExtraSunny sun intensity is positive"), ExtraSunny.SunIntensityLux > 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWeatherLightningTest, "Aedir.Weather.Lightning", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWeatherLightningTest::RunTest(const FString& Parameters)
{
	const FAedirWeatherProfile Thunderstorm = FAedirWeatherProfile::MakeDefaultProfile(EAedirWeather::Thunderstorm);
	TestTrue(TEXT("Thunderstorm produces lightning"), Thunderstorm.LightningPerMinute > 0.f);

	for (const EAedirWeather Weather : AedirWeatherTestAllTypes)
	{
		const FAedirWeatherProfile Profile = FAedirWeatherProfile::MakeDefaultProfile(Weather);
		TestTrue(TEXT("Lightning rate is never negative"), Profile.LightningPerMinute >= 0.f);
		TestTrue(TEXT("Thunderstorm has the highest lightning rate"), Profile.LightningPerMinute <= Thunderstorm.LightningPerMinute);
	}

	const EAedirWeather NonStormTypes[] =
	{
		EAedirWeather::Sunny,
		EAedirWeather::ExtraSunny,
		EAedirWeather::Cloudy,
		EAedirWeather::Rainy,
		EAedirWeather::Snowy
	};

	for (const EAedirWeather Weather : NonStormTypes)
	{
		const FAedirWeatherProfile Profile = FAedirWeatherProfile::MakeDefaultProfile(Weather);
		TestTrue(TEXT("Non-storm weather produces no lightning"), Profile.LightningPerMinute == 0.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWeatherRangeTest, "Aedir.Weather.ProfileRanges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWeatherRangeTest::RunTest(const FString& Parameters)
{
	for (const EAedirWeather Weather : AedirWeatherTestAllTypes)
	{
		const FAedirWeatherProfile Profile = FAedirWeatherProfile::MakeDefaultProfile(Weather);

		TestTrue(TEXT("CloudCoverage is within 0..1"), Profile.CloudCoverage >= 0.f && Profile.CloudCoverage <= 1.f);
		TestTrue(TEXT("RainIntensity is within 0..1"), Profile.RainIntensity >= 0.f && Profile.RainIntensity <= 1.f);
		TestTrue(TEXT("SnowIntensity is within 0..1"), Profile.SnowIntensity >= 0.f && Profile.SnowIntensity <= 1.f);
		TestTrue(TEXT("WetnessTarget is within 0..1"), Profile.WetnessTarget >= 0.f && Profile.WetnessTarget <= 1.f);
		TestTrue(TEXT("SnowTarget is within 0..1"), Profile.SnowTarget >= 0.f && Profile.SnowTarget <= 1.f);

		TestTrue(TEXT("FogDensity is non-negative"), Profile.FogDensity >= 0.f);
		TestTrue(TEXT("SunIntensityLux is non-negative"), Profile.SunIntensityLux >= 0.f);
		TestTrue(TEXT("SkyLightIntensity is non-negative"), Profile.SkyLightIntensity >= 0.f);
		TestTrue(TEXT("WindSpeed is non-negative"), Profile.WindSpeed >= 0.f);

		TestTrue(TEXT("SunColor channels are within 0..1"), Profile.SunColor.R >= 0.f && Profile.SunColor.R <= 1.f && Profile.SunColor.G >= 0.f && Profile.SunColor.G <= 1.f && Profile.SunColor.B >= 0.f && Profile.SunColor.B <= 1.f);
	}

	return true;
}

#endif
