#include "Npc/AedirNpc.h"

#include "Aedir.h"

#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/AedirPlayerCharacter.h"
#include "UI/AedirSubtitleSubsystem.h"

namespace
{
	constexpr float ChatterTauntMinSeconds = 7.f;
	constexpr float ChatterTauntMaxSeconds = 14.f;
	constexpr float ChatterSeeDot = 0.25f;

	// Remarks made in passing to whoever walks by, by how the speaker stands with the world.
	const TCHAR* const ChatterVillagerRemarks[] =
	{
		TEXT("Both moons up and the sun eaten at noon again. Never gets any less strange."),
		TEXT("Mind your step. Mud's deep after the rain."),
		TEXT("You're not from around here, are you?"),
		TEXT("If you're looking for trouble, look somewhere else."),
		TEXT("Sol keep the ground under your feet, traveller."),
		TEXT("Another sword-arm in town. Wonderful."),
		TEXT("They say the dwarves can hear the stone breathe. I can't even hear myself think."),
		TEXT("Don't stand there staring. Either buy something or move along."),
	};
	const TCHAR* const ChatterFollowerRemarks[] =
	{
		TEXT("Say the word and I'll draw steel."),
		TEXT("Quiet out here. Too quiet, if you ask me."),
		TEXT("My feet hurt. Don't tell anyone I said that."),
		TEXT("I'm with you. Wherever this goes."),
		TEXT("If we live through the day, the first drink is on you."),
	};
	const TCHAR* const ChatterFarewells[] =
	{
		TEXT("Well, I'll leave you to it."),
		TEXT("Take care of yourself."),
		TEXT("Gods keep you."),
		TEXT("Back to work, then."),
		TEXT("Mind how you go."),
		TEXT("Till later."),
	};

	const TCHAR* const ChatterEnemyTaunts[] =
	{
		TEXT("Come on then! Come on!"),
		TEXT("I'll feed you to the crows!"),
		TEXT("You'll bleed for that!"),
		TEXT("Is that all you've got?"),
		TEXT("Run while you still have legs!"),
		TEXT("Cali take you!"),
	};

	// Conversations between two people who stop to talk, the first one speaking first.
	struct FChatterTalk
	{
		TArray<const TCHAR*> Lines;
	};
	const TArray<FChatterTalk>& ChatterTalks()
	{
		static const TArray<FChatterTalk> Talks =
		{
			{ { TEXT("Did you see the eclipse today? The sky went dark as a cellar."),
				TEXT("Same as every day. Two moons, one sun, and the noon goes black."),
				TEXT("My grandmother said it's Ans turning the wheel. Keeping everything going round."),
				TEXT("Your grandmother also said the river talked to her."),
				TEXT("Maybe it did. Cael's waters remember things, they say."),
				TEXT("Then I hope they forget what I dumped in them last night.") } },
			{ { TEXT("Heard the dwarves sealed another gate in the mountains."),
				TEXT("Hiding again? From what this time?"),
				TEXT("Nobody knows. They say they dug too deep and found something Fod buried on purpose."),
				TEXT("Gold, I'd wager. It's always gold with them."),
				TEXT("Not gold. Something that looked back.") } },
			{ { TEXT("Bandits on the north road again."),
				TEXT("Again? The guard was supposed to clear them out."),
				TEXT("The guard was paid to clear them out. That's not the same thing."),
				TEXT("Then I'm taking the long way. My cart can't outrun an arrow.") } },
			{ { TEXT("Do you ever wonder what the Angas were thinking? Making all of us?"),
				TEXT("I wonder what they were thinking making turnips. Mine won't grow."),
				TEXT("I'm serious. The elves say we were the last try. The one that got free will."),
				TEXT("Then I freely choose to stop talking about gods and go eat."),
				TEXT("Fair enough.") } },
			{ { TEXT("That dark elf at the inn, did he say where he came from?"),
				TEXT("From under the hills. Barely opens his eyes in daylight."),
				TEXT("They hear everything down there, you know. Every whisper."),
				TEXT("Then lower your voice.") } },
			{ { TEXT("Rain's coming. You can smell it."),
				TEXT("Good. The fields need it."),
				TEXT("The fields, yes. My roof, no."),
				TEXT("Still haven't fixed it?"),
				TEXT("Hok blows the thatch off every time I do.") } },
			{ { TEXT("Watch that one. Came in with a sword nicked from end to end."),
				TEXT("A sword like that has seen use."),
				TEXT("Exactly my point."),
				TEXT("I'll keep my door barred tonight.") } },
		};
		return Talks;
	}

	template <int32 N>
	FText ChatterPick(const TCHAR* const (&Lines)[N])
	{
		return FText::FromString(Lines[FMath::RandRange(0, N - 1)]);
	}
}

bool AAedirNpc::IsFreeToChat() const
{
	return Stats && !Stats->IsDead() && !Stats->IsDying() && !TalkPartner.IsValid() && !(Combat && Combat->HasAITarget())
		&& Order == EAedirNpcOrder::Routine && !FetchItem && Role_Npc != EAedirNpcRole::Enemy;
}

