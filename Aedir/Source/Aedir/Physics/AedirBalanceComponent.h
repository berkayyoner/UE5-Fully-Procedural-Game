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

	// Support foot bones. The support polygon is built from the heel and toe points of every grounded foot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Support")
	FName FootLeftBone = FName(TEXT("foot_l"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Support")
	FName FootRightBone = FName(TEXT("foot_r"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Support")
	FName BallLeftBone = FName(TEXT("ball_l"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Support")
	FName BallRightBone = FName(TEXT("ball_r"));

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

	// Pure centre of mass model helpers, shared by the balance update and by tests.
	// OutHull receives the convex hull in counter-clockwise order (monotone chain).
	static void ComputeConvexHull2D(const TArray<FVector2D>& Points, TArray<FVector2D>& OutHull);

	// Signed distance in cm from Point to the hull boundary: positive inside, negative outside.
	// Hulls with fewer than three points return the negative distance to the nearest point or segment.
	static float ComputeSupportMargin(const TArray<FVector2D>& Hull, const FVector2D& Point);

	// Balance drained this tick while the centre of mass projection is outside the support polygon.
	static float ComputeComBalanceLoss(float MarginCm, float DeltaSeconds);

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

	// Mass-weighted centre of mass of the body, sampled every 0.1 s. Zero until the first sample.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	FVector GetCenterOfMassWorld() const;

	// Signed support margin in cm, positive while the centre of mass projection is inside the support polygon.
	// Zero while airborne or ragdolled.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetSupportMargin() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsCenterOfMassOutsideSupport() const;

	// True while the support margin is below -2 cm and the creature is not ragdolled. OutDirectionWorld is the horizontal
	// unit direction from the support centroid towards the centre of mass, OutUrgency is clamp(-margin / 12, 0, 1).
	bool GetRecoveryStepRequest(FVector& OutDirectionWorld, float& OutUrgency) const;

	// Protective arm reflex. OutAlpha is the smoothed blend in [0, 1], OutGroundTargetWorld the ground point ahead of the fall.
	// Returns false when the reflex is not active.
	bool GetProtectiveReflex(float& OutAlpha, FVector& OutGroundTargetWorld) const;

private:
	bool IsAuthoritative() const;
	UAedirStatsComponent* FindStats() const;
	void SetBalance(float NewBalance);
	void AddLeanDirection(const FVector& WorldDirection, float Magnitude);
	void DecayLeanDirection(float Amount);
	void UpdateFallState();
	void UpdateServerState(float DeltaTime);
	void ApplyCenterOfMassLoss(float DeltaTime);
	void EnterRagdoll(float MinimumSeconds);
	void UpdatePelvisLean(float DeltaTime);
	void DrivePelvis();
	void SetRagdollPresentation(bool bRagdoll);
	void ApplyRagdollPhysics(bool bRagdoll);
	void ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit);
	void SetupMotors();
	void UpdateMotors(float DeltaTime, bool bForce);
	void ApplyMotorGroup(int32 GroupIndex, float Strength);
	void SampleCenterOfMass();
	void AddFootSupportPoints(FName FootBone, FName BallBone, TArray<FVector2D>& OutPoints) const;
	void SampleProtectiveReflex();
	void SmoothProtectiveReflex(float DeltaTime);
	FVector GetFallVelocity();
	bool TraceGround(const FVector& Start, const FVector& End, FVector& OutHitPoint) const;

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

	// Centre of mass model, refreshed every 0.1 s on every machine.
	float ComSampleTimer = 0.f;
	FVector CenterOfMassWorld = FVector::ZeroVector;
	// Horizontal unit direction from the support centroid to the centre of mass projection.
	FVector ComOffsetDirection = FVector::ZeroVector;
	float SupportMargin = 0.f;
	bool bSupportGrounded = false;

	// Protective reflex. The target is sampled every 0.1 s, the blend is smoothed every tick.
	float TargetProtectiveAlpha = 0.f;
	float ProtectiveAlpha = 0.f;
	FVector ProtectiveGroundWorld = FVector::ZeroVector;
	// Alpha at the last tick the reflex was active, used to soften the fall damage on landing.
	float LastProtectiveReflexAlpha = 0.f;

	FRotator CurrentLean = FRotator::ZeroRotator;
	float TimeBelowThreshold = 0.f;
	double RagdollStartTime = 0.0;
	float CurrentRagdollMinSeconds = 0.f;
	float LastFallSpeed = 0.f;
	float PelvisRestHeight = 0.f;
	bool bWasFalling = false;
};
