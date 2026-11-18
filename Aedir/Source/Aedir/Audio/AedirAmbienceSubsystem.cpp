#include "Audio/AedirAmbienceSubsystem.h"

#include "Aedir.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/AedirSettingsSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"
#include "Weather/AedirWeatherTypes.h"

namespace
{
	// The lasting sound of each part of the day, in the order of the beds.
	const TCHAR* const AmbienceBeds[] =
	{
		TEXT("/Game/SFX_Essentials_FREE_SAMPLE/SFX_Essentials_10_AMBIENCE_01/Cues/AMBForst_InsJ_Ambience_Day-Wildlife_01-01_LOOP_Cue"),
		TEXT("/Game/SFX_Essentials_FREE_SAMPLE/SFX_Essentials_19_AMBIENCE_02/Cues/AMBMisc_InsJ_Ambience_Night-Wildlife_07-03_LOOP_EQ_Cue"),
		TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/sfx_Ambient_Twilight_Birds_pgfx_meta"),
	};

	// The single sounds heard now and then, by the part of the day they belong to; the last are heard at any hour.
	const TArray<const TCHAR*> AmbienceSpots[] =
	{
		{
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_ambient_forest_bird_robin_OVERHEAD_PGFX_A1"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_Ambient_SeperateBirds"),
			TEXT("/Game/SFX_Essentials_FREE_SAMPLE/SFX_Essentials_16_BIRDS/Cues/BIRDSong_InsJ_Bird-Whistling_Robin_Single_01-01_Cue"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_ambient_CrowFlapCaw_mono_pgfx_01"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_ambient_CrowFlapCaw_mono_pgfx_03"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_ambient_CrowFlapCaw_mono_pgfx_05"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_Ambient_DistCrows"),
		},
		{
			TEXT("/Game/SFX_Essentials_FREE_SAMPLE/SFX_Essentials_16_BIRDS/Cues/BIRDPrey_InsJ_Owl_Tawny-Male_Hooting_01-01_DRY_Cue"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_Ambient_Raaven_Single_01"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/wav/sfx_Ambient_Raaven_Single_02"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Insects/wav/Amb_Cricket_01"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Insects/wav/Amb_Cricket_03"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Insects/wav/Amb_Cricket_05"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Insects/wav/Amb_Cricket_Burst_Long_01"),
		},
		{
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/Amb_TwilightBird_01"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/Amb_TwilightBird_02"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/Amb_TwilightBird_03"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Birds/Amb_TwilightBird_04"),
		},
		{
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_Ambience_Twig_Snap_Single_nl_01"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_Ambience_Twig_Snap_Single_nl_03"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_Ambience_Twig_Snap_Single_nl_05"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_ambient_forest_leaves_PGFX_A1"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_ambient_forest_leaves_PGFX_B3"),
			TEXT("/Game/Audio/_Content/SFX/Amb_Misc/Foliage/wav/sfx_ambient_forest_leaves_PGFX_D2"),
		},
	};

	TAutoConsoleVariable<int32> CVarAedirAmbience(TEXT("aedir.Ambience"), 1, TEXT("The sound of the land around: on (1) or off (0)."));
	TAutoConsoleVariable<int32> CVarAedirAmbienceLog(TEXT("aedir.AmbienceLog"), 0, TEXT("Log the ambience levels and every single sound it plays (1)."));

	// 0 before From, 1 after Full, smoothly between.
	float AmbienceRamp(float Hour, float From, float Full)
	{
		return Full > From ? FMath::SmoothStep(From, Full, Hour) : (Hour >= Full ? 1.f : 0.f);
	}
}

bool UAedirAmbienceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer && Super::ShouldCreateSubsystem(Outer);
}

TStatId UAedirAmbienceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAedirAmbienceSubsystem, STATGROUP_Tickables);
}

