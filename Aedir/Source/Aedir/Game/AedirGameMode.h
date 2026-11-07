#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AedirGameMode.generated.h"

UCLASS(Blueprintable)
class AEDIR_API AAedirGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAedirGameMode();

	UPROPERTY(EditAnywhere, Category="Aedir|Net")
	int32 MaxCoopPlayers = 4;

	// When set, the local player is shown the character creation screen shortly after spawning.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Game")
	bool bShowCharacterCreationOnStart = false;

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void StartPlay() override;
};
