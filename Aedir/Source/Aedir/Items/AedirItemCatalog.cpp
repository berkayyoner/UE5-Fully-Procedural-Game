#include "Items/AedirItemCatalog.h"

namespace
{
	const FLinearColor CatalogSteel(0.55f, 0.57f, 0.6f);
	const FLinearColor CatalogWood(0.36f, 0.22f, 0.12f);
	const FLinearColor CatalogLeather(0.30f, 0.18f, 0.10f);
	const FLinearColor CatalogIron(0.32f, 0.33f, 0.35f);
	const FLinearColor CatalogStone(0.35f, 0.34f, 0.33f);
	const FLinearColor CatalogPoison(0.20f, 0.55f, 0.25f);

	FAedirItemDef CatalogMakeWeapon(const TCHAR* Id, const TCHAR* Name, float WeightKg, float LengthCm, EAedirDamageType DamageType, float FlatDamage, bool bTwoHanded, const FLinearColor& Color, const FVector& BodyScale)
	{
		FAedirItemDef Def;
		Def.Id = FName(Id);
		Def.DisplayName = FText::FromString(Name);
		Def.ItemClass = EAedirItemClass::Weapon;
		Def.WeightKg = WeightKg;
		Def.WeaponLengthCm = LengthCm;
		Def.DamageType = DamageType;
		Def.FlatDamage = FlatDamage;
		Def.bTwoHanded = bTwoHanded;
		Def.Color = Color;
		Def.BodyScale = BodyScale;
		return Def;
	}

	FAedirItemDef CatalogMakeArmor(const TCHAR* Id, const TCHAR* Name, float WeightKg, EAedirBodyZone Zone, float Protection, const TCHAR* AttachBone, const FLinearColor& Color, const FVector& BodyScale)
	{
		FAedirItemDef Def;
		Def.Id = FName(Id);
		Def.DisplayName = FText::FromString(Name);
		Def.ItemClass = EAedirItemClass::Armor;
		Def.WeightKg = WeightKg;
		Def.ArmorZone = Zone;
		Def.ArmorProtection = Protection;
		Def.ArmorAttachBone = FName(AttachBone);
		Def.Color = Color;
		Def.BodyScale = BodyScale;
		return Def;
	}

	FAedirItemDef CatalogMakeMisc(const TCHAR* Id, const TCHAR* Name, EAedirItemClass ItemClass, float WeightKg, const FLinearColor& Color, const FVector& BodyScale)
	{
		FAedirItemDef Def;
		Def.Id = FName(Id);
		Def.DisplayName = FText::FromString(Name);
		Def.ItemClass = ItemClass;
		Def.WeightKg = WeightKg;
		Def.Color = Color;
		Def.BodyScale = BodyScale;
		return Def;
	}

