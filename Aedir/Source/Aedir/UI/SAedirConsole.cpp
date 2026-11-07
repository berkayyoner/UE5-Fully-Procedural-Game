#include "UI/SAedirConsole.h"

#include "Dev/AedirConsoleCommands.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float ConsoleWidth = 640.f;
	constexpr float ConsoleLogHeight = 220.f;
	constexpr int32 ConsoleMaxLogLines = 80;
	constexpr int32 ConsoleMaxHistory = 100;

	// The log and the typed commands outlive the widget, so closing the console loses nothing; commands also outlive the game.
	TArray<TPair<FString, FLinearColor>> ConsoleKeptLog;

	FString ConsoleHistoryFile()
	{
		return FPaths::ProjectSavedDir() / TEXT("ConsoleHistory.txt");
	}

	TArray<FString>& ConsoleHistory()
	{
		static TArray<FString> History;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			FFileHelper::LoadFileToStringArray(History, *ConsoleHistoryFile());
			History.RemoveAll([](const FString& Line) { return Line.TrimStartAndEnd().IsEmpty(); });
		}
		return History;
	}
}

void SAedirConsole::Construct(const FArguments& InArgs)
{
	CommandDelegate = InArgs._OnCommand;
	PickDelegate = InArgs._OnPick;
	SetVisibility(EVisibility::Visible);
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(16.f))
		[
		SNew(SBox)
		.WidthOverride(ConsoleWidth)
		[
			SNew(SBorder)
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.78f))
			.Padding(FMargin(8.f))
			.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 0.f, 0.f, 6.f))
				[
					SAssignNew(SelectionText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 11))
					.ColorAndOpacity(FLinearColor(0.95f, 0.8f, 0.4f, 1.f))
					.AutoWrapText(true)
					.Visibility(EVisibility::Collapsed)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(ConsoleLogHeight)
					[
						SAssignNew(LogScroll, SScrollBox)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SAssignNew(SuggestionText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
					.ColorAndOpacity(FLinearColor(0.6f, 0.75f, 0.6f, 1.f))
					.AutoWrapText(true)
					.Visibility(EVisibility::Collapsed)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SAssignNew(InputBox, SEditableTextBox)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 11))
					.HintText(FText::FromString(TEXT("Command, for example help or player.additem Bread 2")))
					.ClearKeyboardFocusOnCommit(false)
					.OnTextChanged(this, &SAedirConsole::HandleTextChanged)
					.OnTextCommitted(this, &SAedirConsole::HandleTextCommitted)
				]
			]
		]
		]
	];

	if (!ConsoleKeptLog.IsEmpty())
	{
		RestoreLog();
		return;
	}
	AddLogLine(TEXT("Console. Click something to select it. Type help for the commands, help * for every base id. [\"] or [Esc] closes."), FLinearColor(0.9f, 0.8f, 0.4f, 1.f));
}

void SAedirConsole::RestoreLog()
{
	const TArray<TPair<FString, FLinearColor>> Kept = MoveTemp(ConsoleKeptLog);
	ConsoleKeptLog.Reset();
	for (const TPair<FString, FLinearColor>& Line : Kept)
	{
		AddLogLine(Line.Key, Line.Value);
	}
}

void SAedirConsole::AddLogLine(const FString& Line, const FLinearColor& Color)
{
	ConsoleKeptLog.Emplace(Line, Color);
	while (ConsoleKeptLog.Num() > ConsoleMaxLogLines)
	{
		ConsoleKeptLog.RemoveAt(0);
	}
	if (!LogScroll.IsValid())
	{
		return;
	}

	LogScroll->AddSlot()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Line))
		.Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
		.ColorAndOpacity(Color)
		.AutoWrapText(true)
	];

	while (LogScroll->GetChildren()->Num() > ConsoleMaxLogLines)
	{
		LogScroll->RemoveSlot(LogScroll->GetChildren()->GetChildAt(0));
	}
	LogScroll->ScrollToEnd();
}

