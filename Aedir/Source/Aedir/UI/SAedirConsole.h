#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SScrollBox;

DECLARE_DELEGATE_OneParam(FAedirConsoleCommand, const FString&);
DECLARE_DELEGATE_OneParam(FAedirConsolePick, const FVector2D&);

// Skyrim-style console in the lower left corner: a short log above a single input line.
class SAedirConsole : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirConsole)
	{}
		SLATE_EVENT(FAedirConsoleCommand, OnCommand)
		SLATE_EVENT(FAedirConsolePick, OnPick)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Adds one line to the log and scrolls to it.
	void AddLogLine(const FString& Line, const FLinearColor& Color = FLinearColor(0.85f, 0.85f, 0.85f, 1.f));

	// What is selected, shown above the log. Empty when nothing is.
	void SetSelection(const FString& Text);

	// Gives the input line the keyboard focus.
	void FocusInput();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	void HandleTextChanged(const FText& NewText);
	void HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType);

	FAedirConsoleCommand CommandDelegate;
	FAedirConsolePick PickDelegate;
	TSharedPtr<class STextBlock> SelectionText;
	FSimpleDelegate CloseDelegate;

	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<SScrollBox> LogScroll;

	TArray<FString> History;
	int32 HistoryIndex = INDEX_NONE;
};
