#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AedirSaveSubsystem.generated.h"

class APlayerController;

// One entry of the save list.
class AAedirCreature;
struct FAedirCreatureSave;

USTRUCT(BlueprintType)
struct AEDIR_API FAedirSaveInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	FString Slot;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	FString DisplayName;

	// UTC, as stored in the save.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	FDateTime Timestamp = FDateTime(0);

	// 0 when the save was written before levels were stored.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	int32 PlayerLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	FString Location;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Save")
	FString MapName;
};

// Slot naming and list ordering, free of any file access.
namespace AedirSaveSlots
{
	inline const TCHAR* const QuickSaveSlot = TEXT("QuickSave");

	// 1 becomes "Save_001".
	AEDIR_API FString MakeSlotName(int32 Number);

	// "Save_012" gives 12. Anything that is not a numbered slot gives INDEX_NONE.
	AEDIR_API int32 ParseSlotNumber(const FString& Slot);

	// One above the highest numbered slot, so the number of a deleted save in the middle is not reused.
	AEDIR_API FString NextSlotName(const TArray<FString>& ExistingSlots);

	// "Save_001.sav" gives "Save_001". Files that are not saves give an empty string.
	AEDIR_API FString SlotFromFileName(const FString& FileName);

	// "QuickSave" reads "Quick Save", "Save_012" reads "Save 12", other slots keep their name.
	AEDIR_API FString MakeDisplayName(const FString& Slot);

	// Newest first. Equal times fall back to the slot name so the order never flickers.
	AEDIR_API void SortNewestFirst(TArray<FAedirSaveInfo>& Saves);

	// "Level 3  -  Riverwood", leaving out whatever is not known.
	AEDIR_API FString MakeSummary(int32 PlayerLevel, const FString& Location);

	AEDIR_API FString FormatTimestamp(const FDateTime& Time);
}

UCLASS(BlueprintType)
class AEDIR_API UAedirSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static FString MakePlayerKey(const APlayerController* PlayerController);
	virtual void Deinitialize() override;

	// Server or standalone only.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool SaveWorld(const FString& Slot);

	// Server or standalone only. Fails when the saved map differs from the current level.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool LoadWorld(const FString& Slot);

	// Which save this play belongs to. The host writes the world back to it, a guest only the character.
	void SetCurrentSlot(const FString& Slot) { CurrentSlot = Slot; }
	const FString& GetCurrentSlot() const { return CurrentSlot; }
	void SetGuest(bool bNewGuest) { bGuest = bNewGuest; }
	bool IsGuest() const { return bGuest; }

	// A guest: the character of this play goes into its own save, and nothing else does.
	bool StoreGuestCharacter(const FString& Slot, const APlayerController* PlayerController);
	void GetCharacterOfSlot(const FString& Slot, FAedirCreatureSave& OutCharacter, bool& bOutFound) const;
	// Server only: gives a body the character a guest brought along.
	void ApplyCharacter(AAedirCreature* Creature, const FAedirCreatureSave& Character);
	FString PendingLoadSlot;

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool DoesSaveExist(const FString& Slot) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool DeleteSave(const FString& Slot);

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	FDateTime GetSaveTimestamp(const FString& Slot) const;

	// Every Aedir save on disk, newest first. Reads each file, so call it once per menu page and not per frame.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	TArray<FAedirSaveInfo> ListSaves() const;

	// Writes the next free Save_NNN slot. Returns its name, or an empty string when saving failed.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	FString SaveToNewSlot();

	// Server or standalone only. Replaces any running autosave.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	void StartAutosave(const FString& Slot, float IntervalSeconds);

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	void StopAutosave();

private:
	static void ApplyGuestSafeCharacter(AAedirCreature* Creature, const FAedirCreatureSave& CreatureSave);
	FString CurrentSlot;
	bool bGuest = false;
	static TArray<FString> FindSlotsOnDisk();
	void AutosaveTick();

	FTimerHandle AutosaveTimerHandle;
	FString AutosaveSlot;
};
