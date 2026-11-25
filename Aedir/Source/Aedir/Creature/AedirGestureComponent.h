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
	// The hands are up or on their way: they do nothing else.
	bool AreHandsUp() const { return bSurrender || SurrenderShare > 0.f; }
	float GetSurrenderShare() const { return SurrenderShare; }

	// A blow or a foe in sight.
	void Threaten();

	// The place it looks at, which it points to.
	FVector GetPointedPlace() const;

	// Seconds down it takes for this body to give up now; below 0 it never does.
	float GetSurrenderSeconds() const;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetPointing(bool bOn);

	UFUNCTION(Server, Reliable)
	void ServerSetSurrendering(bool bOn);

	AAedirCreature* GetCreature() const;
	void LetGoOfEverything() const;
	void UpdateNpc(float DeltaTime);

	UPROPERTY(Replicated)
	bool bPointing = false;

	UPROPERTY(Replicated)
	bool bSurrender = false;

	float SurrenderShare = 0.f;
	float DownSeconds = 0.f;
	double LastThreat = -1.0e6;
};
