#include "UI/SAedirMenu.h"

#include "Combat/AedirCombatTypes.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/AedirJournalSubsystem.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const MenuInventoryTabNames[] = { TEXT("EQUIPPED"), TEXT("BACKPACK") };
	const TCHAR* const MenuInventoryHintText = TEXT("[W/S] Select  [A/D] Tab  [LMB] Select, again to use  [RMB] Actions  [E] Use  [Q] Drop  [Tab] Close");
	const TCHAR* const MenuListHintText = TEXT("[W/S] Select  [Tab] Close");
	constexpr float MenuContextWidth = 190.f;
	constexpr float MenuContextHeight = 28.f;

	const FLinearColor MenuDimColor(0.45f, 0.45f, 0.45f, 1.f);
	const FLinearColor MenuTextColor(0.85f, 0.85f, 0.85f, 1.f);
	const FLinearColor MenuLineColor(1.f, 1.f, 1.f, 0.35f);

	const EAedirSkill MenuMagicSchools[] =
	{
		EAedirSkill::DestructionMagic,
		EAedirSkill::RegenerationMagic,
		EAedirSkill::IllusionMagic,
		EAedirSkill::MetamorphosisMagic,
		EAedirSkill::SummonMagic
	};

	const TCHAR* MenuItemClassName(EAedirItemClass ItemClass)
	{
		switch (ItemClass)
		{
		case EAedirItemClass::Weapon: return TEXT("Weapon");
		case EAedirItemClass::Unusable: return TEXT("Unusable");
		case EAedirItemClass::Poison: return TEXT("Poison");
		case EAedirItemClass::Book: return TEXT("Book");
		case EAedirItemClass::Armor: return TEXT("Armor");
		case EAedirItemClass::Object: return TEXT("Object");
		default: break;
		}
		return TEXT("Unknown");
	}

	const TCHAR* MenuDamageTypeName(EAedirDamageType DamageType)
	{
		switch (DamageType)
		{
		case EAedirDamageType::Blunt: return TEXT("Blunt");
		case EAedirDamageType::Slash: return TEXT("Slash");
		case EAedirDamageType::Pierce: return TEXT("Pierce");
		default: break;
		}
		return TEXT("Unknown");
	}

	FString MenuSchoolDescription(EAedirSkill School)
	{
		switch (School)
		{
		case EAedirSkill::DestructionMagic:
			return TEXT("Harnesses fire, frost and shock to strike targets at range.");
		case EAedirSkill::RegenerationMagic:
			return TEXT("Mends wounds and restores health and magicka.");
		case EAedirSkill::IllusionMagic:
			return TEXT("Bends the minds of others, calming, frightening or confusing them.");
		case EAedirSkill::MetamorphosisMagic:
			return TEXT("Reshapes the body and the matter around it, changing form and substance.");
		case EAedirSkill::SummonMagic:
			return TEXT("Calls creatures and blades out of nothing to fight on your behalf.");
		default: break;
		}
		return FString();
	}

	void MenuAddLine(TArray<FAedirMenuLine>& Lines, EAedirMenuLineKind Kind, const FString& Content)
	{
		FAedirMenuLine Line;
		Line.Kind = Kind;
		Line.Content = Content;
		Lines.Add(Line);
	}

	void MenuAddCharacterStats(const UAedirStatsComponent* Stats, TArray<FAedirMenuLine>& Lines);

	AAedirItemActor* MenuFindRowItem(const AAedirCreature* Pawn, const FAedirMenuRow& Row)
	{
		const UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
		if (!Interaction)
		{
			return nullptr;
		}

		if (Row.bIsHand)
		{
			return Interaction->GetHeldItem(static_cast<EAedirHand>(Row.Index));
		}

		const TArray<AAedirItemActor*> Items = Row.bIsBackpack ? Interaction->GetBackpackItemsCopy() : Interaction->GetWaistItemsCopy();
		return Items.IsValidIndex(Row.Index) ? Items[Row.Index] : nullptr;
	}

	void MenuBuildInventoryDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, bool bBackpackTab, TArray<FAedirMenuLine>& Lines)
	{
		const AAedirItemActor* Item = Selected ? MenuFindRowItem(Pawn, *Selected) : nullptr;

		if (IsValid(Item))
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Title, Item->ItemName.ToString());
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Class: %s"), MenuItemClassName(Item->ItemClass)));
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Weight: %s kg"), *UAedirJournalSubsystem::FormatWeight(Item->Weight)));
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Damage: %.0f"), Item->GetDamage()));
			if (Item->GetWeaponLength() > 0.f)
			{
				MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Length: %.0f cm"), Item->GetWeaponLength()));
			}
			else
			{
				MenuAddLine(Lines, EAedirMenuLineKind::Text, TEXT("Length: Short"));
			}
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Damage type: %s"), MenuDamageTypeName(Item->GetDamageType())));
		}
		else
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, bBackpackTab ? TEXT("The backpack is empty.") : TEXT("Nothing in this slot."));
		}

		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
		MenuAddCharacterStats(Pawn ? Pawn->Stats.Get() : nullptr, Lines);
		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
		const float Carried = Pawn ? Pawn->GetCarriedWeight() : 0.f;
		const float Capacity = Pawn ? Pawn->GetCarryCapacity() : 0.f;
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Carry Weight: %s / %.0f"), *UAedirJournalSubsystem::FormatWeight(Carried), Capacity));
	}

	void MenuBuildMagicDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
	{
		if (!Selected)
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No school selected."));
			return;
		}

		const EAedirSkill School = static_cast<EAedirSkill>(Selected->Index);
		const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
		const float SkillValue = Stats ? Stats->GetSkill(School) : 0.f;

		MenuAddLine(Lines, EAedirMenuLineKind::Title, Selected->Name);
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Skill: %d  %s"), FMath::FloorToInt(SkillValue), *UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString()));
		if (Stats)
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Magicka: %d / %d"), FMath::RoundToInt(Stats->GetMana()), FMath::RoundToInt(Stats->GetMaxMana())));
		}
		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
		MenuAddLine(Lines, EAedirMenuLineKind::Body, MenuSchoolDescription(School));
	}

	// Overall level: every 25 skill points above the starting values raise it by one.
	int32 MenuCharacterLevel(const UAedirStatsComponent* Stats)
	{
		float Total = 0.f;
		for (int32 Index = 0; Index < static_cast<int32>(EAedirSkill::Count); ++Index)
		{
			Total += Stats->GetSkill(static_cast<EAedirSkill>(Index));
		}
		return 1 + FMath::FloorToInt(Total / 25.f);
	}

	void MenuAddCharacterStats(const UAedirStatsComponent* Stats, TArray<FAedirMenuLine>& Lines)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Heading, TEXT("Character"));
		if (!Stats)
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No stat data."));
			return;
		}

		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Level: %d"), MenuCharacterLevel(Stats)));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Strength: %.0f"), Stats->Strength));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Athletism: %.0f"), Stats->Athletism));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Health: %d / %d"), FMath::RoundToInt(Stats->GetHealth()), FMath::RoundToInt(Stats->GetMaxHealth())));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Magicka: %d / %d"), FMath::RoundToInt(Stats->GetMana()), FMath::RoundToInt(Stats->GetMaxMana())));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Stamina: %d / %d"), FMath::RoundToInt(Stats->GetStamina()), FMath::RoundToInt(Stats->GetMaxStamina())));
	}

	void MenuBuildSkillsDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
	{
		if (!Selected)
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No skill selected."));
			return;
		}

		const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
		const float SkillValue = Stats ? Stats->GetSkill(static_cast<EAedirSkill>(Selected->Index)) : 0.f;

		// The character stats come first so they are always on screen.
		MenuAddCharacterStats(Stats, Lines);
		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());

		MenuAddLine(Lines, EAedirMenuLineKind::Title, Selected->Name);
		MenuAddLine(Lines, EAedirMenuLineKind::Value, FString::Printf(TEXT("%d"), FMath::FloorToInt(SkillValue)));
		MenuAddLine(Lines, EAedirMenuLineKind::Heading, FString::Printf(TEXT("Rank: %s"), *UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString()));
		MenuAddLine(Lines, EAedirMenuLineKind::Progress, FString());
	}
}

