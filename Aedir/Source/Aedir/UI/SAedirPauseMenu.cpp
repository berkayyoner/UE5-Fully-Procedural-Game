#include "UI/SAedirPauseMenu.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Graphics/AedirUpscalerSubsystem.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/AedirSaveSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Travel/AedirTravelSubsystem.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const PauseTabNames[] = { TEXT("OPTIONS"), TEXT("JOURNAL") };
	const TCHAR* const PauseHintText = TEXT("[W/S] Select  [A/D] Tab  [Enter] Confirm  [Esc] Close");
	const TCHAR* const PauseQuickSaveSlot = TEXT("QuickSave");
	const TCHAR* const PauseSaveSlot = TEXT("Save1");
	const TCHAR* const PauseQualityNames[] = { TEXT("Native"), TEXT("Quality"), TEXT("Balanced"), TEXT("Performance"), TEXT("Ultra Performance") };

	enum class EPauseAction : int32
	{
		QuickSave,
		Save,
		Load,
		Settings,
		Quit,
		Upscaler,
		Quality,
		Back,
		QuitYes,
		QuitNo
	};

	void PauseAddLine(TArray<FAedirMenuLine>& Lines, EAedirMenuLineKind Kind, const FString& Content)
	{
		FAedirMenuLine Line;
		Line.Kind = Kind;
		Line.Content = Content;
		Lines.Add(Line);
	}

	FAedirMenuRow PauseMakeRow(const TCHAR* Name, EPauseAction Action, const FString& Value = FString())
	{
		FAedirMenuRow Row;
		Row.Name = Name;
		Row.Value = Value;
		Row.Index = static_cast<int32>(Action);
		return Row;
	}

	UAedirUpscalerSubsystem* PauseGetUpscaler(const UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UAedirUpscalerSubsystem>() : nullptr;
	}

	UAedirSaveSubsystem* PauseGetSaves(const UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UAedirSaveSubsystem>() : nullptr;
	}

	FString PauseSaveInfo(const UAedirSaveSubsystem* Saves, const TCHAR* Slot)
	{
		if (!Saves || !Saves->DoesSaveExist(Slot))
		{
			return TEXT("empty");
		}
		return Saves->GetSaveTimestamp(Slot).ToString(TEXT("%Y-%m-%d %H:%M"));
	}
}

void SAedirPauseMenu::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	CloseDelegate = InArgs._OnClose;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	TSharedRef<SHorizontalBox> TabBar = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EAedirPauseTab::Count); ++Index)
	{
		TabBar->AddSlot()
		.AutoWidth()
		.Padding(FMargin(22.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(FMargin(2.f))
			.Cursor(EMouseCursor::Hand)
			.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
			{
				SetTab(Index);
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this, Index]()
				{
					const FString Name = PauseTabNames[Index];
					return FText::FromString(Index == CurrentTab ? FString::Printf(TEXT("[ %s ]"), *Name) : Name);
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(Index == CurrentTab ? AedirUIStyle::Accent : AedirUIStyle::TextDim); })
			]
		];
	}

	ChildSlot
	.Padding(FMargin(96.f, 64.f))
	[
		SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor(AedirUIStyle::Panel)
		.Padding(FMargin(28.f, 20.f))
		[
			SNew(SVerticalBox)
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
					.BorderBackgroundColor(AedirUIStyle::Line)
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
						.BorderBackgroundColor(AedirUIStyle::Line)
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
				.Text(FText::FromString(PauseHintText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(AedirUIStyle::TextDim)
			]
		]
	];

	RefreshView(true);
}

void SAedirPauseMenu::SetTab(int32 NewTab)
{
	const int32 Count = static_cast<int32>(EAedirPauseTab::Count);
	CurrentTab = ((NewTab % Count) + Count) % Count;
	Page = EPage::Main;
	SelectedRow = 0;
	RefreshView(true);
}

