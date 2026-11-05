#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "AedirBalanceComponent.generated.h"

class ACharacter;
class UCapsuleComponent;
class UCharacterMovementComponent;
class UAedirStatsComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAedirBalanceEvent);

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	FName PelvisBoneName = FName(TEXT("hip"));

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

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void ApplyBalanceImpact(FVector WorldDirection, float Magnitude);

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void ApplyImpulseImpact(FVector WorldImpulse);

	UFUNCTION(BlueprintCallable, Category="Aedir|Balance")
	void HandleLanded(float LandingSpeed);

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetBalance() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsRagdoll() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	FRotator GetPelvisLean() const;

private:
	bool IsAuthoritative() const;
	UAedirStatsComponent* FindStats() const;
	void SetBalance(float NewBalance);
	void DecayLeanDirection(float Amount);
	void UpdateFallState();
	void UpdateServerState(float DeltaTime);
	void UpdatePelvisLean(float DeltaTime);
	void DrivePelvis();
	void SetRagdollPresentation(bool bRagdoll);
	void ApplyRagdollPhysics(bool bRagdoll);
	void ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit);

	UFUNCTION()
	void OnRep_Ragdoll();

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

	FRotator CurrentLean = FRotator::ZeroRotator;
	float TimeBelowThreshold = 0.f;
	double RagdollStartTime = 0.0;
	float LastFallSpeed = 0.f;
	float PelvisRestHeight = 0.f;
	bool bWasFalling = false;
};
