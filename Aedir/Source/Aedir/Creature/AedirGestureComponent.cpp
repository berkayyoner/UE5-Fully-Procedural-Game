#include "Creature/AedirGestureComponent.h"

#include "Aedir.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Fists/AedirFistArms.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Items/AedirInteractionComponent.h"
#include "Net/UnrealNetwork.h"
#include "Npc/AedirNpc.h"
#include "Physics/AedirBalanceComponent.h"

UAedirGestureComponent::UAedirGestureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UAedirGestureComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UAedirGestureComponent, bPointing, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UAedirGestureComponent, bSurrender, COND_SkipOwner);
}

AAedirCreature* UAedirGestureComponent::GetCreature() const
{
	return Cast<AAedirCreature>(GetOwner());
}

void UAedirGestureComponent::SetPointing(bool bOn)
{
	bPointing = bOn;
	UE_LOG(LogAedir, Log, TEXT("[Gesture] %s %s"), *GetNameSafe(GetOwner()), bOn ? TEXT("points") : TEXT("stops pointing"));
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetPointing(bOn);
	}
}

void UAedirGestureComponent::ServerSetPointing_Implementation(bool bOn)
{
	bPointing = bOn;
}

bool UAedirGestureComponent::IsPointing() const
{
	return bPointing && !AreHandsUp();
}

void UAedirGestureComponent::SetSurrendering(bool bOn)
{
	if (bSurrender == bOn)
	{
		return;
	}
	bSurrender = bOn;
	UE_LOG(LogAedir, Log, TEXT("[Gesture] %s %s"), *GetNameSafe(GetOwner()), bOn ? TEXT("starts to raise its hands") : TEXT("lowers its hands"));
	// The hands leave whatever they were doing.
	AAedirCreature* Creature = GetCreature();
	if (bOn && Creature)
	{
		UAedirFistArms* Fists = Creature->FindComponentByClass<UAedirFistArms>();
		for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
		{
			if (Fists)
			{
				Fists->SetHeld(Hand, false);
			}
			if (Creature->Combat)
			{
				Creature->Combat->SetGuard(Hand, false);
				Creature->Combat->Flinch(Hand, SurrenderSeconds);
			}
		}
	}
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerSetSurrendering(bOn);
	}
}

void UAedirGestureComponent::ServerSetSurrendering_Implementation(bool bOn)
{
	SetSurrendering(bOn);
}

void UAedirGestureComponent::LetGoOfEverything() const
{
	const AAedirCreature* Creature = GetCreature();
	UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	if (!Interaction || !Creature->HasAuthority())
	{
		return;
	}
	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		Interaction->ReleaseBodyGrab(Hand);
		Interaction->CancelReach(Hand);
		// A weapon in both hands is first let go of by one of them.
		for (int32 Times = 0; Times < 2 && Interaction->GetHeldItem(Hand); ++Times)
		{
			Interaction->DropHeld(Hand);
		}
	}
}

void UAedirGestureComponent::Threaten()
{
	LastThreat = GetWorld() ? GetWorld()->GetTimeSeconds() : LastThreat;
}

float UAedirGestureComponent::GetAggression() const
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	return Npc ? FMath::Clamp(Npc->Aggression, 0.f, 1.f) * 100.f : 0.f;
}

float UAedirGestureComponent::GetSurrenderBlood() const
{
	const float Aggression = GetAggression();
	if (bNeverAgain || Aggression > SurrenderNeverAggression)
	{
		return -1.f;
	}
	return FMath::Lerp(SurrenderBloodCalm, SurrenderBloodFierce, FMath::Clamp(Aggression / FMath::Max(SurrenderNeverAggression, 1.f), 0.f, 1.f));
}

float UAedirGestureComponent::GetSurrenderSeconds() const
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	const float Aggression = GetAggression();
	if (bNeverAgain || Aggression > SurrenderNeverAggression)
	{
		return -1.f;
	}
	const float Seconds = FMath::Lerp(SurrenderDownSecondsMin, SurrenderDownSecondsMax, Aggression / FMath::Max(SurrenderNeverAggression, 1.f));
	const float Blood = Npc && Npc->Stats ? Npc->Stats->GetBloodFraction() : 1.f;
	return Seconds * FMath::Lerp(SurrenderLowBloodShare, 1.f, FMath::Clamp((Blood - SurrenderLowBlood) / FMath::Max(1.f - SurrenderLowBlood, KINDA_SMALL_NUMBER), 0.f, 1.f));
}

