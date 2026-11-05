#include "AedirStatsComponent.h"

#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float AedirStatMin = 1.f;
	constexpr float AedirStatMax = 100.f;
	constexpr float AedirSkillMin = 0.f;
	constexpr float AedirSkillMax = 100.f;
	constexpr float AedirSpeedPerAthletism = 0.5f;
	constexpr float AedirJumpPerAthletism = 0.4f;
	constexpr float AedirCarryCapacityPerStrength = 2.f;

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

	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastStaminaConsumptionTime >= StaminaRegenDelay)
	{
		const float NewStamina = FMath::Min(MaxStamina, Stamina + StaminaRegenPerSecond * DeltaTime);
		bChanged |= NewStamina != Stamina;
		Stamina = NewStamina;
	}

	const float NewHealth = FMath::Min(MaxHealth, Health + HealthRegenPerSecond * DeltaTime);
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

void UAedirStatsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirStatsComponent, Health);
	DOREPLIFETIME(UAedirStatsComponent, MaxHealth);
	DOREPLIFETIME(UAedirStatsComponent, Mana);
	DOREPLIFETIME(UAedirStatsComponent, MaxMana);
	DOREPLIFETIME(UAedirStatsComponent, Stamina);
	DOREPLIFETIME(UAedirStatsComponent, MaxStamina);
	DOREPLIFETIME(UAedirStatsComponent, Strength);
	DOREPLIFETIME(UAedirStatsComponent, Athletism);
	DOREPLIFETIME(UAedirStatsComponent, Skills);
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
	if (!IsAuthoritative() || bIsDead || Amount <= 0.f)
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
	return SafeFraction(Stamina, MaxStamina);
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
	return FMath::Lerp(MinSizeScale, MaxSizeScale, Alpha);
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

void UAedirStatsComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		OnDeath.Broadcast();
	}

	OnStatChanged.Broadcast();
}
