#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Wounds/AedirWoundComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundClassifyTest, "Aedir.Wounds.Classify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundClassifyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Blunt makes bruises"), UAedirWoundComponent::ClassifyWound(EAedirDamageType::Blunt) == EAedirWoundType::Bruise);
	TestTrue(TEXT("Slash makes cuts"), UAedirWoundComponent::ClassifyWound(EAedirDamageType::Slash) == EAedirWoundType::Cut);
	TestTrue(TEXT("Pierce makes punctures"), UAedirWoundComponent::ClassifyWound(EAedirDamageType::Pierce) == EAedirWoundType::Puncture);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundShouldCreateTest, "Aedir.Wounds.ShouldCreate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundShouldCreateTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Weak slash makes no cut"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Slash, 5.f, 0.f));
	TestTrue(TEXT("Strong slash cuts bare skin"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Slash, 20.f, 0.f));
	TestFalse(TEXT("Heavy armor blocks a slash cut"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Slash, 20.f, 0.8f));

	TestFalse(TEXT("Weak blunt hit makes no bruise"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Blunt, 5.f, 0.f));
	TestTrue(TEXT("Strong blunt hit through light armor bruises"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Blunt, 20.f, 0.3f));
	TestFalse(TEXT("Blunt hit through very heavy armor leaves no bruise"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Blunt, 20.f, 0.9f));

	TestFalse(TEXT("Weak pierce makes no puncture"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Pierce, 7.f, 0.f));
	TestTrue(TEXT("Pierce through light armor punctures"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Pierce, 9.f, 0.5f));
	TestFalse(TEXT("Pierce through heavy armor does not puncture"), UAedirWoundComponent::ShouldCreateWound(EAedirDamageType::Pierce, 20.f, 0.75f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundSeverityTest, "Aedir.Wounds.Severity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundSeverityTest::RunTest(const FString& Parameters)
{
	const float Low = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Blunt, 10.f, 0.f);
	const float Mid = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Blunt, 30.f, 0.f);
	const float High = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Blunt, 60.f, 0.f);

	TestTrue(TEXT("Severity grows with energy"), Low < Mid && Mid < High);

	const float Unarmored = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Slash, 40.f, 0.f);
	const float Armored = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Slash, 40.f, 0.5f);
	TestTrue(TEXT("Armor reduces severity"), Armored < Unarmored);

	const float Saturated = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Blunt, 100000.f, 0.f);
	TestTrue(TEXT("Severity saturates at 1"), FMath::IsNearlyEqual(Saturated, 1.f, 0.0001f));

	const float Clamped = UAedirWoundComponent::ComputeSeverity(EAedirDamageType::Pierce, 100000.f, 0.9f);
	TestTrue(TEXT("Severity stays within 0..1 with heavy armor"), Clamped >= 0.f && Clamped <= 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundClotTimeTest, "Aedir.Wounds.ClotTime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundClotTimeTest::RunTest(const FString& Parameters)
{
	const float Bruise = UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Bruise, 0.5f);
	const float Cut = UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Cut, 0.5f);
	const float Puncture = UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Puncture, 0.5f);

	TestTrue(TEXT("Bruises never bleed, so clot time is zero"), FMath::IsNearlyZero(Bruise));
	TestTrue(TEXT("Cuts clot before punctures"), Cut > Bruise && Puncture > Cut);

	TestTrue(TEXT("Cut clot time grows with severity"), UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Cut, 0.9f) > UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Cut, 0.1f));
	TestTrue(TEXT("Puncture clot time grows with severity"), UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Puncture, 0.9f) > UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Puncture, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundBleedRateTest, "Aedir.Wounds.BleedRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundBleedRateTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Bruises do not bleed"), FMath::IsNearlyZero(UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Bruise, 0.8f, 0.f)));

	const float CutAtStart = UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, 0.f);
	TestTrue(TEXT("Fresh cut bleeds"), CutAtStart > 0.f);

	const float ClotSeconds = UAedirWoundComponent::GetClotSeconds(EAedirWoundType::Cut, 0.5f);
	const float Early = UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, 5.f);
	const float Later = UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, 20.f);
	const float Latest = UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, 40.f);
	TestTrue(TEXT("Bleeding slows down with age"), CutAtStart > Early && Early > Later && Later > Latest);
	TestTrue(TEXT("Bleeding is still running before the clot time"), Latest > 0.f && 40.f < ClotSeconds);

	TestTrue(TEXT("Bleeding is exactly zero at the clot time"), UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, ClotSeconds) == 0.f);
	TestTrue(TEXT("Bleeding stays zero after the clot time"), UAedirWoundComponent::ComputeBleedRate(EAedirWoundType::Cut, 0.5f, ClotSeconds + 500.f) == 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirWoundAge01Test, "Aedir.Wounds.Age01", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirWoundAge01Test::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Negative age clamps to zero"), FMath::IsNearlyZero(UAedirWoundComponent::ComputeAge01(EAedirWoundType::Cut, 0.5f, -10.f)));
	TestTrue(TEXT("Very old wound clamps to one"), FMath::IsNearlyEqual(UAedirWoundComponent::ComputeAge01(EAedirWoundType::Cut, 0.5f, 1000000.f), 1.f, 0.0001f));

	const float Young = UAedirWoundComponent::ComputeAge01(EAedirWoundType::Cut, 0.5f, 10.f);
	const float Older = UAedirWoundComponent::ComputeAge01(EAedirWoundType::Cut, 0.5f, 100.f);
	TestTrue(TEXT("Age01 grows with age"), Young < Older);
	TestTrue(TEXT("Age01 stays within 0..1"), Young >= 0.f && Older <= 1.f);

	return true;
}

#endif
