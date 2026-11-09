#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/AedirCombatComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMeleeShoveEnergyTest, "Aedir.Melee.ShoveEnergy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMeleeShoveEnergyTest::RunTest(const FString& Parameters)
{
	const float Full = UAedirCombatComponent::ComputeShoveEnergy(10.f, 1.f);
	TestTrue(TEXT("Strength 10 at full stamina shoves with 36 to 40 J"), Full >= 36.f && Full <= 40.f);

	const float Weak = UAedirCombatComponent::ComputeShoveEnergy(5.f, 1.f);
	const float Strong = UAedirCombatComponent::ComputeShoveEnergy(20.f, 1.f);
	TestTrue(TEXT("Shove energy grows with strength"), Strong > Full && Full > Weak);

	const float Exhausted = UAedirCombatComponent::ComputeShoveEnergy(10.f, 0.f);
	const float Ratio = Exhausted / Full;
	TestTrue(TEXT("Zero stamina roughly halves the shove"), Ratio >= 0.45f && Ratio <= 0.55f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirMeleeKickArcTest, "Aedir.Melee.KickArc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirMeleeKickArcTest::RunTest(const FString& Parameters)
{
	const FVector Start(10.f, 5.f, 0.f);
	const FVector Hip(0.f, 0.f, 90.f);
	const FVector Forward(1.f, 0.f, 0.f);

	TestTrue(TEXT("Arc starts at the foot"), UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.f).Equals(Start, 0.01f));
	TestTrue(TEXT("Arc returns to the foot"), UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 1.f).Equals(Start, 0.01f));

	// The extension is reached at 0.62 of the kick, snapped out quickly after the slow chamber at 0.5.
	const FVector Extension = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.62f);
	TestTrue(TEXT("Full extension is 85 cm ahead of the hip"), FMath::IsNearlyEqual(Extension.X, 85.f, 0.01f) && FMath::IsNearlyEqual(Extension.Y, 0.f, 0.01f));
	TestTrue(TEXT("Full extension is 55 cm above the ground reference"), FMath::IsNearlyEqual(Extension.Z, 55.f, 0.01f));

	const FVector Chamber = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.5f);
	TestTrue(TEXT("Chamber hangs below the hip, the knee drawn up to the belly"), Chamber.Z < Hip.Z && Chamber.Z > Hip.Z - 60.f);
	TestTrue(TEXT("Chamber is just in front of the hip"), Chamber.X > Hip.X && Chamber.X < Hip.X + 40.f);
	TestTrue(TEXT("Chamber is to the right of the hip"), Chamber.Y > Hip.Y);

	return true;
}

#endif
