#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

// Short messages stacked in the top left corner of the screen. They fade out and never take input.
class SAedirNotifications : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirNotifications)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void AddNotification(const FText& Message, float Seconds);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct FEntry
	{
		TSharedPtr<SWidget> Widget;
		float Remaining = 0.f;
	};

	TArray<FEntry> Entries;
	TSharedPtr<SVerticalBox> Stack;
};
