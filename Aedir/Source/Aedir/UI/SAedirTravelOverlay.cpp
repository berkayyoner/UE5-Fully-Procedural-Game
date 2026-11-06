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
				.Text(FText::FromString(TEXT("TRAVELLING")))
				.Font(AedirUIStyle::TitleFont(26.f))
				.ColorAndOpacity(AedirUIStyle::Ivory)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 12.f))
			[
				SNew(SAedirUIPaint)
				.DesiredSize(FVector2D(520.f, 12.f))
				.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
				{
					AedirUIStyle::AddOrnamentRule(Mesh, Size * 0.5f, Size.X * 0.5f, AedirUIStyle::Metal);
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(HoursText, STextBlock)
				.Font(AedirUIStyle::LightFont(14.f, 40))
				.ColorAndOpacity(AedirUIStyle::TextDim)
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
