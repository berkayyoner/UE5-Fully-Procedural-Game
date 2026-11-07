#include "Npc/AedirNpc.h"

#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Net/AedirSaveSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Physics/AedirBalanceComponent.h"
#include "Player/AedirPlayerCharacter.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	constexpr float NpcFollowStopCm = 220.f;
	constexpr float NpcCatchUpSeconds = 5.f;
	constexpr float NpcCatchUpProgressCm = 40.f;
	constexpr float NpcFollowRunCm = 650.f;
	constexpr float NpcHomeStopCm = 40.f;
	constexpr float NpcTurnSpeed = 180.f;
	constexpr float NpcEatBelow = 35.f;
	constexpr float NpcMealValue = 45.f;
	constexpr int32 NpcComplimentGain = 4;
	constexpr int32 NpcThreatenLoss = 15;
	constexpr int32 NpcGiftGain = 10;
	constexpr int32 NpcAttackLoss = 35;
	constexpr int32 NpcHelpsFrom = 10;
	constexpr int32 NpcFollowsFrom = -20;
	constexpr float NpcComplimentSeconds = 120.f;
	constexpr float NpcHealOnHelp = 25.f;

	const TCHAR* const NpcRumours[] = {
		TEXT("At noon the moons cross and the light goes thin. The old folk still shut their doors until it passes."),
		TEXT("They say Sol's first children still stand in the deep woods, and that they lean towards whoever walks past."),
		TEXT("A trader swore the well water ran warm for three days. Cali's mark, he said. I drank it anyway."),
		TEXT("Bandits have taken to the high ground round here. They keep their backs to each other and their blades out."),
		TEXT("Nobody has seen an Anga, and everybody has an opinion about them. Cael has the most shrines and the fewest answers."),
		TEXT("Fod's miners dug too far under the eastern hills, or so it goes. Nobody I know has ever met one of them."),
		TEXT("Ans turns everything round in the end. That's what they tell you when your harvest fails.")
	};
}

AAedirNpc::AAedirNpc(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAedirNpc::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirNpc, Role_Npc);
	DOREPLIFETIME(AAedirNpc, NpcName);
	DOREPLIFETIME(AAedirNpc, Order);
	DOREPLIFETIME(AAedirNpc, Leader);
	DOREPLIFETIME(AAedirNpc, Relationships);
}

void AAedirNpc::BeginPlay()
{
	Super::BeginPlay();

	if (!bHasHome)
	{
		SetHome(GetActorLocation(), static_cast<float>(GetActorRotation().Yaw));
	}
}

void AAedirNpc::SetHome(const FVector& Location, float Yaw)
{
	HomeLocation = Location;
	HomeYaw = Yaw;
	bHasHome = true;
}

void AAedirNpc::InitNpc(EAedirNpcRole NewRole, const FText& NewName, const FText& NewBiography)
{
	Role_Npc = NewRole;
	NpcName = NewName;
	Biography = NewBiography;
	switch (NewRole)
	{
	case EAedirNpcRole::Enemy:
		GroupId = FName(TEXT("Bandits"));
		break;
	case EAedirNpcRole::Follower:
		GroupId = FName(TEXT("Heroes"));
		break;
	default:
		GroupId = FName(TEXT("Villagers"));
		break;
	}
	OnGroupIdChanged.Broadcast(GroupId);
}

void AAedirNpc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || !Stats || Stats->IsDead() || (Balance && Balance->IsRagdoll()))
	{
		return;
	}

	UpdateNeeds(DeltaSeconds);
	if (!Combat || !Combat->HasAITarget())
	{
		UpdateOrders(DeltaSeconds);
	}
}

void AAedirNpc::UpdateNeeds(float DeltaSeconds)
{
	// People carry their bread and their water, and see to them when things are quiet.
	if (Combat && Combat->HasAITarget())
	{
		return;
	}
	if (Stats->GetHunger() < NpcEatBelow)
	{
		Stats->AddFood(NpcMealValue, 0.f);
	}
	if (Stats->GetThirst() < NpcEatBelow)
	{
		Stats->AddFood(0.f, NpcMealValue);
	}
}

