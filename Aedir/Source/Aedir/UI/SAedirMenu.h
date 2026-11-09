#pragma once

#include "CoreMinimal.h"
#include "Items/AedirItemTypes.h"
#include "Widgets/SCompoundWidget.h"

class AAedirCreature;
class AAedirItemActor;
class AAedirItemPreview;
class SAedirItemPreviewView;
class SBorder;
class SScrollBox;
class SVerticalBox;
class UWorld;
struct FAedirUIMesh;

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
	Spacer,
	// Ornament line under a title.
	Rule,
	// One row of "LABEL value" pairs, tab separated.
	Pair
};

struct FAedirMenuRow
{
	FString Name;
	FString Value;
	// Small second line under the name.
	FString SubText;
	bool bDimmed = false;
	// Inventory only: true for a hand row (Index is the hand), false for a waist slot (Index is the slot).
	bool bIsHand = false;
	// Inventory only: true for a backpack entry (Index is the backpack index).
	bool bIsBackpack = false;
	// Inventory only: true for a piece of armour being worn (Index is its place among the worn items).
	bool bIsWorn = false;
	// Hand index, waist slot, skill enum value or marker index depending on the category.
	int32 Index = INDEX_NONE;
	FName QuestId;

	bool operator==(const FAedirMenuRow& Other) const
	{
		return Name == Other.Name && Value == Other.Value && SubText == Other.SubText && bDimmed == Other.bDimmed
			&& bIsHand == Other.bIsHand && bIsBackpack == Other.bIsBackpack && bIsWorn == Other.bIsWorn && Index == Other.Index && QuestId == Other.QuestId;
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
	void AddPairLine(TArray<FAedirMenuLine>& Lines, TConstArrayView<FString> LabelsAndValues);

	// Vignette, the list panel fading out to the right, the double line between list and details.
	void PaintListBackdrop(const FVector2f& Size, FAedirUIMesh& Mesh, float VignetteStrength);
	// Plain text row with a small marker instead of a filled background. MarkerStrength is 0 when no marker should show.
	TSharedRef<SWidget> MakeListRow(const FAedirMenuRow& Row, const FSlateFontInfo& Font, TFunction<FLinearColor()> TextColor, TFunction<float()> MarkerStrength, TSharedPtr<SWidget> ValueWidget = nullptr);
	// The layout shared by the category menus and the pause menu.
	TSharedRef<SWidget> MakeListScreen(const TSharedRef<SWidget>& Header, const TSharedRef<SWidget>& List, const TSharedRef<SWidget>& Detail, const TAttribute<FText>& Status, const TAttribute<FText>& Hint, float VignetteStrength);
	TSharedRef<SWidget> MakeTitle(const TAttribute<FText>& Title);
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
	virtual ~SAedirMenu() override;

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
	// The menu closes and the body reaches for the item, on the next tick so that the click has finished.
	void RequestBeltDraw(EAedirHand Hand, int32 Slot);
	int32 PendingBeltSlot = INDEX_NONE;
	int32 PendingPackIndex = INDEX_NONE;
	EAedirHand PendingBeltHand = EAedirHand::Right;
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

	FText GetTitleText() const;
	FText GetStatusText() const;
	FText GetTabText(int32 Index) const;
	FLinearColor GetTabColor(int32 Index) const;
	float GetRowMarker(int32 Index) const;
	FLinearColor GetRowTextColor(int32 Index) const;
	// The item under the mouse, or the selected one, is shown turning on the right in the inventory.
	void UpdatePreview(float DeltaSeconds);
	EVisibility GetPreviewVisibility() const;

	TWeakObjectPtr<AAedirCreature> Creature;
	TWeakObjectPtr<AAedirItemPreview> Preview;
	TSharedPtr<SAedirItemPreviewView> PreviewView;
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
