#include "Weather/AedirWeatherManager.h"

#include "Settings/AedirSettingsSubsystem.h"
#include "Aedir.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkyLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Creature/AedirCreature.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/OverlapResult.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Weather/AedirWeatherSubsystem.h"
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
	// The day: it starts at nine in the morning and goes round in 72 real minutes; the sun stands this high at noon, it is
	// full day once it is this high and night once it is this far below the horizon. The moon stands this high, gives this
	// many lux in this blue, and at night the sky gives this share of its daylight.
	TAutoConsoleVariable<float> CVarAedirHour(TEXT("aedir.Hour"), -1.f, TEXT("Holds the time of day at this hour (0 to 24); below 0 the day goes round."));
	TAutoConsoleVariable<FString> CVarAedirCloudTry(TEXT("aedir.CloudTry"), TEXT(""), TEXT("For measuring: cover and density for the clouds, over the weather's (\"0.2_0.05_0\"); empty for the weather's own."));
	TAutoConsoleVariable<float> CVarAedirWet(TEXT("aedir.Wet"), -1.f, TEXT("For measuring: holds how wet everything looks (0 to 1); below 0 the weather decides."));
	constexpr double WeatherStartHour = 9.0;
	constexpr double WeatherDayRealSeconds = 72.0 * 60.0;
	constexpr float WeatherSunHighestDegrees = 55.f;
	constexpr float WeatherDayAboveDegrees = 8.f;
	constexpr float WeatherDuskBelowDegrees = -4.f;
	constexpr float WeatherMoonHighDegrees = 40.f;
	constexpr float WeatherMoonLux = 0.25f;
	const FLinearColor WeatherMoonColor(0.55f, 0.68f, 1.f);
	constexpr float WeatherNightSkyShare = 0.06f;
	constexpr float WeatherManagerSceneSearchInterval = 5.f;
	constexpr float WeatherManagerSkyLightApplyInterval = 0.25f;
	// The engine's cloud material and how the weather's cover maps onto its controls, as measured: about -0.4 leaves a few
	// wisps, -0.2 fair-weather clouds, 0.1 a sky mostly covered, and with the layer brought down low 0.2 to 0.4 a grey
	// overcast (0.3 at its heaviest). Denser than a few hundredths, or with its storm switch on, the material thins the clouds away instead, and
	// a darker cloud colour only tints the sky blue; a storm's sky is darkened by its weaker light.
	const TCHAR* const WeatherManagerCloudMaterial = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst");
	constexpr float WeatherManagerCloudCoverAt[] = { -0.4f, -0.2f, 0.1f, 0.25f, 0.3f };
	constexpr float WeatherManagerCloudCoverKeys[] = { 0.f, 0.1f, 0.7f, 0.9f, 1.f };
	constexpr float WeatherManagerCloudThinDensity = 0.01f;
	constexpr float WeatherManagerCloudThickDensity = 0.015f;
	// Fair clouds sit high; past this much cover the layer comes down to rain clouds' height and thickness (km).
	constexpr float WeatherManagerCloudLowFrom = 0.6f;
	constexpr float WeatherManagerCloudLowTo = 0.9f;
	constexpr float WeatherManagerCloudHighBottomKm = 5.f;
	constexpr float WeatherManagerCloudHighDepthKm = 10.f;
	constexpr float WeatherManagerCloudLowBottomKm = 1.5f;
	constexpr float WeatherManagerCloudLowDepthKm = 3.f;
	constexpr float WeatherManagerCloudChange = 0.01f;
	const FLinearColor WeatherManagerCloudStormAlbedo(0.8f, 0.8f, 0.82f);
	// The sky's blue in the haze is taken out under an overcast, leaving it grey.
	const FLinearColor WeatherManagerOvercastFogTint(1.f, 0.62f, 0.34f);
	constexpr float WeatherManagerSkyLightChangeFraction = 0.03f;
	constexpr float WeatherManagerLightningFlashDuration = 0.35f;
	constexpr float WeatherManagerLightningSkyOffset = 30000.f;
	// The drawn bolt: the glowing shape it is drawn with, how wide its main channel is, how bright, how long it lasts and how
	// far its light reaches.
	const TCHAR* const WeatherManagerBoltPlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const WeatherManagerBoltMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirBolt.M_AedirBolt");
	constexpr float WeatherManagerBoltPlaneCm = 100.f;
	// The glow round the channel and the white-hot core inside it.
	constexpr float WeatherManagerBoltWidthCm = 320.f;
	constexpr float WeatherManagerBoltCoreWidthCm = 40.f;
	const FLinearColor WeatherManagerBoltColor(2.5f, 3.2f, 7.f);
	const FLinearColor WeatherManagerBoltCoreColor(60.f, 62.f, 75.f);
	// Seen from far off a bolt is still a line a pixel or two wide: its width grows with the distance past this, and its glow
	// is drawn this many times the core's width near by, this many far off.
	constexpr float WeatherManagerBoltWidthFromCm = 33000.f;
	constexpr float WeatherManagerBoltNearGlowShare = 8.f;
	constexpr float WeatherManagerBoltFarGlowShare = 3.f;
	// The channel comes out of the cloud base, though never lower than this over where it strikes (cm).
	constexpr float WeatherManagerBoltMinHeightCm = 60000.f;
	constexpr float WeatherManagerBoltLife = 0.45f;
	constexpr float WeatherManagerBoltLightCandelas = 6000.f;
	constexpr float WeatherManagerBoltLightRadius = 12000.f;
	constexpr float WeatherManagerLightningBalanceImpact = 0.6f;
	// Flashes inside the clouds: how long one lasts, where they happen around the viewer, and the cloud layer when none is
	// found.
	constexpr float WeatherManagerCloudFlashLife = 0.7f;
	constexpr float WeatherManagerCloudFlashNearCm = 150000.f;
	constexpr float WeatherManagerCloudFlashFarCm = 900000.f;
	constexpr float WeatherManagerCloudFlashDefaultBottomCm = 150000.f;
	constexpr float WeatherManagerCloudFlashDefaultThicknessCm = 300000.f;
	constexpr int32 WeatherManagerCloudFlashShadowSamples = 4;
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
	StepBolt(DeltaSeconds);

	const FAedirWeatherProfile Profile = GetBlendedProfile();
	if (GetNetMode() != NM_DedicatedServer)
	{
		StepCloudFlashes(DeltaSeconds, Profile.LightningPerMinute);
	}

	if (HasAuthority())
	{
		UpdateServerSchedules(GetServerTime(), Profile);
	}

	UpdateSurfaceCoupling(Profile, DeltaSeconds);

	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplySceneLighting(Profile);
		StepWet();
	}
}

