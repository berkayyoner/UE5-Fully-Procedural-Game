#include "UI/SAedirPauseMenu.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Settings/AedirSettingsSubsystem.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Travel/AedirTravelSubsystem.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const PauseTabNames[] = { TEXT("OPTIONS"), TEXT("JOURNAL") };
	constexpr float PauseRowFontSize = 18.f;
	constexpr float PauseSaveRowFontSize = 15.f;
	constexpr float PauseConfirmHalfHeight = 104.f;
	constexpr float PauseConfirmFontSize = 24.f;
	constexpr float PauseVignette = 1.08f;

	enum class EPauseAction : int32
	{
		QuickSave,
		Save,
		Load,
		Settings,
		Quit,
		SettingsGraphics,
		SettingsAudio,
		SettingsGameplay,
		Back,
		NewSave,
		NoSaves,
		Resume,
		// Rows of the save lists use this plus their index in the save list.
		FirstSaveRow = 100,
		// Rows of the settings pages use this plus their option index.
		FirstOptionRow = 5000
	};

	EAedirSettingsPage PauseGetSettingsPage(EAedirPausePage Page)
	{
		return Page == EAedirPausePage::SettingsAudio ? EAedirSettingsPage::Audio : (Page == EAedirPausePage::SettingsGameplay ? EAedirSettingsPage::Gameplay : EAedirSettingsPage::Graphics);
	}

	void PauseAddLine(TArray<FAedirMenuLine>& Lines, EAedirMenuLineKind Kind, const FString& Content)
	{
		FAedirMenuLine Line;
		Line.Kind = Kind;
		Line.Content = Content;
		Lines.Add(Line);
	}

	void PauseAddHeader(TArray<FAedirMenuLine>& Lines, const FString& Title)
	{
		PauseAddLine(Lines, EAedirMenuLineKind::Title, Title);
		PauseAddLine(Lines, EAedirMenuLineKind::Rule, FString());
	}

	FAedirMenuRow PauseMakeRow(const TCHAR* Name, EPauseAction Action, const FString& Value = FString())
	{
		FAedirMenuRow Row;
		Row.Name = Name;
		Row.Value = Value;
		Row.Index = static_cast<int32>(Action);
		return Row;
	}

	UAedirSettingsSubsystem* PauseGetSettings(const UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UAedirSettingsSubsystem>() : nullptr;
	}

	UAedirSaveSubsystem* PauseGetSaves(const UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UAedirSaveSubsystem>() : nullptr;
	}

	// Saves store UTC, the player reads local time.
	FString PauseFormatLocalTime(const FDateTime& Utc)
	{
		return AedirSaveSlots::FormatTimestamp(Utc + (FDateTime::Now() - FDateTime::UtcNow()));
	}
}

void SAedirPauseMenu::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	CloseDelegate = InArgs._OnClose;

	using namespace AedirUIStyle;

	RefreshSaveEntries();

	TSharedRef<SHorizontalBox> TabBar = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < static_cast<int32>(EAedirPauseTab::Count); ++Index)
	{
		TabBar->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.f, 0.f, 26.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(FMargin(0.f, 2.f))
			.Cursor(EMouseCursor::Hand)
			.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent&)
			{
				if (!IsConfirmOpen())
				{
					SetTab(Index);
				}
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(PauseTabNames[Index]))
				.Font(RegularFont(11.f, 140))
				.ColorAndOpacity_Lambda([this, Index]() { return FSlateColor(Index == CurrentTab ? IvoryBright : TextDim); })
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
				TAttribute<FText>(),
				TAttribute<FText>::CreateLambda([this]() { return GetHintText(); }),
				PauseVignette)
		]
		+ SOverlay::Slot()
		[
			// Visible, so the band takes every click while the question is open.
			SNew(SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(FMargin(0.f))
			.Visibility_Lambda([this]() { return IsConfirmOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&)
			{
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			})
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SAedirUIPaint)
					.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
					{
						AddModalBand(Mesh, Size, Size.Y * 0.5f, PauseConfirmHalfHeight);
					})
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return GetConfirmPrompt(); })
						.Font(LightFont(PauseConfirmFontSize, 30))
						.ColorAndOpacity(IvoryBright)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.f, 6.f, 0.f, 20.f))
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return GetConfirmDetail(); })
						.Font(RegularFont(11.f, 40))
						.ColorAndOpacity(TextDim)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0.f, 0.f, 70.f, 0.f))
						[
							MakeConfirmButton(TEXT("YES"), true)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							MakeConfirmButton(TEXT("NO"), false)
						]
					]
				]
			]
		]
	];

	RefreshView(true);
}