void AAedirNpc::FollowTo(const FVector& Where, float StopDistance, float RunDistance, float DeltaSeconds)
{
	// A follower that cannot find the way catches up out of sight, as companions do, instead of standing at a wall for ever.
	if (Order == EAedirNpcOrder::Following && IsValid(Leader))
	{
		const float ToLeader = static_cast<float>(FVector::Dist(GetActorLocation(), Leader->GetActorLocation()));
		if (ToLeader < BestLeaderDistance - NpcCatchUpProgressCm)
		{
			BestLeaderDistance = ToLeader;
			NoProgressSeconds = 0.f;
		}
		else if (ToLeader > NpcFollowStopCm * 2.f)
		{
			NoProgressSeconds += DeltaSeconds;
		}
		if (NoProgressSeconds >= NpcCatchUpSeconds)
		{
			const FVector Behind = Leader->GetActorLocation() - Leader->GetActorForwardVector() * NpcFollowStopCm;
			TeleportTo(Behind, Leader->GetActorRotation(), false, false);
			BestLeaderDistance = NpcFollowStopCm;
			NoProgressSeconds = 0.f;
		}
		BestLeaderDistance = FMath::Max(BestLeaderDistance, FMath::Min(ToLeader, BestLeaderDistance + NpcCatchUpProgressCm * DeltaSeconds));
	}

	const FVector ToGoal = (Where - GetActorLocation()) * FVector(1.0, 1.0, 0.0);
	const float Distance = static_cast<float>(ToGoal.Size());
	if (Distance <= StopDistance)
	{
		SetWantsToRun(false);
		return;
	}

	SetWantsToRun(Distance > RunDistance);
	WalkTowards(Where, DeltaSeconds, NpcTurnSpeed);
}

void AAedirNpc::UpdateOrders(float DeltaSeconds)
{
	switch (Order)
	{
	case EAedirNpcOrder::Following:
		if (IsValid(Leader))
		{
			FollowTo(Leader->GetActorLocation(), NpcFollowStopCm, NpcFollowRunCm, DeltaSeconds);
		}
		else
		{
			Order = EAedirNpcOrder::Routine;
		}
		break;

	case EAedirNpcOrder::Routine:
	{
		const float FromHome = static_cast<float>(FVector::Dist2D(GetActorLocation(), HomeLocation));
		if (FromHome > NpcHomeStopCm)
		{
			FollowTo(HomeLocation, NpcHomeStopCm, NpcFollowRunCm, DeltaSeconds);
		}
		else
		{
			TurnBodyTo(HomeYaw, DeltaSeconds, NpcTurnSpeed);
		}
		break;
	}

	default:
		break;
	}
}

int32 AAedirNpc::GetRelationship(const FString& PlayerKey) const
{
	const FAedirNpcRelationship* Found = Relationships.FindByPredicate([&PlayerKey](const FAedirNpcRelationship& Entry) { return Entry.PlayerKey == PlayerKey; });
	return Found ? Found->Value : 0;
}

void AAedirNpc::ChangeRelationship(const FString& PlayerKey, int32 Delta)
{
	FAedirNpcRelationship* Found = Relationships.FindByPredicate([&PlayerKey](const FAedirNpcRelationship& Entry) { return Entry.PlayerKey == PlayerKey; });
	if (!Found)
	{
		Found = &Relationships.AddDefaulted_GetRef();
		Found->PlayerKey = PlayerKey;
	}
	Found->Value = FMath::Clamp(Found->Value + Delta, -100, 100);
}

FText AAedirNpc::GetStandingText(const FString& PlayerKey) const
{
	const int32 Value = GetRelationship(PlayerKey);
	if (Value <= -40) { return NSLOCTEXT("AedirNpc", "Hated", "Hates you"); }
	if (Value < -5) { return NSLOCTEXT("AedirNpc", "Wary", "Wary of you"); }
	if (Value <= 5) { return NSLOCTEXT("AedirNpc", "Stranger", "Stranger"); }
	if (Value < 40) { return NSLOCTEXT("AedirNpc", "Friendly", "Friendly"); }
	return NSLOCTEXT("AedirNpc", "Friend", "Friend");
}

bool AAedirNpc::CanTalkTo(const AAedirCreature* Other) const
{
	return Other && Role_Npc != EAedirNpcRole::Enemy && Stats && !Stats->IsDead() && !(Balance && Balance->IsRagdoll()) && !IsEnemyOf(Other) && !(Combat && Combat->HasAITarget());
}

FText AAedirNpc::GetGreeting(const AAedirPlayerCharacter* Player) const
{
	const int32 Value = GetRelationship(UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Player->GetController())));
	if (Value <= -40) { return NSLOCTEXT("AedirNpc", "GreetHate", "You've got some nerve, showing your face to me."); }
	if (Value < -5) { return NSLOCTEXT("AedirNpc", "GreetWary", "What is it? Keep your hands where I can see them."); }
	if (Value >= 40) { return NSLOCTEXT("AedirNpc", "GreetFriend", "There you are. Good to see you in one piece."); }
	if (Value > 5) { return NSLOCTEXT("AedirNpc", "GreetFriendly", "Well met again. What do you need?"); }
	return NSLOCTEXT("AedirNpc", "GreetStranger", "Hm? Do I know you?");
}

