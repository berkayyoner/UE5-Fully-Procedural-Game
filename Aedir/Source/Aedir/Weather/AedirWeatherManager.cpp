#include "Weather/AedirWeatherManager.h"

#include "Aedir.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Creature/AedirCreature.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/OverlapResult.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Physics/AedirBalanceComponent.h"
#include "Sound/SoundBase.h"
#include "Surface/AedirSurfaceSubsystem.h"
#include "TimerManager.h"
#include "Weather/AedirWeatherSettings.h"

namespace
{
	constexpr float WeatherManagerSceneSearchInterval = 5.f;
	constexpr float WeatherManagerSkyLightApplyInterval = 0.25f;
	constexpr float WeatherManagerSkyLightChangeFraction = 0.03f;
	constexpr float WeatherManagerLightningFlashDuration = 0.35f;
	constexpr float WeatherManagerLightningFlashLux = 30.f;
	constexpr float WeatherManagerLightningStrikeRange = 8000.f;
	constexpr float WeatherManagerLightningSkyOffset = 6000.f;
	constexpr float WeatherManagerLightningBalanceImpact = 0.6f;
	constexpr float WeatherManagerSoundSpeedCmPerSecond = 34300.f;
	constexpr float WeatherManagerFullSunLux = 14.f;
	// The level exposure made the profile values glare, so the applied light is toned down.
	constexpr float WeatherManagerSunBrightnessScale = 0.55f;
	constexpr float WeatherManagerSkyBrightnessScale = 0.6f;
	constexpr float WeatherManagerWetRiseBase = 0.02f;
	constexpr float WeatherManagerWetRiseRain = 0.05f;
	constexpr float WeatherManagerWetDryRate = 0.004f;
	constexpr float WeatherManagerSnowRiseBase = 0.01f;
	constexpr float WeatherManagerSnowRiseSnow = 0.03f;
	constexpr float WeatherManagerSnowMeltRate = 0.003f;
	constexpr float WeatherManagerMinThunderDelay = 0.01f;
	constexpr float WeatherManagerMinAutoInterval = 1.f;