TSharedRef<SWidget> SAedirPauseMenu::MakeConfirmButton(const TCHAR* Label, bool bYes)
{
	using namespace AedirUIStyle;

	return SNew(SBorder)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.Padding(FMargin(0.f))
		.Cursor(EMouseCursor::Hand)
		.OnMouseMove_Lambda([this, bYes](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (!MouseEvent.GetCursorDelta().IsNearlyZero())
			{
				bConfirmYesSelected = bYes;
			}
			return FReply::Unhandled();
		})
		.OnMouseButtonDown_Lambda([this, bYes](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				AnswerConfirm(bYes);
			}
			return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SAedirUIPaint)
				.DesiredSize(FVector2D(12.f, 12.f))
				.Painter([this, bYes](const FVector2f& Size, FAedirUIMesh& Mesh)
				{
					if (bConfirmYesSelected == bYes)
					{
						AddDiamond(Mesh, Size * 0.5f, 4.5f, 4.5f, IvoryBright);
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(TitleFont(18.f))
				.ColorAndOpacity_Lambda([this, bYes]() { return FSlateColor(bConfirmYesSelected == bYes ? IvoryBright : TextDim); })
			]
		];
}

void SAedirPauseMenu::SetTab(int32 NewTab)
{
	const int32 Count = static_cast<int32>(EAedirPauseTab::Count);
	CurrentTab = ((NewTab % Count) + Count) % Count;
	Page = EAedirPausePage::Main;
	SelectedRow = 0;
	RefreshView(true);
}

void SAedirPauseMenu::GoToPage(EAedirPausePage NewPage)
{
	// Coming back to the main page, the cursor returns to the row that opened the page.
	const int32 ReturnRow = Page == EAedirPausePage::SaveList ? 2 : (Page == EAedirPausePage::LoadList ? 3 : (Page == EAedirPausePage::Settings ? 4 : 0));
	const int32 ReturnSettingsRow = Page == EAedirPausePage::SettingsAudio ? 1 : (Page == EAedirPausePage::SettingsGameplay ? 2 : 0);
	CurrentTab = static_cast<int32>(EAedirPauseTab::Options);
	Page = NewPage;
	SelectedRow = NewPage == EAedirPausePage::Main ? ReturnRow : (NewPage == EAedirPausePage::Settings ? ReturnSettingsRow : 0);
	if (Page == EAedirPausePage::SaveList || Page == EAedirPausePage::LoadList)
	{
		RefreshSaveEntries();
	}
	RefreshView(true);
}

bool SAedirPauseMenu::IsOptionPage() const
{
	return Page == EAedirPausePage::SettingsGraphics || Page == EAedirPausePage::SettingsAudio || Page == EAedirPausePage::SettingsGameplay;
}

void SAedirPauseMenu::GoBack()
{
	GoToPage(IsOptionPage() ? EAedirPausePage::Settings : EAedirPausePage::Main);
}

void SAedirPauseMenu::StepSelectedOption(int32 Delta)
{
	UAedirSettingsSubsystem* Settings = PauseGetSettings(World.Get());
	const int32 OptionIndex = Rows.IsValidIndex(SelectedRow) ? Rows[SelectedRow].Index - static_cast<int32>(EPauseAction::FirstOptionRow) : INDEX_NONE;
	if (Settings && OptionIndex >= 0)
	{
		Settings->StepOption(OptionIndex, Delta);
		RefreshView(true);
	}
}

void SAedirPauseMenu::RefreshSaveEntries()
{
	const UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	SaveEntries = Saves ? Saves->ListSaves() : TArray<FAedirSaveInfo>();
}

FReply SAedirPauseMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 Count = static_cast<int32>(EAedirPauseTab::Count);

	if (IsConfirmOpen())
	{
		if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::N)
		{
			AnswerConfirm(false);
		}
		else if (Key == EKeys::Y)
		{
			AnswerConfirm(true);
		}
		else if (Key == EKeys::Enter || Key == EKeys::E)
		{
			AnswerConfirm(bConfirmYesSelected);
		}
		else if (Key == EKeys::A || Key == EKeys::D || Key == EKeys::W || Key == EKeys::S || Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down)
		{
			bConfirmYesSelected = !bConfirmYesSelected;
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape && Page != EAedirPausePage::Main)
	{
		GoBack();
		return FReply::Handled();
	}
	if (IsOptionPage() && (Key == EKeys::A || Key == EKeys::Left || Key == EKeys::D || Key == EKeys::Right))
	{
		StepSelectedOption(Key == EKeys::A || Key == EKeys::Left ? -1 : 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Tab)
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
	if ((Key == EKeys::A || Key == EKeys::Left) && Page == EAedirPausePage::Main)
	{
		SetTab(CurrentTab - 1 + Count);
		return FReply::Handled();
	}
	if ((Key == EKeys::D || Key == EKeys::Right) && Page == EAedirPausePage::Main)
	{
		SetTab(CurrentTab + 1);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E)
	{
		ActivateSelected();
		return FReply::Handled();
	}
	if (Key == EKeys::Delete)
	{
		AskDeleteSelected();
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
	case EAedirPausePage::Main:
		Result.Add(PauseMakeRow(TEXT("Resume"), EPauseAction::Resume));
		Result.Add(PauseMakeRow(TEXT("Quick Save"), EPauseAction::QuickSave));
		Result.Add(PauseMakeRow(TEXT("Save"), EPauseAction::Save));
		Result.Add(PauseMakeRow(TEXT("Load"), EPauseAction::Load));
		Result.Add(PauseMakeRow(TEXT("Settings"), EPauseAction::Settings));
		Result.Add(PauseMakeRow(TEXT("Quit"), EPauseAction::Quit));
		break;
	case EAedirPausePage::Settings:
		Result.Add(PauseMakeRow(TEXT("Graphics"), EPauseAction::SettingsGraphics));
		Result.Add(PauseMakeRow(TEXT("Audio"), EPauseAction::SettingsAudio));
		Result.Add(PauseMakeRow(TEXT("Gameplay"), EPauseAction::SettingsGameplay));
		Result.Add(PauseMakeRow(TEXT("Back"), EPauseAction::Back));
		break;
	case EAedirPausePage::SettingsGraphics:
	case EAedirPausePage::SettingsAudio:
	case EAedirPausePage::SettingsGameplay:
		if (const UAedirSettingsSubsystem* Settings = PauseGetSettings(World.Get()))
		{
			for (const int32 OptionIndex : Settings->GetPageOptions(PauseGetSettingsPage(Page)))
			{
				const FAedirSettingOption* Option = Settings->GetOption(OptionIndex);
				Result.Add(PauseMakeRow(*Option->Name, static_cast<EPauseAction>(static_cast<int32>(EPauseAction::FirstOptionRow) + OptionIndex), Settings->GetValueText(OptionIndex)));
			}
		}
		Result.Add(PauseMakeRow(TEXT("Back"), EPauseAction::Back));
		break;
	case EAedirPausePage::SaveList:
	case EAedirPausePage::LoadList:
		if (Page == EAedirPausePage::SaveList)
		{
			Result.Add(PauseMakeRow(TEXT("New Save"), EPauseAction::NewSave));
		}
		else if (SaveEntries.IsEmpty())
		{
			FAedirMenuRow Empty = PauseMakeRow(TEXT("No saves found"), EPauseAction::NoSaves);
			Empty.bDimmed = true;
			Result.Add(Empty);
		}
		for (int32 Index = 0; Index < SaveEntries.Num(); ++Index)
		{
			const FAedirSaveInfo& Entry = SaveEntries[Index];
			FAedirMenuRow Row;
			Row.Name = Entry.DisplayName;
			Row.Value = PauseFormatLocalTime(Entry.Timestamp);
			Row.SubText = AedirSaveSlots::MakeSummary(Entry.PlayerLevel, Entry.Location);
			Row.Index = static_cast<int32>(EPauseAction::FirstSaveRow) + Index;
			Result.Add(Row);
		}
		Result.Add(PauseMakeRow(TEXT("Back"), EPauseAction::Back));
		break;
	}
	return Result;
}

const FAedirSaveInfo* SAedirPauseMenu::GetRowSave(int32 RowIndex) const
{
	if (!Rows.IsValidIndex(RowIndex))
	{
		return nullptr;
	}
	const int32 EntryIndex = Rows[RowIndex].Index - static_cast<int32>(EPauseAction::FirstSaveRow);
	return SaveEntries.IsValidIndex(EntryIndex) ? &SaveEntries[EntryIndex] : nullptr;
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

	if (!Selected)
	{
		return Result;
	}

	if (Selected->Index >= static_cast<int32>(EPauseAction::FirstOptionRow))
	{
		const UAedirSettingsSubsystem* Settings = PauseGetSettings(World.Get());
		const FAedirSettingOption* Option = Settings ? Settings->GetOption(Selected->Index - static_cast<int32>(EPauseAction::FirstOptionRow)) : nullptr;
		if (Option)
		{
			PauseAddHeader(Result, Option->Name);
			PauseAddLine(Result, EAedirMenuLineKind::Body, Option->Description);
			PauseAddLine(Result, EAedirMenuLineKind::Dim, TEXT("Applies at once and is kept for the next start."));
		}
		return Result;
	}

	const int32 EntryIndex = Selected->Index - static_cast<int32>(EPauseAction::FirstSaveRow);
	if (SaveEntries.IsValidIndex(EntryIndex))
	{
		const FAedirSaveInfo& Entry = SaveEntries[EntryIndex];
		PauseAddHeader(Result, Entry.DisplayName);
		if (Entry.PlayerLevel > 0)
		{
			AedirMenuContent::AddPairLine(Result, { TEXT("LEVEL"), FString::FromInt(Entry.PlayerLevel), TEXT("SAVED"), PauseFormatLocalTime(Entry.Timestamp) });
		}
		else
		{
			AedirMenuContent::AddPairLine(Result, { TEXT("SAVED"), PauseFormatLocalTime(Entry.Timestamp) });
		}
		if (!Entry.Location.IsEmpty())
		{
			AedirMenuContent::AddPairLine(Result, { TEXT("LOCATION"), Entry.Location });
		}
		return Result;
	}

	const FAedirSaveInfo* Newest = SaveEntries.IsEmpty() ? nullptr : &SaveEntries[0];
	const FAedirSaveInfo* Quick = SaveEntries.FindByPredicate([](const FAedirSaveInfo& Entry) { return Entry.Slot == AedirSaveSlots::QuickSaveSlot; });

	switch (static_cast<EPauseAction>(Selected->Index))
	{
	case EPauseAction::Resume:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Closes this menu and goes back to the game."));
		break;
	case EPauseAction::QuickSave:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Saves the world right now to the quick slot."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Last quick save: %s"), Quick ? *PauseFormatLocalTime(Quick->Timestamp) : TEXT("none")));
		break;
	case EPauseAction::Save:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Writes a new save or replaces an existing one."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("%d saves"), SaveEntries.Num()));
		break;
	case EPauseAction::Load:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Returns to one of your saves."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, FString::Printf(TEXT("Latest: %s"), Newest ? *Newest->DisplayName : TEXT("none")));
		break;
	case EPauseAction::Settings:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Graphics, audio and gameplay options."));
		break;
	case EPauseAction::SettingsGraphics:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Monitor, resolution, frame rate, all quality levels, effects and the physics rate."));
		break;
	case EPauseAction::SettingsAudio:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Master volume, footsteps, weather and sound in the background."));
		break;
	case EPauseAction::SettingsGameplay:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Mouse, camera and what the screen shows."));
		break;
	case EPauseAction::Quit:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Leaves the game after a confirmation."));
		break;
	case EPauseAction::NewSave:
		PauseAddHeader(Result, Selected->Name);
		PauseAddLine(Result, EAedirMenuLineKind::Body, TEXT("Writes the world to a new save and keeps all the others."));
		PauseAddLine(Result, EAedirMenuLineKind::Dim, AedirSaveSlots::MakeDisplayName(AedirSaveSlots::NextSlotName([this]()
		{
			TArray<FString> Slots;
			for (const FAedirSaveInfo& Entry : SaveEntries)
			{
				Slots.Add(Entry.Slot);
			}
			return Slots;
		}())));
		break;
	case EPauseAction::NoSaves:
		PauseAddLine(Result, EAedirMenuLineKind::Dim, TEXT("There is nothing to load yet."));
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

	const bool bSaveList = CurrentTab == static_cast<int32>(EAedirPauseTab::Options) && (Page == EAedirPausePage::SaveList || Page == EAedirPausePage::LoadList);
	const FSlateFontInfo RowFont = AedirUIStyle::LightFont(bSaveList ? PauseSaveRowFontSize : PauseRowFontSize, 20);
	const float RowGap = bSaveList ? 2.f : 5.f;

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		TSharedRef<SBorder> RowBorder = SNew(SBorder)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.Padding(FMargin(0.f))
		.Cursor(EMouseCursor::Hand)
		.OnMouseMove_Lambda([this, Index](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			// Slate also sends moves with no motion when widgets change, a resting cursor must not undo W and S.
			if (!MouseEvent.GetCursorDelta().IsNearlyZero())
			{
				SelectedRow = Index;
			}
			return FReply::Unhandled();
		})
		.OnMouseButtonDown_Lambda([this, Index](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				SelectedRow = Index;
				if (IsOptionPage())
				{
					StepSelectedOption(-1);
					return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
				}
				AskDeleteSelected();
				return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
			}
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
			AedirMenuContent::MakeListRow(Rows[Index], RowFont,
				[this, Index]() { return GetRowTextColor(Index); },
				[this, Index]() { return GetRowMarker(Index); })
		];

		ListBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, RowGap))
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
	if (Action >= static_cast<int32>(EPauseAction::FirstOptionRow))
	{
		StepSelectedOption(1);
		return;
	}

	if (const FAedirSaveInfo* Entry = SaveEntries.IsValidIndex(Action - static_cast<int32>(EPauseAction::FirstSaveRow)) ? &SaveEntries[Action - static_cast<int32>(EPauseAction::FirstSaveRow)] : nullptr)
	{
		ActivateSaveRow(*Entry);
		return;
	}

	switch (static_cast<EPauseAction>(Action))
	{
	case EPauseAction::Resume:
		CloseDelegate.ExecuteIfBound();
		break;
	case EPauseAction::QuickSave:
		QuickSave();
		break;
	case EPauseAction::Save:
		GoToPage(EAedirPausePage::SaveList);
		break;
	case EPauseAction::Load:
		GoToPage(EAedirPausePage::LoadList);
		break;
	case EPauseAction::Settings:
		GoToPage(EAedirPausePage::Settings);
		break;
	case EPauseAction::Quit:
		AskConfirm(EConfirm::Quit, FString());
		break;
	case EPauseAction::SettingsGraphics:
		GoToPage(EAedirPausePage::SettingsGraphics);
		break;
	case EPauseAction::SettingsAudio:
		GoToPage(EAedirPausePage::SettingsAudio);
		break;
	case EPauseAction::SettingsGameplay:
		GoToPage(EAedirPausePage::SettingsGameplay);
		break;
	case EPauseAction::Back:
		GoBack();
		break;
	case EPauseAction::NewSave:
		SaveToNewSlot();
		break;
	default:
		break;
	}
}

