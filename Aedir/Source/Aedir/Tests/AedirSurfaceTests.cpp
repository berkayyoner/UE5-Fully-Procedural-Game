#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/HitResult.h"
#include "Surface/AedirSurfaceSubsystem.h"
#include "Surface/AedirPhysicalMaterial.h"

namespace
{
	UAedirSurfaceSubsystem* AedirSurfaceTestCreateSubsystem()
	{
		return NewObject<UAedirSurfaceSubsystem>(GetTransientPackage());
	}

	UAedirPhysicalMaterial* AedirSurfaceTestCreateMaterial()
	{
		return NewObject<UAedirPhysicalMaterial>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceGlobalClampTest, "Aedir.Surface.GlobalWeatherClamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceGlobalClampTest::RunTest(const FString& Parameters)
{
	UAedirSurfaceSubsystem* Surface = AedirSurfaceTestCreateSubsystem();
	if (!TestTrue(TEXT("Surface subsystem is created"), Surface != nullptr))
	{
		return false;
	}

	const FVector Location(0.f, 0.f, 0.f);

	Surface->SetGlobalWeatherWetness(2.f);
	TestEqual(TEXT("Global wetness above one clamps to one"), Surface->GetWetness(Location), 1.f);

	Surface->SetGlobalWeatherWetness(-1.f);
	TestEqual(TEXT("Global wetness below zero clamps to zero"), Surface->GetWetness(Location), 0.f);

	Surface->SetGlobalWeatherWetness(0.4f);
	TestEqual(TEXT("Global wetness applies without a world"), Surface->GetWetness(Location), 0.4f);

	Surface->SetGlobalWeatherSnow(2.f);
	TestEqual(TEXT("Global snow above one clamps to one"), Surface->GetSnow(Location), 1.f);

	Surface->SetGlobalWeatherSnow(-1.f);
	TestEqual(TEXT("Global snow below zero clamps to zero"), Surface->GetSnow(Location), 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceCellLocalityTest, "Aedir.Surface.CellLocality", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceCellLocalityTest::RunTest(const FString& Parameters)
{
	UAedirSurfaceSubsystem* Surface = AedirSurfaceTestCreateSubsystem();
	if (!TestTrue(TEXT("Surface subsystem is created"), Surface != nullptr))
	{
		return false;
	}

	Surface->AddWetness(FVector(10.f, 10.f, 0.f), 0.5f, 0.f);
	TestEqual(TEXT("Wetness is stored for the added location"), Surface->GetWetness(FVector(10.f, 10.f, 0.f)), 0.5f);
	TestEqual(TEXT("Points in the same 50 unit cell share wetness"), Surface->GetWetness(FVector(40.f, 40.f, 0.f)), 0.5f);
	TestEqual(TEXT("Points in a neighbouring cell stay dry"), Surface->GetWetness(FVector(60.f, 10.f, 0.f)), 0.f);

	Surface->AddWetness(FVector(-10.f, -10.f, 0.f), 0.3f, 0.f);
	TestEqual(TEXT("Negative coordinates map to their own cell"), Surface->GetWetness(FVector(-40.f, -40.f, 0.f)), 0.3f);
	TestEqual(TEXT("Negative cell does not overwrite the positive cell"), Surface->GetWetness(FVector(10.f, 10.f, 0.f)), 0.5f);

	Surface->AddWetness(FVector(10.f, 10.f, 0.f), 0.8f, 0.f);
	TestEqual(TEXT("Accumulated wetness clamps to one"), Surface->GetWetness(FVector(10.f, 10.f, 0.f)), 1.f);

	Surface->AddWetness(FVector(200.f, 0.f, 0.f), 0.f, 0.f);
	TestEqual(TEXT("Non-positive amounts add nothing"), Surface->GetWetness(FVector(200.f, 0.f, 0.f)), 0.f);

	Surface->AddSnow(FVector(300.f, 300.f, 0.f), 0.6f, 0.f);
	TestEqual(TEXT("Snow is stored for the added location"), Surface->GetSnow(FVector(300.f, 300.f, 0.f)), 0.6f);
	TestEqual(TEXT("Snow does not leak into wetness"), Surface->GetWetness(FVector(300.f, 300.f, 0.f)), 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceDryingTest, "Aedir.Surface.Drying", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceDryingTest::RunTest(const FString& Parameters)
{
	UAedirSurfaceSubsystem* Surface = AedirSurfaceTestCreateSubsystem();
	if (!TestTrue(TEXT("Surface subsystem is created"), Surface != nullptr))
	{
		return false;
	}

	const FVector Location(20.f, 20.f, 0.f);
	Surface->AddWetness(Location, 0.5f, 0.1f);

	Surface->Tick(1.f);
	TestEqual(TEXT("Wetness decays by dry rate over time"), Surface->GetWetness(Location), 0.4f);

	Surface->Tick(10.f);
	TestEqual(TEXT("Fully dried cell reports zero wetness"), Surface->GetWetness(Location), 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceRadiusTest, "Aedir.Surface.RadiusWetness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceRadiusTest::RunTest(const FString& Parameters)
{
	UAedirSurfaceSubsystem* Surface = AedirSurfaceTestCreateSubsystem();
	if (!TestTrue(TEXT("Surface subsystem is created"), Surface != nullptr))
	{
		return false;
	}

	const FVector Center(25.f, 25.f, 0.f);
	Surface->AddWetnessInRadius(Center, 100.f, 0.5f, 0.f);

	const float CenterWetness = Surface->GetWetness(Center);
	const float NearbyWetness = Surface->GetWetness(FVector(75.f, 25.f, 0.f));
	const float FarWetness = Surface->GetWetness(FVector(1000.f, 1000.f, 0.f));

	TestEqual(TEXT("Center cell receives the full amount"), CenterWetness, 0.5f);
	TestEqual(TEXT("Cell one step away receives half the amount"), NearbyWetness, 0.25f);
	TestTrue(TEXT("Wetness falls off with distance from the center"), CenterWetness > NearbyWetness && NearbyWetness > FarWetness);
	TestEqual(TEXT("Cells outside the radius stay dry"), FarWetness, 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceSlipperinessTest, "Aedir.Surface.SlipperinessFriction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceSlipperinessTest::RunTest(const FString& Parameters)
{
	UAedirPhysicalMaterial* Material = AedirSurfaceTestCreateMaterial();
	if (!TestTrue(TEXT("Physical material is created"), Material != nullptr))
	{
		return false;
	}

	Material->Slipperiness = 0.f;
	Material->ApplySlipperinessToPhysics();
	TestEqual(TEXT("Grippy surface has high friction"), Material->Friction, 0.9f);

	Material->Slipperiness = 0.5f;
	Material->ApplySlipperinessToPhysics();
	TestEqual(TEXT("Half slippery surface has interpolated friction"), Material->Friction, 0.475f);

	Material->Slipperiness = 1.f;
	Material->ApplySlipperinessToPhysics();
	TestEqual(TEXT("Ice-like surface has low friction"), Material->Friction, 0.05f);
	TestEqual(TEXT("Static friction follows friction"), Material->StaticFriction, Material->Friction * 1.2f);

	Material->Slipperiness = 5.f;
	Material->ApplySlipperinessToPhysics();
	TestEqual(TEXT("Slipperiness above one clamps to ice-like friction"), Material->Friction, 0.05f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirSurfaceMaterialLookupTest, "Aedir.Surface.MaterialLookup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirSurfaceMaterialLookupTest::RunTest(const FString& Parameters)
{
	UAedirPhysicalMaterial* Material = AedirSurfaceTestCreateMaterial();
	if (!TestTrue(TEXT("Physical material is created"), Material != nullptr))
	{
		return false;
	}

	FHitResult EmptyHit;
	TestTrue(TEXT("Hit without a physical material yields no Aedir material"), UAedirSurfaceSubsystem::GetAedirMaterial(EmptyHit) == nullptr);

	FHitResult AedirHit;
	AedirHit.PhysMaterial = TWeakObjectPtr<UPhysicalMaterial>(Material);
	TestTrue(TEXT("Hit with an Aedir physical material returns it"), UAedirSurfaceSubsystem::GetAedirMaterial(AedirHit) == Material);

	return true;
}

#endif
