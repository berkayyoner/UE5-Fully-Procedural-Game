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
	// Sizes are Slate units, one pixel at 1080p. Skyrim keeps its dialogue small: a compact column of lines right of the middle
	// and a short subtitle low in the middle, both in a light condensed face.
	constexpr float DialogueWidth = 440.f;
	constexpr float DialogueSpeechWidth = 900.f;
	constexpr float DialogueRowHeight = 30.f;
	constexpr int32 DialogueRowsShown = 10;
	constexpr float DialogueListHeight = DialogueRowHeight * DialogueRowsShown;
	constexpr float DialogueListRaise = 30.f;
	constexpr float DialogueListGap = 20.f;
	constexpr float DialogueNameGap = 22.f;
	// Free width left and right of the column: the column stands a little right of the middle of the screen.
	constexpr float DialogueLeftFill = 1.f;
	constexpr float DialogueRightFill = 0.55f;
	constexpr float DialogueRuleFade = 70.f;
	constexpr int32 DialogueRuleFadeSteps = 10;
	constexpr float DialogueSubtitleBottom = 120.f;
	constexpr float DialogueSpeechFontSize = 15.f;
	constexpr float DialogueNameFontSize = 16.f;
	constexpr float DialogueStandingFontSize = 10.f;
	constexpr float DialogueTopicFontSize = 16.f;

	// Lines asked of a person this session, by person and line.
	TSet<FString>& DialogueHeardTopics()
	{
		static TSet<FString> Heard;
		return Heard;
	}
}

void SAedirDialogue::Construct(const FArguments& InArgs)
{
	Npc = InArgs._Npc;
	Player = InArgs._Player;
	OnClose = InArgs._OnClose;

	const AAedirNpc* Other = Npc.Get();
	const AAedirPlayerCharacter* Me = Player.Get();
	const FString Key = Me ? UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Me->GetController())) : FString();

	// As in Skyrim: the speaker's name left of a thin upright rule, what can be said to its right, what is said back as a subtitle
	// low in the middle of the screen. No boxes: a soft darkness behind the lines keeps them readable over any background.
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
		RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueRuleFade / DialogueRuleFadeSteps, (Step + 1.f) / DialogueRuleFadeSteps)];
	}
	RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueListHeight - 2.f * DialogueRuleFade, 1.f)];
	for (int32 Step = DialogueRuleFadeSteps - 1; Step >= 0; --Step)
	{
		RuleColumn->AddSlot().AutoHeight()[RuleSegment(DialogueRuleFade / DialogueRuleFadeSteps, (Step + 1.f) / DialogueRuleFadeSteps)];
	}

	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.92f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// The darkening: the screen edges a little, and a soft dark cloud behind the column and behind the subtitle.
			SNew(SAedirUIPaint)
			.Painter([](const FVector2f& Size, FAedirUIMesh& Mesh)
			{
				const float RuleX = FMath::Max(0.f, Size.X - DialogueListGap - 1.f - DialogueWidth) * DialogueLeftFill / (DialogueLeftFill + DialogueRightFill);
				const FVector2f ListCenter(RuleX + DialogueWidth * 0.28f, Size.Y * 0.5f - DialogueListRaise);
				AedirUIStyle::AddVignette(Mesh, Size, 0.12f, 0.6f);
				AedirUIStyle::AddGlow(Mesh, ListCenter, FVector2f(DialogueWidth * 1.35f, DialogueListHeight * 1.25f), FLinearColor(0.f, 0.f, 0.f, 0.85f));
				AedirUIStyle::AddGlow(Mesh, FVector2f(Size.X * 0.5f, Size.Y - DialogueSubtitleBottom - 12.f), FVector2f(DialogueSpeechWidth * 0.7f, 90.f), FLinearColor(0.f, 0.f, 0.f, 0.72f));
			})
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, DialogueSubtitleBottom)
		[
			SNew(SBox).WidthOverride(DialogueSpeechWidth)
			[
				SAssignNew(SpeechText, STextBlock)
				.Text(Other && Me ? Other->GetGreeting(Me) : FText::GetEmpty())
				.Font(AedirUIStyle::LightFont(DialogueSpeechFontSize))
				.ColorAndOpacity(AedirUIStyle::IvoryBright)
				.ShadowOffset(FVector2D(1.5f, 1.5f))
				.ShadowColorAndOpacity(Shadow)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(0.f, DialogueListRaise * -2.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(DialogueLeftFill).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, DialogueNameGap, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Text(Other ? Other->NpcName : FText::GetEmpty())
					.Font(AedirUIStyle::RegularFont(DialogueNameFontSize))
					.ColorAndOpacity(AedirUIStyle::IvoryBright)
					.ShadowOffset(FVector2D(1.5f, 1.5f))
					.ShadowColorAndOpacity(Shadow)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SAssignNew(StandingText, STextBlock)
					.Text(Other ? Other->GetStandingText(Key) : FText::GetEmpty())
					.Font(AedirUIStyle::LightFont(DialogueStandingFontSize, 40))
					.ColorAndOpacity(AedirUIStyle::TextDim)
					.ShadowOffset(FVector2D(1.f, 1.f))
					.ShadowColorAndOpacity(Shadow)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				RuleColumn
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(DialogueListGap, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(DialogueWidth).HeightOverride(DialogueListHeight).Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(TAttribute<FMargin>::CreateLambda([this]()
					{
						// A short list stands in the middle of the rule; a long one shows the part around the chosen line.
						const float Content = Topics.Num() * DialogueRowHeight;
						return FMargin(0.f, Content <= DialogueListHeight ? (DialogueListHeight - Content) * 0.5f : -FirstVisible * DialogueRowHeight, 0.f, 0.f);
					}))
					[
						SAssignNew(TopicBox, SVerticalBox)
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(DialogueRightFill)
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
				// The line under the mouse is the chosen one, a click says it, as in Skyrim.
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.IsFocusable(false)
				.ContentPadding(FMargin(0.f))
				.OnHovered_Lambda([this, Index]() { Select(Index); })
				.OnClicked_Lambda([this, Index]() { Choose(Index); return FReply::Handled(); })
				[
					SAssignNew(Row, STextBlock)
					.Text(Topics[Index].Label)
					.Font(AedirUIStyle::LightFont(DialogueTopicFontSize))
					.ShadowOffset(FVector2D(1.5f, 1.5f))
					.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.92f))
				]
			]
		];
		Rows.Add(Row);
	}
	RefreshRows();
}

