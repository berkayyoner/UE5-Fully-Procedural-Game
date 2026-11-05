#include "UI/SAedirMenu.h"

#include "Combat/AedirCombatTypes.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "HUD/AedirMarkerSubsystem.h"
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
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const MenuCategoryNames[] = { TEXT("INVENTORY"), TEXT("MAGIC"), TEXT("SKILLS"), TEXT("MAP"), TEXT("JOURNAL") };
	const TCHAR* const MenuHintText = TEXT("[W/S] Select  [A/D] Category  [E] Use  [Q] Drop  [Tab] Close");

	constexpr float MenuMetersPerUnit = 0.01f;
	constexpr float MenuRadarMinRange = 1000.f;
	constexpr float MenuRadarPadding = 14.f;
	const FLinearColor MenuMissionColor(1.f, 0.78f, 0.2f, 1.f);
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

	const TCHAR* MenuMarkerTypeName(EAedirMarkerType Type)
	{
		switch (Type)
		{
		case EAedirMarkerType::Mission: return TEXT("Mission");
		case EAedirMarkerType::Area: return TEXT("Area");
		case EAedirMarkerType::Custom: return TEXT("Custom");
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

	float MenuDistanceMeters(const AActor* From, const FVector& To)
	{
		return static_cast<float>((To - From->GetActorLocation()).Size2D()) * MenuMetersPerUnit;
	}

	void MenuAddLine(TArray<FAedirMenuLine>& Lines, EAedirMenuLineKind Kind, const FString& Content)
	{
		FAedirMenuLine Line;
		Line.Kind = Kind;
		Line.Content = Content;
		Lines.Add(Line);
	}

	void MenuBuildInventoryDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
	{
		const UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
		const AAedirItemActor* Item = nullptr;
		if (Selected && Interaction)
		{
			if (Selected->bIsHand)
			{
				Item = Interaction->GetHeldItem(static_cast<EAedirHand>(Selected->Index));
			}
			else
			{
				const TArray<AAedirItemActor*> Waist = Interaction->GetWaistItemsCopy();
				Item = Waist.IsValidIndex(Selected->Index) ? Waist[Selected->Index] : nullptr;
			}
		}

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
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("Nothing in this slot."));
		}

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
		MenuAddLine(Lines, EAedirMenuLineKind::Value, FString::Printf(TEXT("%d"), FMath::FloorToInt(SkillValue)));
		MenuAddLine(Lines, EAedirMenuLineKind::Heading, FString::Printf(TEXT("Rank: %s"), *UAedirJournalSubsystem::GetSkillRankName(SkillValue).ToString()));
		MenuAddLine(Lines, EAedirMenuLineKind::Progress, FString());
		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());

		MenuAddLine(Lines, EAedirMenuLineKind::Heading, TEXT("Stats"));
		if (!Stats)
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No stat data."));
			return;
		}

		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Health: %d / %d"), FMath::RoundToInt(Stats->GetHealth()), FMath::RoundToInt(Stats->GetMaxHealth())));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Mana (Magicka): %d / %d"), FMath::RoundToInt(Stats->GetMana()), FMath::RoundToInt(Stats->GetMaxMana())));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Stamina: %d / %d"), FMath::RoundToInt(Stats->GetStamina()), FMath::RoundToInt(Stats->GetMaxStamina())));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Strength: %.0f"), Stats->Strength));
		MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Athletism: %.0f"), Stats->Athletism));
	}

	void MenuBuildMapDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
	{
		MenuAddLine(Lines, EAedirMenuLineKind::Radar, FString());
		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());

		UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
		const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
		if (!Selected || !Markers || !Markers->GetMarkers().IsValidIndex(Selected->Index))
		{
			MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("No marker selected."));
		}
		else
		{
			const FAedirMapMarker& Marker = Markers->GetMarkers()[Selected->Index];
			MenuAddLine(Lines, EAedirMenuLineKind::Title, Marker.Label.ToString());
			MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Type: %s"), MenuMarkerTypeName(Marker.Type)));
			if (Pawn)
			{
				MenuAddLine(Lines, EAedirMenuLineKind::Text, FString::Printf(TEXT("Distance: %.0f m"), MenuDistanceMeters(Pawn, Marker.Location)));
			}
		}

		MenuAddLine(Lines, EAedirMenuLineKind::Spacer, FString());
		MenuAddLine(Lines, EAedirMenuLineKind::Dim, TEXT("Amber diamond: mission. Square: area. Circle: custom."));
	}

	void MenuBuildJournalDetail(const AAedirCreature* Pawn, const FAedirMenuRow* Selected, TArray<FAedirMenuLine>& Lines)
	{
		UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
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

	TArray<FVector2f> MenuRadarPolygon(const FVector2f& Center, float Radius, int32 Sides, float StartDegrees)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Sides + 1);
		for (int32 Index = 0; Index <= Sides; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees + 360.f * Index / Sides);
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}
}