void SAedirPauseMenu::ActivateSaveRow(const FAedirSaveInfo& Entry)
{
	AskConfirm(Page == EAedirPausePage::SaveList ? EConfirm::Overwrite : EConfirm::Load, Entry.Slot);
}

void SAedirPauseMenu::AskConfirm(EConfirm Kind, const FString& Slot)
{
	Confirm = Kind;
	ConfirmSlot = Slot;
	// Enter pressed twice by accident must not destroy anything.
	bConfirmYesSelected = false;
}

void SAedirPauseMenu::AskDeleteSelected()
{
	if (Page != EAedirPausePage::SaveList && Page != EAedirPausePage::LoadList)
	{
		return;
	}

	if (const FAedirSaveInfo* Entry = GetRowSave(SelectedRow))
	{
		AskConfirm(EConfirm::Delete, Entry->Slot);
	}
}

void SAedirPauseMenu::AnswerConfirm(bool bYes)
{
	const EConfirm Kind = Confirm;
	const FString Slot = ConfirmSlot;
	Confirm = EConfirm::None;
	ConfirmSlot.Reset();
	if (!bYes)
	{
		return;
	}

	UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	switch (Kind)
	{
	case EConfirm::Quit:
	{
		UWorld* CurrentWorld = World.Get();
		APlayerController* PlayerController = CurrentWorld ? CurrentWorld->GetFirstPlayerController() : nullptr;
		UKismetSystemLibrary::QuitGame(CurrentWorld, PlayerController, EQuitPreference::Quit, false);
		break;
	}
	case EConfirm::Overwrite:
		Notify(Saves && Saves->SaveWorld(Slot) ? TEXT("Game saved") : TEXT("Save failed"));
		RefreshSaveEntries();
		break;
	case EConfirm::Load:
		if (Saves && Saves->LoadWorld(Slot))
		{
			Notify(TEXT("Game loaded"));
			CloseDelegate.ExecuteIfBound();
			return;
		}
		Notify(TEXT("Load failed"));
		break;
	case EConfirm::Delete:
		Notify(Saves && Saves->DeleteSave(Slot) ? TEXT("Save deleted") : TEXT("Delete failed"));
		RefreshSaveEntries();
		break;
	default:
		break;
	}
	RefreshView(true);
}