void UAedirGestureComponent::UpdateNpc(float DeltaTime)
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	const UWorld* World = GetWorld();
	if (!Npc || !World || !Npc->HasAuthority())
	{
		return;
	}
	if (bSurrender)
	{
		UpdateGivenUp(DeltaTime);
		return;
	}
	bFleeing = false;
	FoesStanding.Reset();
	if (Npc->Combat && Npc->Combat->HasAITarget())
	{
		Threaten();
	}
	// Bleeding in a fight it gives up, standing or not, the sooner the more aggressive it is.
	const float GiveUpBlood = GetSurrenderBlood();
	const float Blood = Npc->Stats ? Npc->Stats->GetBloodFraction() : 1.f;
	if (GiveUpBlood >= 0.f && Npc->Stats && !Npc->Stats->IsDead() && !Npc->Stats->IsDying() && !Npc->Stats->IsKnockedOut()
		&& World->GetTimeSeconds() - LastThreat <= SurrenderThreatSeconds && Blood <= GiveUpBlood && Blood < RearmBlood && HasFoeFighting())
	{
		UE_LOG(LogAedir, Log, TEXT("[Gesture] %s gives up bleeding, blood %.2f at or under %.2f (aggression %.0f)"), *Npc->GetName(), Blood, GiveUpBlood, GetAggression());
		SetSurrendering(true);
		return;
	}
	// Down is lying, and getting up and finding its feet again after it: it may give up at any of those.
	const bool bDown = Npc->Balance && (Npc->Balance->IsRagdoll() || Npc->Balance->IsGettingUp() || Npc->Balance->IsSettling());
	if (!bDown || !Npc->Stats || Npc->Stats->IsDead() || Npc->Stats->IsDying())
	{
		DownSeconds = 0.f;
		return;
	}
	// Out cold it decides nothing, and the time it lay awake before is not forgotten.
	if (Npc->Stats->IsKnockedOut() || World->GetTimeSeconds() - LastThreat > SurrenderThreatSeconds)
	{
		return;
	}
	DownSeconds += DeltaTime;
	const float Needed = GetSurrenderSeconds();
	if (Needed >= 0.f && DownSeconds >= Needed && HasFoeFighting())
	{
		UE_LOG(LogAedir, Log, TEXT("[Gesture] %s gives up after %.1f s down, %s (aggression %.0f, blood %.2f)"), *Npc->GetName(), DownSeconds,
			Npc->Balance->IsHoisting() || Npc->Balance->IsGettingUp() ? TEXT("getting up") : Npc->Balance->IsSettling() ? TEXT("finding its feet") : TEXT("lying"), Npc->Aggression * 100.f, Npc->Stats->GetBloodFraction());
		SetSurrendering(true);
	}
}

void UAedirGestureComponent::StruckWhileGivenUp(const AAedirCreature* Attacker)
{
	// A blow already on its way as the hands went up is not one struck at a body that had given up.
	const UWorld* World = GetWorld();
	if (HasGivenUp() && World && World->GetTimeSeconds() - GaveUpAt >= SurrenderStrikeGraceSeconds && GetAggression() > SurrenderRefightAggression && Cast<AAedirNpc>(GetOwner()))
	{
		bNeverAgain = true;
		TakeBack(*FString::Printf(TEXT("struck by %s, fights on and gives up no more"), *GetNameSafe(Attacker)));
	}
}

bool UAedirGestureComponent::HasFoeFighting() const
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	const UWorld* World = GetWorld();
	if (!Npc || !World || !bEndWhenNoFoes)
	{
		return true;
	}
	const double Reach = SurrenderLookCm * Npc->GetActorScale3D().X;
	for (TActorIterator<AAedirCreature> It(World); It; ++It)
	{
		const AAedirCreature* Other = *It;
		if (Other == Npc || Npc->GetRelationTo(Other) != EAedirRelation::Enemy || FVector::Dist2D(Npc->GetActorLocation(), Other->GetActorLocation()) > Reach)
		{
			continue;
		}
		const bool bOut = Other->Stats && (Other->Stats->IsDead() || Other->Stats->IsDying() || Other->Stats->IsKnockedOut());
		if (!bOut && !(Other->Gesture && Other->Gesture->AreHandsUp()))
		{
			return true;
		}
	}
	return false;
}

void UAedirGestureComponent::TakeBack(const TCHAR* Why, bool bRearmBlood)
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	UE_LOG(LogAedir, Log, TEXT("[Gesture] %s takes back giving up: %s (aggression %.0f)"), *GetNameSafe(GetOwner()), Why, GetAggression());
	bFleeing = false;
	FoesStanding.Reset();
	DownSeconds = 0.f;
	// Its blood has to fall some more before it gives up for it again.
	RearmBlood = bRearmBlood && Npc && Npc->Stats ? Npc->Stats->GetBloodFraction() - SurrenderBloodRearmDrop : RearmBlood;
	SetSurrendering(false);
}

