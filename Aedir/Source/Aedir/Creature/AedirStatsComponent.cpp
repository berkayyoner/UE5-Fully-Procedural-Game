#include "AedirStatsComponent.h"

#include "Creature/AedirRaceCatalog.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float StatsHungerSeconds = 2400.f;
	constexpr float StatsThirstSeconds = 1500.f;
	constexpr float StatsNeedRunSpeed = 500.f;
	constexpr float StatsNeedEffortShare = 0.6f;
	constexpr float StatsNeedFeltBelow = 25.f;
	constexpr float StatsNeedRegenLoss = 0.6f;
	constexpr float StatsEnergyFeltBelow = 70.f;
	constexpr float StatsEnergyNeedLoss = 0.75f;
	constexpr float StatsAthletismBase = 10.f;
	constexpr float StatsAthletismMaxStaminaGain = 0.8f;
	constexpr float StatsAthletismRegenGain = 1.2f;
	constexpr float StatsStarveDamagePerSecond = 0.25f;
	constexpr float AedirStatMin = 1.f;
	constexpr float AedirStatMax = 100.f;
	constexpr float AedirSkillMin = 0.f;
	constexpr float AedirSkillMax = 100.f;
	constexpr float AedirSpeedPerAthletism = 0.5f;
	constexpr float AedirJumpPerAthletism = 0.4f;
	constexpr float AedirCarryCapacityPerStrength = 2.f;
	constexpr float AedirExperienceBankStep = 0.05f;
	constexpr float AedirFatigueStartFraction = 0.5f;

	float SafeFraction(float Value, float Max)
	{
		return Max > 0.f ? Value / Max : 0.f;
	}
}

UAedirStatsComponent::UAedirStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);

	Skills.Init(0.f, static_cast<int32>(EAedirSkill::Count));
}

void UAedirStatsComponent::InitializeComponent()
{
	Super::InitializeComponent();

	const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
	if (Skills.Num() != SkillCount)
	{
		Skills.SetNumZeroed(SkillCount);
	}
}

void UAedirStatsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsAuthoritative() || bIsDead)
	{
		return;
	}

	bool bChanged = false;

	// A body at work burns through both faster. Thirst comes first.
	const AActor* Body = GetOwner();
	const float Effort = 1.f + FMath::Clamp(static_cast<float>(Body ? Body->GetVelocity().Size2D() : 0.0) / StatsNeedRunSpeed, 0.f, 1.f) * StatsNeedEffortShare;
	Hunger = FMath::Max(Hunger - DeltaTime * Effort * 100.f / StatsHungerSeconds, 0.f);
	Thirst = FMath::Max(Thirst - DeltaTime * Effort * 100.f / StatsThirstSeconds, 0.f);
	if (Hunger <= 0.f || Thirst <= 0.f)
	{
		ApplyDamage(StatsStarveDamagePerSecond * DeltaTime);
		if (bIsDead)
		{
			return;
		}
	}

	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastStaminaConsumptionTime >= StaminaRegenDelay)
	{
		const float NewStamina = FMath::Min(GetMaxStamina(), Stamina + GetStaminaRegenRate() * DeltaTime);
		bChanged |= NewStamina != Stamina;
		Stamina = NewStamina;
	}

	const float NewHealth = FMath::Min(MaxHealth, Health + HealthRegenPerSecond * (1.f - GetNeedShare()) * DeltaTime);
	bChanged |= NewHealth != Health;
	Health = NewHealth;

	const float NewMana = FMath::Min(MaxMana, Mana + ManaRegenPerSecond * DeltaTime);
	bChanged |= NewMana != Mana;
	Mana = NewMana;

	if (bChanged)
	{
		OnStatChanged.Broadcast();
	}
}

void UAedirStatsComponent::AddFood(float FoodAmount, float DrinkAmount)
{
	Hunger = FMath::Clamp(Hunger + FoodAmount, 0.f, 100.f);
	Thirst = FMath::Clamp(Thirst + DrinkAmount, 0.f, 100.f);
}

