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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirGroupIdChanged, FName, NewGroupId);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GroupId, Category="Aedir|Groups")
	FName GroupId;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Groups")
	FAedirGroupIdChanged OnGroupIdChanged;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Creature")
	float CarriedWeight = 0.f;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aedir|Groups")
	void SetGroupId(FName NewGroupId);

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
};
