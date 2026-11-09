#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirCreatureTypes.h"
#include "AedirStatsComponent.generated.h"

struct FAedirRaceDef;

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

	// Height multiplier of the chosen race, applied on top of the strength-derived size.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_RaceSizeMultiplier, Category="Aedir|Stats")
	float RaceSizeMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_IsDead, Category="Aedir|Stats")
	bool bIsDead = false;

	// Out of health but not yet dead: down, unable to rise, slipping away over some seconds unless healed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	bool bIsDying = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float HealthRegenPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float ManaRegenPerSecond = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StaminaRegenPerSecond = 12.f;

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

	// How thick the trunk and limbs are against the plain body, from the weakest to the strongest: a weak body is lean, a
	// strong one broad and heavy with muscle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	float MinBulkScale = 0.86f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	float MaxBulkScale = 1.2f;

	// While set, ApplyDamage ignores every hit, including bleeding.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	bool bInvulnerable = false;

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeStamina(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeMana(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	float ApplyDamage(float Amount);

	// How full and how watered the body is, 100 to 0. Empty, it weakens and starts to take harm. Server only for changes.
	void AddFood(float FoodAmount, float DrinkAmount);
	float GetHunger() const { return Hunger; }
	float GetThirst() const { return Thirst; }
	void SetNeeds(float NewHunger, float NewThirst);
	// How much the lack of food or water tells on the body: 0 fine, 1 starved or parched.
	// Athletics make for more energy and bring it back faster, from 0 at the start to 1 at the top.
	float GetAthleticsShare() const;
	float GetStaminaRegenRate() const;
	float GetBaseMaxStamina() const { return MaxStamina; }

	float GetNeedShare() const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void Heal(float Amount);

	// Server only. Refills health, mana and stamina and clears the dead flag.
	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void RestoreAll();

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void AddSkillExperience(EAedirSkill Skill, float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void SetStrength(float NewStrength);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	void SetAthletism(float NewAthletism);

	// Sets what is left of breath, magicka or a skill straight away, within its limits.
	void SetStamina(float NewStamina);
	void SetMana(float NewMana);
	void SetSkill(EAedirSkill Skill, float NewLevel);

	// Server only. Tiny gains from use, gathered up so the body does not change every frame.
	void AddStrengthExperience(float Amount);
	void AddAthleticsExperience(float Amount);

	// Server only. Applies a race's starting stats, refills the pools and adds its skill bonuses.
	void ApplyRace(const FAedirRaceDef& Race);

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetHealthFraction() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetManaFraction() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetStaminaFraction() const;

	// How out of breath: nothing above half the stamina, 1 with none left.
	float GetFatigue() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetMaxHealth() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float Hunger = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float Thirst = 100.f;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetMana() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetMaxMana() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSkill(EAedirSkill Skill) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSizeScale() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetSpeedMultiplier() const;

	// Thickness of trunk and limbs from strength, 1 for a plain body.
	float GetBulkScale() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetJumpHeightMultiplier() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	float GetCarryCapacity() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Stats")
	bool IsDead() const;
	bool IsDying() const { return bIsDying; }

	// Death at once, as when the head is off. Everything else that empties the health leaves a creature dying first.
	void Die();

	// Server only. Restores persisted values, clamping them to the valid ranges, and rebroadcasts the change delegates.
	void ApplySavedState(float InHealth, float InMaxHealth, float InMana, float InMaxMana, float InStamina, float InMaxStamina, float InStrength, float InAthletism, const TArray<float>& InSkills);

private:
	bool IsAuthoritative() const;

	UFUNCTION()
	void OnRep_Stats();

	UFUNCTION()
	void OnRep_Strength();

	UFUNCTION()
	void OnRep_RaceSizeMultiplier();

	UFUNCTION()
	void OnRep_IsDead();

	float DyingSeconds = 0.f;

	double LastStaminaConsumptionTime = -1000000.0;
	float StrengthExperienceBank = 0.f;
	float AthleticsExperienceBank = 0.f;
};
