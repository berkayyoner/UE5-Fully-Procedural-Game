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

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLightning(FVector StrikeLocation, float Seed);

private:
	double GetServerTime() const;
	void UpdateServerSchedules(double ServerTime, const FAedirWeatherProfile& Profile);
	void ScheduleNextAutoChange(double ServerTime);
	void ScheduleNextLightning(double ServerTime, float LightningPerMinute);
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
	TWeakObjectPtr<AExponentialHeightFog> HeightFog;
	TWeakObjectPtr<ASkyLight> SkyLight;
	TWeakObjectPtr<UAedirSurfaceSubsystem> SurfaceSubsystem;

	FLinearColor AppliedSunColor = FLinearColor::Transparent;
	double NextSceneSearchTime = 0.0;
	double NextSkyLightApplyTime = 0.0;
	float AppliedSkyLightIntensity = -1.f;

	double NextLightningServerTime = -1.0;
	double NextAutoChangeServerTime = -1.0;
	double LightningFlashWorldTime = -1000.0;

	float SmoothedWetness = 0.f;
	float SmoothedSnow = 0.f;
};
