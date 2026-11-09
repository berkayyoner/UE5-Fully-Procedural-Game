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
#include "UI/AedirItemPreview.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const MenuInventoryTabNames[] = { TEXT("EQUIPPED"), TEXT("BACKPACK") };
	const TCHAR* const MenuInventoryHintText = TEXT("[W/S] SELECT     [A/D] TAB     [LMB] SELECT, AGAIN TO USE     [RMB] ACTIONS     BELT: [LMB] RIGHT HAND  [RMB] LEFT HAND  [B] BACKPACK     [E] USE     [Q] DROP     [TAB] CLOSE");
	const TCHAR* const MenuListHintText = TEXT("[W/S] SELECT     [TAB] CLOSE");
	constexpr float MenuContextWidth = 210.f;
	constexpr float MenuContextHeight = 34.f;
	constexpr float MenuListFraction = AedirUIStyle::ListSeparatorFraction;
	constexpr float MenuTopMargin = 64.f;
	constexpr float MenuBottomMargin = 112.f;
	constexpr float MenuListLeftMargin = 96.f;
	constexpr float MenuDetailWidth = 660.f;
	constexpr float MenuRowFontSize = 15.f;
	constexpr float MenuHintFontSize = 10.f;
	constexpr float MenuStatusFontSize = 10.5f;
	constexpr float MenuTitleFontSize = 26.f;
	constexpr float MenuDetailTitleFontSize = 30.f;
	constexpr float MenuProgressWidth = 360.f;
	constexpr float MenuProgressHeight = 12.f;
	// The turning model of an item stands in the middle of the right side, above the item card.
	constexpr float MenuPreviewSize = 560.f;
	constexpr float MenuPreviewTop = 84.f;

	FSlateFontInfo MenuLabelFont(float Size)
	{
		return AedirUIStyle::RegularFont(Size, 140);
	}

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

	FString MenuWornPlaceName(EAedirBodyZone Zone)
	{
		switch (Zone)
		{
		case EAedirBodyZone::Head: return TEXT("Worn on the head");
		case EAedirBodyZone::Torso: return TEXT("Worn on the body");
		case EAedirBodyZone::Arm: return TEXT("Worn on the arms");
		case EAedirBodyZone::Leg: return TEXT("Worn on the legs");
		default: return TEXT("Worn");
		}
	}

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

		const TArray<AAedirItemActor*> Items = Row.bIsWorn ? Interaction->GetWornItemsCopy() : Row.bIsBackpack ? Interaction->GetBackpackItemsCopy() : Interaction->GetWaistItemsCopy();
		return Items.IsValidIndex(Row.Index) ? Items[Row.Index] : nullptr;
	}

	void MenuBuildInventoryDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, bool bBackpackTab, TArray<FAedirMenuLine>& Lines)
	{
		const AAedirItemActor* Item = Selected ? MenuFindRowItem(Pawn, *Selected) : nullptr;

		if (!IsValid(Item))
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, bBackpackTab ? TEXT("The backpack is empty.") : TEXT("Nothing in this slot."));
			return;
		}

		const FString Length = Item->GetWeaponLength() > 0.f ? FString::Printf(TEXT("%.0f cm"), Item->GetWeaponLength()) : FString(TEXT("Short"));
		MenuAddLine(Lines, EAedirMenuLineKind::Title, Item->ItemName.ToString());
		MenuAddLine(Lines, EAedirMenuLineKind::Rule, FString());
		AedirMenuContent::AddPairLine(Lines, { TEXT("WEIGHT"), UAedirJournalSubsystem::FormatWeight(Item->Weight), TEXT("DAMAGE"), FString::Printf(TEXT("%.0f"), Item->GetDamage()), TEXT("LENGTH"), Length });
		AedirMenuContent::AddPairLine(Lines, { TEXT("CLASS"), MenuItemClassName(Item->ItemClass), TEXT("TYPE"), MenuDamageTypeName(Item->GetDamageType()) });
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
		MenuAddLine(Lines, EAedirMenuLineKind::Rule, FString());
		AedirMenuContent::AddPairLine(Lines, { TEXT("SKILL"), FString::Printf(TEXT("%d"), FMath::FloorToInt(SkillValue)), TEXT("RANK"), UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString() });
		MenuAddLine(Lines, EAedirMenuLineKind::Body, MenuSchoolDescription(School));
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

		MenuAddLine(Lines, EAedirMenuLineKind::Title, Selected->Name);
		MenuAddLine(Lines, EAedirMenuLineKind::Rule, FString());
		MenuAddLine(Lines, EAedirMenuLineKind::Value, FString::Printf(TEXT("%d"), FMath::FloorToInt(SkillValue)));
		MenuAddLine(Lines, EAedirMenuLineKind::Heading, UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString().ToUpper());
		MenuAddLine(Lines, EAedirMenuLineKind::Progress, FString());
	}
}

