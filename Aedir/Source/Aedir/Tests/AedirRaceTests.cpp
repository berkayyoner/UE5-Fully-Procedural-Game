#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Creature/AedirRaceCatalog.h"
#include "Misc/AutomationTest.h"

namespace
{
	const FAedirRaceDef* RaceTestsFind(const TCHAR* Id)
	{
		return UAedirRaceCatalog::Find(FName(Id));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceRosterTest, "Aedir.Race.Roster", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceRosterTest::RunTest(const FString& Parameters)
{
	const TArray<FAedirRaceDef>& Races = UAedirRaceCatalog::GetAll();
	TestEqual(TEXT("Exactly four races"), Races.Num(), 4);

	TSet<FName> UniqueIds;
	for (const FAedirRaceDef& Race : Races)
	{
		UniqueIds.Add(Race.Id);
	}
	TestEqual(TEXT("Race ids are unique"), UniqueIds.Num(), 4);
	TestTrue(TEXT("Human is listed"), UniqueIds.Contains(FName(TEXT("Human"))));
	TestTrue(TEXT("Elf is listed"), UniqueIds.Contains(FName(TEXT("Elf"))));
	TestTrue(TEXT("DarkElf is listed"), UniqueIds.Contains(FName(TEXT("DarkElf"))));
	TestTrue(TEXT("Dwarf is listed"), UniqueIds.Contains(FName(TEXT("Dwarf"))));

	TestEqual(TEXT("GetIds lists every race"), UAedirRaceCatalog::GetIds().Num(), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceFindTest, "Aedir.Race.Find", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceFindTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Unknown race is not found"), RaceTestsFind(TEXT("Orc")) == nullptr);
	TestTrue(TEXT("Empty id is not found"), UAedirRaceCatalog::Find(NAME_None) == nullptr);

	const FAedirRaceDef* Dwarf = RaceTestsFind(TEXT("Dwarf"));
	TestTrue(TEXT("Dwarf is found"), Dwarf != nullptr);
	if (Dwarf)
	{
		TestEqual(TEXT("Find returns the Dwarf definition"), Dwarf->Id.ToString(), FString(TEXT("Dwarf")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceStatsTest, "Aedir.Race.Stats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceStatsTest::RunTest(const FString& Parameters)
{
	const FAedirRaceDef* Human = RaceTestsFind(TEXT("Human"));
	const FAedirRaceDef* Elf = RaceTestsFind(TEXT("Elf"));
	const FAedirRaceDef* DarkElf = RaceTestsFind(TEXT("DarkElf"));
	const FAedirRaceDef* Dwarf = RaceTestsFind(TEXT("Dwarf"));
	if (!TestNotNull(TEXT("Human exists"), Human) || !TestNotNull(TEXT("Elf exists"), Elf)
		|| !TestNotNull(TEXT("DarkElf exists"), DarkElf) || !TestNotNull(TEXT("Dwarf exists"), Dwarf))
	{
		return false;
	}

	const TArray<const FAedirRaceDef*> All = { Human, Elf, DarkElf, Dwarf };
	for (const FAedirRaceDef* Other : All)
	{
		if (Other != Dwarf)
		{
			TestTrue(TEXT("Dwarf has the highest Strength"), Dwarf->Strength > Other->Strength);
			TestTrue(TEXT("Dwarf has the smallest height"), Dwarf->HeightMultiplier < Other->HeightMultiplier);
		}
		if (Other != Elf)
		{
			TestTrue(TEXT("Elf has the highest MaxMana"), Elf->MaxMana > Other->MaxMana);
			TestTrue(TEXT("Elf has the largest height"), Elf->HeightMultiplier > Other->HeightMultiplier);
		}
		TestTrue(TEXT("DarkElf has Athletism at least as high as every race"), DarkElf->Athletism >= Other->Athletism);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceDescriptionTest, "Aedir.Race.Descriptions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceDescriptionTest::RunTest(const FString& Parameters)
{
	for (const FAedirRaceDef& Race : UAedirRaceCatalog::GetAll())
	{
		TestFalse(TEXT("Race has a display name"), Race.DisplayName.IsEmpty());
		TestFalse(TEXT("Race has a description"), Race.Description.IsEmpty());
		TestTrue(TEXT("Race has at least three skill bonuses"), Race.SkillBonuses.Num() >= 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceSizeScaleTest, "Aedir.Race.SizeScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceSizeScaleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Elf height scales a neutral strength size"), FMath::IsNearlyEqual(UAedirRaceCatalog::ComputeRaceSizeScale(1.f, 1.08f), 1.08f, 0.0001f));
	TestTrue(TEXT("Size is clamped to the lower bound"), FMath::IsNearlyEqual(UAedirRaceCatalog::ComputeRaceSizeScale(0.1f, 0.1f), 0.6f, 0.0001f));
	TestTrue(TEXT("Size is clamped to the upper bound"), FMath::IsNearlyEqual(UAedirRaceCatalog::ComputeRaceSizeScale(2.f, 2.f), 1.5f, 0.0001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirRaceStartSkillTest, "Aedir.Race.StartSkills", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirRaceStartSkillTest::RunTest(const FString& Parameters)
{
	const FAedirRaceDef* Dwarf = RaceTestsFind(TEXT("Dwarf"));
	const FAedirRaceDef* Human = RaceTestsFind(TEXT("Human"));
	if (!TestNotNull(TEXT("Dwarf exists"), Dwarf) || !TestNotNull(TEXT("Human exists"), Human))
	{
		return false;
	}

	TestEqual(TEXT("Dwarf Smelting is 10"), UAedirRaceCatalog::GetStartSkillValue(*Dwarf, EAedirSkill::Smelting), 10.f);
	TestEqual(TEXT("Human Smelting is 5"), UAedirRaceCatalog::GetStartSkillValue(*Human, EAedirSkill::Smelting), 5.f);
	TestEqual(TEXT("Human IllusionMagic is 0"), UAedirRaceCatalog::GetStartSkillValue(*Human, EAedirSkill::IllusionMagic), 0.f);

	return true;
}

#endif
