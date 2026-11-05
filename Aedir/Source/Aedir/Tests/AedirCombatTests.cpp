#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/AedirCombatComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCombatImpactEnergyTest, "Aedir.Combat.ImpactEnergy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCombatImpactEnergyTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("3 kg at 800 cm/s carries 96 J"), FMath::IsNearlyEqual(UAedirCombatComponent::ComputeImpactEnergy(3.f, 800.f), 96.f, 0.01f));

	const float SlowEnergy = UAedirCombatComponent::ComputeImpactEnergy(3.f, 400.f);
	const float FastEnergy = UAedirCombatComponent::ComputeImpactEnergy(3.f, 800.f);
	TestTrue(TEXT("Energy grows with speed"), FastEnergy > SlowEnergy);

	const float LightEnergy = UAedirCombatComponent::ComputeImpactEnergy(2.f, 800.f);
	const float HeavyEnergy = UAedirCombatComponent::ComputeImpactEnergy(5.f, 800.f);
	TestTrue(TEXT("Energy grows with mass"), HeavyEnergy > LightEnergy);

	TestTrue(TEXT("Zero speed carries no energy"), FMath::IsNearlyZero(UAedirCombatComponent::ComputeImpactEnergy(3.f, 0.f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCombatBodyZoneTest, "Aedir.Combat.BodyZone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCombatBodyZoneTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("head is Head"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("head"))) == EAedirBodyZone::Head);
	TestTrue(TEXT("neck_01 is Head"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("neck_01"))) == EAedirBodyZone::Head);
	TestTrue(TEXT("spine_03 is Torso"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("spine_03"))) == EAedirBodyZone::Torso);
	TestTrue(TEXT("pelvis is Torso"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("pelvis"))) == EAedirBodyZone::Torso);
	TestTrue(TEXT("upperarm_l is Arm"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("upperarm_l"))) == EAedirBodyZone::Arm);
	TestTrue(TEXT("hand_r is Arm"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("hand_r"))) == EAedirBodyZone::Arm);
	TestTrue(TEXT("index_02_l is Arm"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("index_02_l"))) == EAedirBodyZone::Arm);
	TestTrue(TEXT("thigh_r is Leg"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("thigh_r"))) == EAedirBodyZone::Leg);
	TestTrue(TEXT("foot_l is Leg"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("foot_l"))) == EAedirBodyZone::Leg);
	TestTrue(TEXT("unknown_bone defaults to Torso"), UAedirCombatComponent::ClassifyBodyZone(FName(TEXT("unknown_bone"))) == EAedirBodyZone::Torso);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCombatZoneMultiplierTest, "Aedir.Combat.ZoneMultiplier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCombatZoneMultiplierTest::RunTest(const FString& Parameters)
{
	const float Head = UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone::Head);
	const float Torso = UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone::Torso);
	const float Leg = UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone::Leg);
	const float Arm = UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone::Arm);

	TestTrue(TEXT("Head takes more damage than Torso"), Head > Torso);
	TestTrue(TEXT("Torso takes more damage than Leg"), Torso > Leg);
	TestTrue(TEXT("Leg takes more damage than Arm"), Leg > Arm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCombatDamageTypeTest, "Aedir.Combat.DamageType", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCombatDamageTypeTest::RunTest(const FString& Parameters)
{
	const float PierceAligned = UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType::Pierce, 0.9f);
	const float PierceGlancing = UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType::Pierce, 0.3f);
	const float Slash = UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType::Slash, 0.f);
	const float Blunt = UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType::Blunt, 0.f);

	TestTrue(TEXT("Aligned pierce beats glancing pierce"), PierceAligned > PierceGlancing);
	TestTrue(TEXT("Aligned pierce beats slash"), PierceAligned > Slash);
	TestTrue(TEXT("Slash beats blunt"), Slash > Blunt);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCombatBoneLimbTest, "Aedir.Combat.BoneLimb", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCombatBoneLimbTest::RunTest(const FString& Parameters)
{
	EAedirLimb Limb = EAedirLimb::LeftLeg;

	TestTrue(TEXT("lowerarm_l maps to the left arm"), UAedirCombatComponent::GetBoneLimb(FName(TEXT("lowerarm_l")), Limb) && Limb == EAedirLimb::LeftArm);

	Limb = EAedirLimb::LeftLeg;
	TestTrue(TEXT("calf_r maps to the right leg"), UAedirCombatComponent::GetBoneLimb(FName(TEXT("calf_r")), Limb) && Limb == EAedirLimb::RightLeg);

	Limb = EAedirLimb::LeftLeg;
	TestFalse(TEXT("spine_01 is not a limb"), UAedirCombatComponent::GetBoneLimb(FName(TEXT("spine_01")), Limb));

	return true;
}

#endif