void UAedirAmbienceSubsystem::Deinitialize()
{
	for (UAudioComponent* Bed : Beds)
	{
		if (Bed)
		{
			Bed->Stop();
		}
	}
	Beds.Reset();
	Super::Deinitialize();
}

USoundBase* UAedirAmbienceSubsystem::LoadSound(const TCHAR* Path)
{
	const FString Name = FPaths::GetBaseFilename(Path);
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("%s.%s"), Path, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Sound)
	{
		Loaded.Add(Sound);
	}
	else
	{
		UE_LOG(LogAedir, Warning, TEXT("[Ambience] Missing %s"), Path);
	}
	return Sound;
}

const TArray<TObjectPtr<USoundBase>>& UAedirAmbienceSubsystem::GetSpots(ESpot Kind)
{
	const int32 Index = static_cast<int32>(Kind);
	if (!bSpotsLoaded[Index])
	{
		bSpotsLoaded[Index] = true;
		for (const TCHAR* Path : AmbienceSpots[Index])
		{
			if (USoundBase* Sound = LoadSound(Path))
			{
				Spots[Index].Add(Sound);
			}
		}
	}
	return Spots[Index];
}

void UAedirAmbienceSubsystem::UpdateBed(EBed Bed, float Wanted, float DeltaTime)
{
	const int32 Index = static_cast<int32>(Bed);
	float& Level = BedLevel[Index];
	Level += (Wanted - Level) * FMath::Min(FadeRate * DeltaTime, 1.f);
	if (Beds.Num() <= Index)
	{
		Beds.SetNum(static_cast<int32>(EBed::Count));
	}
	TObjectPtr<UAudioComponent>& Component = Beds[Index];
	if (Level < 0.005f)
	{
		if (Component && Component->IsPlaying())
		{
			Component->Stop();
		}
		return;
	}
	if (!Component)
	{
		USoundBase* Sound = LoadSound(AmbienceBeds[Index]);
		if (!Sound)
		{
			Level = 0.f;
			return;
		}
		// Not spatial: the land all round, kept when the level changes.
		Component = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, true, false);
		if (!Component)
		{
			return;
		}
		Component->bIsUISound = false;
	}
	Component->SetVolumeMultiplier(Level * UAedirSettingsSubsystem::GetValues().WeatherVolume);
	// One that is not a loop of its own is started again as it ends, a little into it so it is not heard to begin.
	if (!Component->IsPlaying())
	{
		Component->Play(FMath::FRandRange(0.f, 2.f));
	}
}

void UAedirAmbienceSubsystem::PlaySpot(ESpot Kind, const FVector& Listener, float Loudness)
{
	const TArray<TObjectPtr<USoundBase>>& Options = GetSpots(Kind);
	if (Options.IsEmpty() || Loudness <= 0.01f)
	{
		return;
	}
	if (!SpotAttenuation)
	{
		SpotAttenuation = NewObject<USoundAttenuation>(this);
		FSoundAttenuationSettings& Settings = SpotAttenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(SpotNearCm, 0.f, 0.f);
		Settings.FalloffDistance = SpotFarCm * 1.5f;
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	}
	// Somewhere round about, birds up in the trees, the rest near the ground.
	const float Yaw = FMath::FRandRange(0.f, 360.f);
	const float Away = FMath::FRandRange(SpotNearCm, SpotFarCm);
	const bool bUp = Kind != ESpot::Any;
	const FVector Where = Listener + FRotator(0.f, Yaw, 0.f).Vector() * Away + FVector(0.f, 0.f, bUp ? FMath::FRandRange(200.f, 1200.f) : -120.f);
	USoundBase* Sound = Options[FMath::RandRange(0, Options.Num() - 1)];
	const float Volume = SpotVolume * Loudness * FMath::FRandRange(0.6f, 1.f) * UAedirSettingsSubsystem::GetValues().WeatherVolume;
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Where, FRotator::ZeroRotator, Volume, FMath::FRandRange(0.94f, 1.06f), 0.f, SpotAttenuation, nullptr);
	UE_CLOG(CVarAedirAmbienceLog.GetValueOnGameThread() != 0, LogAedir, Log, TEXT("[Ambience] %s %.0f m off, volume %.2f"), *Sound->GetName(), Away / 100.f, Volume);
}

void UAedirAmbienceSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	APlayerController* Viewer = World ? World->GetFirstPlayerController() : nullptr;
	if (!Viewer || DeltaTime <= 0.f)
	{
		return;
	}
	const bool bOn = CVarAedirAmbience.GetValueOnGameThread() != 0;

	// The part of the day.
	const float Hour = AAedirWeatherManager::GetHourOfDay(World);
	const float Day = AmbienceRamp(Hour, DayFromHour, DayFullHour) * (1.f - AmbienceRamp(Hour, NightFromHour, NightFullHour));
	const float Dawn = 1.f - FMath::Clamp(FMath::Abs(Hour - (DayFromHour + DayFullHour) * 0.5f) / FMath::Max(TwilightSpanHours, 0.1f), 0.f, 1.f);
	const float Dusk = 1.f - FMath::Clamp(FMath::Abs(Hour - (NightFromHour + NightFullHour) * 0.5f) / FMath::Max(TwilightSpanHours, 0.1f), 0.f, 1.f);
	const float Twilight = FMath::Max(Dawn, Dusk);

	// Rain and snow hush the animals.
	float Wet = 0.f;
	if (const UAedirWeatherSubsystem* Weather = World->GetSubsystem<UAedirWeatherSubsystem>())
	{
		if (const AAedirWeatherManager* Manager = Weather->GetManager())
		{
			const FAedirWeatherProfile Profile = Manager->GetBlendedProfile();
			Wet = FMath::Clamp(FMath::Max(Profile.RainIntensity, Profile.SnowIntensity), 0.f, 1.f);
		}
	}
	const float Hush = 1.f - WeatherHushShare * Wet;

	UpdateBed(EBed::Day, bOn ? DayVolume * Day * Hush : 0.f, DeltaTime);
	UpdateBed(EBed::Night, bOn ? NightVolume * (1.f - Day) * Hush : 0.f, DeltaTime);
	UpdateBed(EBed::Twilight, bOn ? TwilightVolume * Twilight * Hush : 0.f, DeltaTime);

	// Now and then a single sound nearby, fewer in the wet.
	NextSpotIn -= DeltaTime;
	if (bOn && NextSpotIn <= 0.f)
	{
		NextSpotIn = FMath::FRandRange(SpotMinSeconds, SpotMaxSeconds) / FMath::Max(Hush, 0.25f);
		FVector Listener;
		FRotator Facing;
		Viewer->GetPlayerViewPoint(Listener, Facing);
		const float Pick = FMath::FRand();
		const float AnyShare = 0.25f;
		if (Pick < AnyShare)
		{
			PlaySpot(ESpot::Any, Listener, 0.8f);
		}
		else if (FMath::FRand() < Twilight * 0.6f)
		{
			PlaySpot(ESpot::Twilight, Listener, Hush);
		}
		else
		{
			PlaySpot(FMath::FRand() < Day ? ESpot::Day : ESpot::Night, Listener, Hush);
		}
	}

	static double NextLog = 0.0;
	if (CVarAedirAmbienceLog.GetValueOnGameThread() != 0 && World->GetRealTimeSeconds() >= NextLog)
	{
		NextLog = World->GetRealTimeSeconds() + 2.0;
		UE_LOG(LogAedir, Log, TEXT("[Ambience] hour %.1f: day %.2f night %.2f twilight %.2f, wet %.2f, beds %.2f %.2f %.2f"), Hour, Day, 1.f - Day, Twilight, Wet,
			BedLevel[0], BedLevel[1], BedLevel[2]);
	}
}
