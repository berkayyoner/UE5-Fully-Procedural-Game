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
		Definitions.Add(CatalogMakeWeapon(TEXT("Dagger"), TEXT("Dagger"), 1.1f, 30.f, EAedirDamageType::Pierce, 4.f, false, CatalogSteel, FVector(0.30, 0.03, 0.006)));
		Definitions.Add(CatalogMakeWeapon(TEXT("ShortSword"), TEXT("Short Sword"), 2.4f, 70.f, EAedirDamageType::Slash, 8.f, false, CatalogSteel, FVector(0.70, 0.045, 0.008)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Sword"), TEXT("Sword"), 3.4f, 105.f, EAedirDamageType::Slash, 11.f, true, CatalogSteel, FVector(1.05, 0.05, 0.01)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Longsword"), TEXT("Longsword"), 4.4f, 118.f, EAedirDamageType::Slash, 14.f, true, CatalogSteel, FVector(1.18, 0.052, 0.011)));
		Definitions.Add(CatalogMakeWeapon(TEXT("BattleAxe"), TEXT("Battle Axe"), 4.7f, 80.f, EAedirDamageType::Slash, 12.f, false, CatalogWood, FVector(0.80, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Mace"), TEXT("Mace"), 4.2f, 65.f, EAedirDamageType::Blunt, 9.f, false, CatalogWood, FVector(0.65, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Warhammer"), TEXT("Warhammer"), 6.2f, 90.f, EAedirDamageType::Blunt, 13.f, false, CatalogWood, FVector(0.90, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Club"), TEXT("Club"), 3.2f, 70.f, EAedirDamageType::Blunt, 6.f, false, CatalogWood, FVector(0.62, 0.07, 0.07)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Spear"), TEXT("Spear"), 4.4f, 180.f, EAedirDamageType::Pierce, 9.f, true, CatalogWood, FVector(1.80, 0.025, 0.025)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Quarterstaff"), TEXT("Quarterstaff"), 2.8f, 170.f, EAedirDamageType::Blunt, 8.f, true, CatalogWood, FVector(1.70, 0.035, 0.035)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Halberd"), TEXT("Halberd"), 5.6f, 175.f, EAedirDamageType::Slash, 15.f, true, CatalogWood, FVector(1.75, 0.03, 0.03)));

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

		// The hand closes on the grip, not on the middle of the item.
		const auto SetGrip = [&Definitions](const TCHAR* Id, float GripCm, float SpanCm = 13.f, bool bDiagonal = false)
		{
			for (FAedirItemDef& Def : Definitions)
			{
				if (Def.Id == FName(Id))
				{
					Def.GripCm = GripCm;
					Def.GripSpanCm = SpanCm;
					Def.bHangsDiagonal = bDiagonal;
				}
			}
		};
		const auto AddFood = [&Definitions](const TCHAR* Id, const TCHAR* Name, float WeightKg, const FLinearColor& Color, const FVector& Scale, float Food, float Drink, int32 Uses, const TCHAR* Mesh)
		{
			FAedirItemDef Item = CatalogMakeMisc(Id, Name, EAedirItemClass::Food, WeightKg, Color, Scale);
			Item.FoodValue = Food;
			Item.DrinkValue = Drink;
			Item.Uses = Uses;
			Item.MeshPath = Mesh;
			Definitions.Add(Item);
		};
		AddFood(TEXT("Bread"), TEXT("Loaf of Bread"), 0.4f, FLinearColor(0.42f, 0.24f, 0.09f), FVector(0.15, 0.13, 0.08), 35.f, 0.f, 1, TEXT(""));
		AddFood(TEXT("Apple"), TEXT("Apple"), 0.2f, FLinearColor(0.45f, 0.06f, 0.04f), FVector(0.08, 0.08, 0.08), 12.f, 6.f, 1, TEXT(""));
		AddFood(TEXT("SmokedMeat"), TEXT("Smoked Meat"), 0.5f, FLinearColor(0.24f, 0.08f, 0.05f), FVector(0.2, 0.1, 0.06), 45.f, 0.f, 1, TEXT(""));
		AddFood(TEXT("WaterJug"), TEXT("Jug of Water"), 2.2f, FLinearColor(0.3f, 0.26f, 0.18f), FVector(0.2, 0.2, 0.3), 0.f, 40.f, 3, TEXT("/Game/Megascans/3D_Assets/JuteJug/SM_JuteJug.SM_JuteJug"));
		AddFood(TEXT("Carafe"), TEXT("Wooden Carafe"), 1.1f, FLinearColor(0.3f, 0.2f, 0.1f), FVector(0.12, 0.12, 0.26), 0.f, 30.f, 2, TEXT("/Game/Megascans/3D_Assets/OldWoodenCarafe/SM_OldWoodenCarafe.SM_OldWoodenCarafe"));

		const auto SetMesh = [&Definitions](const TCHAR* Id, const TCHAR* Mesh)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.MeshPath = Mesh;
				}
			}
		};
		SetMesh(TEXT("BattleAxe"), TEXT("/Game/Megascans/3D_Assets/Axe/SM_Axe.SM_Axe"));
		SetMesh(TEXT("Warhammer"), TEXT("/Game/Megascans/3D_Assets/OldHammer/SM_OldHammer.SM_OldHammer"));
		SetMesh(TEXT("Bread"), TEXT("/Game/Megascans/3D_Assets/BreadRoll/SM_BreadRoll.SM_BreadRoll"));
		SetMesh(TEXT("Apple"), TEXT("/Game/Megascans/3D_Assets/Apple03/SM_Apple03.SM_Apple03"));
		SetMesh(TEXT("SmokedMeat"), TEXT("/Game/Megascans/3D_Assets/SmokedMeat/SM_SmokedMeat.SM_SmokedMeat"));
		const auto SetBlade = [&Definitions](const TCHAR* Id, const TCHAR* Mesh, float LengthCm, float CenterCm, bool bTopForward = false)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.MeshPath = Mesh;
					Item.ModelLengthCm = LengthCm;
					Item.ModelCenterCm = CenterCm;
					Item.bModelTopForward = bTopForward;
				}
			}
		};
		SetBlade(TEXT("Longsword"), TEXT("/Game/Atris_swords/Meshes/SM_Sword1k.SM_Sword1k"), 147.f, -14.5f);
		SetBlade(TEXT("Sword"), TEXT("/Game/Atris_swords/Meshes/SM_Sword1h.SM_Sword1h"), 128.f, -12.f);
		SetBlade(TEXT("ShortSword"), TEXT("/Game/Atris_swords/Meshes/SM_Sword1e.SM_Sword1e"), 86.f, -6.8f);
		SetBlade(TEXT("BattleAxe"), TEXT("/Game/Atris_axes/Meshes/SM_axe5.SM_axe5"), 80.f, 0.f, true);
		SetBlade(TEXT("Dagger"), TEXT("/Game/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_Dagger_1.SM_Dagger_1"), 44.f, -6.f, true);
		SetBlade(TEXT("Mace"), TEXT("/Game/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_Mace.SM_Mace"), 65.f, 0.f, true);
		SetBlade(TEXT("Warhammer"), TEXT("/Game/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_GreatHammer.SM_GreatHammer"), 90.f, 0.f, true);
		SetBlade(TEXT("Spear"), TEXT("/Game/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_Spear.SM_Spear"), 180.f, 0.f, true);
		SetBlade(TEXT("Halberd"), TEXT("/Game/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_GreatAxe.SM_GreatAxe"), 175.f, 0.f, true);

		const auto SetWorn = [&Definitions](const TCHAR* Id, const TCHAR* Mesh)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.WornMeshPath = Mesh;
				}
			}
		};
		SetWorn(TEXT("IronCuirass"), TEXT("/Game/Megascans/3D_Assets/ManCuirass/SK_ManCuirass.SK_ManCuirass"));
		SetWorn(TEXT("Bracers"), TEXT("/Game/Megascans/3D_Assets/ManBracers/SK_ManBracers.SK_ManBracers"));

		SetGrip(TEXT("Dagger"), -21.f, 8.f, true);
		SetGrip(TEXT("ShortSword"), -42.f, 8.f, true);
		SetGrip(TEXT("Sword"), -65.f, 9.f, true);
		SetGrip(TEXT("Longsword"), -78.f, 11.f, true);
		SetGrip(TEXT("BattleAxe"), -28.f);
		SetGrip(TEXT("Mace"), -22.f);
		SetGrip(TEXT("Warhammer"), -32.f);
		SetGrip(TEXT("Club"), -20.f);
		SetGrip(TEXT("Spear"), -25.f, 40.f);
		SetGrip(TEXT("Quarterstaff"), -30.f, 50.f);
		SetGrip(TEXT("Halberd"), -35.f, 45.f);

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
