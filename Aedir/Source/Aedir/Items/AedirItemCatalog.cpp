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

		// Weapons, at the weight such weapons really had. Blades run along X with the hilt at -length/2; shafts are centred on the hand.
		Definitions.Add(CatalogMakeWeapon(TEXT("Dagger"), TEXT("Dagger"), 0.45f, 30.f, EAedirDamageType::Pierce, 4.f, false, CatalogSteel, FVector(0.30, 0.03, 0.006)));
		Definitions.Add(CatalogMakeWeapon(TEXT("ShortSword"), TEXT("Short Sword"), 0.95f, 70.f, EAedirDamageType::Slash, 8.f, false, CatalogSteel, FVector(0.70, 0.045, 0.008)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Sword"), TEXT("Sword"), 1.25f, 105.f, EAedirDamageType::Slash, 11.f, true, CatalogSteel, FVector(1.05, 0.05, 0.01)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Longsword"), TEXT("Longsword"), 1.6f, 118.f, EAedirDamageType::Slash, 14.f, true, CatalogSteel, FVector(1.18, 0.052, 0.011)));
		Definitions.Add(CatalogMakeWeapon(TEXT("BattleAxe"), TEXT("Battle Axe"), 1.8f, 80.f, EAedirDamageType::Slash, 12.f, false, CatalogWood, FVector(0.80, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Mace"), TEXT("Mace"), 1.7f, 65.f, EAedirDamageType::Blunt, 9.f, false, CatalogWood, FVector(0.65, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Warhammer"), TEXT("Warhammer"), 2.3f, 90.f, EAedirDamageType::Blunt, 13.f, false, CatalogWood, FVector(0.90, 0.03, 0.03)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Club"), TEXT("Club"), 1.3f, 70.f, EAedirDamageType::Blunt, 6.f, false, CatalogWood, FVector(0.62, 0.07, 0.07)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Spear"), TEXT("Spear"), 2.1f, 180.f, EAedirDamageType::Pierce, 9.f, true, CatalogWood, FVector(1.80, 0.025, 0.025)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Quarterstaff"), TEXT("Quarterstaff"), 1.9f, 170.f, EAedirDamageType::Blunt, 8.f, true, CatalogWood, FVector(1.70, 0.035, 0.035)));
		Definitions.Add(CatalogMakeWeapon(TEXT("Halberd"), TEXT("Halberd"), 3.1f, 175.f, EAedirDamageType::Slash, 15.f, true, CatalogWood, FVector(1.75, 0.03, 0.03)));

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

		// Odds and ends of a yard: everything that can be picked up is an item, nothing small stands fixed in the world.
		// Big things are carried in both arms and do not fit in a backpack; small ones are stowed like anything else.
		const auto AddThing = [&Definitions](const TCHAR* Id, const TCHAR* Name, float WeightKg, const FVector& Scale, const TCHAR* Mesh, bool bBig)
		{
			FAedirItemDef Item = CatalogMakeMisc(Id, Name, bBig ? EAedirItemClass::Object : EAedirItemClass::Unusable, WeightKg, FLinearColor(0.2f, 0.2f, 0.2f), Scale);
			Item.MeshPath = Mesh;
			// Long tools lie along their length; everything else stands as it was made.
			Item.bStandsUpright = Scale.X < Scale.Z * 2.0;
			Definitions.Add(Item);
		};
		AddThing(TEXT("Bucket"), TEXT("Wooden Bucket"), 3.f, FVector(0.3, 0.3, 0.32), TEXT("/Game/Megascans/3D_Assets/OldWoodenBucketA/SM_OldWoodenBucketA.SM_OldWoodenBucketA"), true);
		AddThing(TEXT("Chains"), TEXT("Pile of Chains"), 7.f, FVector(0.42, 0.42, 0.12), TEXT("/Game/Megascans/3D_Assets/PileOfChains/SM_PileOfChains.SM_PileOfChains"), true);
		AddThing(TEXT("Coal"), TEXT("Lump of Coal"), 0.6f, FVector(0.12, 0.1, 0.08), TEXT("/Game/Megascans/3D_Assets/CoalA/SM_CoalA.SM_CoalA"), false);
		AddThing(TEXT("EmptyBucket"), TEXT("Wooden Bucket"), 2.f, FVector(0.3, 0.3, 0.32), TEXT("/Game/Megascans/3D_Assets/OldWoodenBucketB/SM_OldWoodenBucketB.SM_OldWoodenBucketB"), true);
		AddThing(TEXT("MetalPot"), TEXT("Iron Pot"), 2.4f, FVector(0.28, 0.28, 0.2), TEXT("/Game/Megascans/3D_Assets/OldMetalPotA/SM_OldMetalPotA.SM_OldMetalPotA"), false);
		AddThing(TEXT("Shovel"), TEXT("Old Shovel"), 1.8f, FVector(1.1, 0.2, 0.05), TEXT("/Game/Megascans/3D_Assets/OldShovel/SM_OldShovel.SM_OldShovel"), false);
		AddThing(TEXT("Tongs"), TEXT("Smith's Tongs"), 1.2f, FVector(0.5, 0.08, 0.04), TEXT("/Game/Megascans/3D_Assets/BlacksmithTongs/SM_BlacksmithTongs.SM_BlacksmithTongs"), false);

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

		// Brings a weapon down to the size such a weapon really had, keeping where the hand sits on it.
		const auto Shrink = [&Definitions](const TCHAR* Id, float Share)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.WeaponLengthCm *= Share;
					Item.ModelLengthCm *= Share;
					Item.ModelCenterCm *= Share;
					Item.GripCm *= Share;
					Item.GripSpanCm = FMath::Max(Item.GripSpanCm * Share, 7.f);
					Item.BodyScale *= Share;
				}
			}
		};
		Shrink(TEXT("Dagger"), 0.8f);
		Shrink(TEXT("ShortSword"), 0.82f);
		Shrink(TEXT("Sword"), 0.76f);
		Shrink(TEXT("Longsword"), 0.82f);
		Shrink(TEXT("BattleAxe"), 0.85f);
		Shrink(TEXT("Mace"), 0.9f);
		Shrink(TEXT("Warhammer"), 0.82f);
		Shrink(TEXT("Club"), 0.9f);
		Shrink(TEXT("Halberd"), 0.92f);
		// The great axe model stands for a halberd: a real one is a slim shaft with a narrow head.
		for (FAedirItemDef& Item : Definitions)
		{
			if (Item.Id == FName(TEXT("Halberd")))
			{
				Item.ModelThickShare = 0.5f;
			}
		}

		// Weapons from the Fab library and the rest of the Atris sets: each model is fitted by where its guard or head really is,
		// so the hand closes on its grip, the blade's box runs from the guard to the point, and the point faces forward.
		const auto AddModelWeapon = [&Definitions](const TCHAR* Id, const TCHAR* Name, float WeightKg, float ModelLengthCm, float ModelCenterCm, float BladeCm, EAedirDamageType Type, float Flat, bool bTwoHanded, const FVector& Box, const TCHAR* Mesh, bool bTopForward, bool bReversed, float GripCm, float SpanCm, float ThickShare)
		{
			FAedirItemDef Item = CatalogMakeWeapon(Id, Name, WeightKg, BladeCm, Type, Flat, bTwoHanded, CatalogSteel, Box);
			Item.MeshPath = Mesh;
			Item.ModelLengthCm = ModelLengthCm;
			Item.ModelCenterCm = ModelCenterCm;
			Item.bModelTopForward = bTopForward;
			Item.bModelReversed = bReversed;
			Item.GripCm = GripCm;
			Item.GripSpanCm = SpanCm;
			// Blades (a flat box) hang point down and back from the hip; axes and maces, thick at the head, hang by the head.
			Item.bHangsDiagonal = Type != EAedirDamageType::Blunt && BladeCm < 100.f && Box.Y > 0.04;
			Item.ModelThickShare = ThickShare;
			Definitions.Add(Item);
		};
		AddModelWeapon(TEXT("BloodySword"), TEXT("Bloody Sword"), 1.20f, 95.0f, -10.1f, 74.8f, EAedirDamageType::Slash, 11.0f, false, FVector(0.748, 0.048, 0.010), TEXT("/Game/Aedir/Fab/BloodySword/Sword_uitlbiaga_High.Sword_uitlbiaga_High"), true, false, -47.1f, 11.1f, 1.00f);
		AddModelWeapon(TEXT("RustySword"), TEXT("Rusty Sword"), 1.10f, 88.0f, -9.4f, 69.3f, EAedirDamageType::Slash, 9.0f, false, FVector(0.693, 0.048, 0.010), TEXT("/Game/Aedir/Fab/RustySword/uh2tagpfa_LOD0_TIER1_000.uh2tagpfa_LOD0_TIER1_000"), false, false, -43.6f, 10.3f, 1.00f);
		AddModelWeapon(TEXT("IronSword"), TEXT("Iron Sword"), 1.15f, 92.0f, -8.6f, 74.8f, EAedirDamageType::Slash, 10.0f, false, FVector(0.748, 0.048, 0.010), TEXT("/Game/Aedir/Fab/IronSword/viking_sword/StaticMeshes/viking_sword.viking_sword"), false, false, -45.7f, 9.5f, 1.00f);
		AddModelWeapon(TEXT("SteelSword"), TEXT("Steel Sword"), 1.45f, 116.0f, -13.8f, 88.5f, EAedirDamageType::Slash, 13.0f, true, FVector(0.885, 0.050, 0.010), TEXT("/Game/Aedir/Fab/SteelSword/Sword_Long_A.Sword_Long_A"), false, false, -57.4f, 15.2f, 1.00f);
		AddModelWeapon(TEXT("SilverShortsword"), TEXT("Silver Shortsword"), 0.90f, 72.0f, -9.5f, 53.1f, EAedirDamageType::Slash, 9.0f, false, FVector(0.531, 0.048, 0.010), TEXT("/Game/Aedir/Fab/SilverShortsword/scene/StaticMeshes/scene.scene"), false, true, -35.6f, 10.4f, 1.00f);
		AddModelWeapon(TEXT("AshwardBlade"), TEXT("Ashward Blade"), 1.30f, 100.0f, -11.9f, 76.2f, EAedirDamageType::Slash, 12.0f, false, FVector(0.762, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1a.SM_Sword1a"), false, false, -49.5f, 13.1f, 1.00f);
		AddModelWeapon(TEXT("WardenSword"), TEXT("Warden's Sword"), 1.30f, 98.0f, -11.6f, 74.7f, EAedirDamageType::Slash, 12.0f, false, FVector(0.747, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1b.SM_Sword1b"), false, false, -48.5f, 12.8f, 1.00f);
		AddModelWeapon(TEXT("AncestralSword"), TEXT("Ancestral Sword"), 1.25f, 96.0f, -15.0f, 66.0f, EAedirDamageType::Slash, 12.0f, false, FVector(0.660, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1d.SM_Sword1d"), false, false, -47.4f, 16.5f, 1.00f);
		AddModelWeapon(TEXT("RidgeSword"), TEXT("Ridge Sword"), 1.15f, 90.0f, -12.9f, 64.1f, EAedirDamageType::Slash, 11.0f, false, FVector(0.641, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1f.SM_Sword1f"), false, false, -44.5f, 14.2f, 1.00f);
		AddModelWeapon(TEXT("SlenderSword"), TEXT("Slender Sword"), 1.00f, 88.0f, -9.5f, 69.0f, EAedirDamageType::Slash, 10.0f, false, FVector(0.690, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1g.SM_Sword1g"), false, false, -43.6f, 10.5f, 1.00f);
		AddModelWeapon(TEXT("Cleaver"), TEXT("Cleaver"), 1.30f, 68.0f, -8.0f, 52.0f, EAedirDamageType::Slash, 11.0f, false, FVector(0.520, 0.048, 0.010), TEXT("/Game/Atris_swords/Meshes/SM_Sword1j.SM_Sword1j"), false, false, -33.7f, 8.8f, 1.00f);
		AddModelWeapon(TEXT("Axe"), TEXT("Axe"), 1.30f, 62.0f, 0.0f, 62.0f, EAedirDamageType::Slash, 11.0f, false, FVector(0.620, 0.030, 0.030), TEXT("/Game/Aedir/Fab/Axe/Axe.Axe"), false, true, -21.1f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("ElvenBattleaxe"), TEXT("Elven Battleaxe"), 2.40f, 100.0f, 0.0f, 100.0f, EAedirDamageType::Slash, 16.0f, true, FVector(1.000, 0.030, 0.030), TEXT("/Game/Aedir/Fab/ElvenBattleaxe/SF_Golden_double_axe_NakedSingularity.SF_Golden_double_axe_NakedSingularity"), false, false, -34.0f, 35.0f, 0.55f);
		AddModelWeapon(TEXT("RustyIronMace"), TEXT("Rusty Iron Mace"), 1.60f, 60.0f, 0.0f, 60.0f, EAedirDamageType::Blunt, 10.0f, false, FVector(0.600, 0.030, 0.030), TEXT("/Game/Aedir/Fab/RustyIronMace/SM_Mace.SM_Mace"), false, false, -20.4f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("WoodsmanAxe"), TEXT("Woodsman's Axe"), 1.50f, 75.0f, 0.0f, 75.0f, EAedirDamageType::Slash, 12.0f, false, FVector(0.750, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe2.SM_axe2"), true, false, -25.5f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("BeardedGreataxe"), TEXT("Bearded Greataxe"), 2.60f, 100.0f, 0.0f, 100.0f, EAedirDamageType::Slash, 16.0f, true, FVector(1.000, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe3.SM_axe3"), true, false, -34.0f, 35.0f, 1.00f);
		AddModelWeapon(TEXT("HandAxe"), TEXT("Hand Axe"), 0.90f, 50.0f, 0.0f, 50.0f, EAedirDamageType::Slash, 8.0f, false, FVector(0.500, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe4.SM_axe4"), true, false, -17.0f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("RaiderAxe"), TEXT("Raider's Axe"), 1.40f, 70.0f, 0.0f, 70.0f, EAedirDamageType::Slash, 12.0f, false, FVector(0.700, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe6.SM_axe6"), true, false, -23.8f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("CrescentAxe"), TEXT("Crescent Axe"), 1.60f, 76.0f, 0.0f, 76.0f, EAedirDamageType::Slash, 13.0f, false, FVector(0.760, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe7a.SM_axe7a"), true, false, -25.8f, 13.0f, 1.00f);
		AddModelWeapon(TEXT("WarAxe"), TEXT("War Axe"), 1.80f, 80.0f, 0.0f, 80.0f, EAedirDamageType::Slash, 14.0f, false, FVector(0.800, 0.030, 0.030), TEXT("/Game/Atris_axes/Meshes/SM_axe8.SM_axe8"), true, false, -27.2f, 13.0f, 1.00f);

		// Armour from the Fab library, worn as it was modelled on the bone it sits on: the helmet on the head, its face forward;
		// the shield strapped to the left forearm, its boss outward; the pauldrons over both shoulders.
		const auto AddWornModel = [&Definitions](const TCHAR* Id, const TCHAR* Name, float WeightKg, EAedirBodyZone Zone, float Protection, const TCHAR* Bone, const FVector& Box, const TCHAR* Mesh, float ModelLengthCm, const FRotator& Rotation, const FVector& Offset)
		{
			FAedirItemDef Item = CatalogMakeArmor(Id, Name, WeightKg, Zone, Protection, Bone, CatalogIron, Box);
			Item.MeshPath = Mesh;
			Item.ModelLengthCm = ModelLengthCm;
			Item.bModelAsIs = true;
			Item.bStandsUpright = true;
			Item.WornRotation = Rotation;
			Item.WornOffsetCm = Offset;
			Definitions.Add(Item);
		};
		const auto Axes = [](const FVector& X, const FVector& Y, const FVector& Z) { return FMatrix(X, Y, Z, FVector::ZeroVector).Rotator(); };
		AddWornModel(TEXT("IronSouthernHelmet"), TEXT("Iron Southern Helmet"), 2.2f, EAedirBodyZone::Head, 0.6f, TEXT("head"), FVector(0.24, 0.26, 0.3),
			TEXT("/Game/Aedir/Fab/IronSouthernHelmet/helmet.helmet"), 41.f, Axes(FVector(0, 0, -1), FVector(0, 1, 0), FVector(1, 0, 0)), FVector(-7.f, 2.f, 0.f));
		AddWornModel(TEXT("RustyIronShield"), TEXT("Rusty Iron Shield"), 4.5f, EAedirBodyZone::Arm, 0.7f, TEXT("lowerarm_l"), FVector(0.6, 0.6, 0.08),
			TEXT("/Game/Aedir/Fab/RustyIronShield/Historic_Medieval_Armor_Shield_Metal_Round_01_xiwkcgy_High.Historic_Medieval_Armor_Shield_Metal_Round_01_xiwkcgy_High"), 62.f, FRotator::ZeroRotator, FVector(13.f, 0.f, 9.f));
		AddWornModel(TEXT("RustyShoulders"), TEXT("Rusty Layered Pauldrons"), 3.f, EAedirBodyZone::Arm, 0.35f, TEXT("spine_05"), FVector(0.2, 0.5, 0.2),
			TEXT("/Game/Aedir/Fab/RustyShoulders/shoulder.shoulder"), 64.f, Axes(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0)), FVector(6.f, -1.f, 0.f));

		// The steel longsword came as a blade with its guard and grip in separate parts.
		AddModelWeapon(TEXT("SteelLongsword"), TEXT("Steel Longsword"), 1.6f, 96.8f, 0.f, 96.8f, EAedirDamageType::Slash, 15.f, true, FVector(0.968, 0.05, 0.01),
			TEXT("/Game/Aedir/Fab/SteelLongsword/scene/StaticMeshes/defaultMaterial.defaultMaterial"), false, true, -58.4f, 15.f, 1.f);
		for (FAedirItemDef& Item : Definitions)
		{
			if (Item.Id == FName(TEXT("SteelLongsword")))
			{
				Item.ExtraMeshPaths = {
					TEXT("/Game/Aedir/Fab/SteelLongsword/scene/StaticMeshes/defaultMaterial_fd62cd4a56349dc7e26ee6101f086fee.defaultMaterial_fd62cd4a56349dc7e26ee6101f086fee"),
					TEXT("/Game/Aedir/Fab/SteelLongsword/scene/StaticMeshes/defaultMaterial_a60f1a9783c5539b702e73b37cf282d1.defaultMaterial_a60f1a9783c5539b702e73b37cf282d1"),
					TEXT("/Game/Aedir/Fab/SteelLongsword/scene/StaticMeshes/defaultMaterial_6b8974456b5c21b2111eb0640c4a41c7.defaultMaterial_6b8974456b5c21b2111eb0640c4a41c7") };
			}
		}

		// Each model measured on its own: the edge of a blade and the head of an axe across the fingers, as a hand really holds
		// them, and the handle's middle in the palm. The elven axe was modelled lying aslant and is stood along its haft.
		const auto SetModelFix = [&Definitions, &Axes](const TCHAR* Id, const FVector& X, const FVector& Y, const FVector& Z, const FVector& Shift)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.ModelFixRotation = Axes(X, Y, Z);
					Item.ModelShiftCm = Shift;
				}
			}
		};
		SetModelFix(TEXT("BloodySword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 0.589f));
		SetModelFix(TEXT("RustySword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("AshwardBlade"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("WardenSword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("AncestralSword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("RidgeSword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("SlenderSword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("Cleaver"), FVector(0.991f, -0.134f, 0.005f), FVector(-0.005f, 0.001f, 1.f), FVector(-0.134f, -0.991f, 0.f), FVector(0.f, -2.553f, 0.408f));
		SetModelFix(TEXT("Axe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -6.446f, 0.f));
		SetModelFix(TEXT("ElvenBattleaxe"), FVector(0.885f, -0.466f, -0.006f), FVector(-0.005f, 0.003f, -1.f), FVector(0.466f, 0.885f, 0.f), FVector(0.f, -1.131f, 0.f));
		SetModelFix(TEXT("WoodsmanAxe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 3.009f, 0.f));
		SetModelFix(TEXT("BeardedGreataxe"), FVector(0.992f, 0.017f, -0.125f), FVector(0.125f, 0.002f, 0.992f), FVector(0.017f, -1.f, 0.f), FVector(0.f, 1.102f, -6.531f));
		SetModelFix(TEXT("HandAxe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector(0.f, 3.768f, 0.f));
		SetModelFix(TEXT("RaiderAxe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -0.418f, 0.f));
		SetModelFix(TEXT("CrescentAxe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 9.294f, 0.f));
		SetModelFix(TEXT("WarAxe"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 1.166f, 0.f));
		SetModelFix(TEXT("Longsword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -0.739f, 0.f));
		SetModelFix(TEXT("Sword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector(0.f, 3.62f, 0.f));
		SetModelFix(TEXT("ShortSword"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("BattleAxe"), FVector(0.994f, 0.111f, -0.003f), FVector(-0.003f, 0.f, -1.f), FVector(-0.111f, 0.994f, 0.f), FVector(0.f, -5.307f, 0.f));
		SetModelFix(TEXT("Dagger"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("Mace"), FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, 0.f, 0.666f));
		SetModelFix(TEXT("Warhammer"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector::ZeroVector);
		SetModelFix(TEXT("Spear"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, -1.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.886f, 0.f));
		SetModelFix(TEXT("Halberd"), FVector(1.f, 0.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(0.f, -1.f, 0.f), FVector(0.f, 14.03f, 0.f));
		// What each piece of armour is made of and the bones it covers. Whatever it does not cover is bare: the neck, the hands,
		// the thighs under greaves, the groin under a cuirass, the face under an open helmet.
		const auto SetArmour = [&Definitions](const TCHAR* Id, EAedirArmorMaterial Material, std::initializer_list<const TCHAR*> Bones, bool bOpenFace = false)
		{
			for (FAedirItemDef& Item : Definitions)
			{
				if (Item.Id == FName(Id))
				{
					Item.ArmorMaterial = Material;
					Item.ArmorBones.Reset();
					for (const TCHAR* Bone : Bones)
					{
						Item.ArmorBones.Add(FName(Bone));
					}
					Item.bOpenFace = bOpenFace;
				}
			}
		};
		SetArmour(TEXT("LeatherCap"), EAedirArmorMaterial::Soft, { TEXT("head") }, true);
		SetArmour(TEXT("IronHelm"), EAedirArmorMaterial::Plate, { TEXT("head") }, true);
		SetArmour(TEXT("IronSouthernHelmet"), EAedirArmorMaterial::Plate, { TEXT("head") }, true);
		SetArmour(TEXT("LeatherArmor"), EAedirArmorMaterial::Soft, { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") });
		SetArmour(TEXT("IronCuirass"), EAedirArmorMaterial::Plate, { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") });
		SetArmour(TEXT("Bracers"), EAedirArmorMaterial::Soft, { TEXT("lowerarm_l"), TEXT("lowerarm_r") });
		SetArmour(TEXT("IronGreaves"), EAedirArmorMaterial::Plate, { TEXT("calf_l"), TEXT("calf_r") });
		SetArmour(TEXT("RustyShoulders"), EAedirArmorMaterial::Plate, { TEXT("clavicle_l"), TEXT("clavicle_r"), TEXT("upperarm_l"), TEXT("upperarm_r") });
		SetArmour(TEXT("RustyIronShield"), EAedirArmorMaterial::Plate, { TEXT("lowerarm_l"), TEXT("hand_l") });
		// Any other armour covers the bones of its own part of the body.
		for (FAedirItemDef& Item : Definitions)
		{
			if (Item.ItemClass == EAedirItemClass::Armor && Item.ArmorBones.IsEmpty())
			{
				switch (Item.ArmorZone)
				{
				case EAedirBodyZone::Head: Item.ArmorBones = { TEXT("head") }; break;
				case EAedirBodyZone::Torso: Item.ArmorBones = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") }; break;
				case EAedirBodyZone::Arm: Item.ArmorBones = { TEXT("lowerarm_l"), TEXT("lowerarm_r") }; break;
				case EAedirBodyZone::Leg: Item.ArmorBones = { TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r") }; break;
				}
			}
		}

		// Stood along its haft the elven axe is longer than its slanted box: brought back to its length.
		for (FAedirItemDef& Item : Definitions)
		{
			if (Item.Id == FName(TEXT("ElvenBattleaxe")))
			{
				Item.ModelLengthCm *= 0.912f;
			}
		}

		for (FAedirItemDef& Item : Definitions)
		{
			if (Item.Id == FName(TEXT("RustySword")))
			{
				Item.ScabbardMeshPath = TEXT("/Game/Aedir/Fab/RustySword/uh2tagpfa_LOD0_TIER1_001.uh2tagpfa_LOD0_TIER1_001");
				Item.ScabbardLengthCm = Item.WeaponLengthCm * 1.05f;
			}
		}

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
