#include "Npc/AedirNpc.h"

#include "Aedir.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Creature/AedirGroupSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Items/AedirItemCatalog.h"
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
	// How far off the friends of one who is attacked come to their help.
	constexpr float NpcRallyRangeCm = 2500.f;
	constexpr int32 NpcHelpsFrom = 10;
	constexpr int32 NpcFollowsFrom = -20;
	constexpr float NpcComplimentSeconds = 120.f;
	constexpr float NpcHealOnHelp = 25.f;
	constexpr float NpcSheatheCalmSeconds = 4.f;
	constexpr float NpcSpeakSecondsPerLetter = 0.055f;
	constexpr float NpcSpeakMinSeconds = 1.2f;
	constexpr float NpcSpeakMaxSeconds = 9.f;
	constexpr float NpcTalkTurnSpeed = 200.f;
	constexpr float NpcItemSearchSeconds = 1.f;
	constexpr float NpcItemSightCm = 1500.f;
	constexpr float NpcItemSightCosine = 0.15f;
	constexpr float NpcItemNearbyCm = 200.f;
	constexpr double NpcItemGiveUpSeconds = 60.0;
	constexpr float NpcFetchReachCm = 70.f;
	constexpr float NpcGoToStopCm = 80.f;
	constexpr float NpcWayStuckSeconds = 8.f;
	constexpr float NpcWayRoundShare = 0.2f;
	constexpr float NpcWayProgressCm = 40.f;
	constexpr int32 NpcFetchReachTries = 2;

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

	// Placed in a level with a name: who they are and what they carry come from what was set on them there.
	if (HasAuthority() && !NpcName.IsEmpty())
	{
		InitNpc(Role_Npc, NpcName, Biography);
		if (!StartWeapon.IsNone() && Interaction)
		{
			if (AAedirCatalogItem* Weapon = AAedirCatalogItem::SpawnCatalogItem(GetWorld(), StartWeapon, FTransform(GetActorLocation() + FVector(0.f, 0.f, 80.f))))
			{
				if (!Interaction->HolsterImmediate(0, Weapon))
				{
					Interaction->EquipImmediate(EAedirHand::Right, Weapon);
				}
			}
		}
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

	if (!HasAuthority() || !Stats || Stats->IsDead() || Stats->IsDying() || (Balance && Balance->IsRagdoll()))
	{
		return;
	}

	UpdateNeeds(DeltaSeconds);
	UpdateChatter(DeltaSeconds);
	if (TalkPartner.IsValid())
	{
		UpdateConversation(DeltaSeconds);
		return;
	}
	UpdateWeaponReadiness(DeltaSeconds);
	if (Combat && Combat->HasAITarget())
	{
		return;
	}

	UpdateItemSearch(DeltaSeconds);
	if (FetchItem)
	{
		UpdateFetch(DeltaSeconds);
	}
	else
	{
		UpdateOrders(DeltaSeconds);
	}
}

void AAedirNpc::BeginConversation(AAedirCreature* With)
{
	TalkPartner = With;
	TalkSeconds = 0.f;
	SpeakSeconds = 0.f;
	MoveLocal(FVector2D::ZeroVector);
}

void AAedirNpc::EndConversation()
{
	TalkPartner.Reset();
	SpeakSeconds = 0.f;
}

void AAedirNpc::Speak(const FText& Line)
{
	// As long as the words take to say, at a calm pace.
	SpeakSeconds = FMath::Clamp(Line.ToString().Len() * NpcSpeakSecondsPerLetter, NpcSpeakMinSeconds, NpcSpeakMaxSeconds);
}

