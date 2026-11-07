#include "UI/SAedirDialogue.h"

#include "Engine/World.h"
#include "Net/AedirSaveSubsystem.h"
#include "Player/AedirPlayerCharacter.h"
#include "TimerManager.h"
#include "UI/AedirUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr float DialogueWidth = 520.f;
	constexpr float DialogueSpeechWidth = 900.f;
}

void SAedirDialogue::Construct(const FArguments& InArgs)
{
	Npc = InArgs._Npc;
	Player = InArgs._Player;
	OnClose = InArgs._OnClose;

	const AAedirNpc* Other = Npc.Get();
	const AAedirPlayerCharacter* Me = Player.Get();
	const FString Key = Me ? UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Me->GetController())) : FString();

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 90.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(DialogueSpeechWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Other ? Other->NpcName : FText::GetEmpty())
					.Font(AedirUIStyle::RegularFont(20.f, 60))
					.ColorAndOpacity(AedirUIStyle::IvoryBright)
					.ShadowOffset(FVector2D(1.f, 1.f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 12.f)
				[
					SAssignNew(StandingText, STextBlock)
					.Text(Other ? Other->GetStandingText(Key) : FText::GetEmpty())
					.Font(AedirUIStyle::LightFont(13.f, 40))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.ShadowOffset(FVector2D(1.f, 1.f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SAssignNew(SpeechText, STextBlock)
					.Text(Other && Me ? Other->GetGreeting(Me) : FText::GetEmpty())
					.Font(AedirUIStyle::LightFont(19.f))
					.ColorAndOpacity(AedirUIStyle::Ivory)
					.ShadowOffset(FVector2D(1.f, 1.f))
					.AutoWrapText(true)
					.Justification(ETextJustify::Center)
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 40.f, 90.f, 0.f)
		[
			SNew(SBox).WidthOverride(DialogueWidth)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
				.Padding(26.f, 18.f)
				[
					SAssignNew(TopicBox, SVerticalBox)
				]
			]
		]
	];

	RebuildTopics();
}

void SAedirDialogue::RebuildTopics()
{
	Topics.Reset();
	Rows.Reset();
	TopicBox->ClearChildren();
	if (const AAedirNpc* Other = Npc.Get(); Other && Player.IsValid())
	{
		Other->BuildTopics(Player.Get(), Topics);
	}
	Selected = FMath::Clamp(Selected, 0, FMath::Max(Topics.Num() - 1, 0));

	for (int32 Index = 0; Index < Topics.Num(); ++Index)
	{
		TSharedPtr<STextBlock> Row;
		TopicBox->AddSlot().AutoHeight().Padding(0.f, 5.f)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, Index]() { Choose(Index); return FReply::Handled(); })
			.OnHovered_Lambda([this, Index]() { Selected = Index; RefreshRows(); })
			[
				SAssignNew(Row, STextBlock)
				.Text(Topics[Index].Label)
				.Font(AedirUIStyle::RegularFont(17.f, 20))
				.ShadowOffset(FVector2D(1.f, 1.f))
				.AutoWrapText(true)
			]
		];
		Rows.Add(Row);
	}
	RefreshRows();
}

void SAedirDialogue::RefreshRows()
{
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Rows[Index]->SetColorAndOpacity(Index == Selected ? AedirUIStyle::IvoryBright : AedirUIStyle::TextDim);
	}
}

void SAedirDialogue::Choose(int32 Index)
{
	if (!Topics.IsValidIndex(Index) || bClosing)
	{
		return;
	}

	const FAedirDialogueTopic Topic = Topics[Index];
	SpeechText->SetText(Topic.Response);

	AAedirPlayerCharacter* Me = Player.Get();
	if (Me && Topic.Action != EAedirNpcAction::None && Npc.IsValid())
	{
		Me->RequestNpcAction(Npc.Get(), Topic.Action);
	}

	if (Topic.bEndsTalk)
	{
		bClosing = true;
		OnClose.ExecuteIfBound();
		return;
	}

	if (const AAedirNpc* Other = Npc.Get(); Other && Me)
	{
		StandingText->SetText(Other->GetStandingText(UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Me->GetController()))));
	}
	RebuildTopics();
}

void SAedirDialogue::ChooseTopicNamed(const FString& LabelPart)
{
	for (int32 Index = 0; Index < Topics.Num(); ++Index)
	{
		if (Topics[Index].Label.ToString().Contains(LabelPart))
		{
			Choose(Index);
			return;
		}
	}
}

FReply SAedirDialogue::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Tab)
	{
		bClosing = true;
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Topics.IsEmpty())
	{
		return FReply::Handled();
	}
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		Selected = (Selected + Topics.Num() - 1) % Topics.Num();
		RefreshRows();
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		Selected = (Selected + 1) % Topics.Num();
		RefreshRows();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E || Key == EKeys::SpaceBar)
	{
		Choose(Selected);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
