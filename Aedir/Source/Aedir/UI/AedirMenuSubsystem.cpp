#include "UI/AedirMenuSubsystem.h"

#include "Creature/AedirCreature.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/AedirPlayerCharacter.h"
#include "UI/SAedirCharacterCreation.h"
#include "UI/SAedirMenu.h"

void UAedirMenuSubsystem::OpenMenu(int32 StartCategory)
{
	if (CreationWidget.IsValid())
	{
		return;
	}

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

	BeginUIMode(PlayerController, MenuWidget.ToSharedRef());

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
	RemoveUIWidget(ClosingWidget);
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

void UAedirMenuSubsystem::OpenCharacterCreation()
{
	if (MenuWidget.IsValid() || CreationWidget.IsValid())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (!PlayerController || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	CreationWidget = SNew(SAedirCharacterCreation)
		.OnConfirm(FAedirRaceChosenDelegate::CreateUObject(this, &UAedirMenuSubsystem::HandleRaceConfirmed))
		.OnCancel(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseCharacterCreation));
	GEngine->GameViewport->AddViewportWidgetContent(CreationWidget.ToSharedRef(), 100);

	BeginUIMode(PlayerController, CreationWidget.ToSharedRef());
}

bool UAedirMenuSubsystem::IsCharacterCreationOpen() const
{
	return CreationWidget.IsValid();
}

void UAedirMenuSubsystem::Deinitialize()
{
	if (GEngine && GEngine->GameViewport)
	{
		if (MenuWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		}
		if (CreationWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(CreationWidget.ToSharedRef());
		}
	}
	MenuWidget.Reset();
	CreationWidget.Reset();

	UWorld* World = GetWorld();
	if (bPausedByMenu && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	bPausedByMenu = false;

	Super::Deinitialize();
}

void UAedirMenuSubsystem::BeginUIMode(APlayerController* PlayerController, const TSharedRef<SWidget>& FocusWidget)
{
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(FocusWidget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);

	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::SetGamePaused(World, true);
		bPausedByMenu = true;
	}
}

void UAedirMenuSubsystem::RemoveUIWidget(const TSharedRef<SWidget>& ClosingWidget)
{
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ClosingWidget);
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (PlayerController)
	{
		PlayerController->SetShowMouseCursor(false);
		PlayerController->SetInputMode(FInputModeGameOnly());
	}

	UWorld* World = GetWorld();
	if (bPausedByMenu && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	bPausedByMenu = false;
}

void UAedirMenuSubsystem::CloseCharacterCreation()
{
	if (!CreationWidget.IsValid())
	{
		return;
	}

	const TSharedRef<SAedirCharacterCreation> ClosingWidget = CreationWidget.ToSharedRef();
	CreationWidget.Reset();
	RemoveUIWidget(ClosingWidget);
}

void UAedirMenuSubsystem::HandleRaceConfirmed(FName RaceId)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	AAedirPlayerCharacter* Player = PlayerController ? Cast<AAedirPlayerCharacter>(PlayerController->GetPawn()) : nullptr;
	if (Player)
	{
		Player->ServerChooseRace(RaceId);
	}

	CloseCharacterCreation();
}