void SAedirDialogue::Select(int32 Index)
{
	if (Topics.IsValidIndex(Index) && Index != Selected)
	{
		Selected = Index;
		RefreshRows();
	}
}

void SAedirDialogue::RefreshRows()
{
	if (Topics.Num() <= DialogueRowsShown)
	{
		FirstVisible = 0;
	}
	else
	{
		FirstVisible = FMath::Clamp(FirstVisible, Selected - DialogueRowsShown + 1, Selected);
		FirstVisible = FMath::Clamp(FirstVisible, 0, Topics.Num() - DialogueRowsShown);
	}

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		// The chosen line is bold white, a line not yet asked is thin white, one already answered is thin grey.
		const bool bSelected = Index == Selected;
		Rows[Index]->SetFont(bSelected ? AedirUIStyle::BoldFont(DialogueTopicFontSize) : AedirUIStyle::LightFont(DialogueTopicFontSize));
		const FLinearColor Color = bSelected ? AedirUIStyle::IvoryBright : IsHeard(Index) ? AedirUIStyle::TextDim : AedirUIStyle::WithAlpha(AedirUIStyle::IvoryBright, 0.92f);
		Rows[Index]->SetColorAndOpacity(Color);
	}
}

FString SAedirDialogue::GetHeardKey(const FAedirDialogueTopic& Topic) const
{
	const AAedirNpc* Other = Npc.Get();
	return (Other ? Other->GetPathName() : FString()) + TEXT("|") + Topic.Label.ToString();
}

bool SAedirDialogue::IsHeard(int32 Index) const
{
	return Topics.IsValidIndex(Index) && DialogueHeardTopics().Contains(GetHeardKey(Topics[Index]));
}

void SAedirDialogue::Choose(int32 Index)
{
	if (!Topics.IsValidIndex(Index) || bClosing)
	{
		return;
	}

	const FAedirDialogueTopic Topic = Topics[Index];
	DialogueHeardTopics().Add(GetHeardKey(Topic));
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
		Select((Selected + Topics.Num() - 1) % Topics.Num());
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		Select((Selected + 1) % Topics.Num());
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E || Key == EKeys::SpaceBar)
	{
		Choose(Selected);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