void AAedirWeatherManager::StepWet()
{
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	const float Held = CVarAedirWet.GetValueOnGameThread();
	const float Wet = FMath::Clamp(Held >= 0.f ? Held : SmoothedWetness, 0.f, 1.f);
	if (!WetDarken)
	{
		if (Wet <= 0.01f)
		{
			return;
		}
		// A box round the eye pointing down, as deep as the land goes up and down near by.
		const FVector Size(3000.f, Settings->WetReachCm, Settings->WetReachCm);
		const auto Make = [this, &Size](const TCHAR* Path, TObjectPtr<UMaterialInstanceDynamic>& OutMaterial, int32 Order) -> UDecalComponent*
		{
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Base)
			{
				UE_LOG(LogAedir, Warning, TEXT("[Weather] No wet material at %s"), Path);
				return nullptr;
			}
			OutMaterial = UMaterialInstanceDynamic::Create(Base, this);
			UDecalComponent* Decal = NewObject<UDecalComponent>(this);
			Decal->SetAbsolute(true, true, true);
			Decal->SetupAttachment(GetRootComponent());
			Decal->SetDecalMaterial(OutMaterial);
			Decal->SortOrder = Order;
			Decal->DecalSize = Size;
			Decal->RegisterComponent();
			return Decal;
		};
		WetDarken = Make(TEXT("/Game/Aedir/Materials/M_AedirWetDarken.M_AedirWetDarken"), WetDarkenMaterial, -20);
		WetGloss = Make(TEXT("/Game/Aedir/Materials/M_AedirWetGloss.M_AedirWetGloss"), WetGlossMaterial, -19);
		if (!WetDarken || !WetGloss)
		{
			return;
		}
	}
	const FVector Eye = ViewLocation();
	for (UDecalComponent* Decal : { WetDarken.Get(), WetGloss.Get() })
	{
		if (Decal)
		{
			Decal->SetWorldLocationAndRotation(Eye, FRotator(-90.f, 0.f, 0.f));
			Decal->SetVisibility(Wet > 0.01f);
		}
	}
	if (FMath::Abs(Wet - AppliedWet) > 0.005f)
	{
		AppliedWet = Wet;
		WetDarkenMaterial->SetScalarParameterValue(TEXT("Strength"), Wet * Settings->WetDarkenShare);
		WetDarkenMaterial->SetScalarParameterValue(TEXT("ReachCm"), Settings->WetReachCm);
		WetGlossMaterial->SetScalarParameterValue(TEXT("Strength"), Wet * Settings->WetGlossShare);
		WetGlossMaterial->SetScalarParameterValue(TEXT("Roughness"), Settings->WetRoughness);
		WetGlossMaterial->SetScalarParameterValue(TEXT("ReachCm"), Settings->WetReachCm);
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
	DOREPLIFETIME(AAedirWeatherManager, HourShift);
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

void AAedirWeatherManager::BuildBolt(const FVector& StrikeLocation, float Seed)
{
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, WeatherManagerBoltPlanePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, WeatherManagerBoltMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Plane || !Base)
	{
		return;
	}
	if (!BoltMesh)
	{
		BoltMaterial = UMaterialInstanceDynamic::Create(Base, this);
		BoltMaterial->SetVectorParameterValue(TEXT("Color"), WeatherManagerBoltColor);
		BoltMesh = NewObject<UInstancedStaticMeshComponent>(this);
		BoltMesh->SetStaticMesh(Plane);
		BoltMesh->SetMaterial(0, BoltMaterial);
		BoltMesh->SetMobility(EComponentMobility::Movable);
		BoltMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoltMesh->SetCastShadow(false);
		BoltMesh->SetAbsolute(true, true, true);
		BoltMesh->SetupAttachment(GetRootComponent());
		BoltMesh->RegisterComponent();
		BoltCoreMaterial = UMaterialInstanceDynamic::Create(Base, this);
		BoltCoreMaterial->SetVectorParameterValue(TEXT("Color"), WeatherManagerBoltCoreColor);
		BoltCore = NewObject<UInstancedStaticMeshComponent>(this);
		BoltCore->SetStaticMesh(Plane);
		BoltCore->SetMaterial(0, BoltCoreMaterial);
		BoltCore->SetMobility(EComponentMobility::Movable);
		BoltCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoltCore->SetCastShadow(false);
		BoltCore->SetAbsolute(true, true, true);
		BoltCore->SetupAttachment(GetRootComponent());
		BoltCore->RegisterComponent();
		BoltLight = NewObject<UPointLightComponent>(this);
		BoltLight->SetAbsolute(true, true, true);
		BoltLight->SetupAttachment(GetRootComponent());
		BoltLight->SetIntensityUnits(ELightUnits::Candelas);
		BoltLight->SetAttenuationRadius(WeatherManagerBoltLightRadius);
		BoltLight->SetLightColor(FLinearColor(0.75f, 0.82f, 1.f));
		BoltLight->SetCastShadows(false);
		BoltLight->SetVolumetricScatteringIntensity(0.2f);
		BoltLight->RegisterComponent();
	}

	// The main channel comes down from the clouds well to one side, broken again and again into shorter crooked lengths.
	FRandomStream Stream(static_cast<int32>(Seed * 1000000.f) + 17);
	BoltPoints.Reset();
	BoltWidths.Reset();
	const auto Crooked = [&Stream](const FVector& From, const FVector& To, int32 Levels, TArray<FVector>& OutPath)
	{
		OutPath = { From, To };
		for (int32 Level = 0; Level < Levels; ++Level)
		{
			TArray<FVector> Finer;
			for (int32 Index = 0; Index + 1 < OutPath.Num(); ++Index)
			{
				const FVector A = OutPath[Index];
				const FVector B = OutPath[Index + 1];
				const float Length = static_cast<float>(FVector::Dist(A, B));
				Finer.Add(A);
				// Bent off to the side, not along the line, so it zigzags without folding back on itself.
				const FVector Line = (B - A).GetSafeNormal();
				Finer.Add((A + B) * 0.5 + FVector::VectorPlaneProject(Stream.VRand(), Line) * (Length * (Level < 3 ? 0.18f : 0.28f)));
			}
			Finer.Add(OutPath.Last());
			OutPath = MoveTemp(Finer);
		}
	};
	float CloudBaseCm = WeatherManagerCloudFlashDefaultBottomCm;
	if (const UVolumetricCloudComponent* Clouds = CloudComponent.Get())
	{
		CloudBaseCm = Clouds->LayerBottomAltitude * 100000.f;
	}
	const float Height = FMath::Max(CloudBaseCm - static_cast<float>(StrikeLocation.Z), WeatherManagerBoltMinHeightCm) * Stream.FRandRange(1.f, 1.15f);
	const FVector Top = StrikeLocation + FVector(Stream.FRandRange(-0.25f, 0.25f) * Height, Stream.FRandRange(-0.25f, 0.25f) * Height, Height);
	TArray<FVector> Main;
	Crooked(Top, StrikeLocation, 8, Main);
	for (int32 Index = 0; Index + 1 < Main.Num(); ++Index)
	{
		BoltPoints.Add(Main[Index]);
		BoltPoints.Add(Main[Index + 1]);
		BoltWidths.Add(WeatherManagerBoltWidthCm);
	}
	// Forks break off the upper part and die out on the way down.
	const int32 Forks = Stream.RandRange(2, 4);
	for (int32 Fork = 0; Fork < Forks; ++Fork)
	{
		const FVector From = Main[Stream.RandRange(Main.Num() / 8, Main.Num() * 2 / 3)];
		const float Drop = static_cast<float>(From.Z - StrikeLocation.Z) * Stream.FRandRange(0.2f, 0.45f);
		const FVector To = From + FVector(Stream.FRandRange(-1.f, 1.f) * Drop, Stream.FRandRange(-1.f, 1.f) * Drop, -Drop);
		TArray<FVector> Branch;
		Crooked(From, To, 5, Branch);
		for (int32 Index = 0; Index + 1 < Branch.Num(); ++Index)
		{
			BoltPoints.Add(Branch[Index]);
			BoltPoints.Add(Branch[Index + 1]);
			BoltWidths.Add(WeatherManagerBoltWidthCm * 0.4f * (1.f - static_cast<float>(Index) / Branch.Num()));
		}
	}

	const int32 Segments = BoltWidths.Num();
	if (BoltMesh->GetInstanceCount() < Segments)
	{
		TArray<FTransform> More;
		More.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), Segments - BoltMesh->GetInstanceCount());
		BoltMesh->AddInstances(More, false, true);
	}
	if (BoltCore && BoltCore->GetInstanceCount() < Segments)
	{
		TArray<FTransform> More;
		More.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), Segments - BoltCore->GetInstanceCount());
		BoltCore->AddInstances(More, false, true);
	}
	BoltLight->SetWorldLocation(StrikeLocation + FVector(0.f, 0.f, 800.f));
	BoltAge = 0.f;
}