	float WeatherManagerSmoothStep(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	float WeatherManagerMoveTowards(float Current, float Target, float MaxDelta)
	{
		if (Current < Target)
		{
			return FMath::Min(Current + MaxDelta, Target);
		}

		return FMath::Max(Current - MaxDelta, Target);
	}

	float WeatherManagerSunFactor(float SunIntensityLux)
	{
		return FMath::Clamp(SunIntensityLux / WeatherManagerFullSunLux, 0.f, 1.f);
	}

	float WeatherManagerFlashCurve(float ElapsedSeconds)
	{
		if (ElapsedSeconds < 0.f || ElapsedSeconds >= WeatherManagerLightningFlashDuration)
		{
			return 0.f;
		}

		const float FirstPulse = FMath::Exp(-ElapsedSeconds * 22.f);
		const float SecondPulseOffset = ElapsedSeconds - 0.14f;
		const float SecondPulse = 0.6f * FMath::Exp(-SecondPulseOffset * SecondPulseOffset * 900.f);
		return FMath::Clamp(FirstPulse + SecondPulse, 0.f, 1.f);
	}

	FAedirWeatherProfile WeatherManagerBlendProfiles(const FAedirWeatherProfile& From, const FAedirWeatherProfile& To, float Alpha)
	{
		FAedirWeatherProfile Result;
		Result.CloudCoverage = FMath::Lerp(From.CloudCoverage, To.CloudCoverage, Alpha);
		Result.RainIntensity = FMath::Lerp(From.RainIntensity, To.RainIntensity, Alpha);
		Result.SnowIntensity = FMath::Lerp(From.SnowIntensity, To.SnowIntensity, Alpha);
		Result.FogDensity = FMath::Lerp(From.FogDensity, To.FogDensity, Alpha);
		Result.SunIntensityLux = FMath::Lerp(From.SunIntensityLux, To.SunIntensityLux, Alpha);
		Result.SkyLightIntensity = FMath::Lerp(From.SkyLightIntensity, To.SkyLightIntensity, Alpha);
		Result.WindSpeed = FMath::Lerp(From.WindSpeed, To.WindSpeed, Alpha);
		Result.LightningPerMinute = FMath::Lerp(From.LightningPerMinute, To.LightningPerMinute, Alpha);
		Result.WetnessTarget = FMath::Lerp(From.WetnessTarget, To.WetnessTarget, Alpha);
		Result.SnowTarget = FMath::Lerp(From.SnowTarget, To.SnowTarget, Alpha);
		Result.SunColor = From.SunColor * (1.f - Alpha) + To.SunColor * Alpha;
		return Result;
	}

	EAedirWeather WeatherManagerPickNextWeather(const UAedirWeatherSettings& Settings, EAedirWeather Current)
	{
		const EAedirWeather Candidates[] =
		{
			EAedirWeather::Sunny,
			EAedirWeather::ExtraSunny,
			EAedirWeather::Cloudy,
			EAedirWeather::Rainy,
			EAedirWeather::Stormy,
			EAedirWeather::Thunderstorm,
			EAedirWeather::Snowy
		};
		const int32 CandidateCount = static_cast<int32>(UE_ARRAY_COUNT(Candidates));

		TArray<float> Weights;
		Weights.Reserve(CandidateCount);
		float TotalWeight = 0.f;
		for (const EAedirWeather Candidate : Candidates)
		{
			const float* Configured = Settings.AutoWeights.Find(Candidate);
			const float Weight = Candidate == Current ? 0.f : FMath::Max(Configured ? *Configured : 1.f, 0.f);
			Weights.Add(Weight);
			TotalWeight += Weight;
		}

		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			TotalWeight = 0.f;
			for (int32 Index = 0; Index < CandidateCount; ++Index)
			{
				Weights[Index] = Candidates[Index] == Current ? 0.f : 1.f;
				TotalWeight += Weights[Index];
			}
		}

		const float Roll = FMath::FRand() * TotalWeight;
		float Accumulated = 0.f;
		for (int32 Index = 0; Index < CandidateCount; ++Index)
		{
			Accumulated += Weights[Index];
			if (Weights[Index] > 0.f && Roll < Accumulated)
			{
				return Candidates[Index];
			}
		}

		return Current;
	}
}

AAedirWeatherManager::AAedirWeatherManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);
}

void AAedirWeatherManager::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	RandomSeed = FMath::FRand();
	SetWeather(GetDefault<UAedirWeatherSettings>()->StartWeather, 0.01f);
}

void AAedirWeatherManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const FAedirWeatherProfile Profile = GetBlendedProfile();

	if (HasAuthority())
	{
		UpdateServerSchedules(GetServerTime(), Profile);
	}

	UpdateSurfaceCoupling(Profile, DeltaSeconds);

	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplySceneLighting(Profile);
	}
}

void AAedirWeatherManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirWeatherManager, CurrentWeather);
	DOREPLIFETIME(AAedirWeatherManager, PreviousWeather);
	DOREPLIFETIME(AAedirWeatherManager, TransitionStartServerTime);
	DOREPLIFETIME(AAedirWeatherManager, TransitionSeconds);
	DOREPLIFETIME(AAedirWeatherManager, RandomSeed);
}

void AAedirWeatherManager::SetWeather(EAedirWeather NewWeather, float NewTransitionSeconds)
{
	if (!HasAuthority())
	{
		UE_LOG(LogAedir, Warning, TEXT("%s ignored SetWeather on a non-authoritative machine"), *GetNameSafe(this));
		return;
	}

	if (NewWeather == CurrentWeather)
	{
		return;
	}

	PreviousWeather = CurrentWeather;
	CurrentWeather = NewWeather;
	TransitionSeconds = FMath::Max(NewTransitionSeconds, 0.f);
	TransitionStartServerTime = GetServerTime();
	NextLightningServerTime = -1.0;
}

