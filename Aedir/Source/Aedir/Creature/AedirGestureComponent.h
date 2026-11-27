#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirGestureComponent.generated.h"

class AAedirCreature;

// What a body says with its hands: pointing a finger at what it looks at, and holding both hands up in surrender. The gait
// puts the arms, the hands and the face where this asks.
UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirGestureComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirGestureComponent();

	// Seconds the hands take up to the head, and down again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0.05", Units="s"))
	float SurrenderSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0.05", Units="s"))
	float SurrenderLowerSeconds = 0.6f;

	// How far up the hands are when they let go of what they hold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderDropShare = 0.5f;

	// Where the wrists are held, from between the shoulders: ahead, out to the side and up (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender")
	FVector SurrenderHandCm = FVector(22.f, 33.f, 20.f);

	// Which way the elbows go: ahead, out and up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender")
	FVector SurrenderElbow = FVector(-0.1f, 0.6f, -0.8f);

	// On the way up off the ground they are held here instead, by the chest as it is bent: before the shoulders, not over the
	// head. As the chest comes up (how far up its up points, from the first to the second) they go to where they are held
	// standing, and to a standing body's way of holding them over SurrenderRisenSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender")
	FVector SurrenderRisingHandCm = FVector(26.f, 33.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="-1", ClampMax="1"))
	float SurrenderRisingUprightMin = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="-1", ClampMax="1"))
	float SurrenderRisingUprightMax = 0.97f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0.05", Units="s"))
	float SurrenderRisenSeconds = 0.6f;

	// Lying on the face, the wrists are this far ahead of the chest instead (cm): beside the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender")
	float SurrenderFaceDownAheadCm = 2.f;

	// It sinks to a crouch, bent over this share of a crouch's own lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderCrouchLeanShare = 0.3f;

	// Up from the ground, it stands steadily for so long before it sinks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0", Units="s"))
	float SurrenderCrouchSteadySeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderCrouchLeastBalance = 0.6f;

	// Conscious on the ground and unable to stand for so long under threat, a person gives up: the first for one not aggressive
	// at all, the second for one as aggressive as SurrenderNeverAggression (0-100), who no longer gives up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="s"))
	float SurrenderDownSecondsMin = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="s"))
	float SurrenderDownSecondsMax = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="100"))
	float SurrenderNeverAggression = 90.f;

	// With this much of its blood left or less it gives up in this share of the time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderLowBloodShare = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderLowBlood = 0.4f;

	// Seconds after the last blow or the last sight of a foe it still feels threatened.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="s"))
	float SurrenderThreatSeconds = 60.f;

	// Bleeding in a fight an NPC gives up, on its feet or not, once its blood is down to the first share for one not
	// aggressive at all, the second for one as aggressive as SurrenderNeverAggression; more aggressive still it never does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderBloodCalm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderBloodFierce = 0.05f;

	// Back in the fight after giving up, it bleeds this much more before its blood makes it give up again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="1"))
	float SurrenderBloodRearmDrop = 0.1f;

	// Above this aggression (0-100), struck after it has given up it takes it back and fights on, and gives up no more.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="100"))
	float SurrenderRefightAggression = 30.f;

	// A blow landing this soon after the hands are all the way up was on its way before: it does not count.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="s"))
	float SurrenderStrikeGraceSeconds = 0.75f;

	// Above this aggression (0-100), given up, it runs off once every foe on its feet is at least SurrenderFleeFarCm off with
	// its back to it; unless it and the friends fighting about it are more than half the foes who have not given up, when
	// it takes the chance and goes back into the fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="100"))
	float SurrenderFleeAggression = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="cm"))
	float SurrenderFleeFarCm = 800.f;

	// A foe has its back turned when the way it faces and the way to this body make an angle wider than this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="180", Units="deg"))
	float SurrenderBackTurnedDegrees = 100.f;

	// Running off, it stops and holds its hands up again when a foe on its feet comes this near, and is away once none is
	// within SurrenderLookCm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="cm"))
	float SurrenderFleeCaughtCm = 450.f;

	// Above this aggression (0-100), given up, it watches its foes: when one goes down or gives up, and those still on their
	// feet are no more than it and the friends fighting about it, it goes back into the fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", ClampMax="100"))
	float SurrenderWatchAggression = 50.f;

	// Foes this near are counted and watched, friends this near counted with it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="cm"))
	float SurrenderLookCm = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0", Units="cm"))
	float SurrenderFriendCm = 1500.f;

	// Given up, it stops once every foe within SurrenderLookCm is dead, out cold or has given up as well, whatever its
	// aggression.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc")
	bool bEndWhenNoFoes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Surrender|Npc", meta=(ClampMin="0.05", Units="s"))
	float SurrenderLookSeconds = 0.5f;

	// The pointing arm is held out this share of its length, and follows where it points this fast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0.5", ClampMax="1"))
	float PointReachShare = 0.985f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="1"))
	float PointFollowSpeed = 16.f;

	// How far an arm points across the body and out to its own side, and how far up or down, from the way the chest faces (degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0", ClampMax="90"))
	float PointAcrossDegrees = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0", ClampMax="150"))
	float PointOutDegrees = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0", ClampMax="89"))
	float PointPitchDegrees = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="100", Units="cm"))
	float PointRangeCm = 10000.f;

	// Nothing nearer than this in front of the chest is pointed at: the way of the look is, instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0", Units="cm"))
	float PointNearestCm = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Gesture|Point", meta=(ClampMin="0.05", Units="s"))
	float PointRaiseSeconds = 0.3f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetPointing(bool bOn);
	// Pointing mode, whether or not a hand is free for it just now.
	bool IsPointing() const;

	// Up towards surrender or down from it; only once the hands are all the way up has it surrendered.
	void SetSurrendering(bool bOn);
	bool IsSurrendering() const { return bSurrender; }
	bool HasSurrendered() const { return bSurrender && SurrenderShare >= 1.f; }
	// It has given up and not taken it back, whether its hands are up just now or not (out cold, or getting up by them).
	bool HasGivenUp() const { return bSurrender && bGaveUp; }
	// The hands are up or on their way: they do nothing else.
	bool AreHandsUp() const { return bSurrender || SurrenderShare > 0.f; }
	float GetSurrenderShare() const { return SurrenderShare; }

	// A blow or a foe in sight.
	void Threaten();

	// The place it looks at, which it points to.
	FVector GetPointedPlace() const;

	// Seconds down it takes for this body to give up now; below 0 it never does.
	float GetSurrenderSeconds() const;

	// The blood share at which this NPC gives up in a fight; below 0 it never does.
	float GetSurrenderBlood() const;

	// Struck after giving up: an aggressive enough NPC takes it back and gives up no more.
	void StruckWhileGivenUp(const AAedirCreature* Attacker);

	// Given up, it runs off from its foes, its hands down; FleeFrom is where they are.
	bool IsFleeing() const { return bSurrender && bFleeing; }
	FVector GetFleeFrom() const { return FleeFrom; }
	bool WillGiveUp() const { return !bNeverAgain; }