void UAedirGestureComponent::UpdateGivenUp(float DeltaTime)
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	UWorld* World = GetWorld();
	LookTimer -= DeltaTime;
	if (!Npc || !World || !HasGivenUp() || LookTimer > 0.f)
	{
		return;
	}
	LookTimer = SurrenderLookSeconds;
	const float Aggression = GetAggression();
	const bool bAwake = Npc->Stats && !Npc->Stats->IsDead() && !Npc->Stats->IsDying() && !Npc->Stats->IsKnockedOut();
	const bool bUp = !(Npc->Balance && (Npc->Balance->IsRagdoll() || Npc->Balance->IsGettingUp() || Npc->Balance->IsSettling()));
	if (!bAwake)
	{
		bFleeing = false;
		return;
	}

	// Who is about: foes who have not given up, those of them on their feet, and friends fighting on.
	const FVector Here = Npc->GetActorLocation();
	const float Scale = Npc->GetActorScale3D().X;
	int32 Foes = 0;
	int32 Friends = 0;
	TArray<TWeakObjectPtr<const AAedirCreature>> Standing;
	bool bAllTurned = true;
	double NearestCm = TNumericLimits<double>::Max();
	FVector FoesAt = FVector::ZeroVector;
	bool bOneWentDown = false;
	for (TActorIterator<AAedirCreature> It(World); It; ++It)
	{
		const AAedirCreature* Other = *It;
		if (Other == Npc)
		{
			continue;
		}
		const double Cm = FVector::Dist2D(Here, Other->GetActorLocation());
		const bool bOut = Other->Stats && (Other->Stats->IsDead() || Other->Stats->IsDying() || Other->Stats->IsKnockedOut());
		const bool bGivenUp = Other->Gesture && Other->Gesture->AreHandsUp();
		const bool bDown = Other->Balance && (Other->Balance->IsRagdoll() || Other->Balance->IsGettingUp());
		const EAedirRelation Relation = Npc->GetRelationTo(Other);
		if (Relation == EAedirRelation::Enemy)
		{
			if (Cm > SurrenderLookCm * Scale)
			{
				continue;
			}
			// One watched on its feet before that is now down, out or given up.
			if (FoesStanding.Contains(Other) && (bOut || bGivenUp || bDown))
			{
				bOneWentDown = true;
			}
			if (bOut || bGivenUp)
			{
				continue;
			}
			++Foes;
			if (bDown)
			{
				continue;
			}
			Standing.Add(Other);
			FoesAt += Other->GetActorLocation();
			NearestCm = FMath::Min(NearestCm, Cm);
			const FVector ToMe = (Here - Other->GetActorLocation()).GetSafeNormal2D();
			const FVector Facing = Other->GetActorForwardVector().GetSafeNormal2D();
			if (FVector::DotProduct(Facing, ToMe) > FMath::Cos(FMath::DegreesToRadians(SurrenderBackTurnedDegrees)))
			{
				bAllTurned = false;
			}
		}
		else if (Relation == EAedirRelation::Ally && Cm <= SurrenderFriendCm * Scale && !bOut && !bGivenUp)
		{
			++Friends;
		}
	}
	for (const TWeakObjectPtr<const AAedirCreature>& Seen : FoesStanding)
	{
		// One that died and is gone from the world is down too.
		if (!Seen.IsValid())
		{
			bOneWentDown = true;
		}
	}
	FoesStanding = Standing;
	const int32 Side = Friends + 1;

	// Every foe about it dead, out cold or given up too: there is nobody left to give up to, however meek it is.
	if (Foes == 0 && bEndWhenNoFoes)
	{
		TakeBack(TEXT("no foe is left fighting about it"), false);
		return;
	}
	if (Aggression <= SurrenderFleeAggression && Aggression <= SurrenderWatchAggression)
	{
		bFleeing = false;
		return;
	}

	if (!bUp)
	{
		bFleeing = false;
		return;
	}
	if (Aggression > SurrenderWatchAggression && bOneWentDown && Standing.Num() <= Side)
	{
		TakeBack(*FString::Printf(TEXT("a foe went down, %d on their feet against %d of its own"), Standing.Num(), Side));
		return;
	}
	if (Aggression <= SurrenderFleeAggression)
	{
		return;
	}
	if (Standing.Num() == 0)
	{
		// Nobody left on its feet near it to run from: run off and away, it has nothing to give up to.
		if (bFleeing)
		{
			TakeBack(TEXT("it has got away"));
		}
		return;
	}
	FleeFrom = FoesAt / Standing.Num();
	if (bFleeing)
	{
		if (NearestCm <= SurrenderFleeCaughtCm * Scale)
		{
			bFleeing = false;
			UE_LOG(LogAedir, Log, TEXT("[Gesture] %s is caught up with, %.0f cm off, and holds its hands up again"), *Npc->GetName(), NearestCm);
		}
		return;
	}
	if (NearestCm >= SurrenderFleeFarCm * Scale && bAllTurned)
	{
		if (Side * 2 > Foes)
		{
			TakeBack(*FString::Printf(TEXT("the foes turned their backs, %d of its own against %d"), Side, Foes));
			return;
		}
		bFleeing = true;
		UE_LOG(LogAedir, Log, TEXT("[Gesture] %s runs off while %d foes have their backs to it, the nearest %.0f cm off"), *Npc->GetName(), Standing.Num(), NearestCm);
	}
}