TArray<FAedirMenuRow> AedirMenuContent::BuildJournalRows(const UWorld* World)
{
	TArray<FAedirMenuRow> Result;
	const UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	if (!Journal)
	{
		return Result;
	}

	const TArray<FAedirQuest>& Quests = Journal->GetQuests();
	const FName Tracked = Journal->GetTrackedQuest();

	// Active quests first, then completed ones.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FAedirQuest& Quest : Quests)
		{
			const bool bInActivePass = Pass == 0 && Quest.bActive && !Quest.bCompleted;
			const bool bInDonePass = Pass == 1 && Quest.bCompleted;
			if (!bInActivePass && !bInDonePass)
			{
				continue;
			}

			FAedirMenuRow Row;
			Row.Name = Quest.Title.ToString();
			Row.bDimmed = Quest.bCompleted;
			if (Quest.bCompleted)
			{
				Row.Value = TEXT("(done)");
			}
			else if (Quest.Id == Tracked)
			{
				Row.Value = TEXT("tracked");
			}
			Row.QuestId = Quest.Id;
			Result.Add(Row);
		}
	}

	return Result;
}

void AedirMenuContent::BuildJournalDetail(const UWorld* World, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
{
	const UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	const FAedirQuest* Quest = (Journal && Selected)
		? Journal->GetQuests().FindByPredicate([Selected](const FAedirQuest& Candidate)
		{
			return Candidate.Id == Selected->QuestId;
		})
		: nullptr;

	if (!Quest)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No quest selected."));
		return;
	}

	MenuAddLine(Lines, EAedirMenuLineKind::Title, Quest->Title.ToString());
	MenuAddLine(Lines, EAedirMenuLineKind::Body, Quest->Description.ToString());
	MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
	MenuAddLine(Lines, EAedirMenuLineKind::Heading, TEXT("Objectives"));
	for (const FAedirQuestObjective& Objective : Quest->Objectives)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("%s %s"), Objective.bDone ? TEXT("[x]") : TEXT("[ ]"), *Objective.Text.ToString()));
	}

	MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
	if (Quest->bCompleted)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("Completed"));
	}
	else if (Journal->GetTrackedQuest() == Quest->Id)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Text, TEXT("Tracked"));
	}
	else
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("[E] Track quest"));
	}
}