FText AAedirNpc::PickRumour(const FString& PlayerKey) const
{
	// A different one on each asking, and each person starts somewhere of their own.
	static int32 Asked = 0;
	const int32 Index = (GetTypeHash(GetName()) + Asked++) % UE_ARRAY_COUNT(NpcRumours);
	return FText::FromString(NpcRumours[Index]);
}

void AAedirNpc::BuildTopics(const AAedirPlayerCharacter* Player, TArray<FAedirDialogueTopic>& OutTopics) const
{
	const FString Key = UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Player->GetController()));
	const int32 Standing = GetRelationship(Key);
	const auto Add = [&OutTopics](const FText& Label, const FText& Response, EAedirNpcAction Action = EAedirNpcAction::None, bool bEnds = false)
	{
		FAedirDialogueTopic& Topic = OutTopics.AddDefaulted_GetRef();
		Topic.Label = Label;
		Topic.Response = Response;
		Topic.Action = Action;
		Topic.bEndsTalk = bEnds;
	};

	Add(NSLOCTEXT("AedirNpc", "Rumours", "Rumours"), PickRumour(Key));
	Add(NSLOCTEXT("AedirNpc", "WhoAreYou", "Tell me about yourself."), Biography);

	// A follower answers to orders.
	if (Role_Npc == EAedirNpcRole::Follower)
	{
		if (Order != EAedirNpcOrder::Following)
		{
			Standing >= NpcFollowsFrom
				? Add(NSLOCTEXT("AedirNpc", "Follow", "Follow me."), NSLOCTEXT("AedirNpc", "FollowYes", "Lead on. I'll watch your back."), EAedirNpcAction::Follow, true)
				: Add(NSLOCTEXT("AedirNpc", "Follow", "Follow me."), NSLOCTEXT("AedirNpc", "FollowNo", "After what you did? Walk alone."));
		}
		if (Order == EAedirNpcOrder::Following)
		{
			Add(NSLOCTEXT("AedirNpc", "Stay", "Stay here."), NSLOCTEXT("AedirNpc", "StayYes", "Right. I won't move from this spot."), EAedirNpcAction::Stay, true);
		}
		if (Order != EAedirNpcOrder::Routine)
		{
			Add(NSLOCTEXT("AedirNpc", "Part", "We should part ways for now."), NSLOCTEXT("AedirNpc", "PartYes", "As you say. You know where to find me."), EAedirNpcAction::PartWays, true);
		}
	}

	// What else can be said depends on how things are.
	const UAedirStatsComponent* PlayerStats = Player->Stats.Get();
	if (PlayerStats && PlayerStats->GetHealth() < PlayerStats->GetMaxHealth() * 0.6f)
	{
		Standing >= NpcHelpsFrom
			? Add(NSLOCTEXT("AedirNpc", "Hurt", "I'm hurt. Can you help me?"), NSLOCTEXT("AedirNpc", "HurtYes", "Hold still. It's only rag and vinegar, but it'll keep the blood in you."), EAedirNpcAction::AskHelp)
			: Add(NSLOCTEXT("AedirNpc", "Hurt", "I'm hurt. Can you help me?"), NSLOCTEXT("AedirNpc", "HurtNo", "That's a bad one. I don't know you well enough to spend good cloth on it."));
	}
	if (PlayerStats && PlayerStats->GetHunger() < 30.f)
	{
		Add(NSLOCTEXT("AedirNpc", "Hungry", "Is there anything to eat around here?"), NSLOCTEXT("AedirNpc", "HungryAnswer", "You look it. There's little out here but what you carry. Look through what lies about, people leave things."));
	}
	if (PlayerStats && PlayerStats->GetThirst() < 30.f)
	{
		Add(NSLOCTEXT("AedirNpc", "Thirsty", "Where can I find water?"), NSLOCTEXT("AedirNpc", "ThirstyAnswer", "Jugs and carafes go round here, if you find one with something left in it. Drink before you're dizzy."));
	}

	const UAedirInteractionComponent* Hands = Player->Interaction.Get();
	const AAedirItemActor* Held = Hands ? (Hands->GetHeldItem(EAedirHand::Right) ? Hands->GetHeldItem(EAedirHand::Right) : Hands->GetHeldItem(EAedirHand::Left)) : nullptr;
	if (Held && Held->ItemClass == EAedirItemClass::Food)
	{
		const FText Label = FText::Format(NSLOCTEXT("AedirNpc", "Give", "Here, take this {0}."), Held->ItemName);
		Add(Label, Stats && (Stats->GetHunger() < 70.f || Stats->GetThirst() < 70.f)
			? NSLOCTEXT("AedirNpc", "GiveHungry", "You're a good sort. I won't forget it.")
			: NSLOCTEXT("AedirNpc", "GiveFull", "I'll keep it for later. That's kind of you."), EAedirNpcAction::GiveHeld);
	}
	if (Stats && Stats->GetHealth() < Stats->GetMaxHealth() * 0.5f)
	{
		Add(NSLOCTEXT("AedirNpc", "YouHurt", "You've taken a beating."), NSLOCTEXT("AedirNpc", "YouHurtAnswer", "You should see what I did to them. It'll mend, or it won't."));
	}
	if (Wounds && Wounds->IsBleeding())
	{
		Add(NSLOCTEXT("AedirNpc", "YouBleed", "You're bleeding."), NSLOCTEXT("AedirNpc", "YouBleedAnswer", "I know. Thanks for saying so."));
	}

	Add(NSLOCTEXT("AedirNpc", "Compliment", "You seem to know this place well."), NSLOCTEXT("AedirNpc", "ComplimentAnswer", "Better than most. Kind of you to notice."), EAedirNpcAction::Compliment);
	Add(NSLOCTEXT("AedirNpc", "Threaten", "Stay out of my way, or else."), NSLOCTEXT("AedirNpc", "ThreatenAnswer", "Is that so. I'll remember you said it."), EAedirNpcAction::Threaten);
	Add(NSLOCTEXT("AedirNpc", "Goodbye", "Goodbye."), NSLOCTEXT("AedirNpc", "GoodbyeAnswer", "Walk well."), EAedirNpcAction::None, true);
}

