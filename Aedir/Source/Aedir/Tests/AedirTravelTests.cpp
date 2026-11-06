#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HUD/AedirMarkerSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Travel/AedirTravelLogic.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirTravelDiscoveryStatesTest, "Aedir.Travel.DiscoveryStates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirTravelDiscoveryStatesTest::RunTest(const FString& Parameters)
{
	using namespace AedirTravelLogic;

	bool bRevealed = false;
	bool bDiscovered = false;

	TestFalse(TEXT("Far away changes nothing"), UpdateDiscovery(EAedirMarkerType::Area, RevealDistanceCm + 1.f, bRevealed, bDiscovered));
	TestFalse(TEXT("Far away stays hidden"), bRevealed);
	TestFalse(TEXT("Far away stays undiscovered"), bDiscovered);

	TestFalse(TEXT("Reveal distance reveals but does not discover"), UpdateDiscovery(EAedirMarkerType::Area, RevealDistanceCm, bRevealed, bDiscovered));
	TestTrue(TEXT("Within the reveal distance the marker is revealed"), bRevealed);
	TestFalse(TEXT("Within the reveal distance the marker is not discovered"), bDiscovered);

	TestFalse(TEXT("Just outside the discover distance stays undiscovered"), UpdateDiscovery(EAedirMarkerType::Area, DiscoverDistanceCm + 1.f, bRevealed, bDiscovered));
	TestFalse(TEXT("Still undiscovered"), bDiscovered);

	TestTrue(TEXT("Discover distance discovers and reports it"), UpdateDiscovery(EAedirMarkerType::Area, DiscoverDistanceCm, bRevealed, bDiscovered));
	TestTrue(TEXT("Discovered markers are revealed"), bRevealed && bDiscovered);

	TestFalse(TEXT("Walking away does not report a second discovery"), UpdateDiscovery(EAedirMarkerType::Area, RevealDistanceCm * 4.f, bRevealed, bDiscovered));
	TestTrue(TEXT("Discovery is never lost"), bRevealed && bDiscovered);

	bool bSkippedReveal = false;
	bool bSkippedDiscover = false;
	TestTrue(TEXT("A marker first seen up close is discovered at once"), UpdateDiscovery(EAedirMarkerType::Mission, 0.f, bSkippedReveal, bSkippedDiscover));
	TestTrue(TEXT("It is revealed as well"), bSkippedReveal && bSkippedDiscover);

	bool bCustomRevealed = false;
	bool bCustomDiscovered = false;
	TestFalse(TEXT("Custom markers never report a discovery"), UpdateDiscovery(EAedirMarkerType::Custom, RevealDistanceCm * 10.f, bCustomRevealed, bCustomDiscovered));
	TestTrue(TEXT("Custom markers are always discovered"), bCustomRevealed && bCustomDiscovered);

	TestTrue(TEXT("Reveal distance is farther than the discover distance"), RevealDistanceCm > DiscoverDistanceCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirTravelMarkerSubsystemTest, "Aedir.Travel.MarkerDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirTravelMarkerSubsystemTest::RunTest(const FString& Parameters)
{
	UAedirMarkerSubsystem* Markers = NewObject<UAedirMarkerSubsystem>(GetTransientPackage());
	if (!TestNotNull(TEXT("Marker subsystem is created"), Markers))
	{
		return false;
	}

	const FName Id = Markers->AddLocation(FText::FromString(TEXT("Test Ruins")), FVector(50000.0, 0.0, 0.0));
	const FAedirMapMarker* Marker = Markers->FindMarker(Id);
	if (!TestNotNull(TEXT("The location is registered"), Marker))
	{
		return false;
	}
	TestFalse(TEXT("A new location is hidden"), Marker->bRevealed);
	TestFalse(TEXT("A new location is undiscovered"), Marker->bDiscovered);

	TestEqual(TEXT("Nothing happens far away"), Markers->UpdateDiscovery(FVector::ZeroVector).Num(), 0);
	TestFalse(TEXT("Still hidden far away"), Markers->FindMarker(Id)->bRevealed);

	TestEqual(TEXT("Reveal reports no discovery"), Markers->UpdateDiscovery(FVector(40000.0, 0.0, 0.0)).Num(), 0);
	TestTrue(TEXT("Revealed within the reveal distance"), Markers->FindMarker(Id)->bRevealed);
	TestFalse(TEXT("Not discovered yet"), Markers->FindMarker(Id)->bDiscovered);

	const TArray<FText> Found = Markers->UpdateDiscovery(FVector(48000.0, 0.0, 5000.0));
	TestEqual(TEXT("One location is discovered"), Found.Num(), 1);
	TestEqual(TEXT("The label is reported"), Found.Num() > 0 ? Found[0].ToString() : FString(), FString(TEXT("Test Ruins")));
	TestEqual(TEXT("It is only reported once"), Markers->UpdateDiscovery(FVector(48000.0, 0.0, 0.0)).Num(), 0);

	Markers->SetCustomMarker(FVector(10.0, 20.0, 0.0));
	Markers->SetCustomMarker(FVector(30.0, 40.0, 0.0));
	int32 CustomCount = 0;
	for (const FAedirMapMarker& Entry : Markers->GetMarkers())
	{
		CustomCount += Entry.Type == EAedirMarkerType::Custom ? 1 : 0;
	}
	TestEqual(TEXT("A new custom marker replaces the previous one"), CustomCount, 1);
	const FAedirMapMarker* Custom = Markers->FindMarker(AedirCustomMarkerId);
	TestTrue(TEXT("The custom marker is discovered"), Custom && Custom->bDiscovered && Custom->bRevealed);
	TestTrue(TEXT("The custom marker is at the latest position"), Custom && Custom->Location.Equals(FVector(30.0, 40.0, 0.0)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirTravelEligibilityTest, "Aedir.Travel.Eligibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirTravelEligibilityTest::RunTest(const FString& Parameters)
{
	using namespace AedirTravelLogic;

	TestTrue(TEXT("A discovered location can be travelled to"), CanFastTravelTo(EAedirMarkerType::Area, true));
	TestFalse(TEXT("An undiscovered location cannot"), CanFastTravelTo(EAedirMarkerType::Area, false));
	TestTrue(TEXT("A discovered mission can be travelled to"), CanFastTravelTo(EAedirMarkerType::Mission, true));
	TestFalse(TEXT("An undiscovered mission cannot"), CanFastTravelTo(EAedirMarkerType::Mission, false));
	TestFalse(TEXT("The player's own marker is not a destination"), CanFastTravelTo(EAedirMarkerType::Custom, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirTravelEncounterPlacementTest, "Aedir.Travel.EncounterPlacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirTravelEncounterPlacementTest::RunTest(const FString& Parameters)
{
	using namespace AedirTravelLogic;

	TestTrue(TEXT("A low roll triggers an encounter"), ShouldEncounter(0.f));
	TestTrue(TEXT("A roll just below the chance triggers one"), ShouldEncounter(EncounterChance - 0.01f));
	TestFalse(TEXT("A roll at the chance does not"), ShouldEncounter(EncounterChance));
	TestFalse(TEXT("A high roll does not"), ShouldEncounter(0.99f));

	for (const float Roll : { 0.f, 0.1f, 0.5f, 0.9f, 1.f })
	{
		const float Fraction = GetEncounterFraction(Roll);
		TestTrue(TEXT("The encounter happens between 25 and 75 percent of the way"), Fraction >= 0.25f && Fraction <= 0.75f);
	}
	TestTrue(TEXT("Lowest roll maps to the minimum fraction"), FMath::IsNearlyEqual(GetEncounterFraction(0.f), EncounterMinFraction));
	TestTrue(TEXT("Highest roll maps to the maximum fraction"), FMath::IsNearlyEqual(GetEncounterFraction(1.f), EncounterMaxFraction));
	TestTrue(TEXT("Rolls outside 0..1 stay in range"), GetEncounterFraction(-3.f) >= EncounterMinFraction && GetEncounterFraction(7.f) <= EncounterMaxFraction);

	const FVector From(0.0, 0.0, 0.0);
	const FVector To(100000.0, 40000.0, 2000.0);
	const FVector Middle = GetEncounterPoint(From, To, 0.5f);
	TestTrue(TEXT("Half way is the midpoint"), Middle.Equals(FVector(50000.0, 20000.0, 1000.0), 0.01));
	TestTrue(TEXT("The point lies on the straight line"), FVector::CrossProduct((Middle - From).GetSafeNormal2D(), (To - From).GetSafeNormal2D()).IsNearlyZero(0.001));

	const double TotalDistance = FVector::Dist2D(From, To);
	const double EarlyDistance = FVector::Dist2D(From, GetEncounterPoint(From, To, 0.f));
	const double LateDistance = FVector::Dist2D(From, GetEncounterPoint(From, To, 1.f));
	TestTrue(TEXT("Points are clamped to at least a quarter of the way"), EarlyDistance >= TotalDistance * 0.25 - 1.0);
	TestTrue(TEXT("Points are clamped to at most three quarters of the way"), LateDistance <= TotalDistance * 0.75 + 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirTravelHoursTest, "Aedir.Travel.TravelHours", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirTravelHoursTest::RunTest(const FString& Parameters)
{
	using namespace AedirTravelLogic;

	TestTrue(TEXT("No distance takes no time"), FMath::IsNearlyZero(GetTravelHours(0.f)));
	TestTrue(TEXT("Negative distances take no time"), FMath::IsNearlyZero(GetTravelHours(-500.f)));
	TestTrue(TEXT("One hour of walking covers the travel speed in kilometres"), FMath::IsNearlyEqual(GetTravelHours(TravelSpeedKmh * 100000.f), 1.f, 0.001f));
	TestTrue(TEXT("Twice the distance takes twice as long"), FMath::IsNearlyEqual(GetTravelHours(40000.f) * 2.f, GetTravelHours(80000.f), 0.001f));
	TestTrue(TEXT("Longer trips take longer"), GetTravelHours(500000.f) > GetTravelHours(100000.f));
	return true;
}

#endif