FText SAedirPauseMenu::GetConfirmPrompt() const
{
	switch (Confirm)
	{
	case EConfirm::Quit: return FText::FromString(TEXT("Quit the game?"));
	case EConfirm::Overwrite: return FText::FromString(TEXT("Overwrite this save?"));
	case EConfirm::Load: return FText::FromString(TEXT("Load this save?"));
	case EConfirm::Delete: return FText::FromString(TEXT("Delete this save?"));
	default: break;
	}
	return FText::GetEmpty();
}

FText SAedirPauseMenu::GetConfirmDetail() const
{
	if (Confirm == EConfirm::Quit)
	{
		return FText::FromString(TEXT("Unsaved progress is lost."));
	}

	const FAedirSaveInfo* Entry = SaveEntries.FindByPredicate([this](const FAedirSaveInfo& Candidate) { return Candidate.Slot == ConfirmSlot; });
	if (!Entry)
	{
		return FText::GetEmpty();
	}
	if (Confirm == EConfirm::Load)
	{
		return FText::FromString(FString::Printf(TEXT("%s, %s. Unsaved progress is lost."), *Entry->DisplayName, *PauseFormatLocalTime(Entry->Timestamp)));
	}
	return FText::FromString(FString::Printf(TEXT("%s, %s"), *Entry->DisplayName, *PauseFormatLocalTime(Entry->Timestamp)));
}

