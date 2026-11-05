#include "Game/AedirGameMode.h"

#include "Aedir.h"
#include "HUD/AedirHUD.h"
#include "Player/AedirPlayerCharacter.h"

AAedirGameMode::AAedirGameMode()
{
	DefaultPawnClass = AAedirPlayerCharacter::StaticClass();
	HUDClass = AAedirHUD::StaticClass();
}

void AAedirGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

	if (ErrorMessage.IsEmpty() && GetNumPlayers() >= MaxCoopPlayers)
	{
		ErrorMessage = TEXT("Server full");
		UE_LOG(LogAedir, Warning, TEXT("Rejected connection from %s: server full (%d/%d)"), *Address, GetNumPlayers(), MaxCoopPlayers);
	}
}

void AAedirGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	UE_LOG(LogAedir, Log, TEXT("Player joined: %s (%d players)"), *GetNameSafe(NewPlayer), GetNumPlayers());
}

void AAedirGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	UE_LOG(LogAedir, Log, TEXT("Player left: %s (%d players)"), *GetNameSafe(Exiting), GetNumPlayers());
}
