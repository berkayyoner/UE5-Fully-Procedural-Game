#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Items/AedirItemActor.h"
#include "UObject/SoftObjectPtr.h"
#include "Weather/AedirWeatherTypes.h"
#include "AedirSaveGame.generated.h"

USTRUCT(BlueprintType)
struct AEDIR_API FAedirCreatureSave
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	float Health = 100.f;

	UPROPERTY()
	float MaxHealth = 100.f;

	UPROPERTY()
	float Mana = 100.f;

	UPROPERTY()
	float MaxMana = 100.f;

	UPROPERTY()
	float Stamina = 100.f;

	UPROPERTY()
	float MaxStamina = 100.f;

	UPROPERTY()
	float Strength = 10.f;

	UPROPERTY()
	float Athletism = 10.f;

	UPROPERTY()
	TArray<float> Skills;

	UPROPERTY()
	FName GroupId;

	UPROPERTY()
	FName RaceId;

	UPROPERTY()
	float Hunger = 100.f;

	UPROPERTY()
	float Thirst = 100.f;

	// Catalog items are all one class, told apart by their id.
	UPROPERTY()
	FName LeftHandId;

	UPROPERTY()
	FName RightHandId;

	UPROPERTY()
	TArray<FName> WaistIds;

	UPROPERTY()
	TSoftClassPtr<AAedirItemActor> LeftHandItem;

	UPROPERTY()
	TSoftClassPtr<AAedirItemActor> RightHandItem;

	// Six waist slots: indices 0-2 left side, 3-5 right side. Null entries are empty slots.
	UPROPERTY()
	TArray<TSoftClassPtr<AAedirItemActor>> WaistItems;
};

USTRUCT(BlueprintType)
struct AEDIR_API FAedirItemSave
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftClassPtr<AAedirItemActor> ItemClass;

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	FName CatalogId;
};

// A person of the world, as far as the story needs to remember them.
USTRUCT()
struct AEDIR_API FAedirNpcSave
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	uint8 Order = 0;

	UPROPERTY()
	float Health = 100.f;

	UPROPERTY()
	float Hunger = 100.f;

	UPROPERTY()
	float Thirst = 100.f;

	UPROPERTY()
	bool bDead = false;

	UPROPERTY()
	TArray<FString> RelationKeys;

	UPROPERTY()
	TArray<int32> RelationValues;

	UPROPERTY()
	TArray<FName> Gear;

	// Saves from before people could pick things up have no gear to give back.
	UPROPERTY()
	bool bGearSaved = false;
};

UCLASS(BlueprintType)
class AEDIR_API UAedirSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UAedirSaveGame();

	static constexpr int32 CurrentSaveVersion = 1;

	UPROPERTY()
	int32 SaveVersion = CurrentSaveVersion;

	UPROPERTY()
	FDateTime Timestamp;

	UPROPERTY()
	FString MapName;

	UPROPERTY()
	EAedirWeather Weather = EAedirWeather::Sunny;

	UPROPERTY()
	double WorldSeconds = 0.0;

	// Shown in the save list. Saves written before these fields existed read as 0 and empty.
	UPROPERTY()
	int32 PlayerLevel = 0;

	UPROPERTY()
	FString LocationName;

	UPROPERTY()
	TArray<FAedirCreatureSave> Players;

	UPROPERTY()
	TArray<FAedirNpcSave> Npcs;

	UPROPERTY()
	TArray<FAedirItemSave> LooseItems;
};
