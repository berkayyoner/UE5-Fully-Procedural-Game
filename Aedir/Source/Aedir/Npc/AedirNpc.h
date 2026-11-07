#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "AedirNpc.generated.h"

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
	Waiting
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
	AskHelp
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

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Npc")
	EAedirNpcRole Role_Npc = EAedirNpcRole::Neutral;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Npc")
	FText NpcName;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aedir|Npc")
	EAedirNpcOrder Order = EAedirNpcOrder::Routine;

	UPROPERTY(Replicated)
	TObjectPtr<AAedirCreature> Leader;

	UPROPERTY(Replicated)
	TArray<FAedirNpcRelationship> Relationships;

	// Who they are, what they say about themselves.
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

	void SetHome(const FVector& Location, float Yaw);
	const FVector& GetHomeLocation() const { return HomeLocation; }
	float GetHomeYaw() const { return HomeYaw; }

private:
	void UpdateOrders(float DeltaSeconds);
	void UpdateNeeds(float DeltaSeconds);
	void FollowTo(const FVector& Where, float StopDistance, float RunDistance, float DeltaSeconds);
	FText PickRumour(const FString& PlayerKey) const;

	FVector HomeLocation = FVector::ZeroVector;
	float HomeYaw = 0.f;
	bool bHasHome = false;
	float NextComplimentTime = 0.f;
	float BestLeaderDistance = BIG_NUMBER;
	float NoProgressSeconds = 0.f;
};
