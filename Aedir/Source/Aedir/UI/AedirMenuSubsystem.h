#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "AedirMenuSubsystem.generated.h"

class SAedirMenu;

// Owns the in-game menu widget for one local player.
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

	virtual void Deinitialize() override;

private:
	TSharedPtr<SAedirMenu> MenuWidget;
	bool bPausedByMenu = false;
};
