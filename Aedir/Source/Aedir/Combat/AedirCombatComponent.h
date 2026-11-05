#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/AedirCombatTypes.h"
#include "Engine/NetSerialization.h"
#include "Items/AedirItemTypes.h"
#include "AedirCombatComponent.generated.h"

class AActor;
class AAedirCreature;
class AAedirItemActor;
class UPrimitiveComponent;
struct FHitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAedirHitDealt, AAedirCreature*, Victim, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FAedirImpact, AActor*, Victim, FName, Bone, float, Energy, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirClash, AAedirCreature*, Other);

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

	// Swing direction in view space: X is mouse right, Y is mouse up, normalized. Scripted swings leave it zero.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Combat")
	FVector2D Direction = FVector2D::ZeroVector;
};

// Compact arm state replicated from the server. Remote proxies and the owning client read it.
USTRUCT()
struct AEDIR_API FAedirArmNetState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize HandWorld = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 HandVelocity = FVector::ZeroVector;

	UPROPERTY()
	uint8 Active = 0;
};

// Local simulation state of one arm. Not replicated; the server copy is published through FAedirArmNetState.
struct FAedirArmDynamics
{
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector HandWorld = FVector::ZeroVector;
	FVector HandVelocity = FVector::ZeroVector;
	FVector PrevHandWorld = FVector::ZeroVector;
	FVector PrevTipWorld = FVector::ZeroVector;

	// X is yaw in degrees relative to the owner facing, Y is pitch in degrees.
	FVector2D AimOffsetDeg = FVector2D::ZeroVector;
	float Extension = 0.5f;

	bool bScriptedControl = false;
	FVector ScriptedTargetWorld = FVector::ZeroVector;

	float ActiveAlpha = 0.f;
	bool bInitialized = false;
};

struct FAedirTimedContact
{
	TWeakObjectPtr<AActor> Target;
	double Time = 0.0;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirCombatComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirHitDealt OnHitDealt;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirImpact OnImpact;

	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirClash OnClash;

	// Flat bonus for a bare-handed hit. Held items add their own Damage instead.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float FistDamage = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float ImpactDamageScale = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingStaminaCost = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingDurationBase = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingReach = 75.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float HitRadius = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float SwingCooldown = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	float BalanceImpactScale = 0.02f;

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

	// Spring stiffness of an unloaded arm in 1/s^2. Scaled down by weight, low strength, low stamina and injury.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float ArmStiffnessBase = 900.f;

	// Maximum hand acceleration in cm/s^2 for a healthy, fresh, average-strength arm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float MaxArmAcceleration = 9000.f;

	// Mass of the arm itself in kg, added to the held item weight.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float ForearmMass = 2.5f;

	// Degrees of aim offset per unit of raw mouse movement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float AimSensitivity = 0.18f;

	// Minimum tip speed in cm/s for a contact to count as a hit rather than a touch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Impact")
	float MinImpactSpeed = 150.f;

	// Time in seconds before the same victim can be hit again by the same hand.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Impact")
	float ImpactCooldown = 0.3f;

	// Distance in cm between two weapon segments that counts as a clash.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Clash")
	float ClashRadius = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Clash")
	float ClashMinCombinedSpeed = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Clash")
	float ClashCooldown = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Clash")
	float ClashSearchRadius = 300.f;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	bool bLeftGuard = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	bool bRightGuard = false;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	FAedirSwingState LeftSwing;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat")
	FAedirSwingState RightSwing;

	UPROPERTY(Replicated, VisibleAnywhere, Category="Aedir|Combat|Arm")
	FAedirArmNetState LeftArmNet;

	UPROPERTY(Replicated, VisibleAnywhere, Category="Aedir|Combat|Arm")
	FAedirArmNetState RightArmNet;

	// Damage reduction per body zone, indexed by EAedirBodyZone. Range 0..0.9.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Armor")
	TArray<float> ArmorProtection;

	// Injury per limb, indexed by EAedirLimb. Range 0..1.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Injury")
	TArray<float> LimbInjury;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Owning client sends the change to the server; the server applies it to the replicated guard flag.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void SetGuard(EAedirHand Hand, bool bActive);

	// Local player only. While the guard is held the mouse moves the arm: MouseDeltaThisFrame uses raw mouse axis values (MouseY is positive when the mouse moves down).
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void AddSwingInput(EAedirHand Hand, FVector2D MouseDeltaThisFrame, float DeltaTime);

	// Local player only. Mouse wheel steps change how far the arm reaches, 0..1.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void AddExtensionInput(EAedirHand Hand, float WheelDelta);

	// Server or NPC entry point. Validates guard, cooldown and stamina before starting the swing.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	bool StartSwing(EAedirHand Hand, EAedirSwingType Type);

