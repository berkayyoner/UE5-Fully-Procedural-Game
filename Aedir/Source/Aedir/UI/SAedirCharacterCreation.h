#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FAedirRaceChosenDelegate, FName);

class SVerticalBox;

// Keyboard-only race picker in the same style as the in-game menu.
class SAedirCharacterCreation : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirCharacterCreation)
	{}
		SLATE_EVENT(FAedirRaceChosenDelegate, OnConfirm)
		SLATE_EVENT(FSimpleDelegate, OnCancel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void MoveSelection(int32 Delta);
	void RebuildDetail();

	FLinearColor GetRowBackground(int32 Index) const;
	FLinearColor GetRowAccent(int32 Index) const;
	FLinearColor GetRowTextColor(int32 Index) const;

	FAedirRaceChosenDelegate ConfirmDelegate;
	FSimpleDelegate CancelDelegate;

	int32 SelectedIndex = 0;
	TSharedPtr<SVerticalBox> DetailBox;
};
