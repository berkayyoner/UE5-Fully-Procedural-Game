#include "Creature/AedirCreatureTypes.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "Net/AedirSaveGame.h"

namespace
{
	UAedirSaveGame* AedirSaveTestCreateSaveGame()
	{
		return NewObject<UAedirSaveGame>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSaveDefaultsTest, "Aedir.Save.Defaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSaveDefaultsTest::RunTest(const FString& Parameters)
{
	UAedirSaveGame* Save = AedirSaveTestCreateSaveGame();
	if (!TestTrue(TEXT("Save game object is created"), Save != nullptr))
	{
		return false;
	}

	TestEqual(TEXT("Fresh save uses the current save version"), Save->SaveVersion, UAedirSaveGame::CurrentSaveVersion);
	TestTrue(TEXT("Fresh save has no players"), Save->Players.IsEmpty());
	TestTrue(TEXT("Fresh save has no loose items"), Save->LooseItems.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSaveRoundTripTest, "Aedir.Save.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSaveRoundTripTest::RunTest(const FString& Parameters)
{
	UAedirSaveGame* Source = AedirSaveTestCreateSaveGame();
	if (!TestTrue(TEXT("Source save game is created"), Source != nullptr))
	{
		return false;
	}

	Source->MapName = TEXT("TestMap");
	Source->Weather = EAedirWeather::Thunderstorm;
	Source->WorldSeconds = 12345.5;

	FAedirCreatureSave Creature;
	Creature.PlayerKey = TEXT("Player_A");
	Creature.Transform.SetLocation(FVector(100.f, -200.f, 300.f));
	Creature.Health = 42.f;
	Creature.MaxHealth = 120.f;
	Creature.Mana = 30.f;
	Creature.MaxMana = 80.f;
	Creature.Stamina = 55.f;
	Creature.MaxStamina = 150.f;
	Creature.Strength = 25.f;
	Creature.Athletism = 40.f;
	Creature.Skills.SetNumZeroed(static_cast<int32>(EAedirSkill::Count));
	Creature.Skills[static_cast<int32>(EAedirSkill::Smelting)] = 33.f;
	Creature.GroupId = TEXT("Wolves");
	Creature.WaistItems.SetNum(6);
	Source->Players.Add(Creature);

	FAedirItemSave LooseItem;
	LooseItem.Transform.SetLocation(FVector(1.f, 2.f, 3.f));
	Source->LooseItems.Add(LooseItem);

	TArray<uint8> SaveData;
	TestTrue(TEXT("Save game serializes to memory"), UGameplayStatics::SaveGameToMemory(Source, SaveData));
	TestTrue(TEXT("Serialized save data is not empty"), SaveData.Num() > 0);

	UAedirSaveGame* Restored = Cast<UAedirSaveGame>(UGameplayStatics::LoadGameFromMemory(SaveData));
	if (!TestTrue(TEXT("Loaded save game is a UAedirSaveGame"), Restored != nullptr))
	{
		return false;
	}

	TestEqual(TEXT("Save version survives round trip"), Restored->SaveVersion, UAedirSaveGame::CurrentSaveVersion);
	TestEqual(TEXT("Map name survives round trip"), Restored->MapName, FString(TEXT("TestMap")));
	TestTrue(TEXT("Weather survives round trip"), Restored->Weather == EAedirWeather::Thunderstorm);
	TestEqual(TEXT("World seconds survive round trip"), Restored->WorldSeconds, 12345.5);

	if (!TestEqual(TEXT("Player count survives round trip"), Restored->Players.Num(), 1))
	{
		return false;
	}

	const FAedirCreatureSave& RestoredCreature = Restored->Players[0];
	TestEqual(TEXT("Player key survives round trip"), RestoredCreature.PlayerKey, FString(TEXT("Player_A")));
	TestEqual(TEXT("Player location survives round trip"), RestoredCreature.Transform.GetLocation(), FVector(100.f, -200.f, 300.f));
	TestEqual(TEXT("Health survives round trip"), RestoredCreature.Health, 42.f);
	TestEqual(TEXT("Max health survives round trip"), RestoredCreature.MaxHealth, 120.f);
	TestEqual(TEXT("Mana survives round trip"), RestoredCreature.Mana, 30.f);
	TestEqual(TEXT("Max mana survives round trip"), RestoredCreature.MaxMana, 80.f);
	TestEqual(TEXT("Stamina survives round trip"), RestoredCreature.Stamina, 55.f);
	TestEqual(TEXT("Max stamina survives round trip"), RestoredCreature.MaxStamina, 150.f);
	TestEqual(TEXT("Strength survives round trip"), RestoredCreature.Strength, 25.f);
	TestEqual(TEXT("Athletism survives round trip"), RestoredCreature.Athletism, 40.f);
	TestEqual(TEXT("Skill count survives round trip"), RestoredCreature.Skills.Num(), static_cast<int32>(EAedirSkill::Count));
	TestEqual(TEXT("Skill value survives round trip"), RestoredCreature.Skills[static_cast<int32>(EAedirSkill::Smelting)], 33.f);
	TestEqual(TEXT("Group id survives round trip"), RestoredCreature.GroupId, FName(TEXT("Wolves")));
	TestEqual(TEXT("Waist slot count survives round trip"), RestoredCreature.WaistItems.Num(), 6);

	if (!TestEqual(TEXT("Loose item count survives round trip"), Restored->LooseItems.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Loose item location survives round trip"), Restored->LooseItems[0].Transform.GetLocation(), FVector(1.f, 2.f, 3.f));

	return true;
}

#endif