// Top-down radar: player at the centre, north up, markers scaled so the farthest one fits.
class SAedirMenuRadar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirMenuRadar)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<AAedirCreature>, Creature)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Creature = InArgs._Creature;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(200.f, 200.f);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const FVector2f Center = Size * 0.5f;
		const float Radius = FMath::Max(0.f, FMath::Min(Size.X, Size.Y) * 0.5f - MenuRadarPadding);

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry, WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.5f));

		const FLinearColor GridColor(1.f, 1.f, 1.f, 0.18f);
		for (const float Fraction : { -1.f, -0.5f, 0.f, 0.5f, 1.f })
		{
			const float Offset = Radius * Fraction;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry,
				TArray<FVector2f>{ FVector2f(Center.X + Offset, Center.Y - Radius), FVector2f(Center.X + Offset, Center.Y + Radius) },
				ESlateDrawEffect::None, GridColor, true, 1.f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry,
				TArray<FVector2f>{ FVector2f(Center.X - Radius, Center.Y + Offset), FVector2f(Center.X + Radius, Center.Y + Offset) },
				ESlateDrawEffect::None, GridColor, true, 1.f);
		}

		const FLinearColor BorderColor(1.f, 1.f, 1.f, 0.35f);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry,
			TArray<FVector2f>{
				FVector2f(Center.X - Radius, Center.Y - Radius),
				FVector2f(Center.X + Radius, Center.Y - Radius),
				FVector2f(Center.X + Radius, Center.Y + Radius),
				FVector2f(Center.X - Radius, Center.Y + Radius),
				FVector2f(Center.X - Radius, Center.Y - Radius) },
			ESlateDrawEffect::None, BorderColor, true, 1.f);

		const AAedirCreature* Pawn = Creature.Get();
		UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
		const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
		if (Pawn && Markers && Radius > 0.f)
		{
			const FVector PawnLocation = Pawn->GetActorLocation();
			const TArray<FAedirMapMarker>& Entries = Markers->GetMarkers();

			float MaxDistance = MenuRadarMinRange;
			for (const FAedirMapMarker& Marker : Entries)
			{
				MaxDistance = FMath::Max(MaxDistance, static_cast<float>((Marker.Location - PawnLocation).Size2D()));
			}
			const float Scale = Radius / MaxDistance;

			for (const FAedirMapMarker& Marker : Entries)
			{
				const FVector Delta = Marker.Location - PawnLocation;
				// Delta.X points north (screen up), Delta.Y points east (screen right).
				const FVector2f ScreenPos = Center + FVector2f(static_cast<float>(Delta.Y), static_cast<float>(-Delta.X)) * Scale;
				const FLinearColor MarkerColor = Marker.Type == EAedirMarkerType::Mission ? MenuMissionColor : Marker.Color;

				TArray<FVector2f> Shape;
				switch (Marker.Type)
				{
				case EAedirMarkerType::Mission:
					Shape = MenuRadarPolygon(ScreenPos, 6.f, 4, 90.f);
					break;
				case EAedirMarkerType::Area:
					Shape = MenuRadarPolygon(ScreenPos, 6.f, 4, 45.f);
					break;
				default:
					Shape = MenuRadarPolygon(ScreenPos, 5.f, 12, 0.f);
					break;
				}
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, Shape, ESlateDrawEffect::None, MarkerColor, true, 2.f);
			}
		}

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, MenuRadarPolygon(Center, 5.f, 4, 90.f), ESlateDrawEffect::None, FLinearColor::White, true, 2.f);
		return LayerId + 3;
	}

private:
	TWeakObjectPtr<AAedirCreature> Creature;
};

void SAedirMenu::Construct(const FArguments& InArgs)
{
	Creature = InArgs._Creature;
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	TSharedRef<SHorizontalBox> TabBar = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EAedirMenuCategory::Count); ++Index)
	{
		TabBar->AddSlot()
		.AutoWidth()
		.Padding(FMargin(14.f, 0.f))
		[
			SNew(STextBlock)
			.Text_Lambda([this, Index]() { return GetTabText(Index); })
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetTabColor(Index)); })
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
				.Text(FText::FromString(MenuHintText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
			]
		]
	];

	RefreshView(true);
}

void SAedirMenu::SetCategory(int32 NewCategory)
{
	const int32 Count = static_cast<int32>(EAedirMenuCategory::Count);
	CurrentCategory = ((NewCategory % Count) + Count) % Count;
	SelectedRow = 0;
	RefreshView(true);
}