void AedirMenuContent::PopulateDetail(SVerticalBox& Box, const TArray<FAedirMenuLine>& NewLines, const TFunction<TOptional<float>()>& Progress)
{
	Box.ClearChildren();

	for (const FAedirMenuLine& Line : NewLines)
	{
		switch (Line.Kind)
		{
		case EAedirMenuLineKind::Title:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
				.ColorAndOpacity(FLinearColor::White)
			];
			break;
		case EAedirMenuLineKind::Value:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
				.ColorAndOpacity(FLinearColor::White)
			];
			break;
		case EAedirMenuLineKind::Heading:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f, 0.f, 4.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
			];
			break;
		case EAedirMenuLineKind::Text:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 2.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.ColorAndOpacity(MenuTextColor)
			];
			break;
		case EAedirMenuLineKind::Body:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 4.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f, 1.f))
				.AutoWrapText(true)
			];
			break;
		case EAedirMenuLineKind::Dim:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 2.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Content))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(MenuDimColor)
				.AutoWrapText(true)
			];
			break;
		case EAedirMenuLineKind::Progress:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f))
			[
				SNew(SBox)
				.HeightOverride(8.f)
				[
					SNew(SProgressBar)
					.Percent_Lambda([Progress]() -> TOptional<float> { return Progress(); })
					.FillColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.f))
				]
			];
			break;
		case EAedirMenuLineKind::Spacer:
			Box.AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(10.f)
			];
			break;
		default:
			break;
		}
	}
}

