#include "Creature/AedirCreatureTypes.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "Net/AedirSaveGame.h"
#include "Net/AedirSaveSubsystem.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSaveSlotNamingTest, "Aedir.Save.SlotNaming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSaveSlotNamingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("First slot name"), AedirSaveSlots::MakeSlotName(1), FString(TEXT("Save_001")));
	TestEqual(TEXT("Numbers above 999 keep every digit"), AedirSaveSlots::MakeSlotName(1234), FString(TEXT("Save_1234")));

	TestEqual(TEXT("Numbered slot parses"), AedirSaveSlots::ParseSlotNumber(TEXT("Save_012")), 12);
	TestEqual(TEXT("Parsing undoes naming"), AedirSaveSlots::ParseSlotNumber(AedirSaveSlots::MakeSlotName(77)), 77);
	TestEqual(TEXT("Quick save is not numbered"), AedirSaveSlots::ParseSlotNumber(AedirSaveSlots::QuickSaveSlot), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("The old fixed slot is not numbered"), AedirSaveSlots::ParseSlotNumber(TEXT("Save1")), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Missing digits"), AedirSaveSlots::ParseSlotNumber(TEXT("Save_")), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Letters after the prefix"), AedirSaveSlots::ParseSlotNumber(TEXT("Save_abc")), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Negative numbers"), AedirSaveSlots::ParseSlotNumber(TEXT("Save_-3")), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Zero is not a slot"), AedirSaveSlots::ParseSlotNumber(TEXT("Save_000")), static_cast<int32>(INDEX_NONE));

	TestEqual(TEXT("No saves starts at one"), AedirSaveSlots::NextSlotName({}), FString(TEXT("Save_001")));
	TestEqual(TEXT("Other slots do not count"), AedirSaveSlots::NextSlotName({ TEXT("QuickSave"), TEXT("Save1") }), FString(TEXT("Save_001")));
	TestEqual(TEXT("Next is above the highest"), AedirSaveSlots::NextSlotName({ TEXT("Save_002"), TEXT("Save_007"), TEXT("QuickSave") }), FString(TEXT("Save_008")));
	TestEqual(TEXT("A gap is not reused"), AedirSaveSlots::NextSlotName({ TEXT("Save_001"), TEXT("Save_003") }), FString(TEXT("Save_004")));

	TestEqual(TEXT("Save file gives its slot"), AedirSaveSlots::SlotFromFileName(TEXT("Save_001.sav")), FString(TEXT("Save_001")));
	TestEqual(TEXT("Extension case does not matter"), AedirSaveSlots::SlotFromFileName(TEXT("QuickSave.SAV")), FString(TEXT("QuickSave")));
	TestTrue(TEXT("Other files are ignored"), AedirSaveSlots::SlotFromFileName(TEXT("Save_001.bak")).IsEmpty());

	TestEqual(TEXT("Quick save display name"), AedirSaveSlots::MakeDisplayName(AedirSaveSlots::QuickSaveSlot), FString(TEXT("Quick Save")));
	TestEqual(TEXT("Numbered display name drops the zeros"), AedirSaveSlots::MakeDisplayName(TEXT("Save_012")), FString(TEXT("Save 12")));
	TestEqual(TEXT("Unknown slots keep their name"), AedirSaveSlots::MakeDisplayName(TEXT("Save1")), FString(TEXT("Save1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSaveListOrderTest, "Aedir.Save.ListOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSaveListOrderTest::RunTest(const FString& Parameters)
{
	const auto MakeInfo = [](const TCHAR* Slot, int32 Day)
	{
		FAedirSaveInfo Info;
		Info.Slot = Slot;
		Info.Timestamp = FDateTime(2026, 11, Day, 12, 0, 0);
		return Info;
	};

	TArray<FAedirSaveInfo> Saves = { MakeInfo(TEXT("Save_001"), 1), MakeInfo(TEXT("QuickSave"), 3), MakeInfo(TEXT("Save_002"), 2), MakeInfo(TEXT("Save_003"), 2) };
	AedirSaveSlots::SortNewestFirst(Saves);

	TestEqual(TEXT("Newest save first"), Saves[0].Slot, FString(TEXT("QuickSave")));
	TestEqual(TEXT("Same time: higher slot first"), Saves[1].Slot, FString(TEXT("Save_003")));
	TestEqual(TEXT("Same time: lower slot second"), Saves[2].Slot, FString(TEXT("Save_002")));
	TestEqual(TEXT("Oldest save last"), Saves[3].Slot, FString(TEXT("Save_001")));

	TestEqual(TEXT("Summary with level and place"), AedirSaveSlots::MakeSummary(3, TEXT("Riverwood")), FString(TEXT("Level 3  -  Riverwood")));
	TestEqual(TEXT("Summary with level only"), AedirSaveSlots::MakeSummary(3, FString()), FString(TEXT("Level 3")));
	TestEqual(TEXT("Summary of an old save"), AedirSaveSlots::MakeSummary(0, TEXT("Riverwood")), FString(TEXT("Riverwood")));
	TestTrue(TEXT("Summary without data"), AedirSaveSlots::MakeSummary(0, FString()).IsEmpty());

	TestEqual(TEXT("Timestamp format"), AedirSaveSlots::FormatTimestamp(FDateTime(2026, 11, 6, 9, 5, 0)), FString(TEXT("2026-11-06  09:05")));

	return true;
}

#endif