	TArray<FAedirItemDef> CatalogBuildDefinitions()
	{
		TArray<FAedirItemDef> Definitions;

		// Weapons. Blades run along X with the hilt at -length/2; shafts are centred on the hand.
		Definitions.Add(CatalogMakeWeapon(TEXT("Dagger"), TEXT("Dagger"), 0.5f, 30.f, EAedirDamageType::Pierce, 4.f, false, CatalogSteel, FVector(0.30, 0.03, 0.006)));
		Definitions.Add(CatalogMakeWeapon(TEXT("ShortSword"), TEXT("Short Sword"), 1.1f, 70.f, EAedirDamageType::Slash, 8.f, false, CatalogSteel, FVector(0.70, 0.045, 0.008)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Longsword"), TEXT("Longsword"), 1.6f, 105.f, EAedirDamageType::Slash, 11.f, false, CatalogSteel, FVector(1.05, 0.05, 0.01)));
		Definitions.Add(CatalogMakeWeapon(TEXT("BattleAxe"), TEXT("Battle Axe"), 2.4f, 80.f, EAedirDamageType::Slash, 12.f, false, CatalogWood, FVector(0.80, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Mace"), TEXT("Mace"), 2.0f, 65.f, EAedirDamageType::Blunt, 9.f, false, CatalogWood, FVector(0.65, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Warhammer"), TEXT("Warhammer"), 3.2f, 90.f, EAedirDamageType::Blunt, 13.f, false, CatalogWood, FVector(0.90, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Club"), TEXT("Club"), 1.4f, 70.f, EAedirDamageType::Blunt, 6.f, false, CatalogWood, FVector(0.62, 0.07, 0.07)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Spear"), TEXT("Spear"), 2.2f, 180.f, EAedirDamageType::Pierce, 9.f, true, CatalogWood, FVector(1.80, 0.025, 0.025)));

		// Armor. Pieces are centred on their attach bone.
		Definitions.Add(CatalogMakeArmor(TEXT("LeatherCap"), TEXT("Leather Cap"), 0.4f, EAedirBodyZone::Head, 0.25f, TEXT("head"), CatalogLeather, FVector(0.24, 0.24, 0.22)));
		Definitions.Add(CatalogMakeArmor(TEXT("IronHelm"), TEXT("Iron Helm"), 1.6f, EAedirBodyZone::Head, 0.55f, TEXT("head"), CatalogIron, FVector(0.27, 0.27, 0.25)));
		Definitions.Add(CatalogMakeArmor(TEXT("LeatherArmor"), TEXT("Leather Armor"), 4.0f, EAedirBodyZone::Torso, 0.30f, TEXT("spine_03"), CatalogLeather, FVector(0.22, 0.36, 0.44)));
		Definitions.Add(CatalogMakeArmor(TEXT("IronCuirass"), TEXT("Iron Cuirass"), 9.0f, EAedirBodyZone::Torso, 0.60f, TEXT("spine_03"), CatalogIron, FVector(0.24, 0.40, 0.50)));
		Definitions.Add(CatalogMakeArmor(TEXT("Bracers"), TEXT("Bracers"), 1.2f, EAedirBodyZone::Arm, 0.30f, TEXT("lowerarm_l"), CatalogLeather, FVector(0.10, 0.10, 0.24)));
		Definitions.Add(CatalogMakeArmor(TEXT("IronGreaves"), TEXT("Iron Greaves"), 3.5f, EAedirBodyZone::Leg, 0.45f, TEXT("thigh_l"), CatalogIron, FVector(0.13, 0.13, 0.42)));

		// Miscellaneous items.
		Definitions.Add(CatalogMakeMisc(TEXT("SmallPotionPoison"), TEXT("Small Poison Potion"), EAedirItemClass::Poison, 0.3f, CatalogPoison, FVector(0.07, 0.07, 0.13)));
		Definitions.Add(CatalogMakeMisc(TEXT("OldBook"), TEXT("Old Book"), EAedirItemClass::Book, 0.8f, CatalogLeather, FVector(0.22, 0.16, 0.04)));
		Definitions.Add(CatalogMakeMisc(TEXT("Boulder"), TEXT("Boulder"), EAedirItemClass::Object, 25.f, CatalogStone, FVector(0.40, 0.40, 0.40)));

		return Definitions;
	}
}

const TArray<FAedirItemDef>& UAedirItemCatalog::GetAll()
{
	static const TArray<FAedirItemDef> Definitions = CatalogBuildDefinitions();
	return Definitions;
}

const FAedirItemDef* UAedirItemCatalog::Find(FName Id)
{
	if (Id.IsNone())
	{
		return nullptr;
	}

	for (const FAedirItemDef& Def : GetAll())
	{
		if (Def.Id == Id)
		{
			return &Def;
		}
	}

	return nullptr;
}

TArray<FName> UAedirItemCatalog::GetIds()
{
	TArray<FName> Ids;
	Ids.Reserve(GetAll().Num());
	for (const FAedirItemDef& Def : GetAll())
	{
		Ids.Add(Def.Id);
	}

	return Ids;
}
