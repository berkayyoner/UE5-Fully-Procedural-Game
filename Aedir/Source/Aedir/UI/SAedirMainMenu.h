#pragma once

#include "CoreMinimal.h"
#include "Net/AedirSaveSubsystem.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class STextBlock;
class SVerticalBox;

// The title screen: the name of the game on the left under its turning emblem, the choices low on the right.
class AEDIR_API SAedirMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirMainMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FSimpleDelegate, OnSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// A test goes through the menu by the names of its rows.
	void ChooseRowNamed(const FString& Name);
	void Refocus();

private:
	enum class EPage : uint8
	{
		Root,
		Load,
		Coop,
		HostPick,
		JoinPick,
		JoinAddress,
		ExitConfirm,
		Credits
	};

	struct FRow
	{
		FString Label;
		TFunction<void()> Run;
	};

	void GoTo(EPage NewPage);
	void BuildRows();
	void AddSaveRows(TFunction<void(const FString&)> Pick);
	void Choose(int32 Index);
	void GoBack();
	void RefreshHighlight();

	TWeakObjectPtr<UWorld> World;
	FSimpleDelegate OnSettings;
	EPage Page = EPage::Root;
	TArray<FRow> Rows;
	TArray<TSharedPtr<STextBlock>> RowTexts;
	TArray<FAedirSaveInfo> Saves;
	TSharedPtr<SVerticalBox> RowBox;
	TSharedPtr<STextBlock> NoteText;
	TSharedPtr<SEditableTextBox> AddressBox;
	FString PickedSlot;
	int32 Selected = 0;
};