void AAedirNpc::UpdateConversation(float DeltaSeconds)
{
	const AAedirCreature* Partner = TalkPartner.Get();
	TalkSeconds += DeltaSeconds;
	SpeakSeconds = FMath::Max(SpeakSeconds - DeltaSeconds, 0.f);
	MoveLocal(FVector2D::ZeroVector);

	// The whole body comes round to the one it talks to; the eyes meet theirs.
	const FVector Eyes = GetPawnViewLocation();
	const FVector TheirEyes = Partner->GetPawnViewLocation();
	const FRotator Look = (TheirEyes - Eyes).Rotation();
	TurnBodyTo(static_cast<float>(Look.Yaw), DeltaSeconds, NpcTalkTurnSpeed);

	// A living head is never still: it drifts slowly while listening, and nods and turns with the words while speaking.
	const float Speaking = FMath::Clamp(SpeakSeconds * 2.f, 0.f, 1.f);
	const float T = TalkSeconds;
	const float DriftYaw = 2.5f * FMath::Sin(T * 0.53f) + 1.5f * FMath::Sin(T * 1.31f + 1.f);
	const float DriftPitch = 1.5f * FMath::Sin(T * 0.71f + 2.f);
	const float WordsPitch = Speaking * (3.5f * FMath::Sin(T * 6.3f) + 2.f * FMath::Sin(T * 2.9f + 0.5f));
	const float WordsYaw = Speaking * 3.f * FMath::Sin(T * 2.2f + 1.7f);
	if (AController* Mind = GetController())
	{
		Mind->SetControlRotation(FRotator(Look.Pitch + DriftPitch + WordsPitch, Look.Yaw + DriftYaw + WordsYaw, 0.f));
	}
}

void AAedirNpc::UpdateWeaponReadiness(float DeltaSeconds)
{
	if (!Interaction || Interaction->IsHolsterMoving())
	{
		return;
	}

	// A blade comes out at the first sign of trouble, and goes back to the belt when things have been quiet for a while.
	AAedirItemActor* Left = Interaction->GetHeldItem(EAedirHand::Left);
	AAedirItemActor* Right = Interaction->GetHeldItem(EAedirHand::Right);
	if (Combat && Combat->HasAITarget())
	{
		CalmSeconds = 0.f;
		const bool bArmed = (Left && Left->ItemClass == EAedirItemClass::Weapon) || (Right && Right->ItemClass == EAedirItemClass::Weapon);
		const int32 Slot = bArmed ? INDEX_NONE : Interaction->FindWaistWeaponSlot();
		if (Slot != INDEX_NONE && Interaction->CanDrawWaistSlot(EAedirHand::Right, Slot))
		{
			Interaction->DrawWaistSlot(EAedirHand::Right, Slot);
		}
		return;
	}

	CalmSeconds += DeltaSeconds;
	if (CalmSeconds < NpcSheatheCalmSeconds)
	{
		return;
	}
	if (Right && Right->ItemClass == EAedirItemClass::Weapon && Right->FitsOnBelt())
	{
		Interaction->HolsterToWaist(EAedirHand::Right);
	}
	else if (Left && Left->ItemClass == EAedirItemClass::Weapon && Left->FitsOnBelt())
	{
		Interaction->HolsterToWaist(EAedirHand::Left);
	}
}

bool AAedirNpc::HasWeapon() const
{
	if (!Interaction)
	{
		return false;
	}
	const AAedirItemActor* Left = Interaction->GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* Right = Interaction->GetHeldItem(EAedirHand::Right);
	return (Left && Left->ItemClass == EAedirItemClass::Weapon) || (Right && Right->ItemClass == EAedirItemClass::Weapon) || Interaction->FindWaistWeaponSlot() != INDEX_NONE;
}

bool AAedirNpc::LacksItem(const AAedirItemActor* Item) const
{
	if (!Item || !Interaction)
	{
		return false;
	}
	if (Item->ItemClass == EAedirItemClass::Weapon)
	{
		return !HasWeapon();
	}
	return Item->ItemClass == EAedirItemClass::Armor && Interaction->GetWornArmorProtection(Item->ArmorZone) <= 0.f;
}

