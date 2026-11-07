#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AedirMenuGameMode.generated.h"

class UProceduralMeshComponent;

// The start of the game: a dark room with the emblem turning in the light on the left, and the menu at the lower right.
UCLASS()
class AEDIR_API AAedirMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAedirMenuGameMode();

	virtual void StartPlay() override;
};

// The sign of the game, for now: a wide ring of dark iron with a blade through it.
UCLASS()
class AEDIR_API AAedirMenuEmblem : public AActor
{
	GENERATED_BODY()

public:
	AAedirMenuEmblem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Mesh;
};