void SAedirMenu::Construct(const FArguments& InArgs)
{
	Creature = InArgs._Creature;
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	TSharedRef<SHorizontalBox> TabBar = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EAedirInventoryTab::Count); ++Index)
	{
		TabBar->AddSlot()
		.AutoWidth()
		.Padding(FMargin(14.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(FMargin(2.f))
			.Visibility_Lambda([this, Index]() { return GetTabVisibility(Index); })
			.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
			{
				SetInventoryTab(Index);
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Index]() { return GetTabText(Index); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetTabColor(Index)); })
			]
		];
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
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
			.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("AEDIR")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
			[
				TabBar
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(1.f)
				[
					SNew(SBorder)
					.BorderImage(WhiteBrush)
					.BorderBackgroundColor(MenuLineColor)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(FMargin(0.f, 12.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.42f)
				[
					SAssignNew(ListScroll, SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ListBox, SVerticalBox)
					]
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
						.BorderBackgroundColor(MenuLineColor)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.58f)
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
				.Text_Lambda([this]() { return GetHintText(); })
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
			]
		]
		]
		+ SOverlay::Slot()
		[
			SNew(SCanvas)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SCanvas::Slot()
			.Position_Lambda([this]() { return ContextPosition; })
			.Size(FVector2D(MenuContextWidth, MenuContextHeight))
			[
				SAssignNew(ContextButton, SBorder)
				.BorderImage(WhiteBrush)
				.BorderBackgroundColor_Lambda([this]()
				{
					const bool bHovered = ContextButton.IsValid() && ContextButton->IsHovered();
					return FSlateColor(bHovered ? FLinearColor(0.35f, 0.35f, 0.35f, 0.98f) : FLinearColor(0.08f, 0.08f, 0.08f, 0.98f));
				})
				.Visibility_Lambda([this]() { return ContextRow != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
				.Padding(FMargin(10.f, 4.f))
				.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
				{
					RunContextAction();
					return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetContextText(); })
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		]
	];

	RefreshView(true);
}

void SAedirMenu::SetCategory(int32 NewCategory)
{
	const int32 Count = static_cast<int32>(EAedirMenuCategory::Count);
	const int32 WrappedCategory = ((NewCategory % Count) + Count) % Count;
	if (WrappedCategory != CurrentCategory)
	{
		InventoryTab = 0;
	}
	CurrentCategory = WrappedCategory;
	SelectedRow = 0;
	CloseContext();
	RefreshView(true);
}

void SAedirMenu::SetInventoryTab(int32 NewTab)
{
	if (CurrentCategory != static_cast<int32>(EAedirMenuCategory::Inventory))
	{
		return;
	}

	const int32 Count = static_cast<int32>(EAedirInventoryTab::Count);
	InventoryTab = ((NewTab % Count) + Count) % Count;
	SelectedRow = 0;
	CloseContext();
	RefreshView(true);
}

