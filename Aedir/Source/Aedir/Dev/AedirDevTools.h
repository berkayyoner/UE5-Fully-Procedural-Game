#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AedirDevTools.generated.h"

class AAedirPlayerCharacter;

// Developer console commands used by the player character wrappers. Implemented in non-shipping builds only.
UCLASS()
class AEDIR_API UAedirDevTools : public UObject
{
	GENERATED_BODY()

public:
	// Runs one command by its console name with the raw argument string. Authority only for the commands that change the world.
	static void Execute(AAedirPlayerCharacter* Player, const FString& Command, const FString& Args);

	static void SpawnItem(AAedirPlayerCharacter* Player, const FString& Args);
	static void ListItems(AAedirPlayerCharacter* Player, const FString& Args);
	static void SpawnEnemy(AAedirPlayerCharacter* Player, const FString& Args);
	static void KillAll(AAedirPlayerCharacter* Player, const FString& Args);
	static void SetWeather(AAedirPlayerCharacter* Player, const FString& Args);
	static void Heal(AAedirPlayerCharacter* Player, const FString& Args);
	static void Hurt(AAedirPlayerCharacter* Player, const FString& Args);
	static void God(AAedirPlayerCharacter* Player, const FString& Args);
	static void GiveSkill(AAedirPlayerCharacter* Player, const FString& Args);
	static void Wound(AAedirPlayerCharacter* Player, const FString& Args);
	static void BloodTest(AAedirPlayerCharacter* Player, const FString& Args);
	static void DebugBody(AAedirPlayerCharacter* Player, const FString& Args);
	static void Help(AAedirPlayerCharacter* Player, const FString& Args);
};
