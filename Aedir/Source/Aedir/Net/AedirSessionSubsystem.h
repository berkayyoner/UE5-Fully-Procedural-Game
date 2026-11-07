#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/TimerHandle.h"
#include "Net/Core/Connection/NetEnums.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AedirSessionSubsystem.generated.h"

class UNetDriver;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirSessionError, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirPlayerCountChanged, int32, NewPlayerCount);

UCLASS(BlueprintType)
class AEDIR_API UAedirSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Session")
	FAedirSessionError OnSessionError;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Session")
	FAedirPlayerCountChanged OnPlayerCountChanged;

	UFUNCTION(BlueprintCallable, Category="Aedir|Session")
	bool HostGame(FName MapName, int32 MaxPlayers = 4);

	UFUNCTION(BlueprintCallable, Category="Aedir|Session")
	bool JoinGame(const FString& Address);

	// From the title screen. An empty slot is a new game. The map comes from the save. When hosts get servers of their own, this and the next are what changes.
	bool StartFromSave(const FString& Slot, bool bHost);
	bool JoinWithCharacter(const FString& Address, const FString& CharacterSlot);
	// Back to the title screen, saving a guest's character on the way.
	void ReturnToMainMenu();

	UFUNCTION(BlueprintCallable, Category="Aedir|Session")
	void LeaveGame(FName MenuMap);

	UFUNCTION(BlueprintPure, Category="Aedir|Session")
	FString GetLocalAddress() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Session")
	bool IsCoopHost() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Session")
	bool IsCoopClient() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Session")
	int32 GetPlayerCount() const;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);
	void BroadcastSessionError(const FString& Message);
	void PollPlayerCount();

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PostWorldInitHandle;
	FTimerHandle PlayerCountTimerHandle;
	int32 LastPlayerCount = -1;
};