FReply SAedirMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	CloseContext();
	// A click anywhere on the menu keeps the keyboard focus here, so Tab can still close it.
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Tab || Key == EKeys::Escape)
	{
		CloseDelegate.ExecuteIfBound();
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
	if (Key == EKeys::A || Key == EKeys::Left)
	{
		SetInventoryTab(InventoryTab - 1);
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		SetInventoryTab(InventoryTab + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::E)
	{
		ActivateSelected(false);
		return FReply::Handled();
	}
	if (Key == EKeys::Q)
	{
		const int32 Hovered = GetHoveredRow();
		DropRow(Hovered != INDEX_NONE ? Hovered : SelectedRow);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAedirMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RefreshView(false);
}

TArray<FAedirMenuRow> SAedirMenu::BuildRows() const
{
	TArray<FAedirMenuRow> Result;
	const AAedirCreature* Pawn = Creature.Get();

	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory:
	{
		const UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
		if (InventoryTab == static_cast<int32>(EAedirInventoryTab::Backpack))
		{
			const TArray<AAedirItemActor*> Stowed = Interaction ? Interaction->GetBackpackItemsCopy() : TArray<AAedirItemActor*>();
			for (int32 StowedIndex = 0; StowedIndex < Stowed.Num(); ++StowedIndex)
			{
				if (!IsValid(Stowed[StowedIndex]))
				{
					continue;
				}

				FAedirMenuRow Row;
				Row.Name = Stowed[StowedIndex]->ItemName.ToString();
				Row.Value = FString::Printf(TEXT("%s kg"), *UAedirJournalSubsystem::FormatWeight(Stowed[StowedIndex]->Weight));
				Row.bIsBackpack = true;
				Row.Index = StowedIndex;
				Result.Add(Row);
			}
			break;
		}

		const TArray<AAedirItemActor*> Waist = Interaction ? Interaction->GetWaistItemsCopy() : TArray<AAedirItemActor*>();

		for (int32 HandIndex = 0; HandIndex < 2; ++HandIndex)
		{
			const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(static_cast<EAedirHand>(HandIndex)) : nullptr;
			FAedirMenuRow Row;
			Row.Name = HandIndex == 0 ? TEXT("Left hand") : TEXT("Right hand");
			Row.Value = IsValid(Item) ? Item->ItemName.ToString() : FString(TEXT("Empty"));
			Row.bDimmed = !IsValid(Item);
			Row.bIsHand = true;
			Row.Index = HandIndex;
			Result.Add(Row);
		}

		for (int32 SlotIndex = 0; SlotIndex < 6; ++SlotIndex)
		{
			const AAedirItemActor* Item = Waist.IsValidIndex(SlotIndex) ? Waist[SlotIndex] : nullptr;
			FAedirMenuRow Row;
			Row.Name = FString::Printf(TEXT("Waist %s %d"), SlotIndex < 3 ? TEXT("Left") : TEXT("Right"), SlotIndex % 3 + 1);
			Row.Value = IsValid(Item) ? Item->ItemName.ToString() : FString(TEXT("Empty"));
			Row.bDimmed = !IsValid(Item);
			Row.bIsHand = false;
			Row.Index = SlotIndex;
			Result.Add(Row);
		}
		break;
	}
	case EAedirMenuCategory::Magic:
	{
		const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
		for (const EAedirSkill School : MenuMagicSchools)
		{
			const float SkillValue = Stats ? Stats->GetSkill(School) : 0.f;
			FAedirMenuRow Row;
			Row.Name = UAedirJournalSubsystem::GetSkillDisplayName(School);
			Row.Value = FString::Printf(TEXT("%d  %s"), FMath::FloorToInt(SkillValue), *UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString());
			Row.Index = static_cast<int32>(School);
			Result.Add(Row);
		}
		break;
	}
	case EAedirMenuCategory::Skills:
	{
		const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
		for (uint8 Raw = 0; Raw < static_cast<uint8>(EAedirSkill::Count); ++Raw)
		{
			const EAedirSkill Skill = static_cast<EAedirSkill>(Raw);
			const float SkillValue = Stats ? Stats->GetSkill(Skill) : 0.f;
			FAedirMenuRow Row;
			Row.Name = UAedirJournalSubsystem::GetSkillDisplayName(Skill);
			Row.Value = FString::Printf(TEXT("%d  %s"), FMath::FloorToInt(SkillValue), *UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString());
			Row.Index = static_cast<int32>(Skill);
			Result.Add(Row);
		}
		break;
	}
	default:
		break;
	}

	return Result;
}

TArray<FAedirMenuLine> SAedirMenu::BuildDetailLines(const TArray<FAedirMenuRow>& NewRows) const
{
	TArray<FAedirMenuLine> Result;
	const AAedirCreature* Pawn = Creature.Get();
	const FAedirMenuRow* Selected = NewRows.IsValidIndex(SelectedRow) ? &NewRows[SelectedRow] : nullptr;

	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory:
		MenuBuildInventoryDetail(Pawn, Selected, InventoryTab == static_cast<int32>(EAedirInventoryTab::Backpack), Result);
		break;
	case EAedirMenuCategory::Magic:
		MenuBuildMagicDetail(Pawn, Selected, Result);
		break;
	case EAedirMenuCategory::Skills:
		MenuBuildSkillsDetail(Pawn, Selected, Result);
		break;
	default:
		break;
	}

	return Result;
}