void AedirMenuContent::AddPairLine(TArray<FAedirMenuLine>& Lines, TConstArrayView<FString> LabelsAndValues)
{
	MenuAddLine(Lines, EAedirMenuLineKind::Pair, FString::Join(LabelsAndValues, TEXT("\t")));
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
	MenuAddLine(Lines, EAedirMenuLineKind::Rule, FString());
	MenuAddLine(Lines, EAedirMenuLineKind::Body, Quest->Description.ToString());
	MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
	MenuAddLine(Lines, EAedirMenuLineKind::Heading, TEXT("OBJECTIVES"));
	for (const FAedirQuestObjective& Objective : Quest->Objectives)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("%s  %s"), Objective.bDone ? TEXT("\u25C6") : TEXT("\u25C7"), *Objective.Text.ToString()));
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
		MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("[E] TRACK QUEST"));
	}
}


void AedirMenuContent::PopulateDetail(SVerticalBox& Box, const TArray<FAedirMenuLine>& NewLines, const TFunction<TOptional<float>()>& Progress)
{
	using namespace AedirUIStyle;

	Box.ClearChildren();

	const auto AddText = [&Box](const FString& Content, const FSlateFontInfo& Font, const FLinearColor& Color, const FMargin& Padding)
	{
		Box.AddSlot()
		.AutoHeight()
		.Padding(Padding)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Content))
			.Font(Font)
			.ColorAndOpacity(Color)
			.Justification(ETextJustify::Center)
			.AutoWrapText(true)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
		];
	};

	for (const FAedirMenuLine& Line : NewLines)
	{
		switch (Line.Kind)
		{
		case EAedirMenuLineKind::Title:
			AddText(Line.Content, LightFont(MenuDetailTitleFontSize, 30), IvoryBright, FMargin(0.f, 0.f, 0.f, 2.f));
			break;
		case EAedirMenuLineKind::Value:
			AddText(Line.Content, LightFont(44.f), IvoryBright, FMargin(0.f, 0.f, 0.f, 0.f));
			break;
		case EAedirMenuLineKind::Heading:
			AddText(Line.Content, MenuLabelFont(11.f), Metal, FMargin(0.f, 4.f, 0.f, 6.f));
			break;
		case EAedirMenuLineKind::Text:
			AddText(Line.Content, RegularFont(13.f), Ivory, FMargin(0.f, 3.f));
			break;
		case EAedirMenuLineKind::Body:
			AddText(Line.Content, LightFont(14.f), Ivory, FMargin(20.f, 8.f));
			break;
		case EAedirMenuLineKind::Dim:
			AddText(Line.Content, RegularFont(11.f, 40), TextDim, FMargin(0.f, 3.f));
			break;
		case EAedirMenuLineKind::Rule:
			Box.AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 4.f, 0.f, 10.f))
			[
				SNew(SAedirUIPaint)
				.DesiredSize(FVector2D(MenuDetailWidth, 12.f))
				.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
				{
					AddOrnamentRule(Mesh, Size * 0.5f, FMath::Min(Size.X * 0.5f, 300.f), Metal);
				})
			];
			break;
		case EAedirMenuLineKind::Pair:
		{
			TArray<FString> Parts;
			Line.Content.ParseIntoArray(Parts, TEXT("\t"), false);
			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			for (int32 Index = 0; Index + 1 < Parts.Num(); Index += 2)
			{
				Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(Index == 0 ? 0.f : 34.f, 0.f, 9.f, 0.f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Parts[Index]))
					.Font(MenuLabelFont(10.f))
					.ColorAndOpacity(TextDim)
				];
				Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Parts[Index + 1]))
					.Font(RegularFont(14.f))
					.ColorAndOpacity(Ivory)
					.ShadowOffset(FVector2D(1.f, 1.f))
					.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
				];
			}
			Box.AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 3.f))
			[
				Row
			];
			break;
		}
		case EAedirMenuLineKind::Progress:
			Box.AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 4.f))
			[
				SNew(SAedirUIPaint)
				.DesiredSize(FVector2D(MenuProgressWidth, MenuProgressHeight))
				.Painter([Progress](const FVector2f& Size, FAedirUIMesh& Mesh)
				{
					const FVector2f Center = Size * 0.5f;
					const float HalfWidth = Size.X * 0.5f;
					const float Fraction = FMath::Clamp(Progress().Get(0.f), 0.f, 1.f);
					AddTaperedBar(Mesh, Center, HalfWidth, Size.Y * 0.4f, 12.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), FLinearColor(0.f, 0.f, 0.f, 0.72f), FLinearColor(0.f, 0.f, 0.f, 0.5f));
					AddTaperedBar(Mesh, Center, (HalfWidth - 3.f) * Fraction, Size.Y * 0.28f, 8.f, WithAlpha(Metal, 0.5f), Ivory, WithAlpha(Metal, 0.4f));
					AddTaperedFrame(Mesh, Center, HalfWidth, Size.Y * 0.4f, 12.f, 1.f, WithAlpha(Metal, 0.8f));
				})
			];
			break;
		case EAedirMenuLineKind::Spacer:
			Box.AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(12.f)
			];
			break;
		default:
			break;
		}
	}
}