private:
	UFUNCTION(Server, Reliable)
	void ServerSetPointing(bool bOn);

	UFUNCTION(Server, Reliable)
	void ServerSetSurrendering(bool bOn);

	AAedirCreature* GetCreature() const;
	void LetGoOfEverything() const;
	void UpdateNpc(float DeltaTime);
	void UpdateCrouch(float DeltaTime);
	// Given up, it weighs running off or going back into the fight.
	void UpdateGivenUp(float DeltaTime);
	// bRearmBlood: its blood has to fall some more before it gives up for that again.
	void TakeBack(const TCHAR* Why, bool bRearmBlood = true);
	// A foe within SurrenderLookCm still fighting: alive, awake and not given up itself. Nobody gives up to no one.
	bool HasFoeFighting() const;
	float GetAggression() const;

	UPROPERTY(Replicated)
	bool bPointing = false;

	UPROPERTY(Replicated)
	bool bSurrender = false;

	float SurrenderShare = 0.f;
	bool bGaveUp = false;
	bool bSunk = false;
	float SteadySeconds = 1.f;
	float DownSeconds = 0.f;
	double LastThreat = -1.0e6;
	bool bNeverAgain = false;
	double GaveUpAt = 0.0;
	bool bFleeing = false;
	FVector FleeFrom = FVector::ZeroVector;
	float RearmBlood = 2.f;
	float LookTimer = 0.f;
	TArray<TWeakObjectPtr<const AAedirCreature>> FoesStanding;
};