bool AAedirNpc::CanSeeItem(const AAedirItemActor* Item) const
{
	const FVector Center = Item->GetBoundsCenterWorld();
	const FVector ToItem = Center - GetActorLocation();
	if (ToItem.SizeSquared2D() > FMath::Square(NpcItemNearbyCm) && FVector::DotProduct(ToItem.GetSafeNormal2D(), GetActorForwardVector()) < NpcItemSightCosine)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirNpcItemSight), false, this);
	Params.AddIgnoredActor(Item);
	return !GetWorld()->LineTraceTestByChannel(GetPawnViewLocation(), Center, ECC_Visibility, Params);
}

void AAedirNpc::UpdateItemSearch(float DeltaSeconds)
{
	// A person without a weapon or with a bare head picks up what they see lying about, unless told to stay put or already going for something.
	ItemSearchTimer -= DeltaSeconds;
	if (ItemSearchTimer > 0.f || FetchItem || !Interaction || (Order != EAedirNpcOrder::Routine && Order != EAedirNpcOrder::Following))
	{
		return;
	}
	ItemSearchTimer = NpcItemSearchSeconds;

	const double Now = GetWorld()->GetTimeSeconds();
	AAedirItemActor* Best = nullptr;
	double BestDistance = FMath::Square(static_cast<double>(NpcItemSightCm));
	for (TActorIterator<AAedirItemActor> It(GetWorld()); It; ++It)
	{
		AAedirItemActor* Item = *It;
		const double* GivenUpAt = ItemsGivenUp.Find(Item);
		const double Distance = FVector::DistSquared(Item->GetActorLocation(), GetActorLocation());
		if (Distance < BestDistance && AAedirItemActor::IsPickable(Item) && LacksItem(Item) && (!GivenUpAt || Now > *GivenUpAt + NpcItemGiveUpSeconds) && CanSeeItem(Item))
		{
			Best = Item;
			BestDistance = Distance;
		}
	}
	if (Best)
	{
		FetchItem = Best;
		bFetchOrdered = false;
		FetchReaches = 0;
		BestWayDistance = BIG_NUMBER;
		WayStuckSeconds = 0.f;
	}
}

bool AAedirNpc::IsStuckOnTheWay(float DistanceLeft, float DeltaSeconds)
{
	if (DistanceLeft < BestWayDistance - NpcWayProgressCm)
	{
		BestWayDistance = DistanceLeft;
		WayStuckSeconds = 0.f;
		return false;
	}
	WayStuckSeconds += DeltaSeconds * (IsFollowingWay() ? NpcWayRoundShare : 1.f);
	return WayStuckSeconds >= NpcWayStuckSeconds;
}

