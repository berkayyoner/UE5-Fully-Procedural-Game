#include "UI/AedirJournalSubsystem.h"

#include "Engine/World.h"
#include "HUD/AedirMarkerSubsystem.h"

void UAedirJournalSubsystem::AddQuest(const FAedirQuest& Quest)
{
	if (Quest.Id.IsNone())
	{
		return;
	}

	if (FAedirQuest* Existing = FindQuest(Quest.Id))
	{
		*Existing = Quest;
	}
	else
	{
		Quests.Add(Quest);
	}

	if (const FAedirQuest* Stored = FindQuest(Quest.Id))
	{
		SyncMarker(*Stored);

		if (TrackedQuestId.IsNone() && Stored->bActive && !Stored->bCompleted)
		{
			TrackedQuestId = Stored->Id;
		}
	}
}

void UAedirJournalSubsystem::CompleteObjective(FName QuestId, int32 ObjectiveIndex)
{
	FAedirQuest* Quest = FindQuest(QuestId);
	if (!Quest || !Quest->bActive || Quest->bCompleted || !Quest->Objectives.IsValidIndex(ObjectiveIndex))
	{
		return;
	}

	Quest->Objectives[ObjectiveIndex].bDone = true;

	bool bAllDone = true;
	for (const FAedirQuestObjective& Objective : Quest->Objectives)
	{
		if (!Objective.bDone)
		{
			bAllDone = false;
			break;
		}
	}

	if (bAllDone)
	{
		Quest->bCompleted = true;
		Quest->bActive = false;
		RemoveMarker(QuestId);

		if (TrackedQuestId == QuestId)
		{
			TrackedQuestId = NAME_None;
		}
	}
}

void UAedirJournalSubsystem::FailQuest(FName QuestId)
{
	FAedirQuest* Quest = FindQuest(QuestId);
	if (!Quest || Quest->bCompleted)
	{
		return;
	}

	Quest->bActive = false;
	RemoveMarker(QuestId);

	if (TrackedQuestId == QuestId)
	{
		TrackedQuestId = NAME_None;
	}
}

void UAedirJournalSubsystem::RemoveQuest(FName QuestId)
{
	RemoveMarker(QuestId);
	Quests.RemoveAll([QuestId](const FAedirQuest& Quest)
	{
		return Quest.Id == QuestId;
	});

	if (TrackedQuestId == QuestId)
	{
		TrackedQuestId = NAME_None;
	}
}

void UAedirJournalSubsystem::SetActiveQuest(FName QuestId)
{
	if (QuestId.IsNone())
	{
		TrackedQuestId = NAME_None;
		return;
	}

	const FAedirQuest* Quest = FindQuest(QuestId);
	if (Quest && Quest->bActive && !Quest->bCompleted)
	{
		TrackedQuestId = QuestId;
	}
}

FName UAedirJournalSubsystem::GetTrackedQuest() const
{
	return TrackedQuestId;
}

const TArray<FAedirQuest>& UAedirJournalSubsystem::GetQuests() const
{
	return Quests;
}

FText UAedirJournalSubsystem::GetSkillRankName(float SkillValue)
{
	if (SkillValue >= 100.f)
	{
		return FText::FromString(TEXT("Master"));
	}
	if (SkillValue >= 75.f)
	{
		return FText::FromString(TEXT("Expert"));
	}
	if (SkillValue >= 50.f)
	{
		return FText::FromString(TEXT("Adept"));
	}
	if (SkillValue >= 25.f)
	{
		return FText::FromString(TEXT("Apprentice"));
	}
	return FText::FromString(TEXT("Novice"));
}

float UAedirJournalSubsystem::GetSkillRankProgress01(float SkillValue)
{
	if (SkillValue >= 100.f)
	{
		return 1.f;
	}

	const float Clamped = FMath::Clamp(SkillValue, 0.f, 100.f);
	const float BandStart = FMath::FloorToFloat(Clamped / 25.f) * 25.f;
	return FMath::Clamp((Clamped - BandStart) / 25.f, 0.f, 1.f);
}

FString UAedirJournalSubsystem::FormatWeight(float Kg)
{
	return FString::Printf(TEXT("%.1f"), Kg);
}

FString UAedirJournalSubsystem::GetSkillDisplayName(EAedirSkill Skill)
{
	switch (Skill)
	{
	case EAedirSkill::SingleHandWeapon: return TEXT("One-Handed");
	case EAedirSkill::TwoHandedWeapon: return TEXT("Two-Handed");
	case EAedirSkill::RangedWeapon: return TEXT("Archery");
	case EAedirSkill::DestructionMagic: return TEXT("Destruction");
	case EAedirSkill::RegenerationMagic: return TEXT("Restoration");
	case EAedirSkill::IllusionMagic: return TEXT("Illusion");
	case EAedirSkill::MetamorphosisMagic: return TEXT("Alteration");
	case EAedirSkill::SummonMagic: return TEXT("Conjuration");
	case EAedirSkill::HeavyArmor: return TEXT("Heavy Armor");
	case EAedirSkill::LightArmor: return TEXT("Light Armor");
	case EAedirSkill::Smelting: return TEXT("Smithing");
	case EAedirSkill::Alchemy: return TEXT("Alchemy");
	case EAedirSkill::Enchanting: return TEXT("Enchanting");
	default: break;
	}
	return TEXT("Unknown");
}

FAedirQuest* UAedirJournalSubsystem::FindQuest(FName QuestId)
{
	return Quests.FindByPredicate([QuestId](const FAedirQuest& Quest)
	{
		return Quest.Id == QuestId;
	});
}

void UAedirJournalSubsystem::SyncMarker(const FAedirQuest& Quest)
{
	if (!Quest.bHasMarker || !Quest.bActive || Quest.bCompleted)
	{
		RemoveMarker(Quest.Id);
		return;
	}

	UWorld* World = GetWorld();
	UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers)
	{
		return;
	}

	FAedirMapMarker Marker;
	Marker.Id = GetMarkerId(Quest.Id);
	Marker.Label = Quest.Title;
	Marker.Location = Quest.MarkerLocation;
	Marker.Type = EAedirMarkerType::Mission;
	Markers->AddMarker(Marker);
}

void UAedirJournalSubsystem::RemoveMarker(FName QuestId)
{
	UWorld* World = GetWorld();
	UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (Markers)
	{
		Markers->RemoveMarker(GetMarkerId(QuestId));
	}
}

FName UAedirJournalSubsystem::GetMarkerId(FName QuestId)
{
	return FName(*FString::Printf(TEXT("Quest.%s"), *QuestId.ToString()));
}
