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

	// The extension is reached at 0.62 of the kick, swung up quickly after the slower wind-up at 0.42.
	const FVector Extension = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.62f);
	TestTrue(TEXT("Full extension is 85 cm ahead of the hip"), FMath::IsNearlyEqual(Extension.X, 85.f, 0.01f) && FMath::IsNearlyEqual(Extension.Y, 0.f, 0.01f));
	TestTrue(TEXT("Full extension is 80 cm above the ground reference"), FMath::IsNearlyEqual(Extension.Z, 80.f, 0.01f));

	const FVector WindUp = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.42f);
	TestTrue(TEXT("The wind-up draws the foot back of the hip"), WindUp.X < Hip.X);
	TestTrue(TEXT("The wind-up lifts the foot off the ground"), WindUp.Z > Start.Z + 10.f && WindUp.Z < Hip.Z);

	// On the way the foot swings round the hip, rising through the front: never pushed straight out, never into the ground.
	bool bAboveGround = true;
	for (float T = 0.44f; T <= 0.62f; T += 0.02f)
	{
		const FVector Foot = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, T);
		const float Below = static_cast<float>((Foot - Hip).Size());
		bAboveGround &= Foot.Z > Start.Z + 5.f && Below > 60.f;
	}
	const FVector Midway = UAedirCombatComponent::EvaluateKickArc(Start, Hip, Forward, 0.56f);
	TestTrue(TEXT("Halfway through the swing the foot is in front of the hip and below it"), Midway.X > Hip.X && Midway.Z < Hip.Z);
	TestTrue(TEXT("The swing keeps the leg's length and off the ground"), bAboveGround);
	TestTrue(TEXT("The swing ends higher than it started"), Extension.Z > Midway.Z && Midway.Z > WindUp.Z);

	return true;
}

#endif