void AAedirNpc::UpdateFetch(float DeltaSeconds)
{
	AAedirItemActor* Item = FetchItem.Get();
	if (!IsValid(Item) || !Interaction)
	{
		GiveUpFetch();
		return;
	}
	if (Item->Holder == this)
	{
		FinishFetch(Item);
		return;
	}
	if (!AAedirItemActor::IsPickable(Item))
	{
		GiveUpFetch();
		return;
	}

	const FVector ItemLocation = Item->GetBoundsCenterWorld();
	const float Distance = static_cast<float>(FVector::Dist2D(ItemLocation, GetActorLocation()));
	if (Distance > NpcFetchReachCm)
	{
		if (IsStuckOnTheWay(Distance, DeltaSeconds))
		{
			// Sent there by someone, they find a way round the way a follower catches up. Left to themselves they forget about it.
			if (!bFetchOrdered)
			{
				GiveUpFetch();
				return;
			}
			TeleportTo(ItemLocation + FVector(0.0, 0.0, GetSimpleCollisionHalfHeight()), GetActorRotation(), false, false);
			BestWayDistance = BIG_NUMBER;
			WayStuckSeconds = 0.f;
			return;
		}
		SetWantsToRun(Distance > NpcFollowRunCm);
		WalkTowards(ItemLocation, DeltaSeconds, NpcTurnSpeed);
		return;
	}

	SetWantsToRun(false);
	TurnBodyTo(static_cast<float>((ItemLocation - GetActorLocation()).Rotation().Yaw), DeltaSeconds, NpcTurnSpeed);
	if (Interaction->IsHolsterMoving() || Interaction->GetHandState(EAedirHand::Left) == EAedirHandState::Reaching || Interaction->GetHandState(EAedirHand::Right) == EAedirHandState::Reaching)
	{
		return;
	}

	// Hands free first. What fits on the belt goes there, and a long shaft is dropped for another weapon or held in one hand for anything else.
	for (const EAedirHand Hand : { EAedirHand::Right, EAedirHand::Left })
	{
		const AAedirItemActor* Held = Interaction->GetHeldItem(Hand);
		if (Held && Held->FitsOnBelt())
		{
			Interaction->HolsterToWaist(Hand);
			return;
		}
	}
	if (const AAedirItemActor* Shaft = Interaction->GetHeldItem(EAedirHand::Right))
	{
		if (Item->ItemClass == EAedirItemClass::Weapon || Item->bTwoHanded)
		{
			Interaction->DropHeld(EAedirHand::Left);
			Interaction->DropHeld(EAedirHand::Right);
		}
		else if (Interaction->GetHeldItem(EAedirHand::Left) == Shaft)
		{
			Interaction->DropHeld(EAedirHand::Left);
		}
	}
	const EAedirHand FreeHand = Interaction->GetHeldItem(EAedirHand::Right) ? EAedirHand::Left : EAedirHand::Right;
	if (Interaction->GetHeldItem(FreeHand))
	{
		GiveUpFetch();
		return;
	}

	// A hand that cannot get to it after all is helped along, so that nobody stands bowing over an item for ever.
	if (FetchReaches >= NpcFetchReachTries)
	{
		if (!Interaction->EquipImmediate(FreeHand, Item))
		{
			GiveUpFetch();
		}
		return;
	}
	++FetchReaches;
	UE_LOG(LogAedir, Log, TEXT("%s reaches for %s, try %d"), *NpcName.ToString(), *Item->ItemName.ToString(), FetchReaches);
	Interaction->StartReachForItem(FreeHand, Item);
}

void AAedirNpc::FinishFetch(AAedirItemActor* Item)
{
	ClearFetch();
	if (Item->ItemClass == EAedirItemClass::Armor)
	{
		Item->Use(this);
	}
	else if (Item->ItemClass != EAedirItemClass::Weapon && Interaction)
	{
		Interaction->AddToBackpack(Item);
	}

	// The hand that let go of a long shaft goes back to it.
	if (Interaction)
	{
		AAedirItemActor* Right = Interaction->GetHeldItem(EAedirHand::Right);
		if (Right && Right->bTwoHanded && Interaction->GetHandState(EAedirHand::Left) == EAedirHandState::Idle)
		{
			Interaction->StartReachForItem(EAedirHand::Left, Right);
		}
	}
}

void AAedirNpc::GiveUpFetch()
{
	if (FetchItem)
	{
		UE_LOG(LogAedir, Log, TEXT("%s gives up on %s at %.0f cm, %d reaches, stuck %.1f s"), *NpcName.ToString(), *FetchItem->ItemName.ToString(), FVector::Dist2D(FetchItem->GetActorLocation(), GetActorLocation()), FetchReaches, WayStuckSeconds);
		ItemsGivenUp.Add(FetchItem.Get(), GetWorld()->GetTimeSeconds());
	}
	ClearFetch();
}

void AAedirNpc::ClearFetch()
{
	if (Interaction)
	{
		Interaction->CancelReach(EAedirHand::Left);
		Interaction->CancelReach(EAedirHand::Right);
	}
	FetchItem = nullptr;
	bFetchOrdered = false;
	SetWantsToRun(false);
	BestLeaderDistance = BIG_NUMBER;
	NoProgressSeconds = 0.f;
}

