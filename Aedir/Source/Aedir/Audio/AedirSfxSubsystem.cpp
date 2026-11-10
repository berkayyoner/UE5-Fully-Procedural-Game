#include "Audio/AedirSfxSubsystem.h"

#include "Aedir.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/AedirSettingsSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	// How loud each kind is at full, and how far it carries.
	struct FAedirSfxKind
	{
		const TCHAR* Name;
		float Volume;
		float HeardCm;
		float FadeCm;
	};
	const FAedirSfxKind AedirSfxKinds[] =
	{
		{ TEXT("Clash"), 0.85f, 300.f, 3500.f },
		{ TEXT("MetalHit"), 0.7f, 250.f, 2500.f },
		{ TEXT("WoodHit"), 0.7f, 200.f, 2000.f },
		{ TEXT("WoodCut"), 0.8f, 200.f, 2200.f },
		{ TEXT("Punch"), 0.75f, 150.f, 1500.f },
		{ TEXT("BodyImpact"), 0.8f, 200.f, 2000.f },
		{ TEXT("Cut"), 0.8f, 150.f, 1500.f },
		{ TEXT("Stab"), 0.8f, 150.f, 1500.f },
		{ TEXT("Bone"), 0.9f, 150.f, 1800.f },
		{ TEXT("Armour"), 0.85f, 250.f, 3000.f },
		{ TEXT("Whoosh"), 0.45f, 80.f, 900.f },
	};
	static_assert(UE_ARRAY_COUNT(AedirSfxKinds) == static_cast<int32>(EAedirSfx::Count), "A volume for every kind of sound");
	constexpr int32 AedirSfxMaxTakes = 8;
	// The same kind within this far and this long of another is the same sound, heard once.
	constexpr double AedirSfxRepeatSeconds = 0.06;
	constexpr double AedirSfxRepeatCm = 40.0;
	TAutoConsoleVariable<int32> CVarAedirSfxLog(TEXT("aedir.SfxLog"), 0, TEXT("Log every combat sound played (1)."));
}

const TCHAR* UAedirSfxSubsystem::GetName(EAedirSfx Kind)
{
	return AedirSfxKinds[static_cast<int32>(Kind)].Name;
}

void UAedirSfxSubsystem::Play(const UObject* WorldContext, EAedirSfx Kind, const FVector& Location, float Strength)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UAedirSfxSubsystem* Sfx = World->GetSubsystem<UAedirSfxSubsystem>())
	{
		Sfx->PlayAt(Kind, Location, FMath::Clamp(Strength, 0.f, 1.f));
	}
}

const TArray<TObjectPtr<USoundBase>>& UAedirSfxSubsystem::GetTakes(EAedirSfx Kind)
{
	const int32 Index = static_cast<int32>(Kind);
	if (!bLoaded[Index])
	{
		bLoaded[Index] = true;
		for (int32 Take = 0; Take < AedirSfxMaxTakes; ++Take)
		{
			const FString Path = FString::Printf(TEXT("/Game/Aedir/Sounds/Combat/SFX_%s_%d.SFX_%s_%d"), AedirSfxKinds[Index].Name, Take, AedirSfxKinds[Index].Name, Take);
			if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Takes[Index].Add(Sound);
				AllTakes.Add(Sound);
			}
		}
		UE_CLOG(Takes[Index].IsEmpty(), LogAedir, Warning, TEXT("[Sfx] No sounds found for %s"), AedirSfxKinds[Index].Name);
	}
	return Takes[Index];
}

void UAedirSfxSubsystem::PlayAt(EAedirSfx Kind, const FVector& Location, float Strength)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	Recent.RemoveAll([Now](const FRecent& Was) { return Now - Was.Time > AedirSfxRepeatSeconds; });
	for (const FRecent& Was : Recent)
	{
		if (Was.Kind == Kind && FVector::DistSquared(Was.Location, Location) < AedirSfxRepeatCm * AedirSfxRepeatCm)
		{
			return;
		}
	}
	Recent.Add({ Kind, Location, Now });

	const TArray<TObjectPtr<USoundBase>>& Options = GetTakes(Kind);
	if (Options.IsEmpty())
	{
		return;
	}
	if (!Attenuation)
	{
		Attenuation = NewObject<USoundAttenuation>(this);
	}
	const FAedirSfxKind& Info = AedirSfxKinds[static_cast<int32>(Kind)];
	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(Info.HeardCm, 0.f, 0.f);
	Settings.FalloffDistance = Info.FadeCm;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;

	// A harder blow is louder and a little lower; every take is pitched a little differently so no two sound the same.
	USoundBase* Sound = Options[FMath::RandRange(0, Options.Num() - 1)];
	const float Volume = Info.Volume * FMath::Lerp(0.35f, 1.f, Strength) * UAedirSettingsSubsystem::GetValues().EffectsVolume;
	const float Pitch = FMath::FRandRange(0.92f, 1.08f) * FMath::Lerp(1.08f, 0.92f, Strength);
	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, Attenuation, nullptr);
	UE_CLOG(CVarAedirSfxLog.GetValueOnGameThread() != 0, LogAedir, Log, TEXT("[Sfx] %s at %s, strength %.2f -> %s"), Info.Name, *Location.ToCompactString(), Strength, *Sound->GetName());
}
