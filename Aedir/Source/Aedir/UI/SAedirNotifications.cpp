#include "UI/SAedirNotifications.h"

#include "UI/AedirUIStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float NotificationFadeOutSeconds = 0.8f;
	constexpr float NotificationFadeInSeconds = 0.35f;
	constexpr int32 NotificationMaxEntries = 5;
	constexpr float NotificationFontSize = 19.f;
	const FVector2D NotificationRuleSize(420.f, 10.f);
}

void SAedirNotifications::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	AvoidMenu = InArgs._AvoidMenu;

	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Top)
	.Padding(TAttribute<FMargin>::CreateSP(this, &SAedirNotifications::GetStackPadding))
	[
		SAssignNew(Stack, SVerticalBox)
	];
}

FMargin SAedirNotifications::GetStackPadding() const
{
	const float Left = AvoidMenu.Get(false) ? GetCachedGeometry().GetLocalSize().X * AedirUIStyle::ListSeparatorFraction + 40.f : 56.f;
	return FMargin(Left, 48.f, 0.f, 0.f);
}

void SAedirNotifications::AddNotification(const FText& Message, float Seconds)
{
	if (Entries.Num() >= NotificationMaxEntries)
	{
		Stack->RemoveSlot(Entries[0].Widget.ToSharedRef());
		Entries.RemoveAt(0);
	}

	TSharedRef<SWidget> Widget = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Message)
			.Font(AedirUIStyle::LightFont(NotificationFontSize, 40))
			.ColorAndOpacity(AedirUIStyle::Ivory)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
		[
			SNew(SAedirUIPaint)
			.DesiredSize(NotificationRuleSize)
			.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
			{
				const float Y = Size.Y * 0.5f;
				AedirUIStyle::AddDiamond(Mesh, FVector2f(4.f, Y), 4.f, 4.f, AedirUIStyle::Metal);
				AedirUIStyle::AddFadedLine(Mesh, FVector2f(12.f, Y), FVector2f(Size.X, Y), 1.f, AedirUIStyle::Metal, 0.f, 0.9f);
			})
		];
	Widget->SetRenderOpacity(0.f);

	Stack->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 0.f, 0.f, 12.f))
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
		Entry.Age += InDeltaTime;
		if (Entry.Remaining <= 0.f)
		{
			Stack->RemoveSlot(Entry.Widget.ToSharedRef());
			Entries.RemoveAt(Index);
			continue;
		}

		const float FadeIn = FMath::Clamp(Entry.Age / NotificationFadeInSeconds, 0.f, 1.f);
		const float FadeOut = FMath::Clamp(Entry.Remaining / NotificationFadeOutSeconds, 0.f, 1.f);
		Entry.Widget->SetRenderOpacity(FMath::Min(FadeIn, FadeOut));
	}
}
