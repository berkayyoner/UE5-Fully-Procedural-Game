#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "AedirMenuSubsystem.generated.h"

class APlayerController;
class SAedirCharacterCreation;
class SAedirMap;
class SAedirMenu;
class SAedirPauseMenu;
class SAedirRadialMenu;
class SWidget;
enum class EAedirRadialOption : uint8;

// Owns the Tab selector, the category menu, the Esc pause menu, the map and the character creation widgets for one local player.
// Only one of them is open at a time, opening another closes the previous one.
UCLASS()
class AEDIR_API UAedirMenuSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	// Category indices: 0 Inventory, 1 Magic, 2 Skills open the category menu (and change its category when already open),
	// 3 opens the map and 4 opens the Journal tab of the pause menu.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void OpenMenu(int32 StartCategory = 0);

	// Closes whichever menu is open.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void CloseMenu();

	// Opens the Tab selector, or closes the open menu.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void ToggleMenu();

	// Opens the pause menu, or closes it when it is already open. Tab 0 is Options, 1 is Journal.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void TogglePauseMenu(int32 StartTab = 0);

	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void OpenMap();

	// True while the selector, category menu, pause menu or map is open.
	UFUNCTION(BlueprintPure, Category="Aedir|Menu")
	bool IsMenuOpen() const;

	// True while a menu with a title in the top left corner is open: the category menu or the pause menu.
	bool IsListMenuOpen() const;

	// Shows the race picker. Does nothing while a menu is open. Confirming sends the race to the possessed character.
	UFUNCTION(BlueprintCallable, Category="Aedir|Menu")
	void OpenCharacterCreation();

	UFUNCTION(BlueprintPure, Category="Aedir|Menu")
	bool IsCharacterCreationOpen() const;

	virtual void Deinitialize() override;

private:
	// Closes every menu and returns the controller when a new one may open.
	APlayerController* PrepareToOpen();
	void BeginUIMode(APlayerController* PlayerController, const TSharedRef<SWidget>& FocusWidget);
	void RemoveUIWidget(const TSharedRef<SWidget>& ClosingWidget);
	void OpenCategoryMenu(int32 StartCategory);
	void OpenRadialMenu();
	void OpenPauseMenu(int32 StartTab);
	void CloseRadialMenu();
	void ClosePauseMenu();
	void CloseMap();
	void CloseCharacterCreation();
	void HandleRadialChosen(EAedirRadialOption Option);
	void HandleRaceConfirmed(FName RaceId);
	FText HandleMapTravelQuery(FName MarkerId);
	void HandleMapTravel(FName MarkerId);

	TSharedPtr<SAedirMenu> MenuWidget;
	TSharedPtr<SAedirRadialMenu> RadialWidget;
	TSharedPtr<SAedirPauseMenu> PauseWidget;
	TSharedPtr<SAedirMap> MapWidget;
	TSharedPtr<SAedirCharacterCreation> CreationWidget;
	bool bPausedByMenu = false;
};