void AAedirWeatherManager::StepBolt(float DeltaSeconds)
{
	if (!BoltMesh || BoltAge > WeatherManagerBoltLife + 1.f)
	{
		return;
	}
	BoltAge += DeltaSeconds;
	// It strikes, fades, strikes again down the same channel, and is gone.
	const float Flicker = BoltAge < 0.06f ? 1.f : BoltAge < 0.14f ? 0.25f : BoltAge < 0.22f ? 0.9f : BoltAge < WeatherManagerBoltLife ? FMath::Max(0.f, 0.6f * (1.f - (BoltAge - 0.22f) / (WeatherManagerBoltLife - 0.22f))) : 0.f;
	const bool bShown = Flicker > 0.f;
	BoltMesh->SetVisibility(bShown);
	if (BoltCore)
	{
		BoltCore->SetVisibility(bShown);
	}
	BoltLight->SetIntensity(WeatherManagerBoltLightCandelas * Flicker);
	BoltLight->SetVisibility(bShown);
	if (!bShown)
	{
		BoltAge = WeatherManagerBoltLife + 2.f;
		return;
	}
	BoltMaterial->SetScalarParameterValue(TEXT("Strength"), Flicker);
	if (BoltCoreMaterial)
	{
		BoltCoreMaterial->SetScalarParameterValue(TEXT("Strength"), Flicker);
	}

	FVector Eye = BoltPoints.Num() > 0 ? BoltPoints[0] : FVector::ZeroVector;
	if (const APlayerController* LocalController = GetWorld()->GetFirstPlayerController())
	{
		if (LocalController->PlayerCameraManager)
		{
			Eye = LocalController->PlayerCameraManager->GetCameraLocation();
		}
	}
	TArray<FTransform> Transforms;
	TArray<FTransform> Cores;
	Transforms.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), BoltMesh->GetInstanceCount());
	Cores.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.f)), BoltCore ? BoltCore->GetInstanceCount() : 0);
	for (int32 Segment = 0; Segment < BoltWidths.Num(); ++Segment)
	{
		const FVector A = BoltPoints[Segment * 2];
		const FVector B = BoltPoints[Segment * 2 + 1];
		const FVector Middle = (A + B) * 0.5;
		const FVector Along = (B - A).GetSafeNormal();
		// Far off the line keeps to a pixel or two and its glow grows less round it.
		const float Far = FMath::Max(static_cast<float>(FVector::Dist(Eye, Middle)) / WeatherManagerBoltWidthFromCm, 1.f);
		const float CoreWidth = BoltWidths[Segment] * (WeatherManagerBoltCoreWidthCm / WeatherManagerBoltWidthCm) * Far;
		const float Width = CoreWidth * FMath::Lerp(WeatherManagerBoltNearGlowShare, WeatherManagerBoltFarGlowShare, FMath::Clamp((Far - 1.f) / 8.f, 0.f, 1.f));
		// Turned to face the eye; only a little longer than the segment, so the joints neither gap nor glow twice over.
		const FQuat Facing = FRotationMatrix::MakeFromXZ(Along, (Eye - Middle).GetSafeNormal()).ToQuat();
		const float Length = static_cast<float>(FVector::Dist(A, B));
		Transforms[Segment] = FTransform(Facing, Middle, FVector((Length + Width * 0.15f) / WeatherManagerBoltPlaneCm, Width / WeatherManagerBoltPlaneCm, 1.f));
		if (Cores.IsValidIndex(Segment))
		{
			Cores[Segment] = FTransform(Facing, Middle, FVector((Length + CoreWidth * 0.5f) / WeatherManagerBoltPlaneCm, CoreWidth / WeatherManagerBoltPlaneCm, 1.f));
		}
	}
	BoltMesh->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
	if (BoltCore)
	{
		BoltCore->BatchUpdateInstancesTransforms(0, Cores, true, true, true);
	}
}

