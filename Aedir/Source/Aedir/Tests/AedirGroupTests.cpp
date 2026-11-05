#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "Creature/AedirGroupSettings.h"
#include "Creature/AedirGroupSubsystem.h"

namespace
{
	UAedirGroupSubsystem* AedirGroupTestCreateSubsystem()
	{
		return NewObject<UAedirGroupSubsystem>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirGroupSameGroupTest, "Aedir.Groups.SameGroup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirGroupSameGroupTest::RunTest(const FString& Parameters)
{
	UAedirGroupSubsystem* Groups = AedirGroupTestCreateSubsystem();
	const UAedirGroupSettings* Settings = GetDefault<UAedirGroupSettings>();
	if (!TestTrue(TEXT("Group subsystem is created"), Groups != nullptr) || !TestTrue(TEXT("Group settings default object is available"), Settings != nullptr))
	{
		return false;
	}

	const FName Wolves(TEXT("Wolves"));
	const FName Goblins(TEXT("Goblins"));

	TestTrue(TEXT("Wolves relation to themselves is SameGroupRelation"), Groups->GetRelation(Wolves, Wolves) == Settings->SameGroupRelation);
	TestTrue(TEXT("Goblins relation to themselves is SameGroupRelation"), Groups->GetRelation(Goblins, Goblins) == Settings->SameGroupRelation);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirGroupNoneTest, "Aedir.Groups.NoneGroup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirGroupNoneTest::RunTest(const FString& Parameters)
{
	UAedirGroupSubsystem* Groups = AedirGroupTestCreateSubsystem();
	const UAedirGroupSettings* Settings = GetDefault<UAedirGroupSettings>();
	if (!TestTrue(TEXT("Group subsystem is created"), Groups != nullptr) || !TestTrue(TEXT("Group settings default object is available"), Settings != nullptr))
	{
		return false;
	}

	const FName Wolves(TEXT("Wolves"));
	const FName NoGroup = NAME_None;

	TestTrue(TEXT("None against a group resolves to DefaultRelation"), Groups->GetRelation(NoGroup, Wolves) == Settings->DefaultRelation);
	TestTrue(TEXT("Group against None resolves to DefaultRelation"), Groups->GetRelation(Wolves, NoGroup) == Settings->DefaultRelation);
	TestTrue(TEXT("None against None resolves to DefaultRelation"), Groups->GetRelation(NoGroup, NoGroup) == Settings->DefaultRelation);
	TestTrue(TEXT("None against a group is enemy only when DefaultRelation is enemy"), Groups->AreEnemies(NoGroup, Wolves) == (Settings->DefaultRelation == EAedirRelation::Enemy));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirGroupSymmetryTest, "Aedir.Groups.Symmetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirGroupSymmetryTest::RunTest(const FString& Parameters)
{
	UAedirGroupSubsystem* Groups = AedirGroupTestCreateSubsystem();
	if (!TestTrue(TEXT("Group subsystem is created"), Groups != nullptr))
	{
		return false;
	}

	const FName Wolves(TEXT("Wolves"));
	const FName Goblins(TEXT("Goblins"));
	const FName Villagers(TEXT("Villagers"));
	const FName Undead(TEXT("Undead"));
	const FName NoGroup = NAME_None;

	const FName Names[] = { Wolves, Goblins, Villagers, Undead, NoGroup };
	for (const FName A : Names)
	{
		for (const FName B : Names)
		{
			TestTrue(TEXT("GetRelation is symmetric"), Groups->GetRelation(A, B) == Groups->GetRelation(B, A));
			TestTrue(TEXT("AreEnemies is symmetric"), Groups->AreEnemies(A, B) == Groups->AreEnemies(B, A));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirGroupEnemyTest, "Aedir.Groups.AreEnemies", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirGroupEnemyTest::RunTest(const FString& Parameters)
{
	UAedirGroupSubsystem* Groups = AedirGroupTestCreateSubsystem();
	const UAedirGroupSettings* Settings = GetDefault<UAedirGroupSettings>();
	if (!TestTrue(TEXT("Group subsystem is created"), Groups != nullptr) || !TestTrue(TEXT("Group settings default object is available"), Settings != nullptr))
	{
		return false;
	}

	const FName Wolves(TEXT("Wolves"));
	const FName Goblins(TEXT("Goblins"));

	TestTrue(TEXT("AreEnemies for the same group matches SameGroupRelation"), Groups->AreEnemies(Wolves, Wolves) == (Settings->SameGroupRelation == EAedirRelation::Enemy));
	TestTrue(TEXT("AreEnemies for distinct groups matches GetRelation"), Groups->AreEnemies(Wolves, Goblins) == (Groups->GetRelation(Wolves, Goblins) == EAedirRelation::Enemy));

	return true;
}

#endif
