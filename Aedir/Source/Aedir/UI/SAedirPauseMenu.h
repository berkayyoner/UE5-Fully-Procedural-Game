#pragma once

#include "CoreMinimal.h"
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

// Esc menu: an Options tab with save, load, settings and quit, and a Journal tab with the quest list.
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

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	enum class EPage : uint8
	{
		Main,
		Settings,
		QuitConfirm
	};

	TArray<FAedirMenuRow> BuildRows() const;
	TArray<FAedirMenuLine> BuildDetailLines(const TArray<FAedirMenuRow>& NewRows) const;
	void RefreshView(bool bForce);
	void RebuildList();
	void MoveSelection(int32 Delta);
	void ActivateSelected();
	void Activate(int32 Action);
	void GoToPage(EPage NewPage);
	void Notify(const FString& Message) const;

	void QuickSave();
	void SaveToSlot();
	void LoadLatest();
	void CycleUpscaler();
	void CycleQuality();
	void ConfirmQuit();

	FLinearColor GetRowBackground(int32 Index) const;
	FLinearColor GetRowAccent(int32 Index) const;
	FLinearColor GetRowTextColor(int32 Index) const;

	TWeakObjectPtr<UWorld> World;
	FSimpleDelegate CloseDelegate;

	int32 CurrentTab = 0;
	EPage Page = EPage::Main;
	int32 SelectedRow = 0;
	TArray<FAedirMenuRow> Rows;
	TArray<FAedirMenuLine> Lines;
	TArray<TSharedPtr<SWidget>> RowWidgets;

	TSharedPtr<SScrollBox> ListScroll;
	TSharedPtr<SVerticalBox> ListBox;
	TSharedPtr<SVerticalBox> DetailBox;
};
