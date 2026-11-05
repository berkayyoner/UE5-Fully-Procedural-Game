#include "Net/AedirSessionSubsystem.h"

#include "Aedir.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "SocketSubsystem.h"

namespace
{
	constexpr float SessionPlayerCountPollInterval = 1.f;

	APlayerController* SessionFindLocalController(const UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}

		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = Iterator->Get();
			if (PlayerController && PlayerController->IsLocalController())
			{
				return PlayerController;
			}
		}

		return nullptr;
	}
}

void UAedirSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UAedirSessionSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UAedirSessionSubsystem::HandleTravelFailure);
	}

	// The game instance timer manager is not ticked, so the player count poll runs on each new game world.
	PostWorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
		[this](UWorld* World, const UWorld::InitializationValues)
		{
			if (!World || !World->IsGameWorld())
			{
				return;
			}

			LastPlayerCount = -1;
			World->GetTimerManager().SetTimer(PlayerCountTimerHandle, FTimerDelegate::CreateUObject(this, &UAedirSessionSubsystem::PollPlayerCount), SessionPlayerCountPollInterval, true);
		});
}

void UAedirSessionSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitHandle);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlayerCountTimerHandle);
	}

	Super::Deinitialize();
}

bool UAedirSessionSubsystem::HostGame(FName MapName, int32 MaxPlayers)
{
	UWorld* World = GetWorld();
	if (!World || MapName.IsNone())
	{
		return false;
	}

	const int32 ClampedMaxPlayers = FMath::Max(1, MaxPlayers);
	UE_LOG(LogAedir, Log, TEXT("Hosting %s for up to %d players"), *MapName.ToString(), ClampedMaxPlayers);

	UGameplayStatics::OpenLevel(World, MapName, true, FString::Printf(TEXT("listen?MaxPlayers=%d"), ClampedMaxPlayers));
	return true;
}

bool UAedirSessionSubsystem::JoinGame(const FString& Address)
{
	const FString TrimmedAddress = Address.TrimStartAndEnd();
	if (TrimmedAddress.IsEmpty())
	{
		return false;
	}

	APlayerController* LocalController = SessionFindLocalController(GetWorld());
	if (!LocalController)
	{
		return false;
	}

	UE_LOG(LogAedir, Log, TEXT("Joining %s"), *TrimmedAddress);
	LocalController->ClientTravel(TrimmedAddress, ETravelType::TRAVEL_Absolute);
	return true;
}

void UAedirSessionSubsystem::LeaveGame(FName MenuMap)
{
	UWorld* World = GetWorld();
	if (!World || MenuMap.IsNone())
	{
		return;
	}

	UE_LOG(LogAedir, Log, TEXT("Leaving game for %s"), *MenuMap.ToString());

	if (World->GetNetMode() == NM_Client)
	{
		if (APlayerController* LocalController = SessionFindLocalController(World))
		{
			LocalController->ClientTravel(MenuMap.ToString(), ETravelType::TRAVEL_Absolute);
			return;
		}
	}

	UNetDriver* NetDriver = World->GetNetDriver();
	if (NetDriver && GEngine)
	{
		GEngine->HandleDisconnect(World, NetDriver);
		return;
	}

	UGameplayStatics::OpenLevel(World, MenuMap);
}

FString UAedirSessionSubsystem::GetLocalAddress() const
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		return FString();
	}

	bool bCanBindAll = false;
	const TSharedRef<FInternetAddr> LocalAddress = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
	return LocalAddress->IsValid() ? LocalAddress->ToString(true) : FString();
}

bool UAedirSessionSubsystem::IsCoopHost() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_ListenServer;
}

bool UAedirSessionSubsystem::IsCoopClient() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() == NM_Client;
}

int32 UAedirSessionSubsystem::GetPlayerCount() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	return GameState ? GameState->PlayerArray.Num() : 0;
}

void UAedirSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	BroadcastSessionError(FString::Printf(TEXT("Network failure (%s): %s"), ENetworkFailure::ToString(FailureType), *ErrorString));
}

void UAedirSessionSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	BroadcastSessionError(FString::Printf(TEXT("Travel failure (%s): %s"), ETravelFailure::ToString(FailureType), *ErrorString));
}

void UAedirSessionSubsystem::BroadcastSessionError(const FString& Message)
{
	UE_LOG(LogAedir, Warning, TEXT("Session error: %s"), *Message);
	OnSessionError.Broadcast(Message);
}

void UAedirSessionSubsystem::PollPlayerCount()
{
	const int32 PlayerCount = GetPlayerCount();
	if (PlayerCount != LastPlayerCount)
	{
		LastPlayerCount = PlayerCount;
		OnPlayerCountChanged.Broadcast(PlayerCount);
	}
}
