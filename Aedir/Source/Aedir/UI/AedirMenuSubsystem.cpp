#include "UI/AedirMenuSubsystem.h"

#include "Creature/AedirCreature.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/SAedirMenu.h"

void UAedirMenuSubsystem::OpenMenu(int32 StartCategory)
{
	if (MenuWidget.IsValid())
	{
		MenuWidget->SetCategory(StartCategory);
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (!PlayerController || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	AAedirCreature* Creature = Cast<AAedirCreature>(PlayerController->GetPawn());

	MenuWidget = SNew(SAedirMenu)
		.Creature(TWeakObjectPtr<AAedirCreature>(Creature))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	MenuWidget->SetCategory(StartCategory);
	GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 100);

	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(MenuWidget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(false);

	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::SetGamePaused(World, true);
		bPausedByMenu = true;
	}

	if (Creature)
	{
		Creature->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::CloseMenu()
{
	if (!MenuWidget.IsValid())
	{
		return;
	}

	const TSharedRef<SAedirMenu> ClosingWidget = MenuWidget.ToSharedRef();
	MenuWidget.Reset();

	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ClosingWidget);
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (PlayerController)
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
	}

	UWorld* World = GetWorld();
	if (bPausedByMenu && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	bPausedByMenu = false;
}

void UAedirMenuSubsystem::ToggleMenu()
{
	if (IsMenuOpen())
	{
		CloseMenu();
	}
	else
	{
		OpenMenu(0);
	}
}

bool UAedirMenuSubsystem::IsMenuOpen() const
{
	return MenuWidget.IsValid();
}

void UAedirMenuSubsystem::Deinitialize()
{
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();

	UWorld* World = GetWorld();
	if (bPausedByMenu && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	bPausedByMenu = false;

	Super::Deinitialize();
}
