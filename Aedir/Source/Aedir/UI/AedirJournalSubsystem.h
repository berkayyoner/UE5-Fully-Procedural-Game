#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreatureTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirJournalSubsystem.generated.h"

USTRUCT(BlueprintType)
struct AEDIR_API FAedirQuestObjective
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	bool bDone = false;
};

USTRUCT(BlueprintType)
struct AEDIR_API FAedirQuest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	TArray<FAedirQuestObjective> Objectives;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	bool bActive = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	bool bCompleted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	FVector MarkerLocation = FVector::ZeroVector;

	// When set, an active quest shows a Mission marker on the compass and map.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Journal")
	bool bHasMarker = false;
};

// Local to each machine, not replicated.
UCLASS()
class AEDIR_API UAedirJournalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Replaces a quest with the same Id. The first active quest added becomes the tracked quest.
	UFUNCTION(BlueprintCallable, Category="Aedir|Journal")
	void AddQuest(const FAedirQuest& Quest);

	// Completes the quest once every objective is done, which also removes its marker.
	UFUNCTION(BlueprintCallable, Category="Aedir|Journal")
	void CompleteObjective(FName QuestId, int32 ObjectiveIndex);

	UFUNCTION(BlueprintCallable, Category="Aedir|Journal")
	void FailQuest(FName QuestId);

	UFUNCTION(BlueprintCallable, Category="Aedir|Journal")
	void RemoveQuest(FName QuestId);

	// Tracks an active quest. Passing None clears the tracked quest.
	UFUNCTION(BlueprintCallable, Category="Aedir|Journal")
	void SetActiveQuest(FName QuestId);

	UFUNCTION(BlueprintPure, Category="Aedir|Journal")
	FName GetTrackedQuest() const;

	const TArray<FAedirQuest>& GetQuests() const;

	static FText GetSkillRankName(float SkillValue);

	// Progress inside the current rank band, 0 at the band start and 1 at Master.
	static float GetSkillRankProgress01(float SkillValue);

	static FString FormatWeight(float Kg);

	static FString GetSkillDisplayName(EAedirSkill Skill);

	// Overall level: every 25 skill points raise it by one.
	static int32 GetCharacterLevel(const class UAedirStatsComponent* Stats);

private:
	FAedirQuest* FindQuest(FName QuestId);
	void SyncMarker(const FAedirQuest& Quest);
	void RemoveMarker(FName QuestId);
	static FName GetMarkerId(FName QuestId);

	TArray<FAedirQuest> Quests;
	FName TrackedQuestId;
};
