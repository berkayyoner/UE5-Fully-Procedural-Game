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
	constexpr float DialogueWidth = 560.f;
	constexpr float DialogueSpeechWidth = 1150.f;
	constexpr float DialogueRowHeight = 38.f;
	constexpr float DialogueListHeight = 380.f;
	constexpr float DialogueListRaise = 30.f;
	constexpr float DialogueRuleFadeCm = 90.f;
	constexpr int32 DialogueRuleFadeSteps = 10;
	constexpr float DialogueSubtitleBottom = 110.f;
	constexpr float DialogueSpeechFontSize = 23.f;
	constexpr float DialogueNameFontSize = 22.f;
	constexpr float DialogueTopicFontSize = 21.f;
}

void SAedirDialogue::Construct(const FArguments& InArgs)
{
	Npc = InArgs._Npc;
	Player = InArgs._Player;
	OnClose = InArgs._OnClose;

	const AAedirNpc* Other = Npc.Get();
	const AAedirPlayerCharacter* Me = Player.Get();
	const FString Key = Me ? UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Me->GetController())) : FString();

	// As in Skyrim: the speaker's name left of a thin upright rule, what can be said to its right with the chosen line kept at
	// the middle of the rule and marked by a small knot, the lines further from it fading away; what is said back stands as a
	// subtitle low in the middle of the screen. No boxes, only shadowed text over the world.
	const FLinearColor Rule = AedirUIStyle::LineColor;
	const auto RuleSegment = [&Rule](float Height, float Alpha)
	{
		return SNew(SBox).WidthOverride(1.f).HeightOverride(Height)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Rule.CopyWithNewOpacity(Rule.A * Alpha))
			];
	};
	TSharedRef<SVerticalBox> RuleColumn = SNew(SVerticalBox);
	for (int32 Step = 0; Step < DialogueRuleFadeSteps; ++Step)
	{
		RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueRuleFadeCm / DialogueRuleFadeSteps, (Step + 1.f) / DialogueRuleFadeSteps)];
	}
	RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueListHeight - 2.f * DialogueRuleFadeCm, 1.f)];
	for (int32 Step = DialogueRuleFadeSteps - 1; Step >= 0; --Step)
	{
		RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueRuleFadeCm / DialogueRuleFadeSteps, (Step + 1.f) / DialogueRuleFadeSteps)];
	}

	// The knot: a turned square in the rule's colour around a small bright one.
	const auto Diamond = [](float Size, const FLinearColor& Color)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size).RenderTransformPivot(FVector2D(0.5, 0.5))
			.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(45.f))))
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Color)
			];
	};
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.92f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, DialogueSubtitleBottom)
		[
			SNew(SBox).WidthOverride(DialogueSpeechWidth)
			[
				SAssignNew(SpeechText, STextBlock)
				.Text(Other && Me ? Other->GetGreeting(Me) : FText::GetEmpty())
				.Font(AedirUIStyle::LightFont(DialogueSpeechFontSize))
				.ColorAndOpacity(AedirUIStyle::IvoryBright)
				.ShadowOffset(FVector2D(2.f, 2.f))
				.ShadowColorAndOpacity(Shadow)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(0.f, DialogueListRaise * -2.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 22.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(Other ? Other->NpcName : FText::GetEmpty())
					.Font(AedirUIStyle::RegularFont(DialogueNameFontSize))
					.ColorAndOpacity(AedirUIStyle::IvoryBright)
					.ShadowOffset(FVector2D(2.f, 2.f))
					.ShadowColorAndOpacity(Shadow)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SAssignNew(StandingText, STextBlock)
					.Text(Other ? Other->GetStandingText(Key) : FText::GetEmpty())
					.Font(AedirUIStyle::LightFont(13.f, 40))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.ShadowOffset(FVector2D(1.f, 1.f))
					.ShadowColorAndOpacity(Shadow)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[RuleColumn]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Diamond(15.f, Rule)]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Diamond(11.f, FLinearColor(0.02f, 0.02f, 0.02f, 0.9f))]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[Diamond(6.f, AedirUIStyle::IvoryBright)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(DialogueWidth).HeightOverride(DialogueListHeight).Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(TAttribute<FMargin>::CreateLambda([this]()
					{
						// The list slides so the chosen line sits on the knot.
						return FMargin(0.f, DialogueListHeight * 0.5f - (Selected + 0.5f) * DialogueRowHeight, 0.f, 0.f);
					}))
					[
						SAssignNew(TopicBox, SVerticalBox)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(0.55f)
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
		TopicBox->AddSlot().AutoHeight()
		[
			SNew(SBox).HeightOverride(DialogueRowHeight).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, Index]() { Selected == Index ? Choose(Index) : (void)(Selected = Index, RefreshRows()); return FReply::Handled(); })
				[
					SAssignNew(Row, STextBlock)
					.Text(Topics[Index].Label)
					.Font(AedirUIStyle::LightFont(DialogueTopicFontSize))
					.ShadowOffset(FVector2D(2.f, 2.f))
					.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.92f))
				]
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
		// The chosen line is bright; the others dim the further they are from it.
		const float Away = static_cast<float>(FMath::Abs(Index - Selected));
		const FLinearColor Color = Index == Selected ? AedirUIStyle::IvoryBright : AedirUIStyle::TextDim;
		Rows[Index]->SetColorAndOpacity(Color.CopyWithNewOpacity(Index == Selected ? 1.f : FMath::Max(0.72f - 0.16f * (Away - 1.f), 0.18f)));
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
