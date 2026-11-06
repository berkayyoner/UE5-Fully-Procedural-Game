#include "UI/SAedirTravelOverlay.h"

#include "Styling/CoreStyle.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SAedirTravelOverlay::Construct(const FArguments& InArgs)
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	ChildSlot
	[
		SAssignNew(Cover, SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor(FLinearColor::Black)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Travelling...")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
				.ColorAndOpacity(AedirUIStyle::Accent)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				SAssignNew(HoursText, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
				.ColorAndOpacity(AedirUIStyle::Text)
			]
		]
	];

	SetCover(0.f);
}

void SAedirTravelOverlay::SetCover(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
	// Hit-testable while covering, so clicks do not reach anything below.
	SetVisibility(Clamped > 0.f ? EVisibility::Visible : EVisibility::HitTestInvisible);
	SetRenderOpacity(Clamped);
}

void SAedirTravelOverlay::SetHours(float Hours)
{
	HoursText->SetText(FText::FromString(FString::Printf(TEXT("%.1f hours"), Hours)));
}
