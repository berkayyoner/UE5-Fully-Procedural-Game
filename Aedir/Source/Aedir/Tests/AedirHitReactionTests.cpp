#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Physics/AedirBalanceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirHitReactionBalanceLossTest, "Aedir.HitReaction.BalanceLoss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirHitReactionBalanceLossTest::RunTest(const FString& Parameters)
{
	const float Overkill = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Head, 1000.f, 0.f, 1.f);
	const float Nothing = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, -5.f, 0.f, 1.f);
	const float Middle = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 0.f, 1.f);

	TestTrue(TEXT("Loss never exceeds 1"), Overkill <= 1.f && Overkill >= 0.f);
	TestTrue(TEXT("Negative energy gives no loss"), FMath::IsNearlyZero(Nothing));
	TestTrue(TEXT("Mid energy gives a loss between 0 and 1"), Middle > 0.f && Middle < 1.f);

	const float Weak = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 10.f, 0.f, 1.f);
	const float Strong = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 0.f, 1.f);
	TestTrue(TEXT("Loss grows with energy"), Strong > Weak);

	const float LowAthletism = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 0.f, 1.f);
	const float HighAthletism = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 100.f, 1.f);
	TestTrue(TEXT("High athletism lowers loss"), HighAthletism < LowAthletism);

	const float FreshStamina = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 0.f, 1.f);
	const float ExhaustedStamina = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 30.f, 0.f, 0.f);
	TestTrue(TEXT("Low stamina raises loss"), ExhaustedStamina > FreshStamina);

	const float HeadLoss = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Head, 20.f, 0.f, 1.f);
	const float TorsoLoss = UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone::Torso, 20.f, 0.f, 1.f);
	TestTrue(TEXT("Head loses more balance than torso for the same energy"), HeadLoss > TorsoLoss);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirHitReactionStaggerThresholdTest, "Aedir.HitReaction.StaggerThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirHitReactionStaggerThresholdTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Loss below 0.15 does not stagger"), UAedirBalanceComponent::ShouldStagger(0.14f));
	TestTrue(TEXT("Loss at 0.15 staggers"), UAedirBalanceComponent::ShouldStagger(0.15f));
	TestTrue(TEXT("Full loss staggers"), UAedirBalanceComponent::ShouldStagger(1.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirHitReactionKnockdownOrderTest, "Aedir.HitReaction.KnockdownOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirHitReactionKnockdownOrderTest::RunTest(const FString& Parameters)
{
	const float Leg = UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone::Leg);
	const float Head = UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone::Head);
	const float Torso = UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone::Torso);
	const float Arm = UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone::Arm);

	TestTrue(TEXT("Leg is knocked down most easily"), Leg < Head);
	TestTrue(TEXT("Head is knocked down before torso"), Head < Torso);
	TestTrue(TEXT("Torso is knocked down before arm"), Torso < Arm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirHitReactionDazeTest, "Aedir.HitReaction.Daze", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirHitReactionDazeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Only head hits daze"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Torso, 1000.f)));
	TestTrue(TEXT("Arm hits do not daze"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Arm, 1000.f)));
	TestTrue(TEXT("Leg hits do not daze"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Leg, 1000.f)));

	const float Light = UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Head, 15.f);
	const float Heavy = UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Head, 30.f);
	TestTrue(TEXT("Daze grows with head energy"), Heavy > Light && Light > 0.f);

	const float Capped = UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone::Head, 1000.f);
	TestTrue(TEXT("Daze is capped at 2.5 seconds"), FMath::IsNearlyEqual(Capped, 2.5f, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirHitReactionStaggerSpeedTest, "Aedir.HitReaction.StaggerSpeed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirHitReactionStaggerSpeedTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("No energy gives no push"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeStaggerSpeed(0.f)));
	TestTrue(TEXT("Push grows with energy"), UAedirBalanceComponent::ComputeStaggerSpeed(100.f) > UAedirBalanceComponent::ComputeStaggerSpeed(50.f));
	TestTrue(TEXT("Push is capped at 300 cm/s"), FMath::IsNearlyEqual(UAedirBalanceComponent::ComputeStaggerSpeed(10000.f), 300.f, 0.001f));

	return true;
}

#endif