void AedirMenuContent::PaintListBackdrop(const FVector2f& Size, FAedirUIMesh& Mesh, float VignetteStrength)
{
	using namespace AedirUIStyle;

	AddVignette(Mesh, Size, 0.24f * VignetteStrength, 0.86f * VignetteStrength);

	const float SeparatorX = Size.X * MenuListFraction;
	AddGradientPanel(Mesh, FVector2f::ZeroVector, FVector2f(Size.X * 0.52f, Size.Y), Size.X * 0.26f, FLinearColor(0.f, 0.f, 0.f, 0.86f));

	// Darker floor behind the item card and the key hints.
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);
	const FLinearColor Floor(0.f, 0.f, 0.f, 0.72f);
	Mesh.AddGradientRect(FVector2f(0.f, Size.Y * 0.56f), FVector2f(Size.X, Size.Y), Clear, Clear, Floor, Floor);
	const float DetailCenterX = (SeparatorX + Size.X) * 0.5f;
	AddGlow(Mesh, FVector2f(DetailCenterX, Size.Y - MenuBottomMargin - 90.f), FVector2f(620.f, 320.f), FLinearColor(0.f, 0.f, 0.f, 0.62f));

	AddDoubleLine(Mesh, SeparatorX, MenuTopMargin - 18.f, Size.Y - MenuBottomMargin + 6.f, Metal);
	AddOrnamentRule(Mesh, FVector2f(Size.X * 0.5f, Size.Y - MenuBottomMargin + 28.f), Size.X * 0.46f, WithAlpha(Metal, 0.85f));
}

TSharedRef<SWidget> AedirMenuContent::MakeListRow(const FAedirMenuRow& Row, const FSlateFontInfo& Font, TFunction<FLinearColor()> TextColor, TFunction<float()> MarkerStrength, TSharedPtr<SWidget> ValueWidget)
{
	using namespace AedirUIStyle;

	TSharedRef<SVerticalBox> NameBox = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.Name))
			.Font(Font)
			.ColorAndOpacity_Lambda([TextColor]() { return FSlateColor(TextColor()); })
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f))
		];

	if (!Row.SubText.IsEmpty())
	{
		NameBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row.SubText))
			.Font(RegularFont(10.f, 30))
			.ColorAndOpacity(TextDim)
		];
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			SNew(SAedirUIPaint)
			.DesiredSize(FVector2D(22.f, 16.f))
			.Painter([MarkerStrength](const FVector2f& Size, FAedirUIMesh& Mesh)
			{
				const float Strength = MarkerStrength();
				if (Strength > 0.f)
				{
					const FVector2f Spot(Size.X - 7.f, Size.Y * 0.5f);
					const FLinearColor Color = WithAlpha(IvoryBright, Strength);
					AddDiamond(Mesh, Spot, 4.5f, 4.5f, Color);
					AddFadedLine(Mesh, FVector2f(0.f, Spot.Y), Spot - FVector2f(5.f, 0.f), 1.f, Color, 1.f, 0.f);
				}
			})
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 5.f))
		[
			NameBox
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(14.f, 5.f, 6.f, 5.f))
		[
			ValueWidget.IsValid() ? ValueWidget.ToSharedRef() : StaticCastSharedRef<SWidget>(SNew(STextBlock)
			.Text(FText::FromString(Row.Value))
			.Font(LightFont(FMath::Max(12.f, Font.Size - 2.f)))
			.ColorAndOpacity_Lambda([TextColor]() { return FSlateColor(WithAlpha(TextColor(), 0.78f)); })
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f)))
		];
}

