#pragma once

#include "CoreMinimal.h"
#include "Net/AedirSaveSubsystem.h"
#include "UI/SAedirMenu.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SVerticalBox;
class UWorld;

enum class EAedirPauseTab : uint8
{
	Options,
	Journal,
	Count
};

// Pages of the Options tab that can also be opened directly.
enum class EAedirPausePage : uint8
{
	Main,
	Settings,
	SaveList,
	LoadList,
	SettingsGraphics,
	SettingsAudio,
	SettingsGameplay
};

// Esc menu: an Options tab with save, load, settings and quit, and a Journal tab with the quest list.
// Save and Load open a list of the saves on disk, every overwrite, load, delete and quit asks first.
class SAedirPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirPauseMenu)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetTab(int32 NewTab);
	void GoToPage(EAedirPausePage NewPage);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	enum class EConfirm : uint8
	{
		None,
		Quit,
		Overwrite,
		Load,
		Delete
	};

	TArray<FAedirMenuRow> BuildRows() const;
	TArray<FAedirMenuLine> BuildDetailLines(const TArray<FAedirMenuRow>& NewRows) const;
	void RefreshView(bool bForce);
	void RebuildList();
	void MoveSelection(int32 Delta);
	void ActivateSelected();
	void Activate(int32 Action);
	void ActivateSaveRow(const FAedirSaveInfo& Entry);
	void Notify(const FString& Message) const;
	void RefreshSaveEntries();

	void QuickSave();
	void SaveToNewSlot();
	bool IsOptionPage() const;
	void GoBack();
	void StepSelectedOption(int32 Delta);

	void AskConfirm(EConfirm Kind, const FString& Slot);
	void AnswerConfirm(bool bYes);
	void AskDeleteSelected();
	const FAedirSaveInfo* GetRowSave(int32 RowIndex) const;
	bool IsConfirmOpen() const { return Confirm != EConfirm::None; }
	FText GetConfirmPrompt() const;
	FText GetConfirmDetail() const;
	FText GetHintText() const;
	FText GetTitleText() const;
	TSharedRef<SWidget> MakeConfirmButton(const TCHAR* Label, bool bYes);

	float GetRowMarker(int32 Index) const;
	FLinearColor GetRowTextColor(int32 Index) const;

	TWeakObjectPtr<UWorld> World;
	FSimpleDelegate CloseDelegate;

	int32 CurrentTab = 0;
	EAedirPausePage Page = EAedirPausePage::Main;
	int32 SelectedRow = 0;
	TArray<FAedirMenuRow> Rows;
	TArray<FAedirMenuLine> Lines;
	TArray<TSharedPtr<SWidget>> RowWidgets;

	// Filled when a page opens and after each save or delete. Reading it from disk every frame would stall the game.
	TArray<FAedirSaveInfo> SaveEntries;

	EConfirm Confirm = EConfirm::None;
	FString ConfirmSlot;
	bool bConfirmYesSelected = false;

	TSharedPtr<SScrollBox> ListScroll;
	TSharedPtr<SVerticalBox> ListBox;
	TSharedPtr<SVerticalBox> DetailBox;
};
