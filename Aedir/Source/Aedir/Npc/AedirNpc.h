#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "AedirNpc.generated.h"

class AAedirItemActor;
class AAedirPlayerCharacter;

UENUM(BlueprintType)
enum class EAedirNpcRole : uint8
{
	Neutral,
	Follower,
	Enemy
};

UENUM(BlueprintType)
enum class EAedirNpcOrder : uint8
{
	Routine,
	Following,
	Waiting,
	GoingTo
};

UENUM(BlueprintType)
enum class EAedirNpcAction : uint8
{
	None,
	Follow,
	Stay,
	PartWays,
	Compliment,
	Threaten,
	GiveHeld,
	AskHelp,
	GoThere
};

// What one player has done to this person so far, from -100 to 100.
USTRUCT()
struct AEDIR_API FAedirNpcRelationship
{
	GENERATED_BODY()

	UPROPERTY()
	FString PlayerKey;

	UPROPERTY()
	int32 Value = 0;
};

struct FAedirDialogueTopic
{
	FText Label;
	FText Response;
	EAedirNpcAction Action = EAedirNpcAction::None;
	bool bEndsTalk = false;
};

// A person of the world: stands about, needs food and water, sees its enemies, follows when asked, and talks unless it is out for blood.
UCLASS()
class AEDIR_API AAedirNpc : public AAedirCreature
{
	GENERATED_BODY()

public:
	AAedirNpc(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category="Aedir|Npc")
	EAedirNpcRole Role_Npc = EAedirNpcRole::Neutral;

	// A placed NPC with a name takes its role, biography and weapon from these when the game starts.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category="Aedir|Npc")
	FText NpcName;

	// The catalog id of the weapon they carry from the start, or none.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Npc")
	FName StartWeapon;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Npc")
	EAedirNpcOrder Order = EAedirNpcOrder::Routine;

	UPROPERTY(Replicated)
	TObjectPtr<AAedirCreature> Leader;

	UPROPERTY(Replicated)
	TArray<FAedirNpcRelationship> Relationships;

	// Who they are, what they say about themselves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Npc", meta=(MultiLine=true))
	FText Biography;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitNpc(EAedirNpcRole NewRole, const FText& NewName, const FText& NewBiography);

	int32 GetRelationship(const FString& PlayerKey) const;
	void ChangeRelationship(const FString& PlayerKey, int32 Delta);
	FText GetStandingText(const FString& PlayerKey) const;

	bool CanTalkTo(const AAedirCreature* Other) const;
	FText GetGreeting(const AAedirPlayerCharacter* Player) const;
	void BuildTopics(const AAedirPlayerCharacter* Player, TArray<FAedirDialogueTopic>& OutTopics) const;

	// Server only.
	void PerformAction(AAedirPlayerCharacter* Player, EAedirNpcAction Action);
	void OnAttackedBy(AAedirCreature* Attacker);

	bool TakesOrdersFrom(const AAedirPlayerCharacter* Player) const;

	// Server only. Goes to the spot and waits there until told to follow again.
	void OrderGoTo(AAedirPlayerCharacter* Player, const FVector& Where);

	// Server only. Goes for the item, takes it and puts it on, then follows again.
	void OrderFetch(AAedirPlayerCharacter* Player, AAedirItemActor* Item);

	// Server only. What they carry and wear, by catalog id, and the same gear given back to them after a load.
	void CollectGear(TArray<FName>& OutGear) const;
	void RestoreGear(const TArray<FName>& Gear);

	// In a conversation the NPC stands, turns to face whoever it talks to and keeps its eyes on them, its head moving as it speaks.
	void BeginConversation(AAedirCreature* With);
	void EndConversation();
	void Speak(const FText& Line);
	bool IsInConversation() const { return TalkPartner.IsValid(); }

	// Says a line aloud: it shows as a subtitle to anyone within earshot and the head moves with it.
	void SayLine(const FText& Line);
	bool IsAlive() const;

	void SetHome(const FVector& Location, float Yaw);
	const FVector& GetHomeLocation() const { return HomeLocation; }
	float GetHomeYaw() const { return HomeYaw; }

private:
	void UpdateConversation(float DeltaSeconds);

	// Remarks to the player, shouts in a fight and talks with other NPCs.
	void UpdateChatter(float DeltaSeconds);
	bool IsFreeToChat() const;
	void EndChat();
	void RemarkOnBlow();
	TWeakObjectPtr<AAedirNpc> ChatPartner;
	int32 ChatTalk = INDEX_NONE;
	int32 ChatLine = 0;
	float ChatWait = 0.f;
	float LineSeconds = 0.f;
	float RemarkTimer = FMath::FRandRange(5.f, 25.f);
	float TalkTimer = FMath::FRandRange(8.f, 40.f);
	TWeakObjectPtr<AAedirCreature> TalkPartner;
	float SpeakSeconds = 0.f;
	float TalkSeconds = 0.f;

	void UpdateOrders(float DeltaSeconds);
	void UpdateNeeds(float DeltaSeconds);
	void FollowTo(const FVector& Where, float StopDistance, float RunDistance, float DeltaSeconds);
	void UpdateWeaponReadiness(float DeltaSeconds);
	void UpdateItemSearch(float DeltaSeconds);
	void UpdateFetch(float DeltaSeconds);
	void FinishFetch(AAedirItemActor* Item);
	void GiveUpFetch();
	void ClearFetch();
	bool HasWeapon() const;
	bool LacksItem(const AAedirItemActor* Item) const;
	bool CanSeeItem(const AAedirItemActor* Item) const;
	bool IsStuckOnTheWay(float DistanceLeft, float DeltaSeconds);
	FText PickRumour(const FString& PlayerKey) const;

	FVector HomeLocation = FVector::ZeroVector;
	float HomeYaw = 0.f;
	bool bHasHome = false;
	float NextComplimentTime = 0.f;
	float BestLeaderDistance = BIG_NUMBER;
	float NoProgressSeconds = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> FetchItem;

	bool bFetchOrdered = false;
	int32 FetchReaches = 0;
	FVector GoToGoal = FVector::ZeroVector;
	float BestWayDistance = BIG_NUMBER;
	float WayStuckSeconds = 0.f;
	float CalmSeconds = 0.f;
	float ItemSearchTimer = 0.f;
	TMap<TWeakObjectPtr<AAedirItemActor>, double> ItemsGivenUp;
};
