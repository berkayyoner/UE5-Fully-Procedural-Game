#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "AedirMenuSubsystem.generated.h"

class APlayerController;
class SAedirCharacterCreation;
class SAedirMenu;
class SWidget;

// Owns the in-game menu and the character creation widgets for one local player.
UCLASS()
class AEDIR_API UAedirMenuSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// Category indices: 0 Inventory, 1 Magic, 2 Skills, 3 Map, 4 Journal. Changes the category when already open.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void OpenMenu(int32 StartCategory = 0);

	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void CloseMenu();

	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void ToggleMenu();

	UFUNCTION(BlueprintPure, Category="Aedir|Menu")
	bool IsMenuOpen() const;

	// Shows the race picker. Does nothing while the menu is open. Confirming sends the race to the possessed character.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void OpenCharacterCreation();

	UFUNCTION(BlueprintPure, Category="Aedir|Menu")
	bool IsCharacterCreationOpen() const;

	virtual void Deinitialize() override;

private:
	void BeginUIMode(APlayerController* PlayerController, const TSharedRef<SWidget>& FocusWidget);
	void RemoveUIWidget(const TSharedRef<SWidget>& ClosingWidget);
	void CloseCharacterCreation();
	void HandleRaceConfirmed(FName RaceId);

	TSharedPtr<SAedirMenu> MenuWidget;
	TSharedPtr<SAedirCharacterCreation> CreationWidget;
	bool bPausedByMenu = false;
};
