#include "Creature/AedirRaceCatalog.h"

namespace
{
	constexpr float RaceCatalogMinSizeScale = 0.6f;
	constexpr float RaceCatalogMaxSizeScale = 1.5f;

	FAedirSkillBonus RaceCatalogBonus(EAedirSkill Skill, float Bonus)
	{
		FAedirSkillBonus Result;
		Result.Skill = Skill;
		Result.Bonus = Bonus;
		return Result;
	}

	TArray<FAedirRaceDef> RaceCatalogBuild()
	{
		TArray<FAedirRaceDef> Races;

		FAedirRaceDef Human;
		Human.Id = FName(TEXT("Human"));
		Human.DisplayName = FText::FromString(TEXT("Human"));
		Human.Description = FText::FromString(TEXT("Short-lived and quick to adapt, humans are the mixed children of an elf and a dwarf. They inherit neither the endless years of the elves nor the stone-hard frame of the dwarves. Whatever trade or weapon comes their way, they learn it."));
		Human.MaxHealth = 100.f;
		Human.MaxMana = 80.f;
		Human.MaxStamina = 100.f;
		Human.Strength = 10.f;
		Human.Athletism = 10.f;
		Human.HeightMultiplier = 1.00f;
		Human.SkillBonuses =
		{
			RaceCatalogBonus(EAedirSkill::SingleHandWeapon, 5.f),
			RaceCatalogBonus(EAedirSkill::TwoHandedWeapon, 5.f),
			RaceCatalogBonus(EAedirSkill::RangedWeapon, 5.f),
			RaceCatalogBonus(EAedirSkill::Smelting, 5.f)
		};
		Races.Add(Human);

		FAedirRaceDef Elf;
		Elf.Id = FName(TEXT("Elf"));
		Elf.DisplayName = FText::FromString(TEXT("Elf"));
		Elf.Description = FText::FromString(TEXT("Ageless and drawn to knowledge, magic and perfection, the elves are the tallest and most magical of the peoples of Aedir. Their minds hold more magicka than any other race, and their spells are shaped with patient, unhurried care. Their bodies are slender, but their power runs deep."));
		Elf.MaxHealth = 90.f;
		Elf.MaxMana = 140.f;
		Elf.MaxStamina = 95.f;
		Elf.Strength = 8.f;
		Elf.Athletism = 11.f;
		Elf.HeightMultiplier = 1.08f;
		Elf.SkillBonuses =
		{
			RaceCatalogBonus(EAedirSkill::IllusionMagic, 10.f),
			RaceCatalogBonus(EAedirSkill::RegenerationMagic, 10.f),
			RaceCatalogBonus(EAedirSkill::Enchanting, 5.f),
			RaceCatalogBonus(EAedirSkill::MetamorphosisMagic, 5.f)
		};
		Races.Add(Elf);

		FAedirRaceDef DarkElf;
		DarkElf.Id = FName(TEXT("DarkElf"));
		DarkElf.DisplayName = FText::FromString(TEXT("Dark Elf"));
		DarkElf.Description = FText::FromString(TEXT("Once elves who lived underground, far from the sun, the dark elves have pale matte skin and senses sharpened by the dark. Their skin is sensitive to light, but they are keen with destruction magic and move quickly in light armor. Sunlight still stings their eyes, and they never quite forgave the surface for it."));
		DarkElf.MaxHealth = 100.f;
		DarkElf.MaxMana = 120.f;
		DarkElf.MaxStamina = 100.f;
		DarkElf.Strength = 9.f;
		DarkElf.Athletism = 12.f;
		DarkElf.HeightMultiplier = 1.03f;
		DarkElf.SkillBonuses =
		{
			RaceCatalogBonus(EAedirSkill::DestructionMagic, 10.f),
			RaceCatalogBonus(EAedirSkill::IllusionMagic, 5.f),
			RaceCatalogBonus(EAedirSkill::LightArmor, 10.f),
			RaceCatalogBonus(EAedirSkill::RangedWeapon, 5.f)
		};
		Races.Add(DarkElf);

		FAedirRaceDef Dwarf;
		Dwarf.Id = FName(TEXT("Dwarf"));
		Dwarf.DisplayName = FText::FromString(TEXT("Dwarf"));
		Dwarf.Description = FText::FromString(TEXT("Born of Fod and Cali, the dwarves are short, long-lived and strong. They are masters of fire, stone, mining and smithing, and their halls are carved deep beneath the mountains. Their hands are never idle at the forge."));
		Dwarf.MaxHealth = 130.f;
		Dwarf.MaxMana = 60.f;
		Dwarf.MaxStamina = 110.f;
		Dwarf.Strength = 14.f;
		Dwarf.Athletism = 8.f;
		Dwarf.HeightMultiplier = 0.88f;
		Dwarf.SkillBonuses =
		{
			RaceCatalogBonus(EAedirSkill::Smelting, 10.f),
			RaceCatalogBonus(EAedirSkill::HeavyArmor, 10.f),
			RaceCatalogBonus(EAedirSkill::TwoHandedWeapon, 5.f),
			RaceCatalogBonus(EAedirSkill::DestructionMagic, 5.f)
		};
		Races.Add(Dwarf);

		return Races;
	}
}

const TArray<FAedirRaceDef>& UAedirRaceCatalog::GetAll()
{
	static const TArray<FAedirRaceDef> Races = RaceCatalogBuild();
	return Races;
}

const FAedirRaceDef* UAedirRaceCatalog::Find(FName Id)
{
	for (const FAedirRaceDef& Race : GetAll())
	{
		if (Race.Id == Id)
		{
			return &Race;
		}
	}

	return nullptr;
}

TArray<FName> UAedirRaceCatalog::GetIds()
{
	TArray<FName> Ids;
	for (const FAedirRaceDef& Race : GetAll())
	{
		Ids.Add(Race.Id);
	}

	return Ids;
}

float UAedirRaceCatalog::ComputeRaceSizeScale(float StrengthDerivedScale, float HeightMultiplier)
{
	return FMath::Clamp(StrengthDerivedScale * HeightMultiplier, RaceCatalogMinSizeScale, RaceCatalogMaxSizeScale);
}

float UAedirRaceCatalog::GetStartSkillValue(const FAedirRaceDef& Race, EAedirSkill Skill)
{
	float Total = 0.f;
	for (const FAedirSkillBonus& Bonus : Race.SkillBonuses)
	{
		if (Bonus.Skill == Skill)
		{
			Total += Bonus.Bonus;
		}
	}

	return Total;
}
