#pragma once

#include "CoreMinimal.h"
#include "Npc/AedirNpc.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AAedirPlayerCharacter;
class SVerticalBox;
class STextBlock;

// Talking: what the other person last said across the top, what you can say down the right side.
class AEDIR_API SAedirDialogue : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirDialogue) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AAedirNpc>, Npc)
		SLATE_ARGUMENT(TWeakObjectPtr<AAedirPlayerCharacter>, Player)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	// A test can say something without a keyboard.
	void ChooseTopicNamed(const FString& LabelPart);

private:
	void RebuildTopics();
	void Choose(int32 Index);
	void RefreshRows();

	TWeakObjectPtr<AAedirNpc> Npc;
	TWeakObjectPtr<AAedirPlayerCharacter> Player;
	FSimpleDelegate OnClose;
	TArray<FAedirDialogueTopic> Topics;
	TArray<TSharedPtr<STextBlock>> Rows;
	TSharedPtr<SVerticalBox> TopicBox;
	TSharedPtr<STextBlock> SpeechText;
	TSharedPtr<STextBlock> StandingText;
	int32 Selected = 0;
	bool bClosing = false;
};