void UAedirStatsComponent::SetNeeds(float NewHunger, float NewThirst)
{
	Hunger = FMath::Clamp(NewHunger, 0.f, 100.f);
	Thirst = FMath::Clamp(NewThirst, 0.f, 100.f);
}

float UAedirStatsComponent::GetAthleticsShare() const
{
	return FMath::Clamp((Athletism - StatsAthletismBase) / (AedirStatMax - StatsAthletismBase), 0.f, 1.f);
}

float UAedirStatsComponent::GetStaminaRegenRate() const
{
	// The hungrier and thirstier, the slower the breath comes back. A trained body gets it back sooner.
	const float NeedLoss = FMath::Clamp(1.f - FMath::Min(Hunger, Thirst) / StatsEnergyFeltBelow, 0.f, 1.f) * StatsEnergyNeedLoss;
	return StaminaRegenPerSecond * (1.f + GetAthleticsShare() * StatsAthletismRegenGain) * (1.f - NeedLoss);
}

float UAedirStatsComponent::GetNeedShare() const
{
	return FMath::Clamp(1.f - FMath::Min(Hunger, Thirst) / StatsNeedFeltBelow, 0.f, 1.f);
}

void UAedirStatsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirStatsComponent, Hunger);
	DOREPLIFETIME(UAedirStatsComponent, Thirst);

	DOREPLIFETIME(UAedirStatsComponent, Health);
	DOREPLIFETIME(UAedirStatsComponent, MaxHealth);
	DOREPLIFETIME(UAedirStatsComponent, Mana);
	DOREPLIFETIME(UAedirStatsComponent, MaxMana);
	DOREPLIFETIME(UAedirStatsComponent, Stamina);
	DOREPLIFETIME(UAedirStatsComponent, MaxStamina);
	DOREPLIFETIME(UAedirStatsComponent, Strength);
	DOREPLIFETIME(UAedirStatsComponent, Athletism);
	DOREPLIFETIME(UAedirStatsComponent, Skills);
	DOREPLIFETIME(UAedirStatsComponent, RaceSizeMultiplier);
	DOREPLIFETIME(UAedirStatsComponent, bIsDead);

	DOREPLIFETIME_CONDITION(UAedirStatsComponent, HealthRegenPerSecond, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, ManaRegenPerSecond, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, StaminaRegenPerSecond, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, StaminaRegenDelay, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, MaxStaminaGainPerStaminaSpent, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, MaxStaminaCap, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, StrengthMin, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, StrengthMax, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, MinSizeScale, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UAedirStatsComponent, MaxSizeScale, COND_InitialOnly);

	DOREPLIFETIME(UAedirStatsComponent, bInvulnerable);
}

bool UAedirStatsComponent::ConsumeStamina(float Amount)
{
	if (!IsAuthoritative() || Amount < 0.f || Stamina < Amount)
	{
		return false;
	}

	Stamina -= Amount;

	if (const UWorld* World = GetWorld())
	{
		LastStaminaConsumptionTime = World->GetTimeSeconds();
	}

	if (MaxStamina < MaxStaminaCap)
	{
		MaxStamina = FMath::Min(MaxStaminaCap, MaxStamina + Amount * MaxStaminaGainPerStaminaSpent);
	}

	OnStatChanged.Broadcast();
	return true;
}

bool UAedirStatsComponent::ConsumeMana(float Amount)
{
	if (!IsAuthoritative() || Amount < 0.f || Mana < Amount)
	{
		return false;
	}

	Mana -= Amount;
	OnStatChanged.Broadcast();
	return true;
}

float UAedirStatsComponent::ApplyDamage(float Amount)
{
	if (!IsAuthoritative() || bIsDead || bInvulnerable || Amount <= 0.f)
	{
		return 0.f;
	}

	const float AppliedDamage = FMath::Min(Amount, Health);
	Health -= AppliedDamage;

	if (Health <= 0.f)
	{
		Health = 0.f;
		bIsDead = true;
		OnDeath.Broadcast();
	}

	OnStatChanged.Broadcast();
	return AppliedDamage;
}