	// Server only.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat|Armor")
	void SetArmorProtection(EAedirBodyZone Zone, float Protection);

	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Armor")
	float GetArmorProtection(EAedirBodyZone Zone) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Injury")
	float GetLimbInjury(EAedirLimb Limb) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Arm")
	FVector GetHandVelocity(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Arm")
	FVector GetTipWorld(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsGuardActive(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsInFightStance() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool GetGuardPose(EAedirHand Hand, float& OutAlpha) const;

	// Returns the simulated hand position of the arm. Alpha is the arm activity, 1 while guarded or swinging.
	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool GetSwingPose(EAedirHand Hand, FVector& OutHandTargetWorld, float& OutAlpha) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	float GetFistAlpha(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat")
	bool IsLeadFootRight() const;

	// Pure helpers used by the impact model and by tests.
	static float ComputeImpactEnergy(float MassKg, float SpeedCmPerSec);
	static EAedirBodyZone ClassifyBodyZone(FName BoneName);
	static float GetZoneDamageMultiplier(EAedirBodyZone Zone);
	static float ComputeDamageTypeFactor(EAedirDamageType Type, float AxialAlignment);
	static bool GetBoneLimb(FName BoneName, EAedirLimb& OutLimb);

private:
	bool IsAuthoritative() const;
	bool IsAutonomousProxy() const;
	bool IsSimulatedProxy() const;
	static int32 HandIndex(EAedirHand Hand);
	FAedirSwingState& GetSwingState(EAedirHand Hand);
	const FAedirSwingState& GetSwingState(EAedirHand Hand) const;
	FAedirArmDynamics& GetArm(EAedirHand Hand);
	const FAedirArmDynamics& GetArm(EAedirHand Hand) const;
	FAedirArmNetState& GetArmNet(EAedirHand Hand);
	const FAedirArmNetState& GetArmNet(EAedirHand Hand) const;
	AAedirItemActor* GetHeldItem(EAedirHand Hand) const;
	EAedirLimb GetArmLimb(EAedirHand Hand) const;

	void SetGuardInternal(EAedirHand Hand, bool bActive);
	bool CanStartSwing(EAedirHand Hand) const;
	bool IsArmActive(EAedirHand Hand) const;
	float ComputeWeightFactor(EAedirHand Hand) const;
	float ComputeSwingDuration(EAedirHand Hand) const;
	float GetEffectiveWeaponLength(EAedirHand Hand) const;
	float GetArmTotalMass(EAedirHand Hand) const;
	bool TryStartSwing(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction);
	void StartSwingInternal(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction);
	void CancelSwing(EAedirHand Hand);
	void UpdateSwings(float DeltaTime);
	FVector GetSwingWorldTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const;
	FVector GetSwingLocalTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const;
	void SetGuardRestAim(EAedirHand Hand);

	void UpdateArms(float DeltaTime);
	FVector ComputeArmShoulder(EAedirHand Hand) const;
	FVector ComputeArmTarget(EAedirHand Hand, const FAedirArmDynamics& Arm) const;
	FVector ComputeArmTip(EAedirHand Hand, const FAedirArmDynamics& Arm) const;
	void IntegrateArm(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime);
	void ApplyRemoteArmState(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime);
	void PublishArmState(EAedirHand Hand, const FAedirArmDynamics& Arm);
	void SendArmAimToServer(float DeltaTime);
	void UpdateArmActivity(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime);

	void UpdateClashes(float DeltaTime);
	void RefreshClashCandidates();
	bool IsClashCooldownElapsed(const AAedirCreature* Other, double Now) const;
	void ResolveClash(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Other, const FVector& OtherVelocity, const FVector& Normal, float CombinedSpeed, double Now);

	void UpdateImpacts(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime);
	bool IsImpactOnCooldown(EAedirHand Hand, const AActor* Victim, double Now) const;
	void RegisterImpact(EAedirHand Hand, AActor* Victim, double Now);
	void ProcessImpactSweep(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& From, const FVector& To, const FVector& TipVelocity, float MassEff, float AxialAlignment, double Now);
	void ApplyCreatureImpact(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Victim, const FHitResult& Hit, float RelativeSpeed, float MassEff, float AxialAlignment);
	void ApplyPhysicsImpact(FAedirArmDynamics& Arm, const FHitResult& Hit, UPrimitiveComponent* HitComponent, float RelativeSpeed, float MassEff);
	void ApplyLimbInjury(EAedirLimb Limb, float Amount);
	void HealLimbInjuries(float DeltaTime);

	void UpdateAI(float DeltaTime);
	void UpdateAITarget(double Now);
	bool CanSeeTarget(const AAedirCreature* Target) const;

	UFUNCTION(Server, Reliable)
	void ServerSetGuard(EAedirHand Hand, bool bActive);

	UFUNCTION(Server, Unreliable)
	void ServerUpdateArmAim(EAedirHand Hand, FVector2D Aim, float Extension);

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> Creature;

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> AITarget;

	float GuardAlpha[2] = { 0.f, 0.f };
	float SwingCooldownRemaining[2] = { 0.f, 0.f };
	FAedirArmDynamics Arms[2];
	TArray<FAedirTimedContact> ImpactRecords[2];
	TArray<FAedirTimedContact> ClashRecords;
	TArray<TWeakObjectPtr<AAedirCreature>> ClashCandidates;
	TArray<TWeakObjectPtr<AActor>> ClashedThisTick;
	float ClashCandidateTimer = 0.f;
	float ArmAimSendTimer = 0.f;

	float AIRetargetTimer = 0.f;
	float AIAttackTimer = 0.f;
	double AILastSeenTime = 0.0;
	bool bAIApproaching = false;
	bool bAINextLeft = true;
};