void AAedirWeatherManager::FlashClouds(const FVector& Below)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!CloudFlashLight)
	{
		CloudFlashLight = NewObject<UPointLightComponent>(this);
		CloudFlashLight->SetAbsolute(true, true, true);
		CloudFlashLight->SetupAttachment(GetRootComponent());
		CloudFlashLight->SetIntensityUnits(ELightUnits::Candelas);
		CloudFlashLight->SetAttenuationRadius(GetDefault<UAedirWeatherSettings>()->CloudFlashRadiusCm);
		CloudFlashLight->SetLightColor(FLinearColor(0.78f, 0.84f, 1.f));
		CloudFlashLight->SetCastShadows(false);
		// Lit in the clouds only: in the haze below it would hang as a ball of light.
		CloudFlashLight->SetVolumetricScatteringIntensity(0.f);
		CloudFlashLight->RegisterComponent();
	}
	// The flash sits in the lower part of the cloud layer, above where it is seen from.
	float BottomCm = WeatherManagerCloudFlashDefaultBottomCm;
	float ThicknessCm = WeatherManagerCloudFlashDefaultThicknessCm;
	if (const UVolumetricCloudComponent* Clouds = CloudComponent.Get())
	{
		BottomCm = Clouds->LayerBottomAltitude * 100000.f;
		ThicknessCm = Clouds->LayerHeight * 100000.f;
	}
	CloudFlashSeed = FMath::FRand();
	CloudFlashLight->SetWorldLocation(FVector(Below.X, Below.Y, BottomCm + ThicknessCm * FMath::FRandRange(0.1f, 0.35f)));
	CloudFlashAge = 0.f;
	// It lights the land below a little, the less the further off it is.
	CloudFlashShare = FMath::Clamp(WeatherManagerCloudFlashNearCm / FMath::Max(static_cast<float>(FVector::Dist2D(Below, ViewLocation())), 1.f), 0.f, 1.f);
	UE_LOG(LogAedir, Log, TEXT("[Weather] Lightning flashes in the clouds at %s"), *CloudFlashLight->GetComponentLocation().ToCompactString());
}