EAedirWeather AAedirWeatherManager::GetWeather() const
{
	return CurrentWeather;
}

FAedirWeatherProfile AAedirWeatherManager::GetBlendedProfile() const
{
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	const FAedirWeatherProfile Current = Settings->GetProfile(CurrentWeather);

	if (PreviousWeather == CurrentWeather || TransitionSeconds <= KINDA_SMALL_NUMBER)
	{
		return Current;
	}

	const double Elapsed = GetServerTime() - TransitionStartServerTime;
	const float Linear = static_cast<float>(FMath::Clamp(Elapsed / static_cast<double>(TransitionSeconds), 0.0, 1.0));
	if (Linear >= 1.f)
	{
		return Current;
	}

	const FAedirWeatherProfile Previous = Settings->GetProfile(PreviousWeather);
	return WeatherManagerBlendProfiles(Previous, Current, WeatherManagerSmoothStep(Linear));
}

float AAedirWeatherManager::GetRainIntensity() const
{
	return GetBlendedProfile().RainIntensity;
}

float AAedirWeatherManager::GetSnowIntensity() const
{
	return GetBlendedProfile().SnowIntensity;
}

FVector AAedirWeatherManager::GetWindVector() const
{
	const float Yaw = FMath::DegreesToRadians(RandomSeed * 360.f);
	const FVector Direction(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
	return Direction * GetBlendedProfile().WindSpeed;
}

void AAedirWeatherManager::MulticastLightning_Implementation(FVector StrikeLocation, float Seed)
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	LightningFlashWorldTime = World->GetTimeSeconds();

	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();

	if (UNiagaraSystem* BoltSystem = Settings->LightningBoltSystem.LoadSynchronous())
	{
		FRandomStream BoltStream(static_cast<int32>(Seed * 1000000.f));
		const FRotator BoltRotation(0.f, BoltStream.FRandRange(0.f, 360.f), 0.f);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BoltSystem, StrikeLocation, BoltRotation);
	}

	if (!Settings->ThunderSound.IsNull())
	{
		FVector ListenerLocation = StrikeLocation;
		if (const APlayerController* LocalController = World->GetFirstPlayerController())
		{
			if (LocalController->PlayerCameraManager)
			{
				ListenerLocation = LocalController->PlayerCameraManager->GetCameraLocation();
			}
		}

		const float DelaySeconds = FVector::Dist(ListenerLocation, StrikeLocation) / WeatherManagerSoundSpeedCmPerSecond;
		FTimerHandle ThunderTimer;
		World->GetTimerManager().SetTimer(
			ThunderTimer,
			FTimerDelegate::CreateUObject(this, &AAedirWeatherManager::PlayThunderAt, StrikeLocation),
			FMath::Max(DelaySeconds, WeatherManagerMinThunderDelay),
			false);
	}
}

double AAedirWeatherManager::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void AAedirWeatherManager::UpdateServerSchedules(double ServerTime, const FAedirWeatherProfile& Profile)
{
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();

	if (Settings->bAutoChangeWeather)
	{
		if (NextAutoChangeServerTime < 0.0)
		{
			ScheduleNextAutoChange(ServerTime);
		}
		else if (ServerTime >= NextAutoChangeServerTime)
		{
			SetWeather(WeatherManagerPickNextWeather(*Settings, CurrentWeather), Settings->DefaultTransitionSeconds);
			ScheduleNextAutoChange(ServerTime);
		}
	}

	if (Profile.LightningPerMinute <= 0.f)
	{
		NextLightningServerTime = -1.0;
	}
	else if (NextLightningServerTime < 0.0)
	{
		ScheduleNextLightning(ServerTime, Profile.LightningPerMinute);
	}
	else if (ServerTime >= NextLightningServerTime)
	{
		StrikeLightning();
		ScheduleNextLightning(ServerTime, Profile.LightningPerMinute);
	}
}

