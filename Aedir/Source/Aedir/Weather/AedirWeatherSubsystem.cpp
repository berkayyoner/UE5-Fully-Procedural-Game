#include "Weather/AedirWeatherSubsystem.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Weather/AedirWeatherManager.h"

namespace
{
	AAedirWeatherManager* WeatherSubsystemFindManager(UWorld* World)
	{
		for (TActorIterator<AAedirWeatherManager> Iterator(World); Iterator; ++Iterator)
		{
			return *Iterator;
		}

		return nullptr;
	}
}

bool UAedirWeatherSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UAedirWeatherSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	CachedManager = WeatherSubsystemFindManager(&InWorld);
	if (CachedManager.IsValid())
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CachedManager = InWorld.SpawnActor<AAedirWeatherManager>(AAedirWeatherManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
}

AAedirWeatherManager* UAedirWeatherSubsystem::GetManager() const
{
	if (CachedManager.IsValid())
	{
		return CachedManager.Get();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AAedirWeatherManager* Found = WeatherSubsystemFindManager(World);
	CachedManager = Found;
	return Found;
}
