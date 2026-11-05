#pragma once

#include "CoreMinimal.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "AedirBalanceComponent.generated.h"

class ACharacter;
class UCapsuleComponent;
class UCharacterMovementComponent;
class UAedirStatsComponent;
class UPrimitiveComponent;
class UPhysicalAnimationComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAedirBalanceEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FAedirHitReaction, EAedirBodyZone, Zone, float, Energy, bool, bStaggered, bool, bKnockedDown);

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirBalanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirBalanceComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Balance")
	FAedirBalanceEvent OnBalanceLost;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Balance")
	FAedirBalanceEvent OnFell;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Balance")
	FAedirBalanceEvent OnGotUp;

	// Server only. Fired once per resolved hit with the outcome of the hit reaction.
	UPROPERTY(BlueprintAssignable, Category="Aedir|Balance")
	FAedirHitReaction OnHitReaction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	FName PelvisBoneName = FName(TEXT("pelvis"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float MaxLeanDegrees = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float RecoveryPerSecond = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallBalanceThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallDelaySeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float GetUpBalanceThreshold = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float MinRagdollSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float WallImpactSpeedThreshold = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float ObjectImpactMassSpeedThreshold = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallSpeedBalanceThreshold = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallDamagePerSpeed = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float ImpactBalanceScale = 0.0005f;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Balance")
	float Balance = 1.f;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Balance")
	FVector2D LeanDirection = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Ragdoll, Category="Aedir|Balance")
	bool bIsRagdoll = false;

	// Remaining seconds of stagger. Server writes it, clients read it.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Balance")
	float StaggerSeconds = 0.f;

	// Remaining seconds of daze from head hits. Server writes it, clients read it.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Balance")
	float DazeSeconds = 0.f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void ApplyBalanceImpact(FVector WorldDirection, float Magnitude);

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void ApplyImpulseImpact(FVector WorldImpulse);

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void HandleLanded(float LandingSpeed);

	// Server only. Resolves a physical hit into balance loss, bone impulse, stagger, knockdown and daze.
	void ApplyHitReaction(const FAedirImpactInfo& Impact);

	// Pure model helpers, shared by the hit reaction and by tests.
	static float ComputeBalanceLoss(EAedirBodyZone Zone, float EnergyJoules, float Athletism, float StaminaFraction);
	static bool ShouldStagger(float BalanceLoss);
	static float GetKnockdownEnergy(EAedirBodyZone Zone);
	static float ComputeDazeSeconds(EAedirBodyZone Zone, float EnergyJoules);
	static float ComputeStaggerSpeed(float EnergyJoules);

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetBalance() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsRagdoll() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsStaggering() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsDazed() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetDazeSeconds() const;

	// 0 when not dazed, 1 at the full daze duration.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetDazeFactor() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	FRotator GetPelvisLean() const;

private:
	bool IsAuthoritative() const;
	UAedirStatsComponent* FindStats() const;
	void SetBalance(float NewBalance);
	void DecayLeanDirection(float Amount);
	void UpdateFallState();
	void UpdateServerState(float DeltaTime);
	void EnterRagdoll(float MinimumSeconds);
	void UpdatePelvisLean(float DeltaTime);
	void DrivePelvis();
	void SetRagdollPresentation(bool bRagdoll);
	void ApplyRagdollPhysics(bool bRagdoll);
	void ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit);
	void SetupMotors();
	void UpdateMotors(float DeltaTime, bool bForce);
	void ApplyMotorGroup(int32 GroupIndex, float Strength);

	UFUNCTION()
	void OnRep_Ragdoll();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBoneImpulse(FName Bone, FVector Impulse, FVector Location);

	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CharacterOwner;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UCapsuleComponent> BodyCapsule;

	// Physical motors: every simulated limb body is driven towards the procedural pose; the strength scales with stamina, injury and daze.
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalAnimationComponent> MotorComponent;

	TArray<FName> MotorGroupBones[5];
	float AppliedMotorStrength[5] = { -1.f, -1.f, -1.f, -1.f, -1.f };
	float MotorUpdateTimer = 0.f;

	FRotator CurrentLean = FRotator::ZeroRotator;
	float TimeBelowThreshold = 0.f;
	double RagdollStartTime = 0.0;
	float CurrentRagdollMinSeconds = 0.f;
	float LastFallSpeed = 0.f;
	float PelvisRestHeight = 0.f;
	bool bWasFalling = false;
};