FText SAedirPauseMenu::GetHintText() const
{
	if (IsConfirmOpen())
	{
		return FText::FromString(TEXT("[A/D] CHOOSE     [ENTER] CONFIRM     [Y] YES     [N] NO     [ESC] BACK"));
	}
	if (CurrentTab == static_cast<int32>(EAedirPauseTab::Options) && (Page == EAedirPausePage::SaveList || Page == EAedirPausePage::LoadList))
	{
		return FText::FromString(TEXT("[W/S] SELECT     [ENTER] CONFIRM     [DEL / RMB] DELETE     [ESC] BACK"));
	}
	if (IsOptionPage())
	{
		return FText::FromString(TEXT("[W/S] SELECT     [A/D] CHANGE     [LMB / RMB] NEXT / PREVIOUS     [ESC] BACK"));
	}
	if (Page != EAedirPausePage::Main)
	{
		return FText::FromString(TEXT("[W/S] SELECT     [ENTER] CONFIRM     [ESC] BACK"));
	}
	return FText::FromString(TEXT("[W/S] SELECT     [A/D] TAB     [ENTER] CONFIRM     [ESC] CLOSE"));
}

FText SAedirPauseMenu::GetTitleText() const
{
	if (CurrentTab == static_cast<int32>(EAedirPauseTab::Journal))
	{
		return FText::FromString(TEXT("JOURNAL"));
	}
	switch (Page)
	{
	case EAedirPausePage::Settings: return FText::FromString(TEXT("SETTINGS"));
	case EAedirPausePage::SettingsGraphics: return FText::FromString(TEXT("GRAPHICS"));
	case EAedirPausePage::SettingsAudio: return FText::FromString(TEXT("AUDIO"));
	case EAedirPausePage::SettingsGameplay: return FText::FromString(TEXT("GAMEPLAY"));
	case EAedirPausePage::SaveList: return FText::FromString(TEXT("SAVE"));
	case EAedirPausePage::LoadList: return FText::FromString(TEXT("LOAD"));
	default: break;
	}
	return FText::FromString(TEXT("PAUSED"));
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
	Notify(Saves && Saves->SaveWorld(AedirSaveSlots::QuickSaveSlot) ? TEXT("Quick saved") : TEXT("Quick save failed"));
	RefreshSaveEntries();
	RefreshView(true);
}

void SAedirPauseMenu::SaveToNewSlot()
{
	UAedirSaveSubsystem* Saves = PauseGetSaves(World.Get());
	const FString Slot = Saves ? Saves->SaveToNewSlot() : FString();
	Notify(Slot.IsEmpty() ? TEXT("Save failed") : FString::Printf(TEXT("Saved as %s"), *AedirSaveSlots::MakeDisplayName(Slot)));
	RefreshSaveEntries();
	RefreshView(true);
}

float SAedirPauseMenu::GetRowMarker(int32 Index) const
{
	return Index == SelectedRow ? 1.f : 0.f;
}

FLinearColor SAedirPauseMenu::GetRowTextColor(int32 Index) const
{
	const bool bSelected = Index == SelectedRow;
	if (Rows.IsValidIndex(Index) && Rows[Index].bDimmed)
	{
		return bSelected ? AedirUIStyle::Ivory : AedirUIStyle::TextDim;
	}
	return bSelected ? AedirUIStyle::IvoryBright : AedirUIStyle::WithAlpha(AedirUIStyle::Ivory, 0.8f);
}