FReply SAedirPauseMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 Count = static_cast<int32>(EAedirPauseTab::Count);

	if (Key == EKeys::Escape && Page != EPage::Main)
	{
		GoToPage(EPage::Main);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Tab)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Page == EPage::QuitConfirm && (Key == EKeys::Y || Key == EKeys::N))
	{
		Activate(static_cast<int32>(Key == EKeys::Y ? EPauseAction::QuitYes : EPauseAction::QuitNo));
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
		SetTab(CurrentTab - 1 + Count);
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		SetTab(CurrentTab + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E)
	{
		ActivateSelected();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SAedirPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RefreshView(false);
}

TArray<FAedirMenuRow> SAedirPauseMenu::BuildRows() const
{
	if (CurrentTab == static_cast<int32>(EAedirPauseTab::Journal))
	{
		return AedirMenuContent::BuildJournalRows(World.Get());
	}

	TArray<FAedirMenuRow> Result;
	switch (Page)
	{
	case EPage::Main:
		Result.Add(PauseMakeRow(TEXT("Quick Save"), EPauseAction::QuickSave));
		Result.Add(PauseMakeRow(TEXT("Save"), EPauseAction::Save));
		Result.Add(PauseMakeRow(TEXT("Load"), EPauseAction::Load));
		Result.Add(PauseMakeRow(TEXT("Settings"), EPauseAction::Settings));
		Result.Add(PauseMakeRow(TEXT("Quit"), EPauseAction::Quit));
		break;
	case EPage::Settings:
	{
		const UAedirUpscalerSubsystem* Upscaler = PauseGetUpscaler(World.Get());
		const FString UpscalerName = Upscaler ? Upscaler->GetUpscalerDisplayName(Upscaler->GetCurrentUpscaler()).ToString() : FString(TEXT("n/a"));
		const int32 Quality = Upscaler ? static_cast<int32>(Upscaler->GetCurrentQuality()) : 0;
		Result.Add(PauseMakeRow(TEXT("Upscaler"), EPauseAction::Upscaler, UpscalerName));
		Result.Add(PauseMakeRow(TEXT("Upscaler quality"), EPauseAction::Quality, PauseQualityNames[FMath::Clamp(Quality, 0, static_cast<int32>(UE_ARRAY_COUNT(PauseQualityNames)) - 1)]));
		Result.Add(PauseMakeRow(TEXT("Back"), EPauseAction::Back));
		break;
	}
	case EPage::QuitConfirm:
		Result.Add(PauseMakeRow(TEXT("Yes, quit the game"), EPauseAction::QuitYes));
		Result.Add(PauseMakeRow(TEXT("No"), EPauseAction::QuitNo));
		break;
	}
	return Result;
}

TArray<FAedirMenuLine> SAedirPauseMenu::BuildDetailLines(const TArray<FAedirMenuRow>& NewRows) const
{
	TArray<FAedirMenuLine> Result;
	const FAedirMenuRow* Selected = NewRows.IsValidIndex(SelectedRow) ? &NewRows[SelectedRow] : nullptr;

	if (CurrentTab == static_cast<int32>(EAedirPauseTab::Journal))
	{
		AedirMenuContent::BuildJournalDetail(World.Get(), Selected, Result);
		return Result;
	}

	if (Page == EPage::QuitConfirm)
	{
		PauseAddLine(Result, EAedirMenuLineKind::Title, TEXT("Quit"));
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Are you sure you want to quit? Unsaved progress is lost."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, TEXT("[Y] Yes  [N] No"));
		return Result;
	}

	if (!Selected)
	{
		return Result;
	}

	const UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	switch (static_cast<EPauseAction>(Selected->Index))
	{
	case EPauseAction::QuickSave:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Saves the world right now to the quick slot."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Last quick save: %s"), *PauseSaveInfo(Saves, PauseQuickSaveSlot)));
		break;
	case EPauseAction::Save:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Saves the world to the manual slot."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Last save: %s"), *PauseSaveInfo(Saves, PauseSaveSlot)));
		break;
	case EPauseAction::Load:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Loads the most recent save."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Quick save: %s"), *PauseSaveInfo(Saves, PauseQuickSaveSlot)));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Save: %s"), *PauseSaveInfo(Saves, PauseSaveSlot)));
		break;
	case EPauseAction::Settings:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Upscaler and its quality level."));
		break;
	case EPauseAction::Quit:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Leaves the game after a confirmation."));
		break;
	case EPauseAction::Upscaler:
	case EPauseAction::Quality:
		PauseAddLine(Result, EAedirMenuLineKind::Title, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Press Enter to switch to the next option. The change applies immediately."));
		break;
	default:
		break;
	}
	return Result;
}