void SAedirMenu::RefreshView(bool bForce)
{
	TArray<FAedirMenuRow> NewRows = BuildRows();
	SelectedRow = NewRows.Num() > 0 ? FMath::Clamp(SelectedRow, 0, NewRows.Num() - 1) : 0;
	TArray<FAedirMenuLine> NewLines = BuildDetailLines(NewRows);

	if (bForce || !(NewRows == Rows))
	{
		Rows = MoveTemp(NewRows);
		CloseContext();
		RebuildList();
	}
	if (bForce || !(NewLines == Lines))
	{
		Lines = MoveTemp(NewLines);
		RebuildDetail();
	}
}

void SAedirMenu::RebuildList()
{
	if (!ListBox.IsValid())
	{
		return;
	}

	ListBox->ClearChildren();
	RowWidgets.Reset();

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FAedirMenuRow& Row = Rows[Index];
		TSharedRef<SBorder> RowBorder = SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor_Lambda([this, Index]() { return FSlateColor(GetRowBackground(Index)); })
		.OnMouseButtonDown_Lambda([this, Index](const FGeometry& RowGeometry, const FPointerEvent& MouseEvent)
		{
			HandleRowClick(Index, RowGeometry, MouseEvent);
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
					.BorderBackgroundColor_Lambda([this, Index]() { return FSlateColor(GetRowAccent(Index)); })
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(10.f, 5.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Name))
				.Font(RowFont)
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetRowTextColor(Index)); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(10.f, 5.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Value))
				.Font(RowFont)
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetRowTextColor(Index)); })
			]
		];

		ListBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f))
		[
			RowBorder
		];
		RowWidgets.Add(RowBorder);
	}

	ScrollToSelection();
}

void SAedirMenu::RebuildDetail()
{
	if (!DetailBox.IsValid())
	{
		return;
	}

	AedirMenuContent::PopulateDetail(*DetailBox, Lines, [this]() -> TOptional<float> { return GetSelectedSkillProgress(); });
}

void SAedirMenu::MoveSelection(int32 Delta)
{
	if (Rows.Num() == 0)
	{
		return;
	}

	const int32 Count = Rows.Num();
	SelectedRow = (SelectedRow + Delta + Count) % Count;
	RefreshView(false);
	ScrollToSelection();
}

