#include "UI/AedirMenuSubsystem.h"

#include "Player/AedirPlayerCharacter.h"
#include "UI/SAedirDialogue.h"
#include "UI/SAedirMainMenu.h"

#include "Creature/AedirCreature.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/AedirPlayerCharacter.h"
#include "Travel/AedirTravelSubsystem.h"
#include "UI/SAedirCharacterCreation.h"
#include "UI/SAedirMap.h"
#include "UI/SAedirMenu.h"
#include "UI/SAedirPauseMenu.h"
#include "UI/SAedirRadialMenu.h"

namespace
{
	constexpr int32 MenuZOrder = 100;
	constexpr int32 MenuInventoryCategory = 0;
	constexpr int32 MenuMagicCategory = 1;
	constexpr int32 MenuSkillsCategory = 2;
	constexpr int32 MenuMapCategory = 3;
	constexpr int32 MenuJournalCategory = 4;

	UAedirTravelSubsystem* MenuGetTravel(const ULocalPlayer* LocalPlayer)
	{
		return LocalPlayer ? LocalPlayer->GetSubsystem<UAedirTravelSubsystem>() : nullptr;
	}
}

APlayerController* UAedirMenuSubsystem::PrepareToOpen()
{
	if (CreationWidget.IsValid())
	{
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UAedirTravelSubsystem* Travel = MenuGetTravel(LocalPlayer);
	if (Travel && Travel->IsTravelling())
	{
		return nullptr;
	}

	APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	if (!PlayerController || !GEngine || !GEngine->GameViewport)
	{
		return nullptr;
	}

	CloseMenu();
	return PlayerController;
}

void UAedirMenuSubsystem::OpenMenu(int32 StartCategory)
{
	if (StartCategory == MenuMapCategory)
	{
		OpenMap();
	}
	else if (StartCategory == MenuJournalCategory)
	{
		if (!PauseWidget.IsValid())
		{
			OpenPauseMenu(1);
		}
		else
		{
			PauseWidget->SetTab(1);
		}
	}
	else if (MenuWidget.IsValid())
	{
		MenuWidget->SetCategory(StartCategory);
	}
	else
	{
		OpenCategoryMenu(StartCategory);
	}
}

void UAedirMenuSubsystem::OpenCategoryMenu(int32 StartCategory)
{
	APlayerController* PlayerController = PrepareToOpen();
	if (!PlayerController)
	{
		return;
	}

	AAedirCreature* Creature = Cast<AAedirCreature>(PlayerController->GetPawn());

	MenuWidget = SNew(SAedirMenu)
		.Creature(TWeakObjectPtr<AAedirCreature>(Creature))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	MenuWidget->SetCategory(StartCategory);
	GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), MenuZOrder);

	BeginUIMode(PlayerController, MenuWidget.ToSharedRef());

	if (Creature)
	{
		Creature->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::OpenRadialMenu()
{
	APlayerController* PlayerController = PrepareToOpen();
	if (!PlayerController)
	{
		return;
	}

	RadialWidget = SNew(SAedirRadialMenu)
		.OnChosen(FAedirRadialChosen::CreateUObject(this, &UAedirMenuSubsystem::HandleRadialChosen))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	GEngine->GameViewport->AddViewportWidgetContent(RadialWidget.ToSharedRef(), MenuZOrder);

	BeginUIMode(PlayerController, RadialWidget.ToSharedRef());

	if (AAedirCreature* Creature = Cast<AAedirCreature>(PlayerController->GetPawn()))
	{
		Creature->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::OpenPauseMenu(int32 StartTab)
{
	APlayerController* PlayerController = PrepareToOpen();
	if (!PlayerController)
	{
		return;
	}

	PauseWidget = SNew(SAedirPauseMenu)
		.World(TWeakObjectPtr<UWorld>(PlayerController->GetWorld()))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	PauseWidget->SetTab(StartTab);
	GEngine->GameViewport->AddViewportWidgetContent(PauseWidget.ToSharedRef(), MenuZOrder);

	BeginUIMode(PlayerController, PauseWidget.ToSharedRef());

	if (AAedirCreature* Creature = Cast<AAedirCreature>(PlayerController->GetPawn()))
	{
		Creature->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::OpenMap()
{
	APlayerController* PlayerController = PrepareToOpen();
	if (!PlayerController)
	{
		return;
	}

	AAedirCreature* Creature = Cast<AAedirCreature>(PlayerController->GetPawn());

	MapWidget = SNew(SAedirMap)
		.Creature(TWeakObjectPtr<AAedirCreature>(Creature))
		.OnQueryTravel(FAedirMapTravelQuery::CreateUObject(this, &UAedirMenuSubsystem::HandleMapTravelQuery))
		.OnTravel(FAedirMapTravelConfirmed::CreateUObject(this, &UAedirMenuSubsystem::HandleMapTravel))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	GEngine->GameViewport->AddViewportWidgetContent(MapWidget.ToSharedRef(), MenuZOrder);

	BeginUIMode(PlayerController, MapWidget.ToSharedRef());

	if (Creature)
	{
		Creature->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::OpenDialogue(AAedirNpc* Npc)
{
	APlayerController* PlayerController = PrepareToOpen();
	if (!PlayerController || !Npc)
	{
		return;
	}

	AAedirPlayerCharacter* Player = Cast<AAedirPlayerCharacter>(PlayerController->GetPawn());
	DialogueWidget = SNew(SAedirDialogue)
		.Npc(TWeakObjectPtr<AAedirNpc>(Npc))
		.Player(TWeakObjectPtr<AAedirPlayerCharacter>(Player))
		.OnClose(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::CloseMenu));
	GEngine->GameViewport->AddViewportWidgetContent(DialogueWidget.ToSharedRef(), MenuZOrder);

	BeginUIMode(PlayerController, DialogueWidget.ToSharedRef());

	if (Player)
	{
		Player->MoveLocal(FVector2D::ZeroVector);
	}
}

void UAedirMenuSubsystem::OpenMainMenu()
{
	const ULocalPlayer* Local = GetLocalPlayer();
	APlayerController* PlayerController = Local ? Local->GetPlayerController(GetWorld()) : nullptr;
	if (!PlayerController || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	MainMenuWidget = SNew(SAedirMainMenu)
		.World(TWeakObjectPtr<UWorld>(PlayerController->GetWorld()))
		.OnSettings(FSimpleDelegate::CreateUObject(this, &UAedirMenuSubsystem::OpenMainMenuSettings));
	GEngine->GameViewport->AddViewportWidgetContent(MainMenuWidget.ToSharedRef(), MenuZOrder - 1);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenuWidget.ToSharedRef());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
}

void UAedirMenuSubsystem::CloseMainMenu()
{
	if (MainMenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MainMenuWidget.ToSharedRef());
	}
	MainMenuWidget.Reset();
}

void UAedirMenuSubsystem::ChooseMainMenuRow(const FString& Name)
{
	if (MainMenuWidget.IsValid())
	{
		MainMenuWidget->ChooseRowNamed(Name);
	}
}

void UAedirMenuSubsystem::OpenMainMenuSettings()
{
	OpenPauseMenu(0);
	if (PauseWidget.IsValid())
	{
		PauseWidget->OpenSettingsOnly();
	}
}

void UAedirMenuSubsystem::CloseMenu()
{
	if (MenuWidget.IsValid())
	{
		const TSharedRef<SAedirMenu> ClosingWidget = MenuWidget.ToSharedRef();
		MenuWidget.Reset();
		RemoveUIWidget(ClosingWidget);
	}
	CloseRadialMenu();
	ClosePauseMenu();
	if (MainMenuWidget.IsValid())
	{
		MainMenuWidget->Refocus();
	}
	if (DialogueWidget.IsValid())
	{
		const TSharedRef<SAedirDialogue> ClosingDialogue = DialogueWidget.ToSharedRef();
		DialogueWidget.Reset();
		RemoveUIWidget(ClosingDialogue);
	}
	CloseMap();
}

void UAedirMenuSubsystem::CloseRadialMenu()
{
	if (!RadialWidget.IsValid())
	{
		return;
	}

	const TSharedRef<SAedirRadialMenu> ClosingWidget = RadialWidget.ToSharedRef();
	RadialWidget.Reset();
	RemoveUIWidget(ClosingWidget);
}

void UAedirMenuSubsystem::ClosePauseMenu()
{
	if (!PauseWidget.IsValid())
	{
		return;
	}

	const TSharedRef<SAedirPauseMenu> ClosingWidget = PauseWidget.ToSharedRef();
	PauseWidget.Reset();
	RemoveUIWidget(ClosingWidget);
}

void UAedirMenuSubsystem::CloseMap()
{
	if (!MapWidget.IsValid())
	{
		return;
	}

	const TSharedRef<SAedirMap> ClosingWidget = MapWidget.ToSharedRef();
	MapWidget.Reset();
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
		OpenRadialMenu();
	}
}

void UAedirMenuSubsystem::TogglePauseMenu(int32 StartTab)
{
	if (PauseWidget.IsValid())
	{
		ClosePauseMenu();
	}
	else
	{
		OpenPauseMenu(StartTab);
	}
}

bool UAedirMenuSubsystem::IsMenuOpen() const
{
	return MenuWidget.IsValid() || RadialWidget.IsValid() || PauseWidget.IsValid() || MapWidget.IsValid();
}

bool UAedirMenuSubsystem::IsListMenuOpen() const
{
	return MenuWidget.IsValid() || PauseWidget.IsValid();
}

void UAedirMenuSubsystem::HandleRadialChosen(EAedirRadialOption Option)
{
	switch (Option)
	{
	case EAedirRadialOption::Skills:
		OpenCategoryMenu(MenuSkillsCategory);
		break;
	case EAedirRadialOption::Magic:
		OpenCategoryMenu(MenuMagicCategory);
		break;
	case EAedirRadialOption::Inventory:
		OpenCategoryMenu(MenuInventoryCategory);
		break;
	case EAedirRadialOption::Map:
		OpenMap();
		break;
	default:
		break;
	}
}

FText UAedirMenuSubsystem::HandleMapTravelQuery(FName MarkerId)
{
	const UAedirTravelSubsystem* Travel = MenuGetTravel(GetLocalPlayer());
	return Travel ? Travel->GetFastTravelBlockReason(MarkerId) : FText::GetEmpty();
}

void UAedirMenuSubsystem::HandleMapTravel(FName MarkerId)
{
	UAedirTravelSubsystem* Travel = MenuGetTravel(GetLocalPlayer());
	CloseMenu();
	if (Travel)
	{
		Travel->BeginFastTravel(MarkerId);
	}
}

void UAedirMenuSubsystem::OpenCharacterCreation()
{
	if (IsMenuOpen() || CreationWidget.IsValid())
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
	GEngine->GameViewport->AddViewportWidgetContent(CreationWidget.ToSharedRef(), MenuZOrder);

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
		if (RadialWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(RadialWidget.ToSharedRef());
		}
		if (PauseWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(PauseWidget.ToSharedRef());
		}
		if (MapWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(MapWidget.ToSharedRef());
		}
		if (CreationWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(CreationWidget.ToSharedRef());
		}
	}
	MenuWidget.Reset();
	RadialWidget.Reset();
	PauseWidget.Reset();
	MapWidget.Reset();
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
