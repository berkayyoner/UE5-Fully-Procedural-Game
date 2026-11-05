#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirWeatherEffectsComponent.generated.h"

class APawn;
class UAudioComponent;
class UNiagaraComponent;
struct FAedirWeatherProfile;

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirWeatherEffectsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirWeatherEffectsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureEffects();
	void UpdateShelter(float DeltaTime);
	void UpdateSplash(float RainLevel);
	void UpdateAudio(const FAedirWeatherProfile& Profile, float DeltaTime);
	bool IsHeadSheltered() const;
	bool TraceGround(FVector& OutGround) const;
	FVector GetViewLocation() const;

	UPROPERTY(Transient)
	TObjectPtr<APawn> OwnerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> RainEffect;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SnowEffect;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SplashEffect;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RainAudio;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WindAudio;

	float ShelterFactor = 1.f;
	float ShelterTraceTimer = 0.f;
	float RainAudioVolume = 0.f;
	float WindAudioVolume = 0.f;
	bool bSheltered = false;
	bool bEffectsCreated = false;
	bool bRainActive = false;
	bool bSnowActive = false;
	bool bSplashActive = false;
};
