#include "UI/SAedirNotifications.h"

#include "Styling/CoreStyle.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float NotificationFadeSeconds = 0.6f;
	constexpr int32 NotificationMaxEntries = 5;
}

void SAedirNotifications::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(FMargin(32.f, 32.f))
	[
		SAssignNew(Stack, SVerticalBox)
	];
}

void SAedirNotifications::AddNotification(const FText& Message, float Seconds)
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	if (Entries.Num() >= NotificationMaxEntries)
	{
		Stack->RemoveSlot(Entries[0].Widget.ToSharedRef());
		Entries.RemoveAt(0);
	}

	TSharedRef<SWidget> Widget = SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor(AedirUIStyle::Panel)
		.Padding(FMargin(0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBorder)
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor(AedirUIStyle::Accent)
				.Padding(FMargin(2.f, 0.f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(14.f, 8.f))
			[
				SNew(STextBlock)
				.Text(Message)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.ColorAndOpacity(AedirUIStyle::Text)
			]
		];

	Stack->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 0.f, 0.f, 6.f))
	[
		Widget
	];

	FEntry Entry;
	Entry.Widget = Widget;
	Entry.Remaining = Seconds;
	Entries.Add(Entry);
}

void SAedirNotifications::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		Entry.Remaining -= InDeltaTime;
		if (Entry.Remaining <= 0.f)
		{
			Stack->RemoveSlot(Entry.Widget.ToSharedRef());
			Entries.RemoveAt(Index);
			continue;
		}

		Entry.Widget->SetRenderOpacity(FMath::Clamp(Entry.Remaining / NotificationFadeSeconds, 0.f, 1.f));
	}
}
