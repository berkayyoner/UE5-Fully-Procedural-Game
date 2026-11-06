#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Misc/CoreDelegates.h"
#include "Travel/AedirTravelSubsystem.h"
#include "UI/AedirMenuSubsystem.h"
#include "UI/SAedirPauseMenu.h"
#include "UnrealClient.h"

namespace
{
	constexpr float UiScriptStepDelay = 0.15f;

	UWorld* UiScriptFindWorld()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
	}

	ULocalPlayer* UiScriptFindPlayer()
	{
		UWorld* World = UiScriptFindWorld();
		return World ? World->GetFirstLocalPlayerFromController() : nullptr;
	}

	UAedirMenuSubsystem* UiScriptFindMenu()
	{
		ULocalPlayer* Player = UiScriptFindPlayer();
		return Player ? Player->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
	}

	void UiScriptOpen(const FString& Name)
	{
		UAedirMenuSubsystem* Menu = UiScriptFindMenu();
		if (!Menu)
		{
			return;
		}

		if (Name == TEXT("radial"))
		{
			Menu->CloseMenu();
			Menu->ToggleMenu();
		}
		else if (Name == TEXT("inventory"))
		{
			Menu->OpenMenu(0);
		}
		else if (Name == TEXT("magic"))
		{
			Menu->OpenMenu(1);
		}
		else if (Name == TEXT("skills"))
		{
			Menu->OpenMenu(2);
		}
		else if (Name == TEXT("map"))
		{
			Menu->OpenMenu(3);
		}
		else if (Name == TEXT("journal"))
		{
			Menu->OpenMenu(4);
		}
		else if (Name == TEXT("pause"))
		{
			Menu->CloseMenu();
			Menu->TogglePauseMenu();
		}
	}

	void UiScriptSendKey(const FString& Name)
	{
		const FKey Key(*Name);
		if (FSlateApplication::IsInitialized() && Key.IsValid())
		{
			const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyDownEvent(Event);
			FSlateApplication::Get().ProcessKeyUpEvent(Event);
		}
	}

	// Coordinates are window pixels.
	void UiScriptMouse(const FString& Arguments, bool bClick, const FKey& Button)
	{
		FString XText;
		FString YText;
		if (!FSlateApplication::IsInitialized() || !Arguments.Split(TEXT(","), &XText, &YText))
		{
			return;
		}

		static FVector2D LastPosition = FVector2D::ZeroVector;
		const FVector2D Position(FCString::Atof(*XText), FCString::Atof(*YText));
		TSet<FKey> Pressed;
		const FPointerEvent Move(0, Position, LastPosition, Pressed, EKeys::Invalid, 0.f, FModifierKeysState());
		LastPosition = Position;
		FSlateApplication::Get().ProcessMouseMoveEvent(Move);
		if (bClick)
		{
			Pressed.Add(Button);
			const FPointerEvent Down(0, Position, Position, Pressed, Button, 0.f, FModifierKeysState());
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, Down);
			Pressed.Remove(Button);
			const FPointerEvent Up(0, Position, Position, Pressed, Button, 0.f, FModifierKeysState());
			FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
		}
	}

	// Steps run on the core ticker, because every menu pauses the world and world timers stop with it.
	class FUiScriptRunner
	{
	public:
		explicit FUiScriptRunner(TArray<FString> InSteps) : Steps(MoveTemp(InSteps))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUiScriptRunner::Tick));
		}

	private:
		bool Tick(float DeltaSeconds)
		{
			Remaining -= DeltaSeconds;
			while (Remaining <= 0.f)
			{
				if (!Steps.IsValidIndex(NextStep))
				{
					delete this;
					return false;
				}
				RunStep(Steps[NextStep++]);
			}
			return true;
		}

		void RunStep(const FString& Step)
		{
			FString Name = Step;
			FString Argument;
			Step.Split(TEXT("="), &Name, &Argument);
			Remaining = UiScriptStepDelay;
			UE_LOG(LogTemp, Display, TEXT("[UiScript] %s"), *Step);

			if (Name == TEXT("open"))
			{
				UiScriptOpen(Argument);
			}
			else if (Name == TEXT("key"))
			{
				UiScriptSendKey(Argument);
			}
			else if (Name == TEXT("move"))
			{
				UiScriptMouse(Argument, false, EKeys::Invalid);
			}
			else if (Name == TEXT("click"))
			{
				UiScriptMouse(Argument, true, EKeys::LeftMouseButton);
			}
			else if (Name == TEXT("rclick"))
			{
				UiScriptMouse(Argument, true, EKeys::RightMouseButton);
			}
			else if (Name == TEXT("wait"))
			{
				Remaining = FCString::Atof(*Argument);
			}
			else if (Name == TEXT("shot"))
			{
				FScreenshotRequest::RequestScreenshot(Argument, true, false);
				Remaining = 1.f;
			}
			else if (Name == TEXT("note"))
			{
				ULocalPlayer* Player = UiScriptFindPlayer();
				if (UAedirTravelSubsystem* Travel = Player ? Player->GetSubsystem<UAedirTravelSubsystem>() : nullptr)
				{
					Travel->ShowNotification(FText::FromString(Argument.Replace(TEXT("_"), TEXT(" "))));
				}
			}
			else if (Name == TEXT("close"))
			{
				if (UAedirMenuSubsystem* Menu = UiScriptFindMenu())
				{
					Menu->CloseMenu();
				}
			}
			else if (Name == TEXT("exec"))
			{
				UWorld* World = UiScriptFindWorld();
				if (APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr)
				{
					Controller->ConsoleCommand(Argument.Replace(TEXT("_"), TEXT(" ")));
				}
			}
			else if (Name == TEXT("quit"))
			{
				FPlatformMisc::RequestExit(false, TEXT("UiScript"));
			}
		}

		TArray<FString> Steps;
		int32 NextStep = 0;
		float Remaining = 0.f;
	};

	// Drives the menus through real Slate input, with the world paused or not. The steps are separated by "+", console commands already use "|" for themselves:
	// open=<radial|inventory|magic|skills|map|journal|pause>, key=<key name>, move=x,y, click=x,y, rclick=x,y, wait=seconds,
	// shot=<file name>, note=<text>, exec=<console command>, close, quit. Underscores in note and exec stand for spaces.
	FAutoConsoleCommand UiScriptCommand(
		TEXT("aedir.UiScript"),
		TEXT("Scripted menu input and screenshots for UI checks. Example: aedir.UiScript open=pause+key=S+key=Enter+shot=aedir_ui_save+close"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Arguments)
		{
			TArray<FString> Steps;
			FString::Join(Arguments, TEXT(" ")).ParseIntoArray(Steps, TEXT("+"), true);
			// Owns itself and deletes itself after the last step.
			new FUiScriptRunner(MoveTemp(Steps));
		}));
}

#endif
