#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AedirCreatureTypes.h"
#include "AedirCreature.generated.h"

class UAedirBalanceComponent;
class UAedirStatsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirGroupIdChanged, FName, NewGroupId);

UCLASS(Blueprintable)
class AEDIR_API AAedirCreature : public ACharacter
{
	GENERATED_BODY()

public:
	AAedirCreature();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirStatsComponent> Stats;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Creature")
	TObjectPtr<UAedirBalanceComponent> Balance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GroupId, Category="Aedir|Groups")
	FName GroupId;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Groups")
	FAedirGroupIdChanged OnGroupIdChanged;

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

	UFUNCTION()
	void ApplySizeScale(float NewSizeScale);

private:
	UFUNCTION()
	void OnRep_GroupId();
};
