#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/ConstraintInstance.h"
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

// How hard the muscles of one part of the body hold it to the pose. Strength is how stiff the pull is, force is the most it can give: past that, what the body runs into wins.
USTRUCT(BlueprintType)
struct FAedirBoneMotor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float OrientationStrength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float AngularVelocityStrength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float PositionStrength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float VelocityStrength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float MaxLinearForce = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float MaxAngularForce = 0.f;
};

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
	float FallDelaySeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float GetUpBalanceThreshold = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float MinRagdollSeconds = 2.5f;

	// Muscles per part of the body, at full strength.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor SpineMotor = { 9000.f, 900.f, 9000.f, 900.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor HeadMotor = { 6000.f, 600.f, 6000.f, 600.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor UpperArmMotor = { 9000.f, 900.f, 9000.f, 900.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor LowerArmMotor = { 9000.f, 900.f, 9000.f, 900.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor HandMotor = { 12000.f, 1200.f, 12000.f, 1200.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor ThighMotor = { 30000.f, 3000.f, 30000.f, 3000.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor CalfMotor = { 40000.f, 4000.f, 40000.f, 4000.f, 0.f, 0.f };

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	FAedirBoneMotor FootMotor = { 60000.f, 6000.f, 60000.f, 6000.f, 0.f, 0.f };

	// The hips are physical too, held up and upright by a pull towards where the legs would carry them. Off, they follow the
	// pose exactly and only the rest of the body is physical.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	bool bActivePelvis = true;

	// How quickly the hips are pulled back to where they belong (rad/s) and how much that pull is damped (1 = no overshoot).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisHoldFrequency = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisHoldDamping = 0.9f;

	// The same for keeping them turned upright and facing the way the body faces.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisTurnFrequency = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisTurnDamping = 1.f;

	// The most acceleration the legs can give the hips at full strength, in cm/s2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisMaxAccel = 3500.f;

	// How much of its strength to stand a body keeps with no stamina left, and with no health left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float UprightExhaustedShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float UprightDyingShare = 0.45f;

	// Hips pushed this far off where the legs carry them start to cost balance, all of it per second at the second distance,
	// and faster for a weak body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisDriftFreeCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisDriftLossCm = 45.f;

	// How far the hips of a body with no strength left sink, the least share of its pull a weak body keeps, and how far below
	// the hips the feet are, which sets how far a shove tips the body over them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisWeakSagCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisMinHoldShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisTipLeverCm = 90.f;

	// Seconds a fresh body takes to get up off the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBaseSeconds = 1.7f;

	// How much longer it takes with no stamina left, as a share.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpTiredShare = 0.6f;

	// How much longer it takes with no health left, as a share.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpHurtShare = 0.9f;

	// How far below standing the hips may start when getting up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpMaxDropCm = 70.f;

	// How far over the trunk is bent at the start of getting up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBendDegrees = 50.f;

	// How weak the muscles are at the start of getting up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpMuscleShare = 0.45f;

	// Going down, no part of the body is thrown upwards faster than this, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntryMaxRiseSpeed = 60.f;

	// Impulse on the chest per cm/s of the speed a wall is met at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float WallImpactBodyImpulsePerSpeed = 9.f;

	// Running into another body: from what closing speed in cm/s it counts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float BumpMinSpeed = 180.f;

	// Balance it costs per cm/s beyond that.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float BumpLossPerSpeed = 0.0015f;

	// The most balance one shove costs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float BumpMaxLoss = 0.6f;

	// How hard the trunks jolt per cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float BumpImpulsePerSpeed = 7.f;

	// How hard the body holds itself up now, 0 to 1: health, stamina, a daze and getting up all take from it; down, none.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance|Upright")
	float GetUprightStrength() const;

	// The hit a loose body gets on top of its faint muscles.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Balance|Motors")
	float RagdollMotorShare = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float WallImpactSpeedThreshold = 230.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float ObjectImpactMassSpeedThreshold = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallSpeedBalanceThreshold = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallDamagePerSpeed = 0.15f;

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

	// A leg is gone: this body does not get up again.
	void SetLegLost() { bLegLost = true; }

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsRagdoll() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsStaggering() const;

	// While a body that was down gets back on its feet: how much of the way it still has to come, 1 just off the ground.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetGetUpShare() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsGettingUp() const { return GetUpLeft > 0.f; }

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
	void ApplyBodyContacts(bool bRagdoll);
	void ApplyLegPhysics(bool bRagdoll);
	void UpdateGetUp(float DeltaTime);
	// Pulls the physical hips towards where the pose and the capsule want them, as hard as the body is strong.
	void HoldPelvis(float DeltaTime);
	void PushHips(const FVector& Impulse);
	bool bHipsInitialized = false;
	FVector HipsLocation = FVector::ZeroVector;
	FVector HipsVelocity = FVector::ZeroVector;
	float BodyMassKg = 70.f;
	void BumpInto(class AAedirCreature* Other, const FHitResult& Hit);
	void ApplyGetUpPose();
	float GetUpLeft = 0.f;
	float GetUpTotal = 0.f;
	float GetUpDropCm = 0.f;
	// How the body was moving the frame before, since by the time a hit is reported the movement has already stopped it.
	FVector LastVelocity = FVector::ZeroVector;
	// When this body last ran into another, so one shove is not counted on every frame of the contact.
	double LastBumpTime = -100.0;
	bool bLegLost = false;
	void ReshapeBodyPhysics();
	float PhysicsShapeScale = 0.f;
	void ApplyJointLimits(bool bRagdoll);
	TArray<FConstraintProfileProperties> LooseJointLimits;
	FTransform MeshRestRelative = FTransform::Identity;
	const FAedirBoneMotor& GetBoneMotor(FName BoneName) const;
	void ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit);
	void SetupMotors();
	void UpdateMotors(float DeltaTime, bool bForce);
	void UpdateLegBlend(float DeltaTime);
	float ArmPhysicsBlend = 1.f;
	float LegPhysicsBlend = 0.f;
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

	// Where the slow drift of a standing body is, and what sets one body's drift apart from another's.
	float SwayClock = 0.f;
	float SwaySeed = FMath::FRandRange(0.f, 100.f);

	// Balance a blow took beyond what there was to lose. A stumbling body has to win this back before it stands again.
	float StumbleDebt = 0.f;
	double RagdollStartTime = 0.0;
	float CurrentRagdollMinSeconds = 0.f;
	float LastFallSpeed = 0.f;
	float PelvisRestHeight = 0.f;
	bool bWasFalling = false;
};