void AAedirWeatherManager::StepCloudFlashes(float DeltaSeconds, float LightningPerMinute)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Clouds take local lights only while a storm can flash in them: the engine marks it as costly.
	const bool bStorm = LightningPerMinute > 0.f;
	if (bStorm != bCloudLightsOn)
	{
		bCloudLightsOn = bStorm;
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud.EnableLocalLightsSampling")))
		{
			Variable->Set(bStorm ? 1 : 0, ECVF_SetByCode);
		}
		if (IConsoleVariable* Samples = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricCloud.LocalLights.ShadowSampleCount")))
		{
			Samples->Set(WeatherManagerCloudFlashShadowSamples, ECVF_SetByCode);
		}
	}

	// Most lightning never reaches the ground: it flickers from cloud to cloud, here and there over the sky.
	const double Now = World->GetTimeSeconds();
	if (bStorm && (NextCloudFlashTime < 0.0 || Now >= NextCloudFlashTime))
	{
		if (NextCloudFlashTime >= 0.0)
		{
			FVector Eye = FVector::ZeroVector;
			if (const APlayerController* LocalController = World->GetFirstPlayerController(); LocalController && LocalController->PlayerCameraManager)
			{
				Eye = LocalController->PlayerCameraManager->GetCameraLocation();
			}
			const FVector Away = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector() * FMath::FRandRange(WeatherManagerCloudFlashNearCm, WeatherManagerCloudFlashFarCm);
			// Too far off to be heard.
			FlashClouds(Eye + Away);
		}
		const float PerMinute = LightningPerMinute * GetDefault<UAedirWeatherSettings>()->CloudFlashesPerStrike;
		// The first one comes soon after the storm breaks.
		NextCloudFlashTime = Now + (NextCloudFlashTime < 0.0 ? FMath::FRandRange(1.f, 4.f) : FMath::FRandRange(0.4f, 1.6f) * 60.f / FMath::Max(PerMinute, 0.1f));
	}
	else if (!bStorm)
	{
		NextCloudFlashTime = -1.0;
	}

	if (!CloudFlashLight || CloudFlashAge > WeatherManagerCloudFlashLife + 1.f)
	{
		CloudFlashGlow = 0.f;
		return;
	}
	CloudFlashAge += DeltaSeconds;
	// A sheet of light that flickers two or three times inside the cloud and dies.
	const float T = CloudFlashAge / WeatherManagerCloudFlashLife;
	const float Pulses = CloudFlashSeed < 0.5f ? 3.f : 2.f;
	const float Flicker = T >= 1.f ? 0.f : FMath::Max(0.f, FMath::Sin(T * UE_PI * Pulses * 2.f)) * (1.f - T) * (0.6f + 0.4f * CloudFlashSeed);
	CloudFlashGlow = Flicker;
	CloudFlashLight->SetIntensity(GetDefault<UAedirWeatherSettings>()->CloudFlashCandelas * Flicker);
	CloudFlashLight->SetVisibility(Flicker > 0.f);
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
	const float Off = static_cast<float>(FVector::Dist(ViewLocation(), StrikeLocation));
	LightningFlashShare = FMath::Min(FMath::Square(Settings->LightningFlashFullWithinCm / FMath::Max(Off, 1.f)), 1.f);
	UE_LOG(LogAedir, Log, TEXT("[Weather] Lightning strikes %.0f m off"), Off / 100.f);

	if (UNiagaraSystem* BoltSystem = Settings->LightningBoltSystem.LoadSynchronous())
	{
		FRandomStream BoltStream(static_cast<int32>(Seed * 1000000.f));
		const FRotator BoltRotation(0.f, BoltStream.FRandRange(0.f, 360.f), 0.f);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BoltSystem, StrikeLocation, BoltRotation);
	}
	else
	{
		BuildBolt(StrikeLocation, Seed);
	}
	// The channel lights up the cloud it came out of.
	FlashClouds(StrikeLocation);

	QueueThunder(StrikeLocation, 1.f);
}