void UAedirGestureComponent::UpdateCrouch(float DeltaTime)
{
	AAedirCreature* Creature = GetCreature();
	if (!Creature || !(Creature->HasAuthority() || Creature->IsLocallyControlled()))
	{
		return;
	}
	const UAedirBalanceComponent* Balance = Creature->Balance;
	if (Balance && (Balance->IsRagdoll() || Balance->IsGettingUp() || Balance->IsSettling()))
	{
		SteadySeconds = 0.f;
	}
	else if (!Balance || Balance->GetBalance() >= SurrenderCrouchLeastBalance)
	{
		SteadySeconds += DeltaTime;
	}
	// On its feet it goes down to a crouch, and stays there for as long as it gives up.
	if (bSurrender && !bFleeing && SteadySeconds >= SurrenderCrouchSteadySeconds)
	{
		bSunk = true;
		if (!Creature->bIsCrouched && Creature->CanCrouch())
		{
			Creature->SetWantsToCrouch(true);
		}
	}
	else if ((!bSurrender || bFleeing) && bSunk)
	{
		bSunk = false;
		const APlayerController* Controller = Cast<APlayerController>(Creature->GetController());
		if (!Controller || !Controller->IsInputKeyDown(EKeys::LeftControl))
		{
			Creature->SetWantsToCrouch(false);
		}
	}
}

void UAedirGestureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateNpc(DeltaTime);
	const AAedirCreature* Creature = GetCreature();
	const UAedirStatsComponent* Stats = Creature ? Creature->Stats.Get() : nullptr;
	if (Stats && (Stats->IsDead() || Stats->IsDying() || Stats->IsKnockedOut()))
	{
		// Out cold nothing is held up; a player who comes round has to raise the hands anew, anyone else raises them again.
		bPointing = false;
		SurrenderShare = 0.f;
		if (Creature->IsPlayerControlled())
		{
			bSurrender = false;
			bGaveUp = false;
		}
		return;
	}
	UpdateCrouch(DeltaTime);
	const float Before = SurrenderShare;
	// Running off the hands come down.
	SurrenderShare = FMath::FInterpConstantTo(SurrenderShare, bSurrender && !bFleeing ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(bSurrender ? SurrenderSeconds : SurrenderLowerSeconds, 0.05f));
	if (bSurrender && SurrenderShare >= SurrenderDropShare)
	{
		const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
		if (Before < SurrenderDropShare || (Interaction && (Interaction->GetHeldItem(EAedirHand::Left) || Interaction->GetHeldItem(EAedirHand::Right))))
		{
			LetGoOfEverything();
		}
	}
	bGaveUp = bSurrender && (bGaveUp || SurrenderShare >= 1.f);
	if (Before < 1.f && SurrenderShare >= 1.f)
	{
		GaveUpAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		UE_LOG(LogAedir, Log, TEXT("[Gesture] %s has surrendered"), *GetNameSafe(GetOwner()));
	}
}

FVector UAedirGestureComponent::GetPointedPlace() const
{
	const AAedirCreature* Creature = GetCreature();
	const UWorld* World = GetWorld();
	if (!Creature || !World)
	{
		return FVector::ZeroVector;
	}
	const FVector Chest = Creature->GetMesh() ? Creature->GetMesh()->GetBoneLocation(TEXT("spine_05")) : Creature->GetActorLocation();
	FVector Eye = Chest;
	FRotator View = Creature->GetBaseAimRotation();
	if (const APlayerController* Controller = Cast<APlayerController>(Creature->GetController()); Controller && Controller->IsLocalController())
	{
		// Seen through another camera, it points the way its own would look.
		if (Controller->GetViewTarget() == Creature)
		{
			Controller->GetPlayerViewPoint(Eye, View);
		}
		else
		{
			View = Controller->GetControlRotation();
		}
	}
	const FVector Way = View.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirPointed), false, Creature);
	FHitResult Hit;
	const double Seen = World->LineTraceSingleByChannel(Hit, Eye, Eye + Way * PointRangeCm, ECC_Visibility, Params) ? Hit.Distance : PointRangeCm;
	// What lies between the camera and the body, or right before it, is not pointed at: the arm would turn back or across for it.
	const double Least = FVector::DotProduct(Chest - Eye, Way) + PointNearestCm;
	return Eye + Way * FMath::Max(Seen, Least);
}
