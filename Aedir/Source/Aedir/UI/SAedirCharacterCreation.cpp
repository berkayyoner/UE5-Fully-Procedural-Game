#include "UI/SAedirCharacterCreation.h"

#include "Creature/AedirRaceCatalog.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"
#include "UI/AedirJournalSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const CreationHintText = TEXT("[W/S] Select race  [Enter] Begin  [Esc] Cancel");
	const FLinearColor CreationTextColor(0.85f, 0.85f, 0.85f, 1.f);
	const FLinearColor CreationDimColor(0.6f, 0.6f, 0.6f, 1.f);
	const FLinearColor CreationLineColor(1.f, 1.f, 1.f, 0.35f);

	void CreationAddLine(TSharedPtr<SVerticalBox> Box, const FString& Content, int32 FontSize, bool bBold, const FLinearColor& Color, bool bWrap, const FMargin& Padding)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(Padding)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Content))
			.Font(bBold ? FCoreStyle::GetDefaultFontStyle("Bold", FontSize) : FCoreStyle::GetDefaultFontStyle("Regular", FontSize))
			.ColorAndOpacity(Color)
			.AutoWrapText(bWrap)
		];
	}
}

void SAedirCharacterCreation::Construct(const FArguments& InArgs)
{
	ConfirmDelegate = InArgs._OnConfirm;
	CancelDelegate = InArgs._OnCancel;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const TArray<FAedirRaceDef>& Races = UAedirRaceCatalog::GetAll();

	TSharedRef<SVerticalBox> ListBox = SNew(SVerticalBox);
	for (int32 Index = 0; Index < Races.Num(); ++Index)
	{
		ListBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SBorder)
			.BorderImage(WhiteBrush)
			.BorderBackgroundColor_Lambda([this, Index]() { return FSlateColor(GetRowBackground(Index)); })
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
						.BorderBackgroundColor_Lambda([this, Index]() { return FSlateColor(GetRowAccent(Index)); })
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 6.f))
				[
					SNew(STextBlock)
					.Text(Races[Index].DisplayName)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
					.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetRowTextColor(Index)); })
				]
			]
		];
	}

	ChildSlot
	.Padding(FMargin(96.f, 64.f))
	[
		SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.82f))
		.Padding(FMargin(28.f, 20.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("CHARACTER CREATION")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				.ColorAndOpacity(FLinearColor::White)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(1.f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(CreationLineColor)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(FMargin(0.f, 12.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.38f)
				[
					ListBox
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(12.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(1.f)
					[
						SNew(SBorder)
						.BorderImage(WhiteBrush)
						.BorderBackgroundColor(CreationLineColor)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.62f)
				.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
				[
					SAssignNew(DetailBox, SVerticalBox)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(CreationHintText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
			]
		]
	];

	RebuildDetail();
}

FReply SAedirCharacterCreation::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape)
	{
		CancelDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Virtual_Accept)
	{
		const TArray<FAedirRaceDef>& Races = UAedirRaceCatalog::GetAll();
		if (Races.IsValidIndex(SelectedIndex))
		{
			ConfirmDelegate.ExecuteIfBound(Races[SelectedIndex].Id);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		MoveSelection(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		MoveSelection(1);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAedirCharacterCreation::MoveSelection(int32 Delta)
{
	const int32 Count = UAedirRaceCatalog::GetAll().Num();
	if (Count == 0)
	{
		return;
	}

	SelectedIndex = (SelectedIndex + Delta + Count) % Count;
	RebuildDetail();
}

void SAedirCharacterCreation::RebuildDetail()
{
	if (!DetailBox.IsValid())
	{
		return;
	}

	DetailBox->ClearChildren();

	const TArray<FAedirRaceDef>& Races = UAedirRaceCatalog::GetAll();
	if (!Races.IsValidIndex(SelectedIndex))
	{
		return;
	}

	const FAedirRaceDef& Race = Races[SelectedIndex];

	CreationAddLine(DetailBox, Race.DisplayName.ToString(), 18, true, FLinearColor::White, false, FMargin(0.f, 0.f, 0.f, 6.f));
	CreationAddLine(DetailBox, Race.Description.ToString(), 10, false, FLinearColor(0.75f, 0.75f, 0.75f, 1.f), true, FMargin(0.f, 4.f, 0.f, 12.f));

	const FString StatLine = FString::Printf(TEXT("Health %.0f, Magicka %.0f, Stamina %.0f, Strength %.0f, Athletism %.0f, Height %d%%"),
		Race.MaxHealth, Race.MaxMana, Race.MaxStamina, Race.Strength, Race.Athletism, FMath::RoundToInt(Race.HeightMultiplier * 100.f));
	CreationAddLine(DetailBox, StatLine, 11, false, CreationTextColor, true, FMargin(0.f, 2.f));

	CreationAddLine(DetailBox, TEXT("Skill bonuses:"), 11, true, CreationDimColor, false, FMargin(0.f, 10.f, 0.f, 4.f));
	for (const FAedirSkillBonus& Bonus : Race.SkillBonuses)
	{
		const FString BonusLine = FString::Printf(TEXT("+%.0f %s"), Bonus.Bonus, *UAedirJournalSubsystem::GetSkillDisplayName(Bonus.Skill));
		CreationAddLine(DetailBox, BonusLine, 11, false, CreationTextColor, false, FMargin(0.f, 2.f));
	}
}

FLinearColor SAedirCharacterCreation::GetRowBackground(int32 Index) const
{
	return Index == SelectedIndex ? FLinearColor(0.92f, 0.92f, 0.92f, 0.18f) : FLinearColor(0.f, 0.f, 0.f, 0.f);
}

FLinearColor SAedirCharacterCreation::GetRowAccent(int32 Index) const
{
	return Index == SelectedIndex ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(1.f, 1.f, 1.f, 0.f);
}

FLinearColor SAedirCharacterCreation::GetRowTextColor(int32 Index) const
{
	return Index == SelectedIndex ? FLinearColor::White : CreationTextColor;
}
