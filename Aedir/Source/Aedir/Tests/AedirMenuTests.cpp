#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/SAedirMenu.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuSkillRankNamesTest, "Aedir.Menu.SkillRankNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuSkillRankNamesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 is Novice"), UAedirJournalSubsystem::GetSkillRankName(0.f).ToString(), FString(TEXT("Novice")));
	TestEqual(TEXT("24.9 is still Novice"), UAedirJournalSubsystem::GetSkillRankName(24.9f).ToString(), FString(TEXT("Novice")));
	TestEqual(TEXT("25 is Apprentice"), UAedirJournalSubsystem::GetSkillRankName(25.f).ToString(), FString(TEXT("Apprentice")));
	TestEqual(TEXT("49.9 is still Apprentice"), UAedirJournalSubsystem::GetSkillRankName(49.9f).ToString(), FString(TEXT("Apprentice")));
	TestEqual(TEXT("50 is Adept"), UAedirJournalSubsystem::GetSkillRankName(50.f).ToString(), FString(TEXT("Adept")));
	TestEqual(TEXT("74.9 is still Adept"), UAedirJournalSubsystem::GetSkillRankName(74.9f).ToString(), FString(TEXT("Adept")));
	TestEqual(TEXT("75 is Expert"), UAedirJournalSubsystem::GetSkillRankName(75.f).ToString(), FString(TEXT("Expert")));
	TestEqual(TEXT("99.9 is still Expert"), UAedirJournalSubsystem::GetSkillRankName(99.9f).ToString(), FString(TEXT("Expert")));
	TestEqual(TEXT("100 is Master"), UAedirJournalSubsystem::GetSkillRankName(100.f).ToString(), FString(TEXT("Master")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuSkillRankProgressTest, "Aedir.Menu.SkillRankProgress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuSkillRankProgressTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Zero at the start of the first band"), FMath::IsNearlyZero(UAedirJournalSubsystem::GetSkillRankProgress01(0.f)));
	TestTrue(TEXT("Zero at the start of a later band"), FMath::IsNearlyZero(UAedirJournalSubsystem::GetSkillRankProgress01(25.f)));
	TestTrue(TEXT("Half way through a band"), FMath::IsNearlyEqual(UAedirJournalSubsystem::GetSkillRankProgress01(37.5f), 0.5f, 0.001f));
	TestTrue(TEXT("Half way through the Expert band"), FMath::IsNearlyEqual(UAedirJournalSubsystem::GetSkillRankProgress01(87.5f), 0.5f, 0.001f));
	TestTrue(TEXT("One at Master"), FMath::IsNearlyEqual(UAedirJournalSubsystem::GetSkillRankProgress01(100.f), 1.f, 0.001f));
	TestTrue(TEXT("Progress stays within 0..1"), UAedirJournalSubsystem::GetSkillRankProgress01(-5.f) >= 0.f && UAedirJournalSubsystem::GetSkillRankProgress01(150.f) <= 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuFormatWeightTest, "Aedir.Menu.FormatWeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuFormatWeightTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Rounds up to one decimal"), UAedirJournalSubsystem::FormatWeight(3.46f), FString(TEXT("3.5")));
	TestEqual(TEXT("Rounds down to one decimal"), UAedirJournalSubsystem::FormatWeight(3.44f), FString(TEXT("3.4")));
	TestEqual(TEXT("Whole numbers keep one decimal"), UAedirJournalSubsystem::FormatWeight(12.f), FString(TEXT("12.0")));
	TestEqual(TEXT("Zero"), UAedirJournalSubsystem::FormatWeight(0.f), FString(TEXT("0.0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuSkillDisplayNamesTest, "Aedir.Menu.SkillDisplayNames", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuSkillDisplayNamesTest::RunTest(const FString& Parameters)
{
	TSet<FString> UniqueNames;
	for (uint8 Raw = 0; Raw < static_cast<uint8>(EAedirSkill::Count); ++Raw)
	{
		const FString Name = UAedirJournalSubsystem::GetSkillDisplayName(static_cast<EAedirSkill>(Raw));
		TestFalse(TEXT("Skill display name is not empty"), Name.IsEmpty());
		UniqueNames.Add(Name);
	}

	TestEqual(TEXT("All 13 skills have unique names"), UniqueNames.Num(), static_cast<int32>(EAedirSkill::Count));
	TestEqual(TEXT("Ranged skill is Archery"), UAedirJournalSubsystem::GetSkillDisplayName(EAedirSkill::RangedWeapon), FString(TEXT("Archery")));
	TestEqual(TEXT("Metamorphosis is Alteration"), UAedirJournalSubsystem::GetSkillDisplayName(EAedirSkill::MetamorphosisMagic), FString(TEXT("Alteration")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuJournalLogicTest, "Aedir.Menu.JournalLogic", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuJournalLogicTest::RunTest(const FString& Parameters)
{
	UAedirJournalSubsystem* Journal = NewObject<UAedirJournalSubsystem>(GetTransientPackage());
	if (!TestNotNull(TEXT("Journal subsystem can be created without a world"), Journal))
	{
		return false;
	}

	const FName FirstId(TEXT("Menu.TestFirst"));
	const FName SecondId(TEXT("Menu.TestSecond"));

	FAedirQuest First;
	First.Id = FirstId;
	First.Title = FText::FromString(TEXT("First"));
	FAedirQuestObjective FirstObjective;
	FirstObjective.Text = FText::FromString(TEXT("Step one"));
	First.Objectives.Add(FirstObjective);
	FirstObjective.Text = FText::FromString(TEXT("Step two"));
	First.Objectives.Add(FirstObjective);

	Journal->AddQuest(First);
	TestEqual(TEXT("Adding a quest stores it"), Journal->GetQuests().Num(), 1);
	TestEqual(TEXT("The first active quest becomes tracked"), Journal->GetTrackedQuest().ToString(), FirstId.ToString());

	Journal->CompleteObjective(FirstId, 0);
	TestFalse(TEXT("Quest stays open while an objective is left"), Journal->GetQuests()[0].bCompleted);

	Journal->CompleteObjective(FirstId, 7);
	Journal->CompleteObjective(FirstId, 1);
	TestTrue(TEXT("Quest completes when every objective is done"), Journal->GetQuests()[0].bCompleted);
	TestFalse(TEXT("Completed quest is no longer active"), Journal->GetQuests()[0].bActive);
	TestTrue(TEXT("Completing the tracked quest clears tracking"), Journal->GetTrackedQuest().IsNone());

	FAedirQuest Second;
	Second.Id = SecondId;
	Second.Title = FText::FromString(TEXT("Second"));
	FAedirQuestObjective SecondObjective;
	SecondObjective.Text = FText::FromString(TEXT("Only step"));
	Second.Objectives.Add(SecondObjective);

	Journal->AddQuest(Second);
	TestEqual(TEXT("Second quest is added"), Journal->GetQuests().Num(), 2);
	Journal->SetActiveQuest(SecondId);
	TestEqual(TEXT("SetActiveQuest tracks an active quest"), Journal->GetTrackedQuest().ToString(), SecondId.ToString());

	Journal->FailQuest(SecondId);
	TestFalse(TEXT("Failed quest is no longer active"), Journal->GetQuests()[1].bActive);
	TestTrue(TEXT("Failing the tracked quest clears tracking"), Journal->GetTrackedQuest().IsNone());

	Journal->AddQuest(Second);
	TestEqual(TEXT("Adding the same Id replaces the quest"), Journal->GetQuests().Num(), 2);

	Journal->RemoveQuest(SecondId);
	TestEqual(TEXT("RemoveQuest deletes the quest"), Journal->GetQuests().Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuBackpackWeightTest, "Aedir.Menu.BackpackWeight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuBackpackWeightTest::RunTest(const FString& Parameters)
{
	UAedirInteractionComponent* Interaction = NewObject<UAedirInteractionComponent>(GetTransientPackage());
	AAedirItemActor* Sword = NewObject<AAedirItemActor>(GetTransientPackage());
	AAedirItemActor* Potion = NewObject<AAedirItemActor>(GetTransientPackage());
	Sword->Weight = 3.5f;
	Potion->Weight = 0.5f;

	TestTrue(TEXT("Empty backpack weighs nothing"), FMath::IsNearlyZero(Interaction->GetCarriedWeight()));

	Interaction->Backpack.Add(Sword);
	Interaction->Backpack.Add(Potion);
	TestTrue(TEXT("Stowed items count towards the carried weight"), FMath::IsNearlyEqual(Interaction->GetCarriedWeight(), 4.f, 0.001f));
	TestEqual(TEXT("Backpack copy keeps every entry"), Interaction->GetBackpackItemsCopy().Num(), 2);

	Interaction->Backpack.RemoveAt(0);
	TestTrue(TEXT("Taking an item out lowers the weight"), FMath::IsNearlyEqual(Interaction->GetCarriedWeight(), 0.5f, 0.001f));
	TestEqual(TEXT("Remaining entry is the potion"), Interaction->GetBackpackItemsCopy()[0], Potion);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMenuRowEqualityTest, "Aedir.Menu.RowEquality", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMenuRowEqualityTest::RunTest(const FString& Parameters)
{
	FAedirMenuRow Waist;
	Waist.Index = 1;
	FAedirMenuRow Stowed = Waist;
	TestTrue(TEXT("Identical rows are equal"), Waist == Stowed);

	Stowed.bIsBackpack = true;
	TestFalse(TEXT("A backpack row differs from a waist row with the same index"), Waist == Stowed);

	return true;
}

#endif
