#include "UI/SAedirRadialMenu.h"

#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float RadialOptionWidth = 230.f;
	constexpr float RadialOptionHeight = 64.f;
	constexpr float RadialHubWidth = 96.f;
	constexpr float RadialGap = 10.f;
	const FLinearColor RadialBackdrop(0.f, 0.f, 0.f, 0.55f);
}

void SAedirRadialMenu::Construct(const FArguments& InArgs)
{
	ChosenDelegate = InArgs._OnChosen;
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor(RadialBackdrop)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SGridPanel)
			+ SGridPanel::Slot(1, 0).Padding(RadialGap)
			[
				MakeOption(EAedirRadialOption::Skills, TEXT("Skills"), TEXT("W"))
			]
			+ SGridPanel::Slot(0, 1).Padding(RadialGap)
			[
				MakeOption(EAedirRadialOption::Magic, TEXT("Magic"), TEXT("A"))
			]
			+ SGridPanel::Slot(1, 1).Padding(RadialGap).VAlign(VAlign_Center).HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(RadialHubWidth)
				.HeightOverride(RadialOptionHeight)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("AEDIR")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					.ColorAndOpacity(AedirUIStyle::TextDim)
				]
			]
			+ SGridPanel::Slot(2, 1).Padding(RadialGap)
			[
				MakeOption(EAedirRadialOption::Inventory, TEXT("Inventory"), TEXT("D"))
			]
			+ SGridPanel::Slot(1, 2).Padding(RadialGap)
			[
				MakeOption(EAedirRadialOption::Map, TEXT("Map"), TEXT("S"))
			]
		]
	];
}

TSharedRef<SWidget> SAedirRadialMenu::MakeOption(EAedirRadialOption Option, const TCHAR* Label, const TCHAR* KeyHint)
{
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const int32 Index = static_cast<int32>(Option);

	return SNew(SBox)
		.WidthOverride(RadialOptionWidth)
		.HeightOverride(RadialOptionHeight)
		[
			SNew(SBorder)
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor_Lambda([this, Index]()
			{
				return Selected == Index ? FSlateColor(AedirUIStyle::PanelSelected) : FSlateColor(AedirUIStyle::Panel);
			})
			.Padding(FMargin(0.f))
			.Cursor(EMouseCursor::Hand)
			.OnMouseMove_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
			{
				Selected = Index;
				return FReply::Handled();
			})
			.OnMouseButtonDown_Lambda([this, Option, Index](const FGeometry&, const FPointerEvent& MouseEvent)
			{
				if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Handled();
				}
				Selected = Index;
				Choose(Option);
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(3.f)
					[
						SNew(SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor_Lambda([this, Index]()
						{
							return Selected == Index ? FSlateColor(AedirUIStyle::Accent) : FSlateColor(AedirUIStyle::Line);
						})
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(18.f, 0.f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
					.ColorAndOpacity_Lambda([this, Index]()
					{
						return Selected == Index ? FSlateColor(FLinearColor::White) : FSlateColor(AedirUIStyle::Text);
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 16.f, 0.f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(KeyHint))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(AedirUIStyle::TextDim)
				]
			]
		];
}

void SAedirRadialMenu::Choose(EAedirRadialOption Option)
{
	ChosenDelegate.ExecuteIfBound(Option);
}

FReply SAedirRadialMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirRadialMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Tab || Key == EKeys::Escape)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Skills);
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Map);
		return FReply::Handled();
	}
	if (Key == EKeys::A || Key == EKeys::Left)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Magic);
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Inventory);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E)
	{
		if (Selected != INDEX_NONE)
		{
			Choose(static_cast<EAedirRadialOption>(Selected));
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}