bool AAedirNpc::TakesOrdersFrom(const AAedirPlayerCharacter* Player) const
{
	return Player && Role_Npc == EAedirNpcRole::Follower && GetRelationship(UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(Player->GetController()))) >= NpcFollowsFrom && CanTalkTo(Player);
}

void AAedirNpc::OrderGoTo(AAedirPlayerCharacter* Player, const FVector& Where)
{
	if (!HasAuthority() || !TakesOrdersFrom(Player))
	{
		return;
	}
	ClearFetch();
	Leader = Player;
	Order = EAedirNpcOrder::GoingTo;
	GoToGoal = Where;
	BestWayDistance = BIG_NUMBER;
	WayStuckSeconds = 0.f;
}

void AAedirNpc::OrderFetch(AAedirPlayerCharacter* Player, AAedirItemActor* Item)
{
	if (!HasAuthority() || !TakesOrdersFrom(Player) || !AAedirItemActor::IsPickable(Item))
	{
		return;
	}
	ClearFetch();
	Leader = Player;
	Order = EAedirNpcOrder::Following;
	FetchItem = Item;
	bFetchOrdered = true;
	FetchReaches = 0;
	BestWayDistance = BIG_NUMBER;
	WayStuckSeconds = 0.f;
}

void AAedirNpc::CollectGear(TArray<FName>& OutGear) const
{
	if (!Interaction)
	{
		return;
	}
	TArray<AAedirItemActor*> Items = Interaction->GetWaistItemsCopy();
	Items.Add(Interaction->GetHeldItem(EAedirHand::Right));
	Items.AddUnique(Interaction->GetHeldItem(EAedirHand::Left));
	Items.Append(Interaction->GetBackpackItemsCopy());
	for (const TObjectPtr<AAedirItemActor>& Worn : Interaction->WornItems)
	{
		Items.Add(Worn.Get());
	}
	for (const AAedirItemActor* Item : Items)
	{
		if (const AAedirCatalogItem* Catalog = Cast<AAedirCatalogItem>(Item))
		{
			OutGear.Add(Catalog->CatalogId);
		}
	}
}

