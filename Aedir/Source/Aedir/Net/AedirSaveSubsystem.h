#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AedirSaveSubsystem.generated.h"

class APlayerController;

UCLASS(BlueprintType)
class AEDIR_API UAedirSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// Server or standalone only.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool SaveWorld(const FString& Slot);

	// Server or standalone only. Fails when the saved map differs from the current level.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool LoadWorld(const FString& Slot);

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool DoesSaveExist(const FString& Slot) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	bool DeleteSave(const FString& Slot);

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	FDateTime GetSaveTimestamp(const FString& Slot) const;

	// Server or standalone only. Replaces any running autosave.
	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	void StartAutosave(const FString& Slot, float IntervalSeconds);

	UFUNCTION(BlueprintCallable, Category="Aedir|Save")
	void StopAutosave();

private:
	static FString MakePlayerKey(const APlayerController* PlayerController);
	void AutosaveTick();

	FTimerHandle AutosaveTimerHandle;
	FString AutosaveSlot;
};