void SAedirPauseMenu::RefreshView(bool bForce)
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
		AedirMenuContent::PopulateDetail(*DetailBox, Lines, []() -> TOptional<float> { return 0.f; });
	}
}

void SAedirPauseMenu::RebuildList()
{
	ListBox->ClearChildren();
	RowWidgets.Reset();

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FAedirMenuRow& Row = Rows[Index];
		TSharedRef<SBorder> RowBorder = SNew(SBorder)
		.BorderImage(WhiteBrush)
		.BorderBackgroundColor_Lambda([this, Index]() { return FSlateColor(GetRowBackground(Index)); })
		.Cursor(EMouseCursor::Hand)
		.OnMouseMove_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
		{
			SelectedRow = Index;
			return FReply::Unhandled();
		})
		.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Handled();
			}

			// Options act on the first click, quests are selected first and tracked on the second.
			const bool bAlreadySelected = SelectedRow == Index;
			SelectedRow = Index;
			if (bAlreadySelected || CurrentTab == static_cast<int32>(EAedirPauseTab::Options))
			{
				ActivateSelected();
			}
			else
			{
				RefreshView(true);
			}
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
			.Padding(FMargin(12.f, 8.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row.Name))
				.Font(RowFont)
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(GetRowTextColor(Index)); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 8.f))
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

	if (RowWidgets.IsValidIndex(SelectedRow))
	{
		ListScroll->ScrollDescendantIntoView(RowWidgets[SelectedRow], true);
	}
}

void SAedirPauseMenu::MoveSelection(int32 Delta)
{
	if (Rows.Num() == 0)
	{
		return;
	}

	const int32 Count = Rows.Num();
	SelectedRow = (SelectedRow + Delta + Count) % Count;
	RefreshView(false);
	if (RowWidgets.IsValidIndex(SelectedRow))
	{
		ListScroll->ScrollDescendantIntoView(RowWidgets[SelectedRow], true);
	}
}

void SAedirPauseMenu::ActivateSelected()
{
	if (!Rows.IsValidIndex(SelectedRow))
	{
		return;
	}

	if (CurrentTab == static_cast<int32>(EAedirPauseTab::Journal))
	{
		UWorld* CurrentWorld = World.Get();
		UAedirJournalSubsystem* Journal = CurrentWorld ? CurrentWorld->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
		if (Journal)
		{
			Journal->SetActiveQuest(Rows[SelectedRow].QuestId);
		}
		return;
	}

	Activate(Rows[SelectedRow].Index);
}

void SAedirPauseMenu::Activate(int32 Action)
{
	switch (static_cast<EPauseAction>(Action))
	{
	case EPauseAction::QuickSave:
		QuickSave();
		break;
	case EPauseAction::Save:
		SaveToSlot();
		break;
	case EPauseAction::Load:
		LoadLatest();
		break;
	case EPauseAction::Settings:
		GoToPage(EPage::Settings);
		break;
	case EPauseAction::Quit:
		GoToPage(EPage::QuitConfirm);
		break;
	case EPauseAction::Upscaler:
		CycleUpscaler();
		break;
	case EPauseAction::Quality:
		CycleQuality();
		break;
	case EPauseAction::Back:
	case EPauseAction::QuitNo:
		GoToPage(EPage::Main);
		break;
	case EPauseAction::QuitYes:
		ConfirmQuit();
		break;
	}
}

void SAedirPauseMenu::GoToPage(EPage NewPage)
{
	Page = NewPage;
	SelectedRow = 0;
	RefreshView(true);
}

