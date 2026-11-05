#include "Weather/AedirWeatherEffectsComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSettings.h"
#include "Weather/AedirWeatherSubsystem.h"

namespace
{
	constexpr float WeatherEffectsActivationThreshold = 0.02f;
	constexpr float WeatherEffectsRainHeightOffset = 600.f;
	constexpr float WeatherEffectsShelterTraceInterval = 0.25f;
	constexpr float WeatherEffectsShelterTraceDistance = 3000.f;
	constexpr float WeatherEffectsShelterInterpSpeed = 2.f;
	constexpr float WeatherEffectsGroundTraceDistance = 3000.f;
	constexpr float WeatherEffectsAudioInterpSpeed = 2.f;
	constexpr float WeatherEffectsShelteredAudioScale = 0.25f;
	constexpr float WeatherEffectsWindReferenceSpeed = 20.f;

	const FName WeatherEffectsIntensityParameter(TEXT("Intensity"));
	const FName WeatherEffectsWindParameter(TEXT("Wind"));

	UAudioComponent* WeatherEffectsCreateLoop(AActor* Owner, USoundBase* Sound)
	{
		UAudioComponent* Audio = NewObject<UAudioComponent>(Owner);
		Owner->AddInstanceComponent(Audio);
		Audio->SetupAttachment(Owner->GetRootComponent());
		Audio->bAutoActivate = false;
		Audio->bAllowSpatialization = false;
		Audio->SetSound(Sound);
		Audio->SetVolumeMultiplier(0.f);
		Audio->RegisterComponent();
		Audio->Play();
		return Audio;
	}

	void WeatherEffectsDriveNiagara(UNiagaraComponent* Effect, float Intensity, const FVector& Wind, bool& bActive)
	{
		if (!Effect)
		{
			return;
		}

		Effect->SetFloatParameter(WeatherEffectsIntensityParameter, Intensity);
		Effect->SetVectorParameter(WeatherEffectsWindParameter, Wind);

		if (Intensity >= WeatherEffectsActivationThreshold)
		{
			if (!bActive)
			{
				Effect->Activate(false);
				bActive = true;
			}
		}
		else if (bActive)
		{
			Effect->Deactivate();
			bActive = false;
		}
	}
}

UAedirWeatherEffectsComponent::UAedirWeatherEffectsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UAedirWeatherEffectsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerPawn)
	{
		OwnerPawn = Cast<APawn>(GetOwner());
	}

	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled() || OwnerPawn->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UWorld* World = GetWorld();
	UAedirWeatherSubsystem* WeatherSubsystem = World ? World->GetSubsystem<UAedirWeatherSubsystem>() : nullptr;
	const AAedirWeatherManager* Manager = WeatherSubsystem ? WeatherSubsystem->GetManager() : nullptr;
	if (!Manager)
	{
		return;
	}

	EnsureEffects();
	UpdateShelter(DeltaTime);

	const FAedirWeatherProfile Profile = Manager->GetBlendedProfile();
	const FVector Wind = Manager->GetWindVector();
	const float RainLevel = Profile.RainIntensity * ShelterFactor;
	const float SnowLevel = Profile.SnowIntensity * ShelterFactor;

	WeatherEffectsDriveNiagara(RainEffect, RainLevel, Wind, bRainActive);
	WeatherEffectsDriveNiagara(SnowEffect, SnowLevel, Wind, bSnowActive);
	UpdateSplash(RainLevel);
	UpdateAudio(Profile, DeltaTime);
}

void UAedirWeatherEffectsComponent::EnsureEffects()
{
	if (bEffectsCreated)
	{
		return;
	}

	bEffectsCreated = true;

	USceneComponent* Root = OwnerPawn->GetRootComponent();
	if (!Root)
	{
		return;
	}

	const UAedirWeatherSettings* Settings = GetDefault<UAedirWeatherSettings>();

	if (UNiagaraSystem* RainSystem = Settings->RainSystem.LoadSynchronous())
	{
		RainEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
			RainSystem,
			Root,
			NAME_None,
			FVector(0.f, 0.f, WeatherEffectsRainHeightOffset),
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false,
			false);
	}

	if (UNiagaraSystem* SnowSystem = Settings->SnowSystem.LoadSynchronous())
	{
		SnowEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
			SnowSystem,
			Root,
			NAME_None,
			FVector(0.f, 0.f, WeatherEffectsRainHeightOffset),
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false,
			false);
	}

	if (UNiagaraSystem* SplashSystem = Settings->RainSplashSystem.LoadSynchronous())
	{
		SplashEffect = UNiagaraFunctionLibrary::SpawnSystemAttached(
			SplashSystem,
			Root,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false,
			false);
	}

	if (USoundBase* RainSound = Settings->RainLoop.LoadSynchronous())
	{
		RainAudio = WeatherEffectsCreateLoop(OwnerPawn, RainSound);
	}

	if (USoundBase* WindSound = Settings->WindLoop.LoadSynchronous())
	{
		WindAudio = WeatherEffectsCreateLoop(OwnerPawn, WindSound);
	}
}

