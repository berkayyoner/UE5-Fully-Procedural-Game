#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weather/AedirWeatherTypes.h"
#include "AedirWeatherManager.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;
class UAedirSurfaceSubsystem;

UCLASS()
class AEDIR_API AAedirWeatherManager : public AActor
{
	GENERATED_BODY()

public:
	// Lightning in the clouds: a flash inside the cloud layer above a point, lighting the clouds from within (the engine's
	// local lights in volumetric clouds, switched on only while a storm has lightning).
	void FlashClouds(const FVector& Below);

	AAedirWeatherManager();

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Weather")
	EAedirWeather CurrentWeather = EAedirWeather::Sunny;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Weather")
	EAedirWeather PreviousWeather = EAedirWeather::Sunny;

	// How hard the wind blows now, from still air to the worst of a storm.
	float GetWindShare() const { return FMath::Clamp(FAedirWeatherProfile::MakeDefaultProfile(CurrentWeather).WindSpeed / 18.f, 0.f, 1.f); }

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Weather")
	double TransitionStartServerTime = 0.0;

	UPROPERTY(ReplicatedUsing=OnRep_Weather, BlueprintReadOnly, Category="Aedir|Weather")
	float TransitionSeconds = 0.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Weather")
	float RandomSeed = 0.f;

	// Hours the day has been moved on from its own course, so the clock reads what settime asked for.
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Weather")
	float HourShift = 0.f;

	// Puts the clock at this hour of the day (0 to 24) from now on; server only.
	void SetHour(float Hour);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Weather")
	void SetWeather(EAedirWeather NewWeather, float NewTransitionSeconds);

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	EAedirWeather GetWeather() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	FAedirWeatherProfile GetBlendedProfile() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	float GetRainIntensity() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	float GetSnowIntensity() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	FVector GetWindVector() const;
	// The way the wind blows, the same for the trees, the grass and the rain (degrees of yaw).
	static constexpr float WeatherWindYawDegrees = 35.f;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLightning(FVector StrikeLocation, float Seed);

	// Strikes a bolt at once, somewhere around the players, whatever the weather.
	void StrikeLightningNow() { StrikeLightning(); }

private:
	double GetServerTime() const;
	void UpdateServerSchedules(double ServerTime, const FAedirWeatherProfile& Profile);
	void ScheduleNextAutoChange(double ServerTime);
	void StrikeLightning();
	bool FindLightningStrikeLocation(FVector& OutLocation) const;
	void ApplyLightningDamage(const FVector& StrikeLocation) const;
	void PlayThunderAt(FVector StrikeLocation);
	void RefreshSceneActors();
	void ApplySceneLighting(const FAedirWeatherProfile& Profile);
	void UpdateSurfaceCoupling(const FAedirWeatherProfile& Profile, float DeltaSeconds);

	// The volumetric clouds follow the weather: how much of the sky they cover, how thick and how dark.
	void ApplyClouds(const FAedirWeatherProfile& Profile);

	UFUNCTION()
	void OnRep_Weather();

	TWeakObjectPtr<class UVolumetricCloudComponent> CloudComponent;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> CloudMaterial;

	float AppliedCloudCover = -100.f;
	float AppliedCloudStorm = -100.f;

	TWeakObjectPtr<ADirectionalLight> SunLight;

	// The day goes round: the sun rises in the east, stands highest at noon and sets in the west; at night the same light is
	// a faint blue moon high in the sky and the sky is dark. The yaw the sun was placed at is where it stands at noon.
	float NoonYaw = 0.f;
	bool bNoonYawKnown = false;
	bool bMoonUp = false;
	void ApplyDayCycle(UDirectionalLightComponent* SunComponent, float WeatherLux);

public:
	// The hour of the day, 0 to 24, and how much of full day it is (0 at night, 1 with the sun well up), for anything lit by
	// its own fire or lamp.
	static float GetHourOfDay(const UWorld* World);
	// How many seconds of play one day of the game's clock takes.
	static float GetDayRealSeconds();
	static float GetDaylight(const UWorld* World);

private:
	TWeakObjectPtr<AExponentialHeightFog> HeightFog;
	TWeakObjectPtr<ASkyLight> SkyLight;
	TWeakObjectPtr<UAedirSurfaceSubsystem> SurfaceSubsystem;

	FLinearColor AppliedSunColor = FLinearColor::Transparent;
	double NextSceneSearchTime = 0.0;
	double NextSkyLightApplyTime = 0.0;
	float AppliedSkyLightIntensity = -1.f;

	// When the server last weighed whether a bolt comes down.
	double LightningCheckServerTime = -1.0;
	double NextAutoChangeServerTime = -1.0;
	double LightningFlashWorldTime = -1000.0;
	// How much of its light the last bolt threw over the land here, by how far off it came down (0..1).
	float LightningFlashShare = 0.f;
	// The same for the flash in the clouds going now.
	float CloudFlashShare = 0.f;
	// Rain on everything round the eye: a decal from above that darkens it, one that makes it shine.
	void StepWet();
	UPROPERTY(Transient)
	TObjectPtr<class UDecalComponent> WetDarken;
	UPROPERTY(Transient)
	TObjectPtr<class UDecalComponent> WetGloss;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> WetDarkenMaterial;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> WetGlossMaterial;
	float AppliedWet = -1.f;
	// When the wet decals were made; they are not shown before WetShowAfterSeconds and their shaders are ready.
	double WetMadeTime = -1.0;
	static constexpr float WetShowAfterSeconds = 1.f;
	bool IsWetReady() const;
	FLinearColor FogSkyColor = FLinearColor::White;
	bool bFogColorKnown = false;

	float SmoothedWetness = 0.f;
	float SmoothedSnow = 0.f;

	// The bolt itself: a jagged glowing line from the clouds to where it strikes, with a few forks, flickering for a moment.
	void BuildBolt(const FVector& StrikeLocation, float Seed);
	void StepBolt(float DeltaSeconds);
	UPROPERTY(Transient)
	TObjectPtr<class UInstancedStaticMeshComponent> BoltMesh;
	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> BoltMaterial;
	UPROPERTY(Transient)
	TObjectPtr<class UPointLightComponent> BoltLight;

	// The white-hot core of the channel, drawn over its glow.
	UPROPERTY(Transient)
	TObjectPtr<class UInstancedStaticMeshComponent> BoltCore;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> BoltCoreMaterial;

	// Thunder from a bolt here: heard at once with the flash when it is near enough, quieter the further off; not at all
	// past that.
	void QueueThunder(const FVector& From, float Loudness);
	// Where the local view is, or this actor when there is none.
	FVector ViewLocation() const;
	void PlayThunder(float Loudness);
	// Segment ends, two per segment, and each segment's width.
	TArray<FVector> BoltPoints;
	TArray<float> BoltWidths;
	float BoltAge = 1000.f;

	void StepCloudFlashes(float DeltaSeconds, float LightningPerMinute);
	UPROPERTY(Transient)
	TObjectPtr<class UPointLightComponent> CloudFlashLight;
	float CloudFlashAge = 1000.f;
	float CloudFlashSeed = 0.f;
	float CloudFlashGlow = 0.f;
	double NextCloudFlashTime = -1.0;
	bool bCloudLightsOn = false;
};
