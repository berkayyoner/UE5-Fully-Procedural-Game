#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AAedirCreature;
class UWorld;

// Returns an empty text when fast travel to the marker is allowed, otherwise the reason it is not.
DECLARE_DELEGATE_RetVal_OneParam(FText, FAedirMapTravelQuery, FName);
DECLARE_DELEGATE_OneParam(FAedirMapTravelConfirmed, FName);

// Full-screen top-down world map. North is up, the player is the arrow, discovered locations can be fast travelled to.
class SAedirMap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirMap)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<AAedirCreature>, Creature)
		SLATE_EVENT(FAedirMapTravelQuery, OnQueryTravel)
		SLATE_EVENT(FAedirMapTravelConfirmed, OnTravel)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(100.f, 100.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	FVector2f WorldToScreen(const FVector& World, const FVector2f& Size) const;
	FVector WorldFromScreen(const FVector2f& Screen, const FVector2f& Size) const;
	int32 FindMarkerAt(const FVector2f& Screen, const FVector2f& Size) const;
	void ShowMessage(const FText& Text);
	void HandleClick(const FVector2f& Screen, const FVector2f& Size);
	void ToggleCustomMarker(const FVector2f& Screen, const FVector2f& Size);
	void ConfirmTravel();
	void CancelTravel();
	void CenterOnPlayer();

	TWeakObjectPtr<AAedirCreature> Creature;
	FAedirMapTravelQuery QueryDelegate;
	FAedirMapTravelConfirmed TravelDelegate;
	FSimpleDelegate CloseDelegate;

	// World XY (cm) shown at the centre of the screen, and how many cm one pixel covers.
	FVector2D Center = FVector2D::ZeroVector;
	float CmPerPixel = 100.f;

	FVector2f MousePosition = FVector2f::ZeroVector;
	FVector2f PressPosition = FVector2f::ZeroVector;
	bool bLeftDown = false;
	bool bDragged = false;

	bool bPanUp = false;
	bool bPanDown = false;
	bool bPanLeft = false;
	bool bPanRight = false;

	FName PendingMarkerId;
	FText Message;
	float MessageRemaining = 0.f;
	FVector2f LastSize = FVector2f(1920.f, 1080.f);
};