void UAedirWeatherEffectsComponent::UpdateShelter(float DeltaTime)
{
	ShelterTraceTimer -= DeltaTime;
	if (ShelterTraceTimer <= 0.f)
	{
		ShelterTraceTimer = WeatherEffectsShelterTraceInterval;
		bSheltered = IsHeadSheltered();
	}

	const float TargetShelter = bSheltered ? 0.f : 1.f;
	ShelterFactor = FMath::FInterpTo(ShelterFactor, TargetShelter, DeltaTime, WeatherEffectsShelterInterpSpeed);
}

void UAedirWeatherEffectsComponent::UpdateSplash(float RainLevel)
{
	if (!SplashEffect)
	{
		return;
	}

	if (RainLevel >= WeatherEffectsActivationThreshold)
	{
		FVector GroundLocation = FVector::ZeroVector;
		if (TraceGround(GroundLocation))
		{
			SplashEffect->SetWorldLocation(GroundLocation);
		}
	}

	WeatherEffectsDriveNiagara(SplashEffect, RainLevel, FVector::ZeroVector, bSplashActive);
}

void UAedirWeatherEffectsComponent::UpdateAudio(const FAedirWeatherProfile& Profile, float DeltaTime)
{
	const float ShelterScale = FMath::Lerp(WeatherEffectsShelteredAudioScale, 1.f, ShelterFactor);
	const float RainTarget = Profile.RainIntensity * ShelterScale;
	const float WindTarget = FMath::Clamp(Profile.WindSpeed / WeatherEffectsWindReferenceSpeed, 0.f, 1.f) * ShelterScale;

	RainAudioVolume = FMath::FInterpTo(RainAudioVolume, RainTarget, DeltaTime, WeatherEffectsAudioInterpSpeed);
	WindAudioVolume = FMath::FInterpTo(WindAudioVolume, WindTarget, DeltaTime, WeatherEffectsAudioInterpSpeed);

	if (RainAudio)
	{
		RainAudio->SetVolumeMultiplier(RainAudioVolume);
	}

	if (WindAudio)
	{
		WindAudio->SetVolumeMultiplier(WindAudioVolume);
	}
}

bool UAedirWeatherEffectsComponent::IsHeadSheltered() const
{
	const UWorld* World = GetWorld();
	if (!World || !OwnerPawn)
	{
		return false;
	}

	float HeadOffset = 0.f;
	if (const ACharacter* Character = Cast<ACharacter>(OwnerPawn))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			HeadOffset = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	const FVector Start = OwnerPawn->GetActorLocation() + FVector::UpVector * HeadOffset;
	const FVector End = Start + FVector::UpVector * WeatherEffectsShelterTraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWeatherEffectsShelter), false, OwnerPawn);
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

bool UAedirWeatherEffectsComponent::TraceGround(FVector& OutGround) const
{
	const UWorld* World = GetWorld();
	if (!World || !OwnerPawn)
	{
		return false;
	}

	const FVector Origin = GetViewLocation();
	const FVector End = Origin - FVector::UpVector * WeatherEffectsGroundTraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWeatherEffectsGround), false, OwnerPawn);
	if (!World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params))
	{
		return false;
	}

	OutGround = Hit.ImpactPoint;
	return true;
}

FVector UAedirWeatherEffectsComponent::GetViewLocation() const
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(OwnerPawn->GetController()))
	{
		if (PlayerController->PlayerCameraManager)
		{
			return PlayerController->PlayerCameraManager->GetCameraLocation();
		}
	}

	return OwnerPawn->GetActorLocation();
}
