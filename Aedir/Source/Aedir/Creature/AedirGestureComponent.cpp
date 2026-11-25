#include "Creature/AedirGestureComponent.h"

#include "Aedir.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Fists/AedirFistArms.h"
#include "GameFramework/PlayerController.h"
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

float UAedirGestureComponent::GetSurrenderSeconds() const
{
	const AAedirNpc* Npc = Cast<AAedirNpc>(GetOwner());
	const float Aggression = Npc ? FMath::Clamp(Npc->Aggression, 0.f, 1.f) * 100.f : 0.f;
	if (Aggression >= SurrenderNeverAggression)
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
	if (!Npc || !World || !Npc->HasAuthority() || bSurrender)
	{
		return;
	}
	if (Npc->Combat && Npc->Combat->HasAITarget())
	{
		Threaten();
	}
	if (!Npc->Balance || !Npc->Balance->IsRagdoll() || !Npc->Stats || Npc->Stats->IsDead() || Npc->Stats->IsDying())
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
	if (Needed >= 0.f && DownSeconds >= Needed)
	{
		UE_LOG(LogAedir, Log, TEXT("[Gesture] %s gives up after %.1f s on the ground (aggression %.0f, blood %.2f)"), *Npc->GetName(), DownSeconds, Npc->Aggression * 100.f, Npc->Stats->GetBloodFraction());
		SetSurrendering(true);
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
		}
		return;
	}
	const float Before = SurrenderShare;
	SurrenderShare = FMath::FInterpConstantTo(SurrenderShare, bSurrender ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(bSurrender ? SurrenderSeconds : SurrenderLowerSeconds, 0.05f));
	if (bSurrender && SurrenderShare >= SurrenderDropShare)
	{
		const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
		if (Before < SurrenderDropShare || (Interaction && (Interaction->GetHeldItem(EAedirHand::Left) || Interaction->GetHeldItem(EAedirHand::Right))))
		{
			LetGoOfEverything();
		}
	}
	if (Before < 1.f && SurrenderShare >= 1.f)
	{
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