void AAedirNpc::RestoreGear(const TArray<FName>& Gear)
{
	if (!Interaction)
	{
		return;
	}

	TArray<AAedirItemActor*> Old = Interaction->GetWaistItemsCopy();
	Old.AddUnique(Interaction->GetHeldItem(EAedirHand::Right));
	Old.AddUnique(Interaction->GetHeldItem(EAedirHand::Left));
	Old.Append(Interaction->GetBackpackItemsCopy());
	for (const TObjectPtr<AAedirItemActor>& Worn : Interaction->WornItems)
	{
		Old.Add(Worn.Get());
		if (Worn && Combat)
		{
			Combat->SetArmorProtection(Worn->ArmorZone, 0.f);
		}
	}
	Interaction->ReleaseAllItems();
	Interaction->WornItems.Reset();
	for (AAedirItemActor* Item : Old)
	{
		if (IsValid(Item))
		{
			Item->Destroy();
		}
	}

	int32 NextSlot = 0;
	const FTransform AtHand(GetActorLocation() + FVector(0.f, 0.f, 80.f));
	for (const FName& Id : Gear)
	{
		AAedirCatalogItem* Item = AAedirCatalogItem::SpawnCatalogItem(GetWorld(), Id, AtHand);
		if (!Item)
		{
			continue;
		}
		if (Item->ItemClass == EAedirItemClass::Armor)
		{
			Item->Use(this);
		}
		else if (Item->ItemClass == EAedirItemClass::Weapon)
		{
			if (!Interaction->HolsterImmediate(NextSlot, Item))
			{
				Interaction->EquipImmediate(EAedirHand::Right, Item);
			}
			++NextSlot;
		}
		else if (Interaction->EquipImmediate(EAedirHand::Right, Item))
		{
			Interaction->AddToBackpack(Item);
		}
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
			// A way round goes further from the leader before it comes closer.
			NoProgressSeconds += DeltaSeconds * (IsFollowingWay() ? NpcWayRoundShare : 1.f);
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
	case EAedirNpcOrder::GoingTo:
	{
		const float Left = static_cast<float>(FVector::Dist2D(GetActorLocation(), GoToGoal));
		if (IsStuckOnTheWay(Left, DeltaSeconds))
		{
			TeleportTo(GoToGoal + FVector(0.0, 0.0, GetSimpleCollisionHalfHeight()), GetActorRotation(), false, false);
			Order = EAedirNpcOrder::Waiting;
		}
		else if (Left <= NpcGoToStopCm)
		{
			SetWantsToRun(false);
			Order = EAedirNpcOrder::Waiting;
		}
		else
		{
			FollowTo(GoToGoal, NpcGoToStopCm, NpcFollowRunCm, DeltaSeconds);
		}
		break;
	}

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
		if (Standing >= NpcFollowsFrom)
		{
			Add(NSLOCTEXT("AedirNpc", "GoThere", "Go over there."), NSLOCTEXT("AedirNpc", "GoThereYes", "Show me where."), EAedirNpcAction::GoThere, true);
		}
		if (Order == EAedirNpcOrder::Following || Order == EAedirNpcOrder::GoingTo)
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
	Add(NSLOCTEXT("AedirNpc", "Goodbye", "Farewell."), NSLOCTEXT("AedirNpc", "GoodbyeAnswer", "Walk well."), EAedirNpcAction::None, true);
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
			ClearFetch();
			Order = EAedirNpcOrder::Following;
			Leader = Player;
		}
		break;

	case EAedirNpcAction::Stay:
		if (Role_Npc == EAedirNpcRole::Follower)
		{
			ClearFetch();
			Order = EAedirNpcOrder::Waiting;
		}
		break;

	case EAedirNpcAction::PartWays:
		if (Role_Npc == EAedirNpcRole::Follower)
		{
			ClearFetch();
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
	if (ChatPartner.IsValid())
	{
		EndChat();
	}
	if (Attacker->IsPlayerControlled())
	{
		RemarkOnBlow();
	}

	if (const APlayerController* AttackerController = Cast<APlayerController>(Attacker->GetController()))
	{
		ChangeRelationship(UAedirSaveSubsystem::MakePlayerKey(AttackerController), -NpcAttackLoss);
	}
	if (Combat)
	{
		Combat->SetAIGrudge(Attacker);
	}

	// Those on the victim's side who see or hear it do not stand by: they take up the fight against the attacker, unless the
	// attacker is one of their own.
	UWorld* World = GetWorld();
	const UAedirGroupSubsystem* Groups = World ? World->GetSubsystem<UAedirGroupSubsystem>() : nullptr;
	if (!Groups)
	{
		return;
	}
	for (TActorIterator<AAedirNpc> It(World); It; ++It)
	{
		AAedirNpc* Friend = *It;
		if (Friend == this || Friend == Attacker || !Friend->Combat || (Friend->Stats && Friend->Stats->IsDead())
			|| FVector::DistSquared(Friend->GetActorLocation(), GetActorLocation()) > FMath::Square(NpcRallyRangeCm))
		{
			continue;
		}
		const bool bOnVictimsSide = Friend->GroupId == GroupId || Groups->GetRelation(Friend->GroupId, GroupId) == EAedirRelation::Ally;
		const bool bOnAttackersSide = Friend->GroupId == Attacker->GroupId;
		if (bOnVictimsSide && !bOnAttackersSide && (Friend->Combat->CanNotice(this) || Friend->Combat->CanNotice(Attacker)))
		{
			Friend->Combat->SetAIGrudge(Attacker);
		}
	}
}
