#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "Creature/AedirStatsComponent.h"

namespace
{
	UAedirStatsComponent* AedirStatsTestCreateComponent()
	{
		return NewObject<UAedirStatsComponent>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsDefaultsTest, "Aedir.Stats.Defaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsDefaultsTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	const int32 ExpectedSkillCount = static_cast<int32>(EAedirSkill::Count);
	TestEqual(TEXT("Skills array has one entry per EAedirSkill"), Stats->Skills.Num(), ExpectedSkillCount);

	for (int32 Index = 0; Index < ExpectedSkillCount; ++Index)
	{
		TestEqual(TEXT("Skill starts at zero"), Stats->Skills[Index], 0.f);
	}

	TestEqual(TEXT("Skill lookup for a valid skill returns zero"), Stats->GetSkill(EAedirSkill::Enchanting), 0.f);
	TestFalse(TEXT("Component starts alive"), Stats->IsDead());
	TestEqual(TEXT("Health starts at max health"), Stats->GetHealth(), Stats->GetMaxHealth());
	TestEqual(TEXT("Mana starts at max mana"), Stats->GetMana(), Stats->GetMaxMana());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsFractionsTest, "Aedir.Stats.Fractions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsFractionsTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	Stats->Health = 50.f;
	Stats->MaxHealth = 100.f;
	TestEqual(TEXT("Health fraction is half of max"), Stats->GetHealthFraction(), 0.5f);

	Stats->Mana = 25.f;
	Stats->MaxMana = 100.f;
	TestEqual(TEXT("Mana fraction is a quarter of max"), Stats->GetManaFraction(), 0.25f);

	Stats->Stamina = 75.f;
	Stats->MaxStamina = 150.f;
	TestEqual(TEXT("Stamina fraction is half of max"), Stats->GetStaminaFraction(), 0.5f);

	Stats->MaxHealth = 0.f;
	TestEqual(TEXT("Health fraction with zero max is zero"), Stats->GetHealthFraction(), 0.f);

	Stats->MaxMana = 0.f;
	TestEqual(TEXT("Mana fraction with zero max is zero"), Stats->GetManaFraction(), 0.f);

	Stats->MaxStamina = 0.f;
	TestEqual(TEXT("Stamina fraction with zero max is zero"), Stats->GetStaminaFraction(), 0.f);

	const float FullHealth = 100.f;
	Stats->MaxHealth = FullHealth;
	Stats->Health = FullHealth;
	TestEqual(TEXT("Full health fraction is one"), Stats->GetHealthFraction(), 1.f);

	Stats->Health = 0.f;
	TestEqual(TEXT("Empty health fraction is zero"), Stats->GetHealthFraction(), 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsSizeScaleTest, "Aedir.Stats.SizeScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsSizeScaleTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	Stats->Strength = Stats->StrengthMin;
	TestEqual(TEXT("Minimum strength yields minimum size scale"), Stats->GetSizeScale(), Stats->MinSizeScale);

	Stats->Strength = Stats->StrengthMax;
	TestEqual(TEXT("Maximum strength yields maximum size scale"), Stats->GetSizeScale(), Stats->MaxSizeScale);

	Stats->Strength = Stats->StrengthMax * 10.f;
	TestEqual(TEXT("Strength above the range clamps to maximum size scale"), Stats->GetSizeScale(), Stats->MaxSizeScale);

	const float Strengths[] = { 1.f, 10.f, 25.f, 50.f, 75.f, 100.f };
	float PreviousScale = -1.f;
	for (const float Strength : Strengths)
	{
		Stats->Strength = Strength;
		const float Scale = Stats->GetSizeScale();

		TestTrue(TEXT("Size scale grows with strength"), Scale > PreviousScale);
		TestTrue(TEXT("Size scale stays within configured bounds"), Scale >= Stats->MinSizeScale && Scale <= Stats->MaxSizeScale);
		PreviousScale = Scale;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsMovementMultiplierTest, "Aedir.Stats.MovementMultipliers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsMovementMultiplierTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	Stats->Athletism = 0.f;
	TestEqual(TEXT("Zero athletism gives no speed bonus"), Stats->GetSpeedMultiplier(), 1.f);
	TestEqual(TEXT("Zero athletism gives no jump bonus"), Stats->GetJumpHeightMultiplier(), 1.f);

	Stats->Athletism = 50.f;
	TestEqual(TEXT("Half athletism gives a 25 percent speed bonus"), Stats->GetSpeedMultiplier(), 1.25f);
	TestEqual(TEXT("Half athletism gives a 20 percent jump bonus"), Stats->GetJumpHeightMultiplier(), 1.2f);

	Stats->Athletism = 100.f;
	TestEqual(TEXT("Max athletism gives a 50 percent speed bonus"), Stats->GetSpeedMultiplier(), 1.5f);
	TestEqual(TEXT("Max athletism gives a 40 percent jump bonus"), Stats->GetJumpHeightMultiplier(), 1.4f);

	const float Athletisms[] = { 0.f, 25.f, 50.f, 75.f, 100.f };
	float PreviousSpeed = 0.f;
	float PreviousJump = 0.f;
	for (const float Athletism : Athletisms)
	{
		Stats->Athletism = Athletism;
		TestTrue(TEXT("Speed multiplier grows with athletism"), Stats->GetSpeedMultiplier() > PreviousSpeed || Athletism == 0.f);
		TestTrue(TEXT("Jump multiplier grows with athletism"), Stats->GetJumpHeightMultiplier() > PreviousJump || Athletism == 0.f);
		PreviousSpeed = Stats->GetSpeedMultiplier();
		PreviousJump = Stats->GetJumpHeightMultiplier();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsCarryCapacityTest, "Aedir.Stats.CarryCapacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsCarryCapacityTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	Stats->Strength = 10.f;
	const float LowCapacity = Stats->GetCarryCapacity();
	TestEqual(TEXT("Carry capacity is two per strength point"), LowCapacity, 20.f);

	Stats->Strength = 40.f;
	const float HighCapacity = Stats->GetCarryCapacity();
	TestEqual(TEXT("Carry capacity scales with strength"), HighCapacity, 80.f);
	TestTrue(TEXT("Higher strength carries more"), HighCapacity > LowCapacity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirStatsAuthorityTest, "Aedir.Stats.MutatorsRequireAuthority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirStatsAuthorityTest::RunTest(const FString& Parameters)
{
	UAedirStatsComponent* Stats = AedirStatsTestCreateComponent();
	if (!TestTrue(TEXT("Stats component is created"), Stats != nullptr))
	{
		return false;
	}

	Stats->Health = 40.f;
	Stats->Mana = 40.f;
	Stats->Stamina = 40.f;
	Stats->Strength = 10.f;
	Stats->Athletism = 10.f;

	TestEqual(TEXT("ApplyDamage without an owner applies nothing"), Stats->ApplyDamage(25.f), 0.f);
	TestEqual(TEXT("Health is unchanged after rejected damage"), Stats->GetHealth(), 40.f);

	Stats->Heal(10.f);
	TestEqual(TEXT("Heal without an owner is ignored"), Stats->GetHealth(), 40.f);

	TestFalse(TEXT("ConsumeStamina without an owner fails"), Stats->ConsumeStamina(10.f));
	TestEqual(TEXT("Stamina is unchanged after rejected consume"), Stats->GetStamina(), 40.f);

	TestFalse(TEXT("ConsumeMana without an owner fails"), Stats->ConsumeMana(10.f));
	TestEqual(TEXT("Mana is unchanged after rejected consume"), Stats->GetMana(), 40.f);

	Stats->SetStrength(50.f);
	TestEqual(TEXT("Strength is unchanged by SetStrength without authority"), Stats->GetCarryCapacity(), 20.f);

	Stats->SetAthletism(50.f);
	TestEqual(TEXT("Athletism is unchanged by SetAthletism without authority"), Stats->GetSpeedMultiplier(), 1.05f);

	Stats->AddSkillExperience(EAedirSkill::Smelting, 20.f);
	TestEqual(TEXT("Skill experience is ignored without authority"), Stats->GetSkill(EAedirSkill::Smelting), 0.f);

	return true;
}

#endif