void AAedirWeatherManager::QueueThunder(const FVector& From, float Loudness)
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Near by the crack comes with the flash; past the hearing distance there is nothing to hear over the rain.
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	const float Distance = static_cast<float>(FVector::Dist(ViewLocation(), From));
	if (Distance > Settings->ThunderHeardWithinCm)
	{
		return;
	}
	const float Far = FMath::Clamp((Distance - Settings->ThunderFullWithinCm) / FMath::Max(Settings->ThunderHeardWithinCm - Settings->ThunderFullWithinCm, 1.f), 0.f, 1.f);
	PlayThunder(Loudness * FMath::Lerp(1.f, Settings->ThunderFarShare, Far));
}

FVector AAedirWeatherManager::ViewLocation() const
{
	const UWorld* World = GetWorld();
	if (const APlayerController* LocalController = World ? World->GetFirstPlayerController() : nullptr; LocalController && LocalController->PlayerCameraManager)
	{
		return LocalController->PlayerCameraManager->GetCameraLocation();
	}
	return GetActorLocation();
}

void AAedirWeatherManager::PlayThunder(float Loudness)
{
	// Thunder fills the whole sky rather than coming from a point, so it is heard all round, one of several rolls of it.
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	USoundBase* Thunder = nullptr;
	const FString Base = Settings->ThunderSound.ToSoftObjectPath().GetLongPackageName();
	if (Base.Len() > 2)
	{
		const FString Path = FString::Printf(TEXT("%s%d"), *Base.LeftChop(1), FMath::RandRange(1, 5));
		Thunder = LoadObject<USoundBase>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Thunder)
	{
		Thunder = Settings->ThunderSound.LoadSynchronous();
	}
	if (Thunder)
	{
		UGameplayStatics::PlaySound2D(this, Thunder, Loudness * UAedirSettingsSubsystem::GetValues().WeatherVolume, FMath::FRandRange(0.9f, 1.05f));
		UE_LOG(LogAedir, Log, TEXT("[Weather] Thunder %s at %.2f"), *Thunder->GetName(), Loudness);
	}
	else
	{
		UE_LOG(LogAedir, Warning, TEXT("[Weather] No thunder sound found at %s"), *Settings->ThunderSound.ToString());
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
	// Spread out by the scale of the distance, leaning far: most come down a long way off, now and then one close by.
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	const float Near = FMath::Max(FMath::Min(Settings->LightningStrikeDistanceMinMaxCm.X, Settings->LightningStrikeDistanceMinMaxCm.Y), 100.f);
	const float Far = FMath::Max(Settings->LightningStrikeDistanceMinMaxCm.X, Settings->LightningStrikeDistanceMinMaxCm.Y);
	const float Radius = Near * FMath::Pow(Far / Near, FMath::Pow(FMath::FRand(), FMath::Clamp(Settings->LightningNearness, 0.05f, 1.f)));
	const FVector Center = Target->GetActorLocation();
	const FVector Start(
		Center.X + FMath::Cos(Angle) * Radius,
		Center.Y + FMath::Sin(Angle) * Radius,
		Center.Z + WeatherManagerLightningSkyOffset);
	const FVector End = Start - FVector::UpVector * (WeatherManagerLightningSkyOffset * 2.f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWeatherLightningTrace), false);
	// Past the land that is there it comes down at the height of the one it was chosen round.
	OutLocation = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params) ? FVector(Hit.ImpactPoint) : FVector(Start.X, Start.Y, Center.Z - 90.f);
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
	QueueThunder(StrikeLocation, 1.f);
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

	// A bolt lights the whole sky for a moment; a flash inside the clouds lights it less, through the clouds and the haze.
	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();
	const float FlashBoost = WeatherManagerFlashCurve(static_cast<float>(WorldTime - LightningFlashWorldTime)) * LightningFlashShare * Settings->LightningFlashLux
		+ CloudFlashGlow * CloudFlashShare * Settings->CloudFlashLandLux;

	if (ADirectionalLight* Sun = SunLight.Get())
	{
		if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			ApplyDayCycle(SunComponent, Profile.SunIntensityLux * WeatherManagerSunBrightnessScale);
			SunComponent->SetIntensity(SunComponent->Intensity + FlashBoost);
			
			const FLinearColor Color = bMoonUp ? WeatherMoonColor : Profile.SunColor;
			if (!AppliedSunColor.Equals(Color, 0.001f))
			{
				SunComponent->SetLightColor(Color);
				AppliedSunColor = Color;
			}
		}
	}

	if (AExponentialHeightFog* Fog = HeightFog.Get())
	{
		if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
		{
			FogComponent->SetFogDensity(Profile.FogDensity);
			// Under a low grey cover the haze is grey too, not the blue of a clear sky.
			if (!bFogColorKnown)
			{
				FogSkyColor = FogComponent->SkyAtmosphereAmbientContributionColorScale;
				bFogColorKnown = true;
			}
			const float Grey = FMath::SmoothStep(WeatherManagerCloudLowFrom, WeatherManagerCloudLowTo, Profile.CloudCoverage);
			const FLinearColor FogColor = FMath::Lerp(FogSkyColor, FogSkyColor * WeatherManagerOvercastFogTint, Grey);
			if (!FogColor.Equals(FogComponent->SkyAtmosphereAmbientContributionColorScale, 0.002f))
			{
				FogComponent->SetSkyAtmosphereAmbientContributionColorScale(FogColor);
			}
		}
	}

	ApplyClouds(Profile);

	if (ASkyLight* Sky = SkyLight.Get())
	{
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			const float Reference = FMath::Max(AppliedSkyLightIntensity, KINDA_SMALL_NUMBER);
			const float Wanted = Profile.SkyLightIntensity * FMath::Lerp(WeatherNightSkyShare, 1.f, GetDaylight(World));
			const bool bChangedNotably = AppliedSkyLightIntensity < 0.f
				|| FMath::Abs(Wanted - AppliedSkyLightIntensity) > Reference * WeatherManagerSkyLightChangeFraction;

			if (bChangedNotably && WorldTime >= NextSkyLightApplyTime)
			{
				SkyComponent->SetIntensity(Wanted * WeatherManagerSkyBrightnessScale);
				SkyComponent->MarkRenderStateDirty();
				AppliedSkyLightIntensity = Wanted;
				NextSkyLightApplyTime = WorldTime + WeatherManagerSkyLightApplyInterval;
			}
		}
	}
}