void AAedirNpc::PerformAction(AAedirPlayerCharacter* Player, EAedirNpcAction Action)
{
	if (!HasAuthority() || !Player || !CanTalkTo(Player))
	{
		return;
	}

	const FString Key = UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Player->GetController()));
	switch (Action)
	{
	case EAedirNpcAction::Follow:
		if (Role_Npc == EAedirNpcRole::Follower && GetRelationship(Key) >= NpcFollowsFrom)
		{
			Order = EAedirNpcOrder::Following;
			Leader = Player;
		}
		break;

	case EAedirNpcAction::Stay:
		if (Role_Npc == EAedirNpcRole::Follower)
		{
			Order = EAedirNpcOrder::Waiting;
		}
		break;

	case EAedirNpcAction::PartWays:
		if (Role_Npc == EAedirNpcRole::Follower)
		{
			Order = EAedirNpcOrder::Routine;
			Leader = nullptr;
		}
		break;

	case EAedirNpcAction::Compliment:
		if (GetWorld()->GetTimeSeconds() >= NextComplimentTime)
		{
			ChangeRelationship(Key, NpcComplimentGain);
			NextComplimentTime = GetWorld()->GetTimeSeconds() + NpcComplimentSeconds;
		}
		break;

	case EAedirNpcAction::Threaten:
		ChangeRelationship(Key, -NpcThreatenLoss);
		break;

	case EAedirNpcAction::GiveHeld:
		if (UAedirInteractionComponent* Hands = Player->Interaction.Get())
		{
			for (const EAedirHand Hand : { EAedirHand::Right, EAedirHand::Left })
			{
				AAedirItemActor* Held = Hands->GetHeldItem(Hand);
				if (Held && Held->ItemClass == EAedirItemClass::Food)
				{
					Held->Use(this);
					ChangeRelationship(Key, NpcGiftGain);
					// It is theirs now, whatever is left of it.
					if (IsValid(Held) && !Held->IsActorBeingDestroyed())
					{
						Hands->DropHeld(Hand);
						Held->Destroy();
					}
					break;
				}
			}
		}
		break;

	case EAedirNpcAction::AskHelp:
		if (GetRelationship(Key) >= NpcHelpsFrom && Player->Stats)
		{
			Player->Stats->Heal(NpcHealOnHelp);
			if (Player->Wounds)
			{
				Player->Wounds->StopBleedingSome();
			}
		}
		break;

	default:
		break;
	}
}

void AAedirNpc::OnAttackedBy(AAedirCreature* Attacker)
{
	if (!HasAuthority() || !Attacker)
	{
		return;
	}

	if (const APlayerController* AttackerController = Cast<APlayerController>(Attacker->GetController()))
	{
		ChangeRelationship(UAedirSaveSubsystem::MakePlayerKey(AttackerController), -NpcAttackLoss);
	}
	if (Combat)
	{
		Combat->SetAIGrudge(Attacker);
	}
}