TSharedRef<SWidget> AedirMenuContent::MakeListScreen(const TSharedRef<SWidget>& Header, const TSharedRef<SWidget>& List, const TSharedRef<SWidget>& Detail, const TAttribute<FText>& Status, const TAttribute<FText>& Hint, float VignetteStrength)
{
	using namespace AedirUIStyle;

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SAedirUIPaint)
			.Painter([VignetteStrength](const FVector2f& Size, FAedirUIMesh& Mesh)
			{
				PaintListBackdrop(Size, Mesh, VignetteStrength);
			})
		]
		+ SOverlay::Slot()
		.Padding(FMargin(0.f, MenuTopMargin, 0.f, MenuBottomMargin))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(MenuListFraction)
			[
				// The margins sit inside the slot: on the slot itself they would shift the column off the double line.
				SNew(SBox)
				.Padding(FMargin(MenuListLeftMargin, 0.f, 30.f, 0.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Header
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 12.f))
					[
						SNew(SAedirUIPaint)
						.DesiredSize(FVector2D(100.f, 3.f))
						.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
						{
							AddFadedLine(Mesh, FVector2f(0.f, Size.Y * 0.5f), FVector2f(Size.X, Size.Y * 0.5f), 1.f, Metal, 0.f, 0.6f);
						})
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						List
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f - MenuListFraction)
			.VAlign(VAlign_Bottom)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(MenuDetailWidth)
				.Padding(FMargin(0.f, 0.f, 0.f, 14.f))
				[
					Detail
				]
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.HAlign(HAlign_Center)
		.Padding(FMargin(40.f, 0.f, 40.f, 20.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
			[
				SNew(STextBlock)
				.Text(Status)
				.Font(MenuLabelFont(MenuStatusFontSize))
				.ColorAndOpacity(Ivory)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Hint)
				.Font(RegularFont(MenuHintFontSize, 60))
				.ColorAndOpacity(TextDim)
			]
		];
}

TSharedRef<SWidget> AedirMenuContent::MakeTitle(const TAttribute<FText>& Title)
{
	return SNew(STextBlock)
		.Text(Title)
		.Font(AedirUIStyle::TitleFont(MenuTitleFontSize))
		.ColorAndOpacity(AedirUIStyle::IvoryBright)
		.ShadowOffset(FVector2D(1.f, 1.f))
		.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
}

