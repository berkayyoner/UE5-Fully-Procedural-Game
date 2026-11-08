#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AedirCreatureTypes.h"
#include "AedirCreature.generated.h"

class UAedirBalanceComponent;
class UAedirCombatComponent;
class UAedirFootstepComponent;
class UAedirGaitComponent;
class UAedirInteractionComponent;
class UAedirStatsComponent;
class UAedirWoundComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirGroupIdChanged, FName, NewGroupId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirRaceChanged, FName, NewRaceId);

UCLASS(Blueprintable)
class AEDIR_API AAedirCreature : public ACharacter
{
	GENERATED_BODY()

public:
	AAedirCreature(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirStatsComponent> Stats;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirBalanceComponent> Balance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirGaitComponent> Gait;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirInteractionComponent> Interaction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirFootstepComponent> Footsteps;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirWoundComponent> Wounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GroupId, Category="Aedir|Groups")
	FName GroupId;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Groups")
	FAedirGroupIdChanged OnGroupIdChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_RaceId, Category="Aedir|Creature")
	FName RaceId;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Creature")
	FAedirRaceChanged OnRaceChanged;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Creature")
	float CarriedWeight = 0.f;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	uint8 BodyMaskBit = 0;

	// The collision mask the body and what it holds share, so its own weapon and fists pass through it.
	uint8 GetBodyMaskBit() const { return BodyMaskBit; }
	void ApplyBodyMask();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aedir|Groups")
	void SetGroupId(FName NewGroupId);

	// Server only. Applies the race's stats through the stats component and stores the race id.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aedir|Creature")
	void SetRace(FName NewRaceId);

	UFUNCTION(BlueprintPure, Category="Aedir|Creature")
	FText GetRaceDisplayName() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Creature")
	EAedirRelation GetRelationTo(const AAedirCreature* Other) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Creature")
	bool IsEnemyOf(const AAedirCreature* Other) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Creature")
	float ReceiveCreatureDamage(float Amount);

	UFUNCTION(BlueprintPure, Category="Aedir|Creature")
	float GetCarriedWeight() const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Creature")
	void SetCarriedWeight(float NewCarriedWeight);

	UFUNCTION(BlueprintPure, Category="Aedir|Creature")
	float GetCarryCapacity() const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void MoveLocal(FVector2D Input);

	// For creatures nobody plays: walks towards a place, around walls and drops, turning the body.
	void WalkTowards(const FVector& Goal, float DeltaSeconds, float TurnDegreesPerSecond);
	void TurnBodyTo(float Yaw, float DeltaSeconds, float TurnDegreesPerSecond);

	// True while it is working its way round something on a way it found for itself.
	bool IsFollowingWay() const { return !WayPoints.IsEmpty() || WaySearch.IsValid(); }

private:
	// Stuck on the way somewhere: goes off to one side for a moment.
	FVector WalkCheckLocation = FVector::ZeroVector;
	float WalkCheckTimer = 0.f;
	float WalkDetourSeconds = 0.f;
	float WalkDetourYaw = 0.f;

	// Where the straight line does not go, the next place on a way round.
	FVector SteerAlongWay(const FVector& Goal, float DeltaSeconds);
	TSharedPtr<class FAedirWayFinder> WaySearch;
	TArray<FVector> WayPoints;
	FVector WayGoal = FVector::ZeroVector;
	float WayCheckTimer = 0.f;

public:
	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void SetWantsToRun(bool bWantsToRun);

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void SetWantsToCrouch(bool bWantsToCrouch);

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void StartJump();

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void StopJump();

	UFUNCTION(BlueprintCallable, Category="Aedir|Movement")
	void SetWantsFightStance(bool bWantsFightStance);

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	bool IsRunning() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Movement")
	bool IsInFightStance() const;

	UFUNCTION()
	void ApplySizeScale(float NewSizeScale);

private:
	UFUNCTION()
	void OnRep_GroupId();

	UFUNCTION()
	void OnRep_RaceId();
};