FReply SAedirMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 Count = static_cast<int32>(EAedirMenuCategory::Count);

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
		SetCategory(CurrentCategory - 1 + Count);
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		SetCategory(CurrentCategory + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::E)
	{
		ActivateSelected();
		return FReply::Handled();
	}
	if (Key == EKeys::Q)
	{
		DropSelected();
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
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;

	switch (static_cast<EAedirMenuCategory>(CurrentCategory))
	{
	case EAedirMenuCategory::Inventory:
	{
		const UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
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
	case EAedirMenuCategory::Map:
	{
		const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
		if (Markers)
		{
			const TArray<FAedirMapMarker>& Entries = Markers->GetMarkers();
			for (int32 MarkerIndex = 0; MarkerIndex < Entries.Num(); ++MarkerIndex)
			{
				const FAedirMapMarker& Marker = Entries[MarkerIndex];
				FAedirMenuRow Row;
				Row.Name = Marker.Label.ToString();
				Row.Value = Pawn
					? FString::Printf(TEXT("%s  %.0f m"), MenuMarkerTypeName(Marker.Type), MenuDistanceMeters(Pawn, Marker.Location))
					: FString(MenuMarkerTypeName(Marker.Type));
				Row.Index = MarkerIndex;
				Result.Add(Row);
			}
		}
		break;
	}
	case EAedirMenuCategory::Journal:
	{
		const UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
		if (Journal)
		{
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
		MenuBuildInventoryDetail(Pawn, Selected, Result);
		break;
	case EAedirMenuCategory::Magic:
		MenuBuildMagicDetail(Pawn, Selected, Result);
		break;
	case EAedirMenuCategory::Skills:
		MenuBuildSkillsDetail(Pawn, Selected, Result);
		break;
	case EAedirMenuCategory::Map:
		MenuBuildMapDetail(Pawn, Selected, Result);
		break;
	case EAedirMenuCategory::Journal:
		MenuBuildJournalDetail(Pawn, Selected, Result);
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

	DetailBox->ClearChildren();

	for (const FAedirMenuLine& Line : Lines)
	{
		switch (Line.Kind)
		{
		case EAedirMenuLineKind::Title:
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
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
			DetailBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f))
			[
				SNew(SBox)
				.HeightOverride(8.f)
				[
					SNew(SProgressBar)
					.Percent_Lambda([this]() -> TOptional<float> { return GetSelectedSkillProgress(); })
					.FillColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f, 1.f))
				]
			];
			break;
		case EAedirMenuLineKind::Radar:
			DetailBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 8.f))
			[
				SNew(SBox)
				.HeightOverride(280.f)
				[
					SNew(SAedirMenuRadar)
					.Creature(Creature)
				]
			];
			break;
		case EAedirMenuLineKind::Spacer:
			DetailBox->AddSlot()
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

void SAedirMenu::ActivateSelected()
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

		if (Row.bIsHand)
		{
			const EAedirHand Hand = static_cast<EAedirHand>(Row.Index);
			if (Interaction->GetHeldItem(Hand))
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
	case EAedirMenuCategory::Journal:
	{
		UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
		UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
		if (Journal)
		{
			Journal->SetActiveQuest(Row.QuestId);
		}
		break;
	}
	default:
		break;
	}
}

void SAedirMenu::DropSelected()
{
	if (CurrentCategory != static_cast<int32>(EAedirMenuCategory::Inventory) || !Rows.IsValidIndex(SelectedRow))
	{
		return;
	}

	const FAedirMenuRow Row = Rows[SelectedRow];
	AAedirCreature* Pawn = Creature.Get();
	UAedirInteractionComponent* Interaction = Pawn ? Pawn->Interaction.Get() : nullptr;
	if (Interaction && Row.bIsHand)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Row.Index);
		if (Interaction->GetHeldItem(Hand))
		{
			Interaction->DropHeld(Hand);
		}
	}
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
	const FString Name = MenuCategoryNames[Index];
	return FText::FromString(Index == CurrentCategory ? FString::Printf(TEXT("[ %s ]"), *Name) : Name);
}

FLinearColor SAedirMenu::GetTabColor(int32 Index) const
{
	return Index == CurrentCategory ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f, 1.f);
}

FLinearColor SAedirMenu::GetRowBackground(int32 Index) const
{
	return Index == SelectedRow ? FLinearColor(0.92f, 0.92f, 0.92f, 0.18f) : FLinearColor(0.f, 0.f, 0.f, 0.f);
}

FLinearColor SAedirMenu::GetRowAccent(int32 Index) const
{
	return Index == SelectedRow ? FLinearColor(1.f, 1.f, 1.f, 0.9f) : FLinearColor(1.f, 1.f, 1.f, 0.f);
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
