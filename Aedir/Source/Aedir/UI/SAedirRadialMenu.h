#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

// The four options of the Tab selector, in screen direction order.
enum class EAedirRadialOption : uint8
{
	Skills,
	Map,
	Magic,
	Inventory,
	Count
};

DECLARE_DELEGATE_OneParam(FAedirRadialChosen, EAedirRadialOption);

// Small cross-shaped selector in the centre of the screen: Skills up, Map down, Magic left, Inventory right.
class SAedirRadialMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirRadialMenu)
	{}
		SLATE_EVENT(FAedirRadialChosen, OnChosen)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> MakeOption(EAedirRadialOption Option, const TCHAR* Label, const TCHAR* KeyHint);
	void Choose(EAedirRadialOption Option);

	FAedirRadialChosen ChosenDelegate;
	FSimpleDelegate CloseDelegate;
	int32 Selected = INDEX_NONE;
};
