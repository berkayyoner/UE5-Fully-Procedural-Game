#include "UI/SAedirConsole.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
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
	constexpr int32 ConsoleMaxHistory = 40;
}

void SAedirConsole::Construct(const FArguments& InArgs)
{
	CommandDelegate = InArgs._OnCommand;
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	ChildSlot
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
			[
				SNew(SVerticalBox)
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
				.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SAssignNew(InputBox, SEditableTextBox)
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 11))
					.HintText(FText::FromString(TEXT("Command, for example DevHelp")))
					.ClearKeyboardFocusOnCommit(false)
					.OnTextChanged(this, &SAedirConsole::HandleTextChanged)
					.OnTextCommitted(this, &SAedirConsole::HandleTextCommitted)
				]
			]
		]
	];

	AddLogLine(TEXT("Console. Type DevHelp for the commands. [`] or [Esc] closes."), FLinearColor(0.9f, 0.8f, 0.4f, 1.f));
}

void SAedirConsole::AddLogLine(const FString& Line, const FLinearColor& Color)
{
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
	if (Key == EKeys::Tilde || Key == EKeys::Escape)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}

	// Up and down walk the command history.
	if ((Key == EKeys::Up || Key == EKeys::Down) && !History.IsEmpty())
	{
		HistoryIndex = Key == EKeys::Up
			? (HistoryIndex == INDEX_NONE ? History.Num() - 1 : FMath::Max(HistoryIndex - 1, 0))
			: (HistoryIndex == INDEX_NONE ? INDEX_NONE : FMath::Min(HistoryIndex + 1, History.Num()));
		const FString Text = History.IsValidIndex(HistoryIndex) ? History[HistoryIndex] : FString();
		InputBox->SetText(FText::FromString(Text));
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
}

void SAedirConsole::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType != ETextCommit::OnEnter)
	{
		return;
	}

	const FString Command = NewText.ToString().TrimStartAndEnd();
	InputBox->SetText(FText::GetEmpty());
	HistoryIndex = INDEX_NONE;
	if (Command.IsEmpty())
	{
		return;
	}

	History.Add(Command);
	if (History.Num() > ConsoleMaxHistory)
	{
		History.RemoveAt(0);
	}

	AddLogLine(TEXT("> ") + Command, FLinearColor(0.55f, 0.85f, 1.f, 1.f));
	CommandDelegate.ExecuteIfBound(Command);
	FocusInput();
}