void SAedirMenu::ActivateSelected(bool bFromClick)
{
	if (!Rows.IsValidIndex(SelectedRow))
	{
		return;
	}

	const FAedirMenuRow Row = Rows[SelectedRow];
	AAedirCreature* Pawn = Creature.Get();

	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory:
	{
		UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
		if (!Interaction)
		{
			break;
		}

		if (Row.bIsBackpack)
		{
			// Stowed items are taken in a free hand, the right one first.
			const EAedirHand Hand = Interaction->GetHandState(EAedirHand::Right) == EAedirHandState::Idle ? EAedirHand::Right : EAedirHand::Left;
			if (Interaction->GetHandState(Hand) == EAedirHandState::Idle)
			{
				Interaction->TakeFromBackpack(Row.Index, Hand);
			}
		}
		else if (Row.bIsHand)
		{
			// Clicking a held item does nothing, [E] uses it.
			const EAedirHand Hand = static_cast<EAedirHand>(Row.Index);
			if (!bFromClick && Interaction->GetHeldItem(Hand))
			{
				Interaction->UseHeld(Hand);
			}
		}
		else
		{
			// Waist entries are drawn on the side's hand, which must be free.
			const EAedirHand Hand = Row.Index < 3 ? EAedirHand::Left : EAedirHand::Right;
			if (Interaction->GetHandState(Hand) == EAedirHandState::Idle)
			{
				Interaction->HolsterToWaist(Hand);
			}
		}
		break;
	}
	default:
		break;
	}
}

void SAedirMenu::DropRow(int32 RowIndex)
{
	if (CurrentCategory != static_cast<int32>(EAedirMenuCategory::Inventory) || !Rows.IsValidIndex(RowIndex))
	{
		return;
	}

	const FAedirMenuRow Row = Rows[RowIndex];
	AAedirCreature* Pawn = Creature.Get();
	UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
	if (!Interaction || !RowHasItem(Row))
	{
		return;
	}

	if (Row.bIsBackpack)
	{
		Interaction->DropFromBackpack(Row.Index);
	}
	else if (Row.bIsHand)
	{
		Interaction->DropHeld(static_cast<EAedirHand>(Row.Index));
	}
	else
	{
		Interaction->DropFromWaist(Row.Index);
	}
}

void SAedirMenu::HandleRowClick(int32 Index, const FGeometry& RowGeometry, const FPointerEvent& MouseEvent)
{
	CloseContext();

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		SelectedRow = Index;
		RefreshView(false);
		OpenContext(Index, MouseEvent.GetScreenSpacePosition());
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// A click selects a row, a second click on the selected row uses it.
		if (SelectedRow == Index)
		{
			ActivateSelected(true);
		}
		else
		{
			SelectedRow = Index;
			RefreshView(false);
		}
	}
}

void SAedirMenu::OpenContext(int32 Index, const FVector2D& ScreenPosition)
{
	if (CurrentCategory != static_cast<int32>(EAedirMenuCategory::Inventory) || !Rows.IsValidIndex(Index) || !RowHasItem(Rows[Index]))
	{
		return;
	}

	const FGeometry& MenuGeometry = GetCachedGeometry();
	const FVector2D Size = MenuGeometry.GetLocalSize();
	const FVector2D Local = MenuGeometry.AbsoluteToLocal(ScreenPosition);
	ContextPosition = FVector2D(
		FMath::Clamp(Local.X, 0.0, FMath::Max(0.0, Size.X - MenuContextWidth)),
		FMath::Clamp(Local.Y, 0.0, FMath::Max(0.0, Size.Y - MenuContextHeight)));
	ContextRow = Index;
}

void SAedirMenu::CloseContext()
{
	ContextRow = INDEX_NONE;
}

void SAedirMenu::RunContextAction()
{
	const int32 Target = ContextRow;
	CloseContext();
	if (!Rows.IsValidIndex(Target) || !RowHasItem(Rows[Target]))
	{
		return;
	}

	const FAedirMenuRow Row = Rows[Target];
	AAedirCreature* Pawn = Creature.Get();
	UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
	if (!Interaction)
	{
		return;
	}

	if (Row.bIsBackpack)
	{
		Interaction->DropFromBackpack(Row.Index);
	}
	else
	{
		Interaction->AddToBackpack(GetRowItem(Row));
	}
}

AAedirItemActor* SAedirMenu::GetRowItem(const FAedirMenuRow& Row) const
{
	return MenuFindRowItem(Creature.Get(), Row);
}