void SAedirPauseMenu::Notify(const FString& Message) const
{
	UWorld* CurrentWorld = World.Get();
	const ULocalPlayer* LocalPlayer = CurrentWorld ? CurrentWorld->GetFirstLocalPlayerFromController() : nullptr;
	UAedirTravelSubsystem* Travel = LocalPlayer ? LocalPlayer->GetSubsystem<UAedirTravelSubsystem>() : nullptr;
	if (Travel)
	{
		Travel->ShowNotification(FText::FromString(Message));
	}
}

void SAedirPauseMenu::QuickSave()
{
	UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	Notify(Saves && Saves->SaveWorld(PauseQuickSaveSlot) ? TEXT("Quick saved") : TEXT("Quick save failed"));
	RefreshView(true);
}

void SAedirPauseMenu::SaveToSlot()
{
	UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	Notify(Saves && Saves->SaveWorld(PauseSaveSlot) ? TEXT("Game saved") : TEXT("Save failed"));
	RefreshView(true);
}

void SAedirPauseMenu::LoadLatest()
{
	UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	const bool bQuick = Saves && Saves->DoesSaveExist(PauseQuickSaveSlot);
	const bool bManual = Saves && Saves->DoesSaveExist(PauseSaveSlot);
	if (!bQuick && !bManual)
	{
		Notify(TEXT("No save found"));
		return;
	}

	const bool bUseQuick = bQuick && (!bManual || Saves->GetSaveTimestamp(PauseQuickSaveSlot) >= Saves->GetSaveTimestamp(PauseSaveSlot));
	if (Saves->LoadWorld(bUseQuick ? PauseQuickSaveSlot : PauseSaveSlot))
	{
		Notify(TEXT("Game loaded"));
		CloseDelegate.ExecuteIfBound();
	}
	else
	{
		Notify(TEXT("Load failed"));
	}
}

void SAedirPauseMenu::CycleUpscaler()
{
	UAedirUpscalerSubsystem* Upscaler = PauseGetUpscaler(World.Get());
	if (!Upscaler)
	{
		return;
	}

	const TArray<EAedirUpscaler> Available = Upscaler->GetAvailableUpscalers();
	if (Available.Num() == 0)
	{
		return;
	}

	const int32 Current = Available.IndexOfByKey(Upscaler->GetCurrentUpscaler());
	const EAedirUpscaler Next = Available[(Current + 1) % Available.Num()];
	if (!Upscaler->SetUpscaler(Next, Upscaler->GetCurrentQuality()))
	{
		Notify(TEXT("Upscaler not available"));
	}
	RefreshView(true);
}

void SAedirPauseMenu::CycleQuality()
{
	UAedirUpscalerSubsystem* Upscaler = PauseGetUpscaler(World.Get());
	if (!Upscaler)
	{
		return;
	}

	const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(PauseQualityNames));
	const EAedirUpscalerQuality Next = static_cast<EAedirUpscalerQuality>((static_cast<int32>(Upscaler->GetCurrentQuality()) + 1) % Count);
	Upscaler->SetUpscaler(Upscaler->GetCurrentUpscaler(), Next);
	RefreshView(true);
}

void SAedirPauseMenu::ConfirmQuit()
{
	UWorld* CurrentWorld = World.Get();
	APlayerController* PlayerController = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
	UKismetSystemLibrary::QuitGame(CurrentWorld, PlayerController, EQuitPreference::Quit, false);
}

FLinearColor SAedirPauseMenu::GetRowBackground(int32 Index) const
{
	return Index == SelectedRow ? AedirUIStyle::PanelSelected : FLinearColor(0.f, 0.f, 0.f, 0.f);
}

FLinearColor SAedirPauseMenu::GetRowAccent(int32 Index) const
{
	return Index == SelectedRow ? AedirUIStyle::Accent : FLinearColor(1.f, 1.f, 1.f, 0.f);
}

FLinearColor SAedirPauseMenu::GetRowTextColor(int32 Index) const
{
	if (Rows.IsValidIndex(Index) && Rows[Index].bDimmed)
	{
		return AedirUIStyle::TextDim;
	}
	return Index == SelectedRow ? FLinearColor::White : AedirUIStyle::Text;
}
