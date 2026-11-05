#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirCreatureTypes.h"
#include "AedirStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAedirStatChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAedirDeath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirStrengthChanged, float, NewSizeScale);

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirStatsComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Stats")
	FAedirStatChanged OnStatChanged;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Stats")
	FAedirDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Stats")
	FAedirStrengthChanged OnStrengthChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Health = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Mana = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float MaxMana = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Stamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float MaxStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Strength, Category="Aedir|Stats")
	float Strength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Athletism = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	TArray<float> Skills;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_IsDead, Category="Aedir|Stats")
	bool bIsDead = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float HealthRegenPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float ManaRegenPerSecond = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StaminaRegenPerSecond = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StaminaRegenDelay = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float MaxStaminaGainPerStaminaSpent = 0.0005f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float MaxStaminaCap = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StrengthMin = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StrengthMax = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float MinSizeScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float MaxSizeScale = 1.2f;

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeStamina(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeMana(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	float ApplyDamage(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void Heal(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void AddSkillExperience(EAedirSkill Skill, float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void SetStrength(float NewStrength);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void SetAthletism(float NewAthletism);

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetHealthFraction() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetManaFraction() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetStaminaFraction() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSkill(EAedirSkill Skill) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSizeScale() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSpeedMultiplier() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetJumpHeightMultiplier() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetCarryCapacity() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	bool IsDead() const;

private:
	bool IsAuthoritative() const;

	UFUNCTION()
	void OnRep_Stats();

	UFUNCTION()
	void OnRep_Strength();

	UFUNCTION()
	void OnRep_IsDead();

	double LastStaminaConsumptionTime = -1000000.0;
};
