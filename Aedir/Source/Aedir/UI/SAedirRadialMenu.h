#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

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

// Four-way star over the darkened game: Skills up, Map down, Magic left, Inventory right.
class SAedirRadialMenu : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirRadialMenu)
	{}
		SLATE_EVENT(FAedirRadialChosen, OnChosen)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(100.f, 100.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	int32 FindOptionAt(const FVector2f& Local, const FVector2f& Size) const;
	void Choose(EAedirRadialOption Option);

	FAedirRadialChosen ChosenDelegate;
	FSimpleDelegate CloseDelegate;
	int32 Selected = INDEX_NONE;
	float Glow[static_cast<int32>(EAedirRadialOption::Count)] = {};
};
