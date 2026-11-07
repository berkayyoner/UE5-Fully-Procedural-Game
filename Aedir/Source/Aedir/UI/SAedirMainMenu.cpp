#include "UI/SAedirMainMenu.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/AedirSessionSubsystem.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 MainMenuMaxSaves = 8;
	const TCHAR* const MainMenuDefaultAddress = TEXT("127.0.0.1");

	const TCHAR* const MainMenuCredits = TEXT("AEDIR\n\nA game of the two moons.\n\nMade by Berkay Oner.\nBuilt in Unreal Engine.\nScanned models and sound from the Medieval Village sample by Epic Games and Quixel Megascans.");
}

void SAedirMainMenu::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	OnSettings = InArgs._OnSettings;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(150.f, 0.f, 0.f, 120.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("AEDIR")))
			.Font(AedirUIStyle::TitleFont(64.f))
			.ColorAndOpacity(AedirUIStyle::IvoryBright)
			.ShadowOffset(FVector2D(2.f, 2.f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 110.f, 70.f)
		[
			SNew(SBox).WidthOverride(460.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 12.f)
				[
					SAssignNew(NoteText, STextBlock)
					.Font(AedirUIStyle::LightFont(14.f))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.AutoWrapText(true)
					.Justification(ETextJustify::Right)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SAssignNew(RowBox, SVerticalBox)
				]
			]
		]
	];

	GoTo(EPage::Root);
}

void SAedirMainMenu::Refocus()
{
	FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SAedirMainMenu::GoTo(EPage NewPage)
{
	Page = NewPage;
	Selected = 0;
	BuildRows();
}

void SAedirMainMenu::AddSaveRows(TFunction<void(const FString&)> Pick)
{
	for (int32 Index = 0; Index < Saves.Num() && Index < MainMenuMaxSaves; ++Index)
	{
		const FAedirSaveInfo& Save = Saves[Index];
		const FString Summary = AedirSaveSlots::MakeSummary(Save.PlayerLevel, Save.Location);
		const FString Slot = Save.Slot;
		Rows.Add({ Save.DisplayName + (Summary.IsEmpty() ? FString() : TEXT("   ") + Summary), [Pick, Slot]() { Pick(Slot); } });
	}
}

void SAedirMainMenu::BuildRows()
{
	UGameInstance* Instance = World.IsValid() ? World->GetGameInstance() : nullptr;
	UAedirSaveSubsystem* SaveSystem = Instance ? Instance->GetSubsystem<UAedirSaveSubsystem>() : nullptr;
	UAedirSessionSubsystem* Sessions = Instance ? Instance->GetSubsystem<UAedirSessionSubsystem>() : nullptr;
	Saves = SaveSystem ? SaveSystem->ListSaves() : TArray<FAedirSaveInfo>();

	Rows.Reset();
	FString Note;
	switch (Page)
	{
	case EPage::Root:
		if (!Saves.IsEmpty())
		{
			const FString Newest = Saves[0].Slot;
			Rows.Add({ TEXT("Continue"), [Sessions, Newest]() { if (Sessions) { Sessions->StartFromSave(Newest, false); } } });
		}
		Rows.Add({ TEXT("New Game"), [Sessions]() { if (Sessions) { Sessions->StartFromSave(FString(), false); } } });
		if (!Saves.IsEmpty())
		{
			Rows.Add({ TEXT("Load"), [this]() { GoTo(EPage::Load); } });
		}
		Rows.Add({ TEXT("Co-op"), [this]() { GoTo(EPage::Coop); } });
		Rows.Add({ TEXT("Credits"), [this]() { GoTo(EPage::Credits); } });
		Rows.Add({ TEXT("Settings"), [this]() { OnSettings.ExecuteIfBound(); } });
		Rows.Add({ TEXT("Exit"), [this]() { GoTo(EPage::ExitConfirm); } });
		break;

	case EPage::Load:
		AddSaveRows([Sessions](const FString& Slot) { if (Sessions) { Sessions->StartFromSave(Slot, false); } });
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;

	case EPage::Coop:
		Rows.Add({ TEXT("Host"), [this]() { GoTo(EPage::HostPick); } });
		Rows.Add({ TEXT("Join"), [this]() { GoTo(EPage::JoinPick); } });
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;

	case EPage::HostPick:
		Note = TEXT("The story and the world go on from this save, and are written back to it.");
		AddSaveRows([Sessions](const FString& Slot) { if (Sessions) { Sessions->StartFromSave(Slot, true); } });
		Rows.Add({ TEXT("New World"), [Sessions]() { if (Sessions) { Sessions->StartFromSave(FString(), true); } } });
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;

	case EPage::JoinPick:
		Note = TEXT("You play this save's character in the host's world. What they carry and learn comes back to this save, the story stays with the host.");
		AddSaveRows([this](const FString& Slot) { PickedSlot = Slot; GoTo(EPage::JoinAddress); });
		Rows.Add({ TEXT("New Character"), [this]() { PickedSlot.Reset(); GoTo(EPage::JoinAddress); } });
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;

	case EPage::JoinAddress:
		Note = TEXT("Address of the host:");
		Rows.Add({ TEXT("Connect"), [this, Sessions]()
		{
			if (Sessions && AddressBox.IsValid())
			{
				Sessions->JoinWithCharacter(AddressBox->GetText().ToString(), PickedSlot);
			}
		} });
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;

	case EPage::ExitConfirm:
		Note = TEXT("Leave the game?");
		Rows.Add({ TEXT("Exit to Desktop"), [this]()
		{
			UWorld* Current = World.Get();
			UKismetSystemLibrary::QuitGame(Current, Current ? Current->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
		} });
		Rows.Add({ TEXT("Cancel"), [this]() { GoBack(); } });
		break;

	case EPage::Credits:
		Note = MainMenuCredits;
		Rows.Add({ TEXT("Back"), [this]() { GoBack(); } });
		break;
	}

	NoteText->SetText(FText::FromString(Note));
	NoteText->SetVisibility(Note.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);

	RowBox->ClearChildren();
	RowTexts.Reset();
	if (Page == EPage::JoinAddress)
	{
		RowBox->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(SBox).WidthOverride(300.f)
			[
				SAssignNew(AddressBox, SEditableTextBox).Text(FText::FromString(MainMenuDefaultAddress))
			]
		];
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		TSharedPtr<STextBlock> Text;
		RowBox->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 4.f)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, Index]() { Choose(Index); return FReply::Handled(); })
			.OnHovered_Lambda([this, Index]() { Selected = Index; RefreshHighlight(); })
			[
				SAssignNew(Text, STextBlock)
				.Text(FText::FromString(Rows[Index].Label))
				.Font(AedirUIStyle::RegularFont(21.f, 60))
				.ShadowOffset(FVector2D(1.f, 1.f))
				.Justification(ETextJustify::Right)
			]
		];
		RowTexts.Add(Text);
	}
	RefreshHighlight();
}

