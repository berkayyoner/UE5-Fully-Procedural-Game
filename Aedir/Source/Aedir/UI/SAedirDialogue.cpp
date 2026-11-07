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
	constexpr float DialogueWidth = 460.f;
	constexpr float DialogueSpeechWidth = 1000.f;
}

void SAedirDialogue::Construct(const FArguments& InArgs)
{
	Npc = InArgs._Npc;
	Player = InArgs._Player;
	OnClose = InArgs._OnClose;

	const AAedirNpc* Other = Npc.Get();
	const AAedirPlayerCharacter* Me = Player.Get();
	const FString Key = Me ? UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Me->GetController())) : FString();

	// As in Skyrim: what the other says stands as a subtitle low in the middle, the topics to the right of centre between two
	// thin gold rules, the selected one bright and the rest dim.
	const auto Rule = []()
	{
		return SNew(SBox).HeightOverride(1.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(AedirUIStyle::LineColor)
			];
	};

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 120.f)
		[
			SNew(SBox).WidthOverride(DialogueSpeechWidth)
			[
				SAssignNew(SpeechText, STextBlock)
				.Text(Other && Me ? Other->GetGreeting(Me) : FText::GetEmpty())
				.Font(AedirUIStyle::LightFont(19.f))
				.ColorAndOpacity(AedirUIStyle::IvoryBright)
				.ShadowOffset(FVector2D(1.5f, 1.5f))
				.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f))
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 140.f, 0.f)
		[
			SNew(SBox).WidthOverride(DialogueWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.Text(Other ? Other->NpcName : FText::GetEmpty())
					.Font(AedirUIStyle::RegularFont(15.f, 120))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.ShadowOffset(FVector2D(1.f, 1.f))
				]
				+ SVerticalBox::Slot().AutoHeight()[Rule()]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
					.Padding(22.f, 14.f)
					[
						SAssignNew(TopicBox, SVerticalBox)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[Rule()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SAssignNew(StandingText, STextBlock)
					.Text(Other ? Other->GetStandingText(Key) : FText::GetEmpty())
					.Font(AedirUIStyle::LightFont(12.f, 40))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.ShadowOffset(FVector2D(1.f, 1.f))
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
				.Font(AedirUIStyle::RegularFont(18.f, 20))
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
	if (AAedirNpc* Speaker = Npc.Get())
	{
		Speaker->Speak(Topic.Response);
	}

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