void UAedirStatsComponent::Heal(float Amount)
{
	if (!IsAuthoritative() || bIsDead || Amount <= 0.f)
	{
		return;
	}

	Health = FMath::Min(MaxHealth, Health + Amount);
	OnStatChanged.Broadcast();
}

void UAedirStatsComponent::RestoreAll()
{
	if (!IsAuthoritative())
	{
		return;
	}

	Health = MaxHealth;
	Mana = MaxMana;
	Stamina = GetMaxStamina();
	bIsDead = false;

	OnStatChanged.Broadcast();
}

void UAedirStatsComponent::AddSkillExperience(EAedirSkill Skill, float Amount)
{
	const int32 Index = static_cast<int32>(Skill);
	if (!IsAuthoritative() || !Skills.IsValidIndex(Index))
	{
		return;
	}

	Skills[Index] = FMath::Clamp(Skills[Index] + Amount, AedirSkillMin, AedirSkillMax);
	OnStatChanged.Broadcast();
}

void UAedirStatsComponent::SetStrength(float NewStrength)
{
	if (!IsAuthoritative())
	{
		return;
	}

	Strength = FMath::Clamp(NewStrength, AedirStatMin, AedirStatMax);
	OnStatChanged.Broadcast();
	OnStrengthChanged.Broadcast(GetSizeScale());
}

void UAedirStatsComponent::SetAthletism(float NewAthletism)
{
	if (!IsAuthoritative())
	{
		return;
	}

	Athletism = FMath::Clamp(NewAthletism, AedirStatMin, AedirStatMax);
	OnStatChanged.Broadcast();
}

void UAedirStatsComponent::AddStrengthExperience(float Amount)
{
	StrengthExperienceBank += Amount;
	if (StrengthExperienceBank >= AedirExperienceBankStep)
	{
		SetStrength(Strength + StrengthExperienceBank);
		StrengthExperienceBank = 0.f;
	}
}

void UAedirStatsComponent::AddAthleticsExperience(float Amount)
{
	AthleticsExperienceBank += Amount;
	if (AthleticsExperienceBank >= AedirExperienceBankStep)
	{
		SetAthletism(Athletism + AthleticsExperienceBank);
		AthleticsExperienceBank = 0.f;
	}
}

float UAedirStatsComponent::GetFatigue() const
{
	return FMath::Clamp((AedirFatigueStartFraction - GetStaminaFraction()) / AedirFatigueStartFraction, 0.f, 1.f);
}

void UAedirStatsComponent::ApplyRace(const FAedirRaceDef& Race)
{
	if (!IsAuthoritative())
	{
		return;
	}

	MaxHealth = Race.MaxHealth;
	Health = MaxHealth;
	MaxMana = Race.MaxMana;
	Mana = MaxMana;
	MaxStamina = FMath::Min(Race.MaxStamina, MaxStaminaCap);
	Stamina = MaxStamina;
	bIsDead = false;

	Strength = FMath::Clamp(Race.Strength, AedirStatMin, AedirStatMax);
	Athletism = FMath::Clamp(Race.Athletism, AedirStatMin, AedirStatMax);
	RaceSizeMultiplier = Race.HeightMultiplier;

	const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
	if (Skills.Num() != SkillCount)
	{
		Skills.SetNumZeroed(SkillCount);
	}

	for (const FAedirSkillBonus& Bonus : Race.SkillBonuses)
	{
		const int32 Index = static_cast<int32>(Bonus.Skill);
		if (Skills.IsValidIndex(Index))
		{
			Skills[Index] = FMath::Clamp(UAedirRaceCatalog::GetStartSkillValue(Race, Bonus.Skill), AedirSkillMin, AedirSkillMax);
		}
	}

	OnStatChanged.Broadcast();
	OnStrengthChanged.Broadcast(GetSizeScale());
}

float UAedirStatsComponent::GetHealthFraction() const
{
	return SafeFraction(Health, MaxHealth);
}

float UAedirStatsComponent::GetManaFraction() const
{
	return SafeFraction(Mana, MaxMana);
}

float UAedirStatsComponent::GetStamina() const
{
	return Stamina;
}