void SAedirMenu::Construct(const FArguments& InArgs)
{
	Creature = InArgs._Creature;
	CloseDelegate = InArgs._OnClose;

	TSharedRef<SHorizontalBox> TabBar = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EAedirInventoryTab::Count); ++Index)
	{
		TabBar->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.f, 0.f, 26.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(FMargin(0.f, 2.f))
			.Visibility_Lambda([this, Index]() { return GetTabVisibility(Index); })
			.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
			{
				SetInventoryTab(Index);
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Index]() { return GetTabText(Index); })
				.Font(MenuLabelFont(11.f))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetTabColor(Index)); })
			]
		];
	}

	const TSharedRef<SWidget> Header = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			AedirMenuContent::MakeTitle(TAttribute<FText>::CreateLambda([this]() { return GetTitleText(); }))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			TabBar
		];

	const TSharedRef<SWidget> List = SAssignNew(ListScroll, SScrollBox)
		.ScrollBarVisibility(EVisibility::Collapsed)
		+ SScrollBox::Slot()
		[
			SAssignNew(ListBox, SVerticalBox)
		];

	const TSharedRef<SWidget> Detail = SAssignNew(DetailBox, SVerticalBox);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			AedirMenuContent::MakeListScreen(Header, List, Detail,
				TAttribute<FText>::CreateLambda([this]() { return GetStatusText(); }),
				TAttribute<FText>::CreateLambda([this]() { return GetHintText(); }),
				1.f)
		]
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SHorizontalBox::Slot()
			.FillWidth(MenuListFraction)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f - MenuListFraction)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(FMargin(0.f, MenuPreviewTop, 0.f, 0.f))
			[
				SAssignNew(PreviewView, SAedirItemPreviewView)
				.DesiredSize(FVector2D(MenuPreviewSize, MenuPreviewSize))
				.Cursor(EMouseCursor::GrabHand)
				.Visibility_Lambda([this]() { return GetPreviewVisibility(); })
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
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor_Lambda([this]()
				{
					const bool bHovered = ContextButton.IsValid() && ContextButton->IsHovered();
					return FSlateColor(bHovered ? AedirUIStyle::PanelSelected : FLinearColor(0.f, 0.f, 0.f, 0.94f));
				})
				.Visibility_Lambda([this]() { return ContextRow != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
				.Padding(FMargin(12.f, 6.f))
				.VAlign(VAlign_Center)
				.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
				{
					RunContextAction();
					return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return GetContextText(); })
					.Font(AedirUIStyle::RegularFont(12.f))
					.ColorAndOpacity(AedirUIStyle::Ivory)
				]
			]
		]
	];

	RefreshView(true);
}

