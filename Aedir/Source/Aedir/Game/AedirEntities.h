#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirEntities.generated.h"

// The personal number of one thing in the world, as in Skyrim's reference ids, kept next to what it is an instance of.
UCLASS(ClassGroup=(Aedir))
class AEDIR_API UAedirEntityIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirEntityIdComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	uint32 RefId = 0;

	UPROPERTY(Replicated)
	bool bDisabled = false;

	bool bWasSimulating = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> ComponentsStopped;
};

// Hands out reference ids to everything that spawns and finds things by them.
UCLASS()
class AEDIR_API UAedirEntitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

	AActor* FindByRef(uint32 RefId) const;

private:
	void Register(AActor* Actor);

	TMap<uint32, TWeakObjectPtr<AActor>> ByRef;
	FDelegateHandle SpawnHandle;
	uint32 NextRef = 0xFF000001;
};

namespace AedirEntities
{
	// What kinds of things there are. Each has a base id that never changes, worked out from its name.
	AEDIR_API uint32 BaseIdOfName(FName BaseName);
	AEDIR_API TArray<FName> GetAllBaseNames();
	AEDIR_API FName BaseNameOfActor(const AActor* Actor);

	// Takes a base id in hexadecimal or the name itself, in any case.
	AEDIR_API FName FindBaseName(const FString& Token);

	AEDIR_API uint32 RefIdOfActor(const AActor* Actor);
	AEDIR_API bool ParseHex(const FString& Token, uint32& OutValue);
	AEDIR_API FString DisplayNameOfActor(const AActor* Actor);
}