void AAedirWeatherManager::ScheduleNextAutoChange(double ServerTime)
{
	const FVector2D Interval = GetDefault<UAedirWeatherSettings>()->WeatherChangeIntervalMinMax;
	const float Low = FMath::Max(FMath::Min(Interval.X, Interval.Y), WeatherManagerMinAutoInterval);
	const float High = FMath::Max(FMath::Max(Interval.X, Interval.Y), Low);
	NextAutoChangeServerTime = ServerTime + static_cast<double>(FMath::FRandRange(Low, High));
}

void AAedirWeatherManager::ScheduleNextLightning(double ServerTime, float LightningPerMinute)
{
	const float RatePerSecond = LightningPerMinute / 60.f;
	const float Uniform = FMath::Max(FMath::FRand(), KINDA_SMALL_NUMBER);
	NextLightningServerTime = ServerTime - static_cast<double>(FMath::Loge(Uniform)) / static_cast<double>(RatePerSecond);
}

void AAedirWeatherManager::StrikeLightning()
{
	FVector StrikeLocation = FVector::ZeroVector;
	if (!FindLightningStrikeLocation(StrikeLocation))
	{
		return;
	}

	ApplyLightningDamage(StrikeLocation);
	MulticastLightning(StrikeLocation, FMath::FRand());
}

bool AAedirWeatherManager::FindLightningStrikeLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	TArray<APawn*> TargetPawns;
	for (auto Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		if (APlayerController* PlayerController = Iterator->Get())
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				TargetPawns.Add(Pawn);
			}
		}
	}

	if (TargetPawns.IsEmpty())
	{
		return false;
	}

	const APawn* Target = TargetPawns[FMath::RandRange(0, TargetPawns.Num() - 1)];
	const float Angle = FMath::FRandRange(0.f, TWO_PI);
	const float Radius = WeatherManagerLightningStrikeRange * FMath::Sqrt(FMath::FRand());
	const FVector Center = Target->GetActorLocation();
	const FVector Start(
		Center.X + FMath::Cos(Angle) * Radius,
		Center.Y + FMath::Sin(Angle) * Radius,
		Center.Z + WeatherManagerLightningSkyOffset);
	const FVector End = Start - FVector::UpVector * (WeatherManagerLightningSkyOffset * 2.f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWeatherLightningTrace), false);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}

	OutLocation = Hit.ImpactPoint;
	return true;
}

void AAedirWeatherManager::ApplyLightningDamage(const FVector& StrikeLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWeatherLightningOverlap), false);
	World->OverlapMultiByChannel(
		Overlaps,
		StrikeLocation,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Settings->LightningStrikeRadius),
		Params);

	TSet<AAedirCreature*> Victims;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AAedirCreature* Creature = Cast<AAedirCreature>(Overlap.GetActor()))
		{
			Victims.Add(Creature);
		}
	}

	for (AAedirCreature* Creature : Victims)
	{
		Creature->ReceiveCreatureDamage(Settings->LightningDamage);

		if (Creature->Balance)
		{
			const FVector Direction = (Creature->GetActorLocation() - StrikeLocation).GetSafeNormal();
			Creature->Balance->ApplyBalanceImpact(Direction, WeatherManagerLightningBalanceImpact);
		}
	}
}

void AAedirWeatherManager::PlayThunderAt(FVector StrikeLocation)
{
	if (USoundBase* Thunder = GetDefault<UAedirWeatherSettings>()->ThunderSound.LoadSynchronous())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Thunder, StrikeLocation);
	}
}

void AAedirWeatherManager::RefreshSceneActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!SunLight.IsValid())
	{
		for (TActorIterator<ADirectionalLight> Iterator(World); Iterator; ++Iterator)
		{
			SunLight = *Iterator;
			break;
		}
	}

	if (!HeightFog.IsValid())
	{
		for (TActorIterator<AExponentialHeightFog> Iterator(World); Iterator; ++Iterator)
		{
			HeightFog = *Iterator;
			break;
		}
	}

	if (!SkyLight.IsValid())
	{
		for (TActorIterator<ASkyLight> Iterator(World); Iterator; ++Iterator)
		{
			SkyLight = *Iterator;
			break;
		}
	}
}