bool SAedirMenu::RowHasItem(const FAedirMenuRow& Row) const
{
	return IsValid(GetRowItem(Row));
}

int32 SAedirMenu::GetHoveredRow() const
{
	for (int32 Index = 0; Index < RowWidgets.Num(); ++Index)
	{
		if (RowWidgets[Index].IsValid() && RowWidgets[Index]->IsHovered())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FText SAedirMenu::GetContextText() const
{
	const bool bBackpack = Rows.IsValidIndex(ContextRow) && Rows[ContextRow].bIsBackpack;
	return FText::FromString(bBackpack ? TEXT("Drop") : TEXT("Stow in backpack"));
}

FText SAedirMenu::GetHintText() const
{
	const bool bInventory = CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory);
	return FText::FromString(bInventory ? MenuInventoryHintText : MenuListHintText);
}

EVisibility SAedirMenu::GetTabVisibility(int32 Index) const
{
	return (Index == 0 || CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory)) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SAedirMenu::ScrollToSelection()
{
	if (ListScroll.IsValid() && RowWidgets.IsValidIndex(SelectedRow))
	{
		ListScroll->ScrollDescendantIntoView(RowWidgets[SelectedRow], true);
	}
}

float SAedirMenu::GetSelectedSkillProgress() const
{
	if (CurrentCategory != static_cast<int32>(EAedirMenuCategory::Skills) || !Rows.IsValidIndex(SelectedRow))
	{
		return 0.f;
	}

	const AAedirCreature* Pawn = Creature.Get();
	const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
	if (!Stats)
	{
		return 0.f;
	}

	return UAedirJournalSubsystem::GetSkillRankProgress01(Stats->GetSkill(static_cast<EAedirSkill>(Rows[SelectedRow].Index)));
}

FText SAedirMenu::GetTabText(int32 Index) const
{
	FString Name;
	bool bActive = true;
	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory:
		Name = MenuInventoryTabNames[FMath::Clamp(Index, 0, 1)];
		bActive = Index == InventoryTab;
		break;
	case EAedirMenuCategory::Magic:
		Name = TEXT("MAGIC");
		break;
	case EAedirMenuCategory::Skills:
		Name = TEXT("SKILLS");
		break;
	default:
		break;
	}

	return FText::FromString(bActive ? FString::Printf(TEXT("[ %s ]"), *Name) : Name);
}

FLinearColor SAedirMenu::GetTabColor(int32 Index) const
{
	const bool bActive = CurrentCategory != static_cast<int32>(EAedirMenuCategory::Inventory) || Index == InventoryTab;
	return bActive ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f, 1.f);
}

FLinearColor SAedirMenu::GetRowBackground(int32 Index) const
{
	if (Index == SelectedRow)
	{
		return FLinearColor(0.92f, 0.92f, 0.92f, 0.18f);
	}
	return RowWidgets.IsValidIndex(Index) && RowWidgets[Index]->IsHovered() ? FLinearColor(0.92f, 0.92f, 0.92f, 0.08f) : FLinearColor(0.f, 0.f, 0.f, 0.f);
}

FLinearColor SAedirMenu::GetRowAccent(int32 Index) const
{
	if (Index == SelectedRow)
	{
		return FLinearColor(1.f, 1.f, 1.f, 0.9f);
	}
	return RowWidgets.IsValidIndex(Index) && RowWidgets[Index]->IsHovered() ? FLinearColor(1.f, 1.f, 1.f, 0.35f) : FLinearColor(1.f, 1.f, 1.f, 0.f);
}

FLinearColor SAedirMenu::GetRowTextColor(int32 Index) const
{
	if (!Rows.IsValidIndex(Index))
	{
		return MenuTextColor;
	}
	if (Rows[Index].bDimmed)
	{
		return MenuDimColor;
	}
	return Index == SelectedRow ? FLinearColor::White : MenuTextColor;
}