SAedirMenu::~SAedirMenu()
{
	AAedirItemPreview* Studio = Preview.Get();
	const UWorld* World = Studio ? Studio->GetWorld() : nullptr;
	if (IsValid(Studio) && World && !World->bIsTearingDown)
	{
		Studio->Destroy();
	}
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
	if (Key == EKeys::B)
	{
		const int32 Hovered = GetHoveredRow();
		const int32 RowIndex = Hovered != INDEX_NONE ? Hovered : SelectedRow;
		const AAedirCreature* Pawn = Creature.Get();
		if (CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory) && Rows.IsValidIndex(RowIndex) && !Rows[RowIndex].bIsBackpack && RowHasItem(Rows[RowIndex]) && Pawn && Pawn->Interaction)
		{
			if (Rows[RowIndex].bIsWorn)
			{
				Pawn->Interaction->TakeOffWorn(Rows[RowIndex].Index);
			}
			else
			{
				Pawn->Interaction->AddToBackpack(GetRowItem(Rows[RowIndex]));
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAedirMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (PendingPackIndex != INDEX_NONE)
	{
		const int32 PackIndex = PendingPackIndex;
		PendingPackIndex = INDEX_NONE;
		const AAedirCreature* Pawn = Creature.Get();
		CloseDelegate.ExecuteIfBound();
		if (Pawn && Pawn->Interaction)
		{
			Pawn->Interaction->DrawFromBackpack(PackIndex, PendingBeltHand);
		}
		return;
	}

	if (PendingBeltSlot != INDEX_NONE)
	{
		const int32 Slot = PendingBeltSlot;
		PendingBeltSlot = INDEX_NONE;
		const AAedirCreature* Pawn = Creature.Get();
		CloseDelegate.ExecuteIfBound();
		if (Pawn && Pawn->Interaction)
		{
			Pawn->Interaction->DrawWaistSlot(PendingBeltHand, Slot);
		}
		return;
	}

	RefreshView(false);
	UpdatePreview();
}

void SAedirMenu::UpdatePreview()
{
	AAedirItemActor* Wanted = nullptr;
	if (CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory))
	{
		// The row under the mouse shows its item; while the model is being turned or the mouse rests on it, it stays; otherwise the
		// selected row's item shows, so the keyboard sees it too.
		const int32 Hovered = GetHoveredRow();
		AAedirItemPreview* Shown = Preview.Get();
		const bool bKeep = PreviewView.IsValid() && (PreviewView->IsTurning() || PreviewView->IsHovered()) && Shown && Shown->HasModel();
		if (Rows.IsValidIndex(Hovered))
		{
			Wanted = GetRowItem(Rows[Hovered]);
		}
		else if (bKeep)
		{
			Shown->Capture();
			return;
		}
		else if (Rows.IsValidIndex(SelectedRow))
		{
			Wanted = GetRowItem(Rows[SelectedRow]);
		}
	}

	AAedirItemPreview* Studio = Preview.Get();
	if (!IsValid(Wanted))
	{
		if (Studio)
		{
			Studio->ShowItem(nullptr);
		}
		return;
	}

	if (!Studio)
	{
		UWorld* World = Creature.IsValid() ? Creature->GetWorld() : nullptr;
		if (!World)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Studio = World->SpawnActor<AAedirItemPreview>(AAedirItemPreview::GetStudioLocation(), FRotator::ZeroRotator, Params);
		Preview = Studio;
		if (!Studio)
		{
			return;
		}
		if (PreviewView.IsValid())
		{
			PreviewView->SetPreview(Studio);
		}
	}

	Studio->ShowItem(Wanted);
	Studio->Capture();
}

EVisibility SAedirMenu::GetPreviewVisibility() const
{
	const AAedirItemPreview* Studio = Preview.Get();
	const bool bShows = CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory) && Studio && Studio->HasModel();
	return bShows ? EVisibility::Visible : EVisibility::Collapsed;
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

		// What is worn on the body, each piece by where it sits; using one takes it off into the backpack.
		const TArray<AAedirItemActor*> Worn = Interaction ? Interaction->GetWornItemsCopy() : TArray<AAedirItemActor*>();
		for (int32 WornIndex = 0; WornIndex < Worn.Num(); ++WornIndex)
		{
			if (!IsValid(Worn[WornIndex]))
			{
				continue;
			}
			FAedirMenuRow Row;
			Row.Name = MenuWornPlaceName(Worn[WornIndex]->ArmorZone);
			Row.Value = Worn[WornIndex]->ItemName.ToString();
			Row.bIsWorn = true;
			Row.Index = WornIndex;
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

	const FSlateFontInfo RowFont = AedirUIStyle::LightFont(MenuRowFontSize, 20);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		TSharedRef<SBorder> RowBorder = SNew(SBorder)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.Padding(FMargin(0.f))
		.OnMouseButtonDown_Lambda([this, Index](const FGeometry& RowGeometry, const FPointerEvent& MouseEvent)
		{
			HandleRowClick(Index, RowGeometry, MouseEvent);
			return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
		})
		[
			AedirMenuContent::MakeListRow(Rows[Index], RowFont,
				[this, Index]() { return GetRowTextColor(Index); },
				[this, Index]() { return GetRowMarker(Index); })
		];

		ListBox->AddSlot()
		.AutoHeight()
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

		if (Row.bIsWorn)
		{
			Interaction->TakeOffWorn(Row.Index);
		}
		else if (Row.bIsBackpack)
		{
			// Stowed items are taken in a free hand, the right one first.
			const EAedirHand Hand = Interaction->GetHandState(EAedirHand::Right) == EAedirHandState::Idle ? EAedirHand::Right : EAedirHand::Left;
			if (Interaction->GetHandState(Hand) == EAedirHandState::Idle)
			{
				PendingBeltHand = Hand;
				PendingPackIndex = Row.Index;
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
			// From the keyboard a belt item goes to a free hand, the one that wears items on this hip first.
			const EAedirHand Near = Row.Index < 3 ? EAedirHand::Right : EAedirHand::Left;
			const EAedirHand Far = Near == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
			RequestBeltDraw(Interaction->CanDrawWaistSlot(Near, Row.Index) ? Near : Far, Row.Index);
		}
		break;
	}
	default:
		break;
	}
}