void AAedirNpc::SayLine(const FText& Line)
{
	UAedirSubtitleSubsystem* Subtitles = GetWorld() ? GetWorld()->GetSubsystem<UAedirSubtitleSubsystem>() : nullptr;
	if (!Subtitles)
	{
		return;
	}
	const float Seconds = UAedirSubtitleSubsystem::GetSpeakSeconds(Line);
	Subtitles->Say(NpcName, Line, GetActorLocation(), Seconds);
	UE_LOG(LogAedir, Log, TEXT("%s says: %s"), *NpcName.ToString(), *Line.ToString());
	Speak(Line);
	LineSeconds = Seconds;
}

void AAedirNpc::UpdateChatter(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !Stats || Stats->IsDead() || Stats->IsDying())
	{
		return;
	}
	LineSeconds = FMath::Max(LineSeconds - DeltaSeconds, 0.f);
	RemarkTimer -= DeltaSeconds;
	TalkTimer -= DeltaSeconds;

	// In a fight an enemy shouts at whoever it fights.
	if (Combat && Combat->HasAITarget())
	{
		if (Role_Npc == EAedirNpcRole::Enemy && RemarkTimer <= 0.f && LineSeconds <= 0.f)
		{
			RemarkTimer = FMath::FRandRange(ChatterTauntMinSeconds, ChatterTauntMaxSeconds);
			SayLine(ChatterPick(ChatterEnemyTaunts));
		}
		return;
	}

	// Leading a talk with another: each line in turn, a short pause between them.
	if (AAedirNpc* Other = ChatPartner.Get())
	{
		const bool bStillTalking = TalkPartner.Get() == Other && Other->TalkPartner.Get() == this && Other->IsAlive() && FVector::Dist(Other->GetActorLocation(), GetActorLocation()) <= ChatterTalkLeaveCm;
		if (!bStillTalking || !ChatterTalks().IsValidIndex(ChatTalk))
		{
			EndChat();
			return;
		}
		ChatWait -= DeltaSeconds;
		if (ChatWait > 0.f)
		{
			return;
		}
		const TArray<const TCHAR*>& Lines = ChatterTalks()[ChatTalk].Lines;
		// When the talk is done each says goodbye, the one who did not speak last first, and only then do they go their ways.
		const int32 Farewell = ChatLine - Lines.Num();
		if (Farewell >= 2)
		{
			EndChat();
			return;
		}
		AAedirNpc* Speaker = ChatLine % 2 == 0 ? this : Other;
		Speaker->SayLine(Farewell >= 0 ? ChatterPick(ChatterFarewells) : FText::FromString(Lines[ChatLine]));
		ChatWait = Speaker->LineSeconds + ChatterPauseSeconds;
		++ChatLine;
		return;
	}

	if (!IsFreeToChat())
	{
		return;
	}

	// Somebody passing close by, looking this way, gets a word.
	if (RemarkTimer <= 0.f)
	{
		RemarkTimer = FMath::FRandRange(ChatterRemarkMinSeconds, ChatterRemarkMaxSeconds);
		const APlayerController* Viewer = World->GetFirstPlayerController();
		const APawn* Player = Viewer ? Viewer->GetPawn() : nullptr;
		if (Player && FVector::Dist(Player->GetActorLocation(), GetActorLocation()) <= ChatterRemarkRangeCm
			&& FVector::DotProduct((Player->GetActorLocation() - GetActorLocation()).GetSafeNormal2D(), GetActorForwardVector()) > ChatterSeeDot)
		{
			SayLine(Role_Npc == EAedirNpcRole::Follower ? ChatterPick(ChatterFollowerRemarks) : ChatterPick(ChatterVillagerRemarks));
			return;
		}
	}

	// Two idle people near each other stop and talk for a while.
	if (TalkTimer <= 0.f)
	{
		TalkTimer = FMath::FRandRange(ChatterTalkMinSeconds, ChatterTalkMaxSeconds);
		for (TActorIterator<AAedirNpc> It(World); It; ++It)
		{
			AAedirNpc* Other = *It;
			if (Other != this && Other->IsFreeToChat() && !Other->ChatPartner.IsValid() && FVector::Dist(Other->GetActorLocation(), GetActorLocation()) <= ChatterTalkRangeCm)
			{
				ChatPartner = Other;
				ChatTalk = FMath::RandRange(0, ChatterTalks().Num() - 1);
				ChatLine = 0;
				ChatWait = 0.f;
				BeginConversation(Other);
				Other->BeginConversation(this);
				Other->TalkTimer = TalkTimer;
				break;
			}
		}
	}
}

void AAedirNpc::EndChat()
{
	if (AAedirNpc* Other = ChatPartner.Get())
	{
		if (Other->TalkPartner.Get() == this)
		{
			Other->EndConversation();
		}
	}
	if (TalkPartner.Get() == ChatPartner.Get())
	{
		EndConversation();
	}
	ChatPartner.Reset();
}

void AAedirNpc::RemarkOnBlow()
{
	// A blow out of nowhere gets an answer at once.
	static const TCHAR* const Outrage[] =
	{
		TEXT("What do you think you're doing?!"),
		TEXT("Are you mad?!"),
		TEXT("Guards! Guards!"),
		TEXT("You'll regret that!"),
	};
	if (Role_Npc != EAedirNpcRole::Enemy && LineSeconds <= 0.f)
	{
		SayLine(ChatterPick(Outrage));
		RemarkTimer = ChatterRemarkMinSeconds;
	}
}

bool AAedirNpc::IsAlive() const
{
	return Stats && !Stats->IsDead() && !Stats->IsDying();
}
