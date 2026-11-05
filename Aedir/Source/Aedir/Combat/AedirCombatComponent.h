#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/AedirCombatTypes.h"
#include "Items/AedirItemTypes.h"
#include "AedirCombatComponent.generated.h"

class AAedirCreature;
class AAedirItemActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAedirHitDealt, AAedirCreature*, Victim, float, Damage);

USTRUCT(BlueprintType)
struct AEDIR_API FAedirSwingState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	EAedirSwingType Type = EAedirSwingType::Jab;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	float Elapsed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	float Duration = 0.f;

	// Swing direction in view space: X is mouse right, Y is mouse up, normalized.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	FVector2D Direction = FVector2D::ZeroVector;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirCombatComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirHitDealt OnHitDealt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float FistDamage = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float StrengthDamageScale = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingStaminaCost = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingDurationBase = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingReach = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float HitRadius = 18.f;

	// Mouse units per second needed to trigger a swing from the guard.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float MinSwingMouseSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingCooldown = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float BalanceImpactScale = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float AIAttackRange = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float AISightRange = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float AISightHalfAngleDeg = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float AIAttackInterval = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float AIApproachSpeedStopDistance = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	bool bLeadFootRight = true;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	bool bLeftGuard = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	bool bRightGuard = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	FAedirSwingState LeftSwing;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	FAedirSwingState RightSwing;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Owning client sends the change to the server; the server applies it to the replicated guard flag.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void SetGuard(EAedirHand Hand, bool bActive);

	// Local player only. MouseDeltaThisFrame uses raw mouse axis values (MouseY is positive when the mouse moves down).
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void AddSwingInput(EAedirHand Hand, FVector2D MouseDeltaThisFrame, float DeltaTime);

	// Server or NPC entry point. Validates guard, cooldown and stamina before starting the swing.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	bool StartSwing(EAedirHand Hand, EAedirSwingType Type);

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsGuardActive(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsInFightStance() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool GetGuardPose(EAedirHand Hand, float& OutAlpha) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool GetSwingPose(EAedirHand Hand, FVector& OutHandTargetWorld, float& OutAlpha) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	float GetFistAlpha(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsLeadFootRight() const;

private:
	bool IsAuthoritative() const;
	static int32 HandIndex(EAedirHand Hand);
	FAedirSwingState& GetSwingState(EAedirHand Hand);
	const FAedirSwingState& GetSwingState(EAedirHand Hand) const;
	AAedirItemActor* GetHeldItem(EAedirHand Hand) const;

	void SetGuardInternal(EAedirHand Hand, bool bActive);
	bool CanStartSwing(EAedirHand Hand) const;
	float ComputeWeightFactor(EAedirHand Hand) const;
	float ComputeSwingDuration(EAedirHand Hand) const;
	bool TryStartSwing(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction);
	void StartSwingInternal(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction);
	void UpdateSwings(float DeltaTime);
	void ProcessStrikeHit(EAedirHand Hand, const FAedirSwingState& State, const FVector& From, const FVector& To);
	void ApplyCreatureHit(EAedirHand Hand, const FAedirSwingState& State, AAedirCreature* Victim, const FVector& Direction);
	FVector GetSwingWorldTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const;
	FVector GetSwingLocalTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const;

	void UpdateAI(float DeltaTime);
	void UpdateAITarget(double Now);
	bool CanSeeTarget(const AAedirCreature* Target) const;

	UFUNCTION(Server, Reliable)
	void ServerSetGuard(EAedirHand Hand, bool bActive);

	UFUNCTION(Server, Reliable)
	void ServerStartSwing(EAedirHand Hand, EAedirSwingType Type, FVector2D Direction);

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> Creature;

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> AITarget;

	float GuardAlpha[2] = { 0.f, 0.f };
	float SwingAlpha[2] = { 0.f, 0.f };
	float SwingCooldownRemaining[2] = { 0.f, 0.f };
	FVector2D MouseVelocity[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	FVector PreviousHandTarget[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bSwingHitRegistered[2] = { false, false };

	float AIRetargetTimer = 0.f;
	float AIAttackTimer = 0.f;
	double AILastSeenTime = 0.0;
	bool bAIApproaching = false;
	bool bAINextLeft = true;
};