void SAedirMenu::RequestBeltDraw(EAedirHand Hand, int32 Slot)
{
	const AAedirCreature* Pawn = Creature.Get();
	if (Pawn && Pawn->Interaction && Pawn->Interaction->CanDrawWaistSlot(Hand, Slot))
	{
		PendingBeltHand = Hand;
		PendingBeltSlot = Slot;
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

	if (Row.bIsWorn)
	{
		Interaction->TakeOffWorn(Row.Index);
	}
	else if (Row.bIsBackpack)
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

	// A belt item goes straight to the hand of the button: the left button is the right hand, as everywhere in the game.
	const bool bInventory = CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory);
	if (bInventory && Rows.IsValidIndex(Index) && !Rows[Index].bIsBackpack && !Rows[Index].bIsHand && !Rows[Index].bIsWorn && RowHasItem(Rows[Index]))
	{
		const AAedirCreature* Pawn = Creature.Get();
		const FKey Button = MouseEvent.GetEffectingButton();
		if (Pawn && Pawn->Interaction && (Button == EKeys::LeftMouseButton || Button == EKeys::RightMouseButton))
		{
			SelectedRow = Index;
			RequestBeltDraw(Button == EKeys::LeftMouseButton ? EAedirHand::Right : EAedirHand::Left, Rows[Index].Index);
			RefreshView(false);
			return;
		}
	}

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

	if (Row.bIsWorn)
	{
		Interaction->TakeOffWorn(Row.Index);
	}
	else if (Row.bIsBackpack)
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
	const bool bWorn = Rows.IsValidIndex(ContextRow) && Rows[ContextRow].bIsWorn;
	return FText::FromString(bWorn ? TEXT("Take off") : bBackpack ? TEXT("Drop") : TEXT("Stow in backpack"));
}

FText SAedirMenu::GetHintText() const
{
	const bool bInventory = CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory);
	return FText::FromString(bInventory ? MenuInventoryHintText : MenuListHintText);
}

EVisibility SAedirMenu::GetTabVisibility(int32 Index) const
{
	return CurrentCategory == static_cast<int32>(EAedirMenuCategory::Inventory) ? EVisibility::Visible : EVisibility::Collapsed;
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

FText SAedirMenu::GetTitleText() const
{
	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory: return FText::FromString(TEXT("INVENTORY"));
	case EAedirMenuCategory::Magic: return FText::FromString(TEXT("MAGIC"));
	case EAedirMenuCategory::Skills: return FText::FromString(TEXT("SKILLS"));
	default: break;
	}
	return FText::GetEmpty();
}

FText SAedirMenu::GetStatusText() const
{
	const AAedirCreature* Pawn = Creature.Get();
	const UAedirStatsComponent* Stats = Pawn ? Pawn->Stats.Get() : nullptr;
	if (!Stats)
	{
		return FText::GetEmpty();
	}

	return FText::FromString(FString::Printf(TEXT("LEVEL %d     HEALTH %d/%d     MAGICKA %d/%d     STAMINA %d/%d     STRENGTH %.0f     ATHLETISM %.0f     CARRY WEIGHT %s/%.0f"),
		UAedirJournalSubsystem::GetCharacterLevel(Stats),
		FMath::RoundToInt(Stats->GetHealth()), FMath::RoundToInt(Stats->GetMaxHealth()),
		FMath::RoundToInt(Stats->GetMana()), FMath::RoundToInt(Stats->GetMaxMana()),
		FMath::RoundToInt(Stats->GetStamina()), FMath::RoundToInt(Stats->GetMaxStamina()),
		Stats->Strength, Stats->Athletism,
		*UAedirJournalSubsystem::FormatWeight(Pawn->GetCarriedWeight()), Pawn->GetCarryCapacity()));
}

FText SAedirMenu::GetTabText(int32 Index) const
{
	return FText::FromString(MenuInventoryTabNames[FMath::Clamp(Index, 0, 1)]);
}

FLinearColor SAedirMenu::GetTabColor(int32 Index) const
{
	return Index == InventoryTab ? AedirUIStyle::IvoryBright : AedirUIStyle::TextDim;
}

float SAedirMenu::GetRowMarker(int32 Index) const
{
	if (Index == SelectedRow)
	{
		return 1.f;
	}
	return RowWidgets.IsValidIndex(Index) && RowWidgets[Index]->IsHovered() ? 0.4f : 0.f;
}

FLinearColor SAedirMenu::GetRowTextColor(int32 Index) const
{
	const bool bSelected = Index == SelectedRow;
	if (Rows.IsValidIndex(Index) && Rows[Index].bDimmed)
	{
		return bSelected ? AedirUIStyle::Ivory : AedirUIStyle::TextDim;
	}
	return bSelected ? AedirUIStyle::IvoryBright : AedirUIStyle::WithAlpha(AedirUIStyle::Ivory, 0.82f);
}
