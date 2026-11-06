#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AAedirCreature;
class AAedirItemActor;
class SBorder;
class SScrollBox;
class SVerticalBox;
class UWorld;

enum class EAedirMenuCategory : uint8
{
	Inventory,
	Magic,
	Skills,
	Count
};

enum class EAedirMenuLineKind : uint8
{
	Title,
	Value,
	Heading,
	Text,
	Body,
	Dim,
	Progress,
	Spacer
};

struct FAedirMenuRow
{
	FString Name;
	FString Value;
	bool bDimmed = false;
	// Inventory only: true for a hand row (Index is the hand), false for a waist slot (Index is the slot).
	bool bIsHand = false;
	// Inventory only: true for a backpack entry (Index is the backpack index).
	bool bIsBackpack = false;
	// Hand index, waist slot, skill enum value or marker index depending on the category.
	int32 Index = INDEX_NONE;
	FName QuestId;

	bool operator==(const FAedirMenuRow& Other) const
	{
		return Name == Other.Name && Value == Other.Value && bDimmed == Other.bDimmed
			&& bIsHand == Other.bIsHand && bIsBackpack == Other.bIsBackpack && Index == Other.Index && QuestId == Other.QuestId;
	}
};

struct FAedirMenuLine
{
	EAedirMenuLineKind Kind = EAedirMenuLineKind::Text;
	FString Content;

	bool operator==(const FAedirMenuLine& Other) const
	{
		return Kind == Other.Kind && Content == Other.Content;
	}
};

// Row and detail builders shared by the main menu and the pause menu journal.
namespace AedirMenuContent
{
	TArray<FAedirMenuRow> BuildJournalRows(const UWorld* World);
	void BuildJournalDetail(const UWorld* World, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines);
	// Replaces the children of Box with one widget per line. Progress feeds the Progress lines.
	void PopulateDetail(SVerticalBox& Box, const TArray<FAedirMenuLine>& NewLines, const TFunction<TOptional<float>()>& Progress);
}

// Inventory tabs shown at the top of the Inventory category.
enum class EAedirInventoryTab : uint8
{
	Equipped,
	Backpack,
	Count
};

// Full-screen Skyrim-style menu: Inventory (Equipped and Backpack tabs), Magic and Skills categories, a selectable list on the left and details on the right.
class SAedirMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirMenu)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<AAedirCreature>, Creature)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetCategory(int32 NewCategory);
	void SetInventoryTab(int32 NewTab);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TArray<FAedirMenuRow> BuildRows() const;
	TArray<FAedirMenuLine> BuildDetailLines(const TArray<FAedirMenuRow>& NewRows) const;
	void RefreshView(bool bForce);
	void RebuildList();
	void RebuildDetail();
	void MoveSelection(int32 Delta);
	void ActivateSelected(bool bFromClick);
	void DropRow(int32 RowIndex);
	void HandleRowClick(int32 Index, const FGeometry& RowGeometry, const FPointerEvent& MouseEvent);
	void OpenContext(int32 Index, const FVector2D& ScreenPosition);
	void CloseContext();
	void RunContextAction();
	bool RowHasItem(const FAedirMenuRow& Row) const;
	AAedirItemActor* GetRowItem(const FAedirMenuRow& Row) const;
	int32 GetHoveredRow() const;
	FText GetContextText() const;
	FText GetHintText() const;
	EVisibility GetTabVisibility(int32 Index) const;
	void ScrollToSelection();
	float GetSelectedSkillProgress() const;

	FText GetTabText(int32 Index) const;
	FLinearColor GetTabColor(int32 Index) const;
	FLinearColor GetRowBackground(int32 Index) const;
	FLinearColor GetRowAccent(int32 Index) const;
	FLinearColor GetRowTextColor(int32 Index) const;

	TWeakObjectPtr<AAedirCreature> Creature;
	FSimpleDelegate CloseDelegate;

	int32 CurrentCategory = 0;
	int32 InventoryTab = 0;
	int32 ContextRow = INDEX_NONE;
	FVector2D ContextPosition = FVector2D::ZeroVector;
	int32 SelectedRow = 0;
	TArray<FAedirMenuRow> Rows;
	TArray<FAedirMenuLine> Lines;
	TArray<TSharedPtr<SWidget>> RowWidgets;

	TSharedPtr<SScrollBox> ListScroll;
	TSharedPtr<SVerticalBox> ListBox;
	TSharedPtr<SVerticalBox> DetailBox;
	TSharedPtr<SBorder> ContextButton;
};