void AAedirWeatherManager::ApplySceneLighting(const FAedirWeatherProfile& Profile)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double WorldTime = World->GetTimeSeconds();
	if ((!SunLight.IsValid() || !HeightFog.IsValid() || !SkyLight.IsValid()) && WorldTime >= NextSceneSearchTime)
	{
		RefreshSceneActors();
		NextSceneSearchTime = WorldTime + WeatherManagerSceneSearchInterval;
	}

	const float FlashBoost = WeatherManagerFlashCurve(static_cast<float>(WorldTime - LightningFlashWorldTime)) * WeatherManagerLightningFlashLux;

	if (ADirectionalLight* Sun = SunLight.Get())
	{
		if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunComponent->SetIntensity(Profile.SunIntensityLux * WeatherManagerSunBrightnessScale + FlashBoost);

			if (!AppliedSunColor.Equals(Profile.SunColor, 0.001f))
			{
				SunComponent->SetLightColor(Profile.SunColor);
				AppliedSunColor = Profile.SunColor;
			}
		}
	}

	if (AExponentialHeightFog* Fog = HeightFog.Get())
	{
		if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
		{
			FogComponent->SetFogDensity(Profile.FogDensity);
		}
	}

	if (ASkyLight* Sky = SkyLight.Get())
	{
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			const float Reference = FMath::Max(AppliedSkyLightIntensity, KINDA_SMALL_NUMBER);
			const bool bChangedNotably = AppliedSkyLightIntensity < 0.f
				|| FMath::Abs(Profile.SkyLightIntensity - AppliedSkyLightIntensity) > Reference * WeatherManagerSkyLightChangeFraction;

			if (bChangedNotably && WorldTime >= NextSkyLightApplyTime)
			{
				SkyComponent->SetIntensity(Profile.SkyLightIntensity * WeatherManagerSkyBrightnessScale);
				SkyComponent->MarkRenderStateDirty();
				AppliedSkyLightIntensity = Profile.SkyLightIntensity;
				NextSkyLightApplyTime = WorldTime + WeatherManagerSkyLightApplyInterval;
			}
		}
	}
}

void AAedirWeatherManager::UpdateSurfaceCoupling(const FAedirWeatherProfile& Profile, float DeltaSeconds)
{
	if (!SurfaceSubsystem.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			SurfaceSubsystem = World->GetSubsystem<UAedirSurfaceSubsystem>();
		}
	}

	const float SunFactor = WeatherManagerSunFactor(Profile.SunIntensityLux);

	const float WetRate = Profile.WetnessTarget > SmoothedWetness
		? WeatherManagerWetRiseBase + WeatherManagerWetRiseRain * Profile.RainIntensity
		: WeatherManagerWetDryRate * (1.f + SunFactor);
	SmoothedWetness = WeatherManagerMoveTowards(SmoothedWetness, Profile.WetnessTarget, WetRate * DeltaSeconds);

	const float SnowRate = Profile.SnowTarget > SmoothedSnow
		? WeatherManagerSnowRiseBase + WeatherManagerSnowRiseSnow * Profile.SnowIntensity
		: WeatherManagerSnowMeltRate * (1.f + SunFactor);
	SmoothedSnow = WeatherManagerMoveTowards(SmoothedSnow, Profile.SnowTarget, SnowRate * DeltaSeconds);

	if (UAedirSurfaceSubsystem* Surface = SurfaceSubsystem.Get())
	{
		Surface->SetGlobalWeatherWetness(SmoothedWetness);
		Surface->SetGlobalWeatherSnow(SmoothedSnow);
	}
}

void AAedirWeatherManager::OnRep_Weather()
{
	AppliedSkyLightIntensity = -1.f;
	NextSkyLightApplyTime = 0.0;
	ApplySceneLighting(GetBlendedProfile());
}
