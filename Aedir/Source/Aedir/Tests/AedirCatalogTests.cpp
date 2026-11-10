#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Containers/Set.h"
#include "Items/AedirItemCatalog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCatalogIdsTest, "Aedir.Catalog.Ids", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCatalogIdsTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Ids = UAedirItemCatalog::GetIds();
	TestTrue(TEXT("Catalog is not empty"), Ids.Num() > 0);

	TSet<FName> UniqueIds;
	bool bAllNamed = true;
	for (const FName Id : Ids)
	{
		bAllNamed &= !Id.IsNone();
		UniqueIds.Add(Id);
	}

	TestTrue(TEXT("Every item has a non-empty id"), bAllNamed);
	TestEqual(TEXT("Every id is unique"), UniqueIds.Num(), Ids.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCatalogFindTest, "Aedir.Catalog.Find", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCatalogFindTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("Unknown id finds nothing"), UAedirItemCatalog::Find(FName(TEXT("NoSuchItem"))));
	TestNull(TEXT("The empty name finds nothing"), UAedirItemCatalog::Find(NAME_None));
	TestNotNull(TEXT("Dagger is found"), UAedirItemCatalog::Find(FName(TEXT("Dagger"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCatalogWeaponTest, "Aedir.Catalog.Weapons", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCatalogWeaponTest::RunTest(const FString& Parameters)
{
	const FAedirItemDef* Spear = UAedirItemCatalog::Find(FName(TEXT("Spear")));
	TestNotNull(TEXT("Spear is in the catalog"), Spear);
	if (!Spear)
	{
		return true;
	}

	for (const FAedirItemDef& Def : UAedirItemCatalog::GetAll())
	{
		if (Def.ItemClass != EAedirItemClass::Weapon)
		{
			continue;
		}

		const FString Id = Def.Id.ToString();
		TestTrue(*FString::Printf(TEXT("%s has a positive weight"), *Id), Def.WeightKg > 0.f);
		TestTrue(*FString::Printf(TEXT("%s has a positive length"), *Id), Def.WeaponLengthCm > 0.f);

		if (Def.Id != Spear->Id)
		{
			TestTrue(*FString::Printf(TEXT("Spear is longer than %s"), *Id), Spear->WeaponLengthCm > Def.WeaponLengthCm);
		}
	}

	const FAedirItemDef* Dagger = UAedirItemCatalog::Find(FName(TEXT("Dagger")));
	const FAedirItemDef* Mace = UAedirItemCatalog::Find(FName(TEXT("Mace")));
	const FAedirItemDef* Longsword = UAedirItemCatalog::Find(FName(TEXT("Longsword")));
	const FAedirItemDef* Sword = UAedirItemCatalog::Find(FName(TEXT("Sword")));
	TestTrue(TEXT("Dagger pierces"), Dagger && Dagger->DamageType == EAedirDamageType::Pierce);
	TestTrue(TEXT("Spear pierces"), Spear->DamageType == EAedirDamageType::Pierce);
	TestTrue(TEXT("Mace is blunt"), Mace && Mace->DamageType == EAedirDamageType::Blunt);
	TestTrue(TEXT("The sword cuts"), Sword && Sword->DamageType == EAedirDamageType::Slash);

	TestTrue(TEXT("Spear is two-handed"), Spear->bTwoHanded);
	TestTrue(TEXT("The longsword takes both hands"), Longsword && Longsword->bTwoHanded);
	// Its hilt holds one fist: an arming sword, held in one hand.
	TestFalse(TEXT("The arming sword takes one hand"), Sword && Sword->bTwoHanded);
	TestFalse(TEXT("The mace is one-handed"), Mace && Mace->bTwoHanded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirCatalogArmorTest, "Aedir.Catalog.Armor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirCatalogArmorTest::RunTest(const FString& Parameters)
{
	for (const FAedirItemDef& Def : UAedirItemCatalog::GetAll())
	{
		if (Def.ItemClass != EAedirItemClass::Armor)
		{
			continue;
		}

		const FString Id = Def.Id.ToString();
		TestTrue(*FString::Printf(TEXT("%s protects between 0 and 0.9"), *Id), Def.ArmorProtection > 0.f && Def.ArmorProtection <= 0.9f);
		TestFalse(*FString::Printf(TEXT("%s has an attach bone"), *Id), Def.ArmorAttachBone.IsNone());
	}

	const FAedirItemDef* LeatherCap = UAedirItemCatalog::Find(FName(TEXT("LeatherCap")));
	const FAedirItemDef* IronHelm = UAedirItemCatalog::Find(FName(TEXT("IronHelm")));
	const FAedirItemDef* LeatherArmor = UAedirItemCatalog::Find(FName(TEXT("LeatherArmor")));
	const FAedirItemDef* IronCuirass = UAedirItemCatalog::Find(FName(TEXT("IronCuirass")));
	TestTrue(TEXT("Iron cuirass protects more than leather armor"), IronCuirass && LeatherArmor && IronCuirass->ArmorProtection > LeatherArmor->ArmorProtection);
	TestTrue(TEXT("Iron helm protects more than leather cap"), IronHelm && LeatherCap && IronHelm->ArmorProtection > LeatherCap->ArmorProtection);

	return true;
}

#endif