void SAedirConsole::SetSelection(const FString& Text)
{
	if (SelectionText.IsValid())
	{
		SelectionText->SetText(FText::FromString(Text));
		SelectionText->SetVisibility(Text.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
	}
}

FReply SAedirConsole::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// The click lands on the world behind the console.
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Share = Local / FVector2D(MyGeometry.GetLocalSize());
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		SizeX = Size.X;
		SizeY = Size.Y;
	}
	PickDelegate.ExecuteIfBound(FVector2D(Share.X * SizeX, Share.Y * SizeY));
	FocusInput();
	return FReply::Handled();
}

void SAedirConsole::FocusInput()
{
	if (InputBox.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(InputBox, EFocusCause::SetDirectly);
	}
}

FReply SAedirConsole::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Tilde || Key == EKeys::Quote || Key == EKeys::Escape)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}

	// Tab finishes the command being typed with the first one it could be.
	if (Key == EKeys::Tab)
	{
		const FString Typed = InputBox->GetText().ToString();
		if (!Typed.Contains(TEXT(" ")))
		{
			const TArray<FString> Options = AedirConsoleCommands::GetSuggestions(Typed);
			if (!Options.IsEmpty())
			{
				FString Verb;
				Options[0].Split(TEXT(" "), &Verb, nullptr);
				InputBox->SetText(FText::FromString((Verb.IsEmpty() ? Options[0] : Verb) + TEXT(" ")));
				InputBox->GoTo(ETextLocation::EndOfDocument);
			}
		}
		return FReply::Handled();
	}

	// Up and down walk the command history.
	const TArray<FString>& History = ConsoleHistory();
	if ((Key == EKeys::Up || Key == EKeys::Down) && !History.IsEmpty())
	{
		HistoryIndex = Key == EKeys::Up
			? (HistoryIndex == INDEX_NONE ? History.Num() - 1 : FMath::Max(HistoryIndex - 1, 0))
			: (HistoryIndex == INDEX_NONE ? INDEX_NONE : FMath::Min(HistoryIndex + 1, History.Num()));
		const FString Text = History.IsValidIndex(HistoryIndex) ? History[HistoryIndex] : FString();
		InputBox->SetText(FText::FromString(Text));
		InputBox->GoTo(ETextLocation::EndOfDocument);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAedirConsole::HandleTextChanged(const FText& NewText)
{
	// The key that opens the console types a character too; it is removed again.
	FString Text = NewText.ToString();
	const FString Cleaned = Text.Replace(TEXT("`"), TEXT("")).Replace(TEXT("\""), TEXT(""));
	if (Cleaned != Text)
	{
		InputBox->SetText(FText::FromString(Cleaned));
	}
	UpdateSuggestions(Cleaned);
}

void SAedirConsole::UpdateSuggestions(const FString& Typed)
{
	if (!SuggestionText.IsValid())
	{
		return;
	}
	TArray<FString> Options = AedirConsoleCommands::GetSuggestions(Typed);
	constexpr int32 MaxShown = 6;
	if (Options.Num() > MaxShown)
	{
		const int32 More = Options.Num() - MaxShown;
		Options.SetNum(MaxShown);
		Options.Add(FString::Printf(TEXT("... %d more"), More));
	}
	SuggestionText->SetText(FText::FromString(FString::Join(Options, LINE_TERMINATOR)));
	SuggestionText->SetVisibility(Options.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
}

void SAedirConsole::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}

	const FString Command = NewText.ToString().TrimStartAndEnd();
	InputBox->SetText(FText::GetEmpty());
	UpdateSuggestions(FString());
	HistoryIndex = INDEX_NONE;
	if (Command.IsEmpty())
	{
		return;
	}

	// The same command twice in a row is kept once, as a shell does.
	TArray<FString>& History = ConsoleHistory();
	if (History.IsEmpty() || History.Last() != Command)
	{
		History.Add(Command);
		while (History.Num() > ConsoleMaxHistory)
		{
			History.RemoveAt(0);
		}
		FFileHelper::SaveStringArrayToFile(History, *ConsoleHistoryFile());
	}

	AddLogLine(TEXT("> ") + Command, FLinearColor(0.55f, 0.85f, 1.f, 1.f));
	CommandDelegate.ExecuteIfBound(Command);
	FocusInput();
}
