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

	// How much of its wits and its strength the body has about it: blows take it, it comes back soon. With next to none left
	// the body drops senseless. It kills nobody: that is the blood's doing, and there is never more health than there is blood.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Health = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float MaxHealth = 100.f;

	// The blood in the body, out of 100. Wounds that bleed let it out and wounds not yet closed leave the body room for less
	// of it; with none left the body dies.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats|Blood")
	float Blood = 100.f;

	// How much blood the body can hold as wounded as it is.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats|Blood")
	float BloodCapacity = 100.f;

	// Blood comes back by itself at this much a second, slower hungry or thirsty; what has been eaten and drunk is made into
	// blood at BloodMakingPerSecond, this much of it for each point of food and of drink, at most BloodMakingMax waiting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0"))
	float BloodRegenPerSecond = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0"))
	float BloodMakingPerSecond = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0"))
	float BloodPerFood = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0"))
	float BloodPerDrink = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0"))
	float BloodMakingMax = 60.f;

	// A blow (not a fist's) also lets this share of what it takes off the health out of the blood, inside the body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats|Blood", meta=(ClampMin="0", ClampMax="1"))
	float BlowBloodShare = 0.2f;

	// Fists, kicks and bumps take the health no lower than this share of all of it, and at that the body drops senseless; it
	// comes round once it has HealthWakeShare of its health again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats", meta=(ClampMin="0", ClampMax="1"))
	float KnockHealthFloor = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats", meta=(ClampMin="0", ClampMax="1"))
	float HealthWakeShare = 0.25f;

	// Health comes back once nothing has hurt the body for this long (seconds).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats", meta=(ClampMin="0"))
	float HealthRegenDelay = 3.f;

	// Lets this much blood out of the body; none left, it dies.
	void LoseBlood(float Amount);
	void AddBlood(float Amount);
	// Blood the body goes on making out of what it has eaten and drunk.
	void AddBloodMaking(float Amount);
	void SetBloodCapacity(float NewCapacity);
	float GetBlood() const { return Blood; }
	float GetBloodFraction() const { return FMath::Clamp(Blood / 100.f, 0.f, 1.f); }
	float GetBloodCapacityFraction() const { return FMath::Clamp(BloodCapacity / 100.f, 0.f, 1.f); }
	float GetBloodMaking() const { return BloodMaking; }
	// The most health the blood left allows.
	float GetHealthCap() const { return MaxHealth * GetBloodFraction(); }
	void SetBlood(float NewBlood);

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

	// How much the body still knows of the world, out of 100: blows to the head and a hand at the throat take it, it comes
	// back a few seconds after the last of them. Low, the body moves, swings and keeps its feet worse; at nothing it drops
	// senseless and lies there until enough of it is back.
	UPROPERTY(BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float Consciousness = 100.f;

	UPROPERTY(BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	bool bKnockedOut = false;

	void LoseConsciousness(float Amount);

	// One blow to the head that takes at least the least of KnockoutHeadShareMinMax of the full health knocks a body that
	// lives through it senseless, for the least of KnockoutSecondsMinMax at that share up to the most at the most of it,
	// in proportion between; it comes round only then.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	FVector2D KnockoutHeadShareMinMax = FVector2D(0.5f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	FVector2D KnockoutSecondsMinMax = FVector2D(10.f, 60.f);

	void TakeHeadBlow(float Damage);
	// How long a knock to the head still keeps the body senseless, in seconds.
	float GetKnockoutSecondsLeft() const { return KnockoutSecondsLeft; }
	float GetConsciousnessFraction() const { return Consciousness / 100.f; }
	bool IsKnockedOut() const { return bKnockedOut; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	float Athletism = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Stats, Category="Aedir|Stats")
	TArray<float> Skills;

	// Height multiplier of the chosen race, applied on top of the strength-derived size.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_RaceSizeMultiplier, Category="Aedir|Stats")
	float RaceSizeMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_IsDead, Category="Aedir|Stats")
	bool bIsDead = false;

	// Out of blood but not yet dead: down, unable to rise, slipping away over some seconds unless blood is got into it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	bool bIsDying = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float HealthRegenPerSecond = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float ManaRegenPerSecond = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StaminaRegenPerSecond = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Aedir|Stats")
	float StaminaRegenDelay = 0.6f;

	// Hurt, the breath comes back slower: with no health left only this share as fast, in proportion between.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Stats", meta=(ClampMin="0", ClampMax="1"))
	float HurtStaminaRegenShare = 0.25f;

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
	float MaxSizeScale = 1.12f;

	// How thick the trunk and limbs are against the plain body, from the weakest to the strongest: a weak body is lean, a
	// strong one broad and heavy with muscle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	float MinBulkScale = 0.86f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	float MaxBulkScale = 1.12f;

	// While set, ApplyDamage ignores every hit, including bleeding.
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category="Aedir|Stats")
	bool bInvulnerable = false;

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeStamina(float Amount);

	// A steady small effort, like the arm carrying a weapon about, wears breath down without stopping it from coming back.
	bool ConsumeStaminaEffort(float Amount, bool bPausesRegen);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	bool ConsumeMana(float Amount);

	UFUNCTION(BlueprintCallable, Category="Aedir|Stats")
	float ApplyDamage(float Amount, EAedirHarm Harm = EAedirHarm::Blow);

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
	// How strong the body is, 0 at StrengthMin to 1 at StrengthMax.
	float GetStrengthShare() const;
	// How many times as big as at the usual strength (10) its strength makes the body: reaches, feet and the like grow with it.
	float GetStrengthGrowth() const;

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

	// Death at once, as when the head is off. Everything else that empties the blood leaves a creature dying first.
	void Die();

	// Server only. Restores persisted values, clamping them to the valid ranges, and rebroadcasts the change delegates.
	void ApplySavedState(float InHealth, float InMaxHealth, float InMana, float InMaxMana, float InStamina, float InMaxStamina, float InStrength, float InAthletism, const TArray<float>& InSkills, float InBlood = 100.f);

private:
	// Seconds since the senses were last knocked or choked.
	float ConsciousnessQuietSeconds = 100.f;
	float KnockoutSecondsLeft = 0.f;
	float HealthQuietSeconds = 100.f;
	float BloodMaking = 0.f;
	// Drops senseless for want of health.
	void GoSenseless(const TCHAR* Why);

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