float UAedirStatsComponent::GetStaminaFraction() const
{
	return SafeFraction(Stamina, GetMaxStamina());
}

float UAedirStatsComponent::GetMaxStamina() const
{
	return MaxStamina * (1.f + GetAthleticsShare() * StatsAthletismMaxStaminaGain);
}

float UAedirStatsComponent::GetHealth() const
{
	return Health;
}

float UAedirStatsComponent::GetMaxHealth() const
{
	return MaxHealth;
}

float UAedirStatsComponent::GetMana() const
{
	return Mana;
}

float UAedirStatsComponent::GetMaxMana() const
{
	return MaxMana;
}

float UAedirStatsComponent::GetSkill(EAedirSkill Skill) const
{
	const int32 Index = static_cast<int32>(Skill);
	return Skills.IsValidIndex(Index) ? Skills[Index] : 0.f;
}

float UAedirStatsComponent::GetSizeScale() const
{
	const float Range = StrengthMax - StrengthMin;
	const float Alpha = Range > 0.f ? FMath::Clamp((Strength - StrengthMin) / Range, 0.f, 1.f) : 0.f;
	const float StrengthScale = FMath::Lerp(MinSizeScale, MaxSizeScale, Alpha);
	return UAedirRaceCatalog::ComputeRaceSizeScale(StrengthScale, RaceSizeMultiplier);
}

float UAedirStatsComponent::GetSpeedMultiplier() const
{
	return 1.f + Athletism / AedirStatMax * AedirSpeedPerAthletism;
}

float UAedirStatsComponent::GetJumpHeightMultiplier() const
{
	return 1.f + Athletism / AedirStatMax * AedirJumpPerAthletism;
}

float UAedirStatsComponent::GetCarryCapacity() const
{
	return Strength * AedirCarryCapacityPerStrength;
}

bool UAedirStatsComponent::IsDead() const
{
	return bIsDead;
}

void UAedirStatsComponent::ApplySavedState(float InHealth, float InMaxHealth, float InMana, float InMaxMana, float InStamina, float InMaxStamina, float InStrength, float InAthletism, const TArray<float>& InSkills)
{
	if (!IsAuthoritative())
	{
		return;
	}

	MaxHealth = FMath::Max(0.f, InMaxHealth);
	Health = FMath::Clamp(InHealth, 0.f, MaxHealth);
	bIsDead = Health <= 0.f;

	MaxMana = FMath::Max(0.f, InMaxMana);
	Mana = FMath::Clamp(InMana, 0.f, MaxMana);

	MaxStamina = FMath::Clamp(InMaxStamina, 0.f, MaxStaminaCap);
	Athletism = FMath::Clamp(InAthletism, AedirStatMin, AedirStatMax);
	Stamina = FMath::Clamp(InStamina, 0.f, GetMaxStamina());

	Strength = FMath::Clamp(InStrength, AedirStatMin, AedirStatMax);
	Athletism = FMath::Clamp(InAthletism, AedirStatMin, AedirStatMax);

	const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
	Skills.SetNumZeroed(SkillCount);
	for (int32 Index = 0; Index < SkillCount && Index < InSkills.Num(); ++Index)
	{
		Skills[Index] = FMath::Clamp(InSkills[Index], AedirSkillMin, AedirSkillMax);
	}

	OnStatChanged.Broadcast();
	OnStrengthChanged.Broadcast(GetSizeScale());
}

bool UAedirStatsComponent::IsAuthoritative() const
{
	return GetOwner() != nullptr && GetOwner()->HasAuthority();
}

void UAedirStatsComponent::OnRep_Stats()
{
	OnStatChanged.Broadcast();
}

void UAedirStatsComponent::OnRep_Strength()
{
	OnStatChanged.Broadcast();
	OnStrengthChanged.Broadcast(GetSizeScale());
}

void UAedirStatsComponent::OnRep_RaceSizeMultiplier()
{
	OnStrengthChanged.Broadcast(GetSizeScale());
}

void UAedirStatsComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		OnDeath.Broadcast();
	}

	OnStatChanged.Broadcast();
}