void SAedirMainMenu::RefreshHighlight()
{
	for (int32 Index = 0; Index < RowTexts.Num(); ++Index)
	{
		RowTexts[Index]->SetColorAndOpacity(Index == Selected ? AedirUIStyle::IvoryBright : AedirUIStyle::TextDim);
	}
}

void SAedirMainMenu::Choose(int32 Index)
{
	if (Rows.IsValidIndex(Index))
	{
		const TFunction<void()> Run = Rows[Index].Run;
		Run();
	}
}

void SAedirMainMenu::ChooseRowNamed(const FString& Name)
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].Label.StartsWith(Name))
		{
			Choose(Index);
			return;
		}
	}
}

void SAedirMainMenu::GoBack()
{
	switch (Page)
	{
	case EPage::HostPick:
	case EPage::JoinPick:
		GoTo(EPage::Coop);
		break;
	case EPage::JoinAddress:
		GoTo(EPage::JoinPick);
		break;
	default:
		GoTo(EPage::Root);
		break;
	}
}

FReply SAedirMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		Selected = Rows.IsEmpty() ? 0 : (Selected + Rows.Num() - 1) % Rows.Num();
		RefreshHighlight();
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		Selected = Rows.IsEmpty() ? 0 : (Selected + 1) % Rows.Num();
		RefreshHighlight();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E || Key == EKeys::SpaceBar)
	{
		Choose(Selected);
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		if (Page != EPage::Root)
		{
			GoBack();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
