#include "Misc/AutomationTest.h"
#include "Items/AedirInteractionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirReachPostureTest, "Aedir.Items.ReachPosture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirReachPostureTest::RunTest(const FString& Parameters)
{
	const FAedirReachPosture AtHand = UAedirInteractionComponent::ComputeReachPosture(30.f, 0.f, 130.f);
	TestEqual(TEXT("Something at shoulder height needs no bend"), AtHand.LowerCm, 0.f);
	TestEqual(TEXT("Something at shoulder height needs no lean"), AtHand.LeanDegrees, 0.f);

	const FAedirReachPosture AtFeet = UAedirInteractionComponent::ComputeReachPosture(15.f, 0.f, 5.f);
	TestTrue(TEXT("An item by the feet takes a squat"), AtFeet.LowerCm > 20.f);
	TestTrue(TEXT("An item by the feet takes a bent spine"), AtFeet.LeanDegrees > 30.f);
	TestTrue(TEXT("Next to the feet the pelvis sits back"), AtFeet.HipForwardCm < 0.f);

	const FAedirReachPosture Far = UAedirInteractionComponent::ComputeReachPosture(90.f, 0.f, 5.f);
	TestTrue(TEXT("A far item moves the pelvis forward"), Far.HipForwardCm > 0.f);

	const FAedirReachPosture Left = UAedirInteractionComponent::ComputeReachPosture(40.f, -40.f, 5.f);
	TestTrue(TEXT("An item on the left shifts the pelvis left"), Left.HipSideCm < 0.f);
	TestTrue(TEXT("An item on the left twists the spine left"), Left.TwistDegrees < 0.f);

	return true;
}

#endif
