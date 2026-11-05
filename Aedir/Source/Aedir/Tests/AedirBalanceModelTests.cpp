#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Physics/AedirBalanceComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirBalanceModelConvexHullTest, "Aedir.BalanceModel.ConvexHull", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirBalanceModelConvexHullTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Points = {
		FVector2D(0.0, 0.0),
		FVector2D(1.0, 0.0),
		FVector2D(1.0, 1.0),
		FVector2D(0.0, 1.0),
		FVector2D(0.5, 0.5),
		FVector2D(0.2, 0.7)
	};

	TArray<FVector2D> Hull;
	UAedirBalanceComponent::ComputeConvexHull2D(Points, Hull);

	TestEqual(TEXT("Unit square with interior points has four hull points"), Hull.Num(), 4);
	TestTrue(TEXT("Hull keeps the corner at the origin"), Hull.Contains(FVector2D(0.0, 0.0)));
	TestTrue(TEXT("Hull keeps the corner at (1, 1)"), Hull.Contains(FVector2D(1.0, 1.0)));
	TestFalse(TEXT("Hull drops the interior point"), Hull.Contains(FVector2D(0.5, 0.5)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirBalanceModelSupportMarginTest, "Aedir.BalanceModel.SupportMargin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirBalanceModelSupportMarginTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Square = {
		FVector2D(0.0, 0.0),
		FVector2D(20.0, 0.0),
		FVector2D(20.0, 20.0),
		FVector2D(0.0, 20.0)
	};

	TArray<FVector2D> Hull;
	UAedirBalanceComponent::ComputeConvexHull2D(Square, Hull);

	const float Center = UAedirBalanceComponent::ComputeSupportMargin(Hull, FVector2D(10.0, 10.0));
	TestTrue(TEXT("Centre of a 20 cm square is 10 cm inside"), FMath::IsNearlyEqual(Center, 10.f, 0.01f));

	const float Outside = UAedirBalanceComponent::ComputeSupportMargin(Hull, FVector2D(25.0, 10.0));
	TestTrue(TEXT("A point 5 cm outside an edge is about -5 cm"), FMath::IsNearlyEqual(Outside, -5.f, 0.01f));

	const float OnEdge = UAedirBalanceComponent::ComputeSupportMargin(Hull, FVector2D(20.0, 10.0));
	TestTrue(TEXT("A point on the edge has a margin of about zero"), FMath::IsNearlyZero(OnEdge, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirBalanceModelShortHullTest, "Aedir.BalanceModel.ShortHullMargin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirBalanceModelShortHullTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Segment = { FVector2D(0.0, 0.0), FVector2D(10.0, 0.0) };
	const float SegmentMargin = UAedirBalanceComponent::ComputeSupportMargin(Segment, FVector2D(5.0, 3.0));
	TestTrue(TEXT("Point 3 cm off a segment gives -3 cm"), FMath::IsNearlyEqual(SegmentMargin, -3.f, 0.01f));

	const TArray<FVector2D> Single = { FVector2D(0.0, 0.0) };
	const float PointMargin = UAedirBalanceComponent::ComputeSupportMargin(Single, FVector2D(3.0, 4.0));
	TestTrue(TEXT("Point 5 cm away from a single support point gives -5 cm"), FMath::IsNearlyEqual(PointMargin, -5.f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirBalanceModelComLossTest, "Aedir.BalanceModel.ComBalanceLoss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirBalanceModelComLossTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Inside the support polygon gives no loss"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeComBalanceLoss(5.f, 0.1f)));
	TestTrue(TEXT("On the support boundary gives no loss"), FMath::IsNearlyZero(UAedirBalanceComponent::ComputeComBalanceLoss(0.f, 0.1f)));

	const float Small = UAedirBalanceComponent::ComputeComBalanceLoss(-3.f, 0.1f);
	const float Large = UAedirBalanceComponent::ComputeComBalanceLoss(-6.f, 0.1f);
	TestTrue(TEXT("Loss grows with the distance outside"), Small > 0.f && Large > Small);

	const float Doubled = UAedirBalanceComponent::ComputeComBalanceLoss(-6.f, 0.2f);
	TestTrue(TEXT("Loss scales with delta seconds"), FMath::IsNearlyEqual(Doubled, 2.f * Large, 0.0001f));

	const float Saturated = UAedirBalanceComponent::ComputeComBalanceLoss(-100.f, 0.1f);
	TestTrue(TEXT("Loss never exceeds 0.9 per second"), Saturated <= 0.9f * 0.1f + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Loss saturates at 0.9 per second beyond 12 cm"), FMath::IsNearlyEqual(Saturated, 0.09f, 0.0001f));

	return true;
}

#endif