float AAedirWeatherManager::GetDayRealSeconds()
{
	return static_cast<float>(WeatherDayRealSeconds);
}

float AAedirWeatherManager::GetHourOfDay(const UWorld* World)
{
	const float Fixed = CVarAedirHour.GetValueOnGameThread();
	if (Fixed >= 0.f)
	{
		return FMath::Fmod(Fixed, 24.f);
	}
	const double Seconds = World ? World->GetTimeSeconds() : 0.0;
	const UAedirWeatherSubsystem* Weather = World ? World->GetSubsystem<UAedirWeatherSubsystem>() : nullptr;
	const AAedirWeatherManager* Manager = Weather ? Weather->GetManager() : nullptr;
	const double Shift = Manager ? Manager->HourShift : 0.0;
	const double Hour = FMath::Fmod(WeatherStartHour + Shift + Seconds * 24.0 / WeatherDayRealSeconds, 24.0);
	return static_cast<float>(Hour < 0.0 ? Hour + 24.0 : Hour);
}

void AAedirWeatherManager::SetHour(float Hour)
{
	if (!HasAuthority())
	{
		return;
	}
	const float Wanted = FMath::Fmod(FMath::Fmod(Hour, 24.f) + 24.f, 24.f);
	// A clock held by aedir.Hour is moved to the new hour too.
	if (IConsoleVariable* Held = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.Hour")); Held && Held->GetFloat() >= 0.f)
	{
		Held->Set(Wanted, ECVF_SetByConsole);
	}
	HourShift = static_cast<float>(FMath::Fmod(HourShift + Wanted - GetHourOfDay(GetWorld()), 24.0));
}

float AAedirWeatherManager::GetDaylight(const UWorld* World)
{
	// Full day once the sun stands a little above the horizon; nothing once it is a little below it.
	const float Elevation = WeatherSunHighestDegrees * FMath::Sin(UE_PI * (GetHourOfDay(World) - 6.f) / 12.f);
	return FMath::SmoothStep(WeatherDuskBelowDegrees, WeatherDayAboveDegrees, Elevation);
}

void AAedirWeatherManager::ApplyDayCycle(UDirectionalLightComponent* SunComponent, float WeatherLux)
{
	AActor* Sun = SunComponent->GetOwner();
	if (!Sun)
	{
		return;
	}
	if (!bNoonYawKnown)
	{
		NoonYaw = static_cast<float>(Sun->GetActorRotation().Yaw);
		bNoonYawKnown = true;
	}
	const float Hour = GetHourOfDay(GetWorld());
	const float Elevation = WeatherSunHighestDegrees * FMath::Sin(UE_PI * (Hour - 6.f) / 12.f);
	const float Daylight = GetDaylight(GetWorld());
	const bool bMoon = Elevation < WeatherDuskBelowDegrees;
	if (bMoon)
	{
		// At night the light is the moon, high over the other way, faint and blue; the sky has no sun in it and goes dark.
		const float FromMidnight = Hour < 12.f ? Hour : Hour - 24.f;
		Sun->SetActorRotation(FRotator(-WeatherMoonHighDegrees, NoonYaw + 180.f + FromMidnight * 15.f, 0.f));
		SunComponent->SetIntensity(WeatherMoonLux);
	}
	else
	{
		Sun->SetActorRotation(FRotator(-FMath::Max(Elevation, 0.5f), NoonYaw + (Hour - 12.f) * 15.f, 0.f));
		SunComponent->SetIntensity(WeatherLux * Daylight);
	}
	if (bMoon != bMoonUp)
	{
		bMoonUp = bMoon;
		SunComponent->SetAtmosphereSunLight(!bMoon);
		UE_LOG(LogAedir, Log, TEXT("[Day] %s at %.1f o'clock"), bMoon ? TEXT("Night falls") : TEXT("Day breaks"), Hour);
	}
}

void AAedirWeatherManager::ApplyClouds(const FAedirWeatherProfile& Profile)
{
	UWorld* World = GetWorld();
	if (!CloudComponent.IsValid() && World)
	{
		for (TActorIterator<AVolumetricCloud> It(World); It; ++It)
		{
			CloudComponent = It->FindComponentByClass<UVolumetricCloudComponent>();
			break;
		}
	}
	UVolumetricCloudComponent* Clouds = CloudComponent.Get();
	if (!Clouds)
	{
		return;
	}
	if (!CloudMaterial)
	{
		UMaterialInterface* Base = Clouds->Material ? Clouds->Material.Get() : LoadObject<UMaterialInterface>(nullptr, WeatherManagerCloudMaterial);
		if (!Base)
		{
			return;
		}
		CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
		Clouds->SetMaterial(CloudMaterial);
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		CloudMaterial->GetAllScalarParameterInfo(Infos, Ids);
		FString Names;
		for (const FMaterialParameterInfo& Info : Infos)
		{
			float Value = 0.f;
			CloudMaterial->GetScalarParameterValue(Info, Value);
			Names += FString::Printf(TEXT("%s=%.2f "), *Info.Name.ToString(), Value);
		}
		UE_LOG(LogAedir, Log, TEXT("[Weather] Clouds %s, layer %.1f km up, %.1f km deep: %s"), *Base->GetPathName(), Clouds->LayerBottomAltitude, Clouds->LayerHeight, *Names);
	}

	// Clear skies thin the cover out to a few wisps, an overcast one closes it, and a storm darkens and thickens it into
	// rain clouds. The sky changes as slowly as the weather does.
	const float Coverage = FMath::Clamp(Profile.CloudCoverage, 0.f, 1.f);
	float Cover = WeatherManagerCloudCoverAt[0];
	for (int32 Key = 1; Key < static_cast<int32>(UE_ARRAY_COUNT(WeatherManagerCloudCoverKeys)); ++Key)
	{
		if (Coverage <= WeatherManagerCloudCoverKeys[Key])
		{
			Cover = FMath::GetMappedRangeValueClamped(FVector2f(WeatherManagerCloudCoverKeys[Key - 1], WeatherManagerCloudCoverKeys[Key]), FVector2f(WeatherManagerCloudCoverAt[Key - 1], WeatherManagerCloudCoverAt[Key]), Coverage);
			break;
		}
	}
	const float Storm = FMath::Clamp(FMath::Max(Profile.RainIntensity, Profile.SnowIntensity * 0.5f), 0.f, 1.f);
	const float Density = FMath::Lerp(WeatherManagerCloudThinDensity, WeatherManagerCloudThickDensity, Storm);
	const float Low = FMath::SmoothStep(WeatherManagerCloudLowFrom, WeatherManagerCloudLowTo, Coverage);
	const FString Try = CVarAedirCloudTry.GetValueOnGameThread();
	if (!Try.IsEmpty())
	{
		TArray<FString> Parts;
		Try.ParseIntoArray(Parts, TEXT("_"));
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), Parts.IsValidIndex(0) ? FCString::Atof(*Parts[0]) : Cover);
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : Density);
		CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"), Parts.IsValidIndex(2) ? FCString::Atof(*Parts[2]) : Storm);
		if (Parts.IsValidIndex(4))
		{
			Clouds->SetLayerBottomAltitude(FCString::Atof(*Parts[3]));
			Clouds->SetLayerHeight(FCString::Atof(*Parts[4]));
		}
		if (Parts.IsValidIndex(5))
		{
			const float Grey = FCString::Atof(*Parts[5]);
			CloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), FLinearColor(Grey, Grey, Grey * 1.05f));
		}
		AppliedCloudCover = -100.f;
		return;
	}
	if (FMath::Abs(Cover - AppliedCloudCover) > WeatherManagerCloudChange * 0.3f || FMath::Abs(Storm - AppliedCloudStorm) > WeatherManagerCloudChange)
	{
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), Cover);
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalDensity"), Density);
		CloudMaterial->SetScalarParameterValue(TEXT("StormClouds"), 0.f);
		CloudMaterial->SetVectorParameterValue(TEXT("Cloud_AlbedoColor"), FMath::Lerp(FLinearColor::White, WeatherManagerCloudStormAlbedo, Storm));
		Clouds->SetLayerBottomAltitude(FMath::Lerp(WeatherManagerCloudHighBottomKm, WeatherManagerCloudLowBottomKm, Low));
		Clouds->SetLayerHeight(FMath::Lerp(WeatherManagerCloudHighDepthKm, WeatherManagerCloudLowDepthKm, Low));
		AppliedCloudCover = Cover;
		AppliedCloudStorm = Storm;
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
