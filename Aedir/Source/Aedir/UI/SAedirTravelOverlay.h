#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class STextBlock;

// Full-screen black cover shown while fast travelling.
class SAedirTravelOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirTravelOverlay)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Alpha 0 is clear, 1 is fully black with the text visible.
	void SetCover(float Alpha);
	void SetHours(float Hours);

private:
	TSharedPtr<SBorder> Cover;
	TSharedPtr<STextBlock> HoursText;
};
