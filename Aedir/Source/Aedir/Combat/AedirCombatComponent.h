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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirShove, AAedirCreature*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAedirKick, AAedirCreature*, Victim);

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

// Compact kick state replicated from the server. Remote machines need the leg and the start point to draw the kick.
USTRUCT()
struct AEDIR_API FAedirKickNetState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	bool bLeftLeg = false;

	UPROPERTY()
	float Elapsed = 0.f;

	// Foot world location at the start of the kick. The foot returns here at the end of the arc.
	UPROPERTY()
	FVector_NetQuantize StartWorld = FVector::ZeroVector;

	// Where the camera looked when the kick started.
	UPROPERTY()
	FVector_NetQuantizeNormal Direction = FVector::ForwardVector;

	UPROPERTY()
	float LiftCm = 0.f;
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

	// Smoothed mouse speed in counts per second, and how far the arm follows the aim (1) instead of holding the guard in front of the face (0).
	float MouseSpeed = 0.f;
	float PunchBlend = 0.f;

	// World direction the blade points in while a weapon is in the stance. It lags the mouse, more so for a heavy weapon.
	FVector WeaponDir = FVector::ZeroVector;

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

	// Broadcast on the server when a shove reaches a creature.
	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirShove OnShove;

	// Broadcast on the server when a kick reaches a creature.
	UPROPERTY(BlueprintAssignable, Category="Aedir|Combat")
	FAedirKick OnKick;

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
	float ArmStiffnessBase = 5000.f;

	// Maximum hand acceleration in cm/s^2 for a healthy, fresh, average-strength arm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float MaxArmAcceleration = 42000.f;

	// Mass of the arm itself in kg, added to the held item weight.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float ForearmMass = 2.5f;

	// Degrees of aim offset per unit of raw mouse movement.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Arm")
	float AimSensitivity = 0.3f;

	// Distance in cm from the primary hand to the grip point of the secondary hand on the weapon.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Grip")
	float GripSpacingCm = 13.f;

	// Share of the weapon mass that the two arms carry together while two-hand gripped. Only the remainder loads the stiffness.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Grip")
	float GripWeaponMassShare = 0.35f;

	// Stiffness multiplier of the primary arm while two-hand gripped.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Grip")
	float GripPrimaryStiffnessScale = 1.35f;

	// Stamina paid for a shove.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Shove")
	float ShoveStaminaCost = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Shove")
	float ShoveCooldown = 0.9f;

	// Reach in cm from the owner to the first creature a shove can hit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Shove")
	float ShoveRange = 130.f;

	// Stamina paid for a kick.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Kick")
	float KickStaminaCost = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Kick")
	float KickCooldown = 1.2f;

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

	// True while the weapon is held in one hand and either it is two-handed or both guards are held.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Combat|Grip")
	bool bTwoHandGrip = false;

	UPROPERTY(Replicated, VisibleAnywhere, Category="Aedir|Combat|Kick")
	FAedirKickNetState KickNet;

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

	// Thrust mode (Alt held): the weapon points where the camera looks and the mouse pushes it forward and pulls it back.
	void SetThrustMode(bool bActive);

	// Places an arm at an exact aim, for tests without a mouse.
	void DevSetArmAim(EAedirHand Hand, FVector2D AimDegrees, float Extension);

	// Direction the blade held by this hand should point in, with how much of the stance is blended in.
	bool GetWeaponAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const;

	// The second hand of a two-hand grip: which hand it is and where it closes on the weapon, in the space of the hand that carries the weapon.
	bool GetSecondaryGrip(EAedirHand& OutHand, FVector& OutPointInPrimaryHand, FVector& OutBladeInPrimaryHand, float& OutAlpha) const;

	// True while the right mouse button holds a weapon with the second hand up on its blade.
	bool IsHalfSwording() const;

	// A big object in both hands: where its middle is, how big it is, and how far the hands have come to it.
	bool GetObjectCarry(FVector& OutCenterWorld, float& OutRadiusCm, float& OutAlpha) const;

	// Holds a kick at that share of its length, for a look at the pose. Below zero lets it run.
	void DevHoldKick(float Share);

	// How far the spine turns with a weapon pulled to the side.
	float GetSwingTwistDegrees() const;

	// Puts the blade of that hand there at once, when it takes over a weapon from elsewhere.
	void SetWeaponDirNow(EAedirHand Hand, const FVector& DirectionWorld);

	// Local player only. Mouse wheel steps change how far the arm reaches, 0..1.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	void AddExtensionInput(EAedirHand Hand, float WheelDelta);

	// Server or NPC entry point. Validates guard, cooldown and stamina before starting the swing.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat")
	bool StartSwing(EAedirHand Hand, EAedirSwingType Type);

	// Owning client predicts the shove and asks the server to apply it. The server and NPCs apply it directly.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat|Shove")
	bool StartShove();

	// Owning client predicts the kick and asks the server to apply it. The server and NPCs apply it directly.
	UFUNCTION(BlueprintCallable, Category="Aedir|Combat|Kick")
	bool StartKick();

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

	// True while the weapon is held in one hand and the second hand is on its grip.
	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Grip")
	bool IsTwoHandGrip() const;

	// The hand that holds the weapon. While two-hand gripped the other hand is the secondary hand.
	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Grip")
	EAedirHand GetGripHand() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Kick")
	bool IsKicking() const;

	// Foot target of the kicking leg while a kick is in progress or still blending out. Returns false when the kick pose is not active.
	UFUNCTION(BlueprintPure, Category="Aedir|Combat|Kick")
	bool GetKickPose(bool& bOutLeftLeg, FVector& OutFootTargetWorld, float& OutAlpha) const;

	// Where the sole of the kicking foot faces, and how far the foot has turned to it.
	bool GetKickSole(FVector& OutDirectionWorld, float& OutAlpha) const;

	// Pure helpers used by the impact model and by tests.
	static float ComputeImpactEnergy(float MassKg, float SpeedCmPerSec);
	static EAedirBodyZone ClassifyBodyZone(FName BoneName);
	static float GetZoneDamageMultiplier(EAedirBodyZone Zone);
	static float ComputeDamageTypeFactor(EAedirDamageType Type, float AxialAlignment);
	static bool GetBoneLimb(FName BoneName, EAedirLimb& OutLimb);

	// Energy in joules of a shove. Scales with strength and with the stamina fraction, between half and full.
	static float ComputeShoveEnergy(float Strength, float StaminaFraction);

	// Foot target of the kick arc. T01 is 0 at the start, 0.25 at the chamber, 0.5 at the full extension and 1 back at the start.
	static FVector EvaluateKickArc(const FVector& Start, const FVector& Hip, const FVector& Forward, float T01, float LiftCm = 0.f);

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
	bool IsArmPosed(EAedirHand Hand) const;
	bool IsSecondaryGripHand(EAedirHand Hand) const;
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

	bool EvaluateTwoHandGrip() const;
	void UpdateTwoHandGrip();
	FVector GetGripPointWorld() const;

	bool CanStartShove() const;
	bool TryStartShove();
	void BeginShove();
	void EndShove();
	void UpdateShove(float DeltaTime);
	FVector GetShoveWorldTarget(EAedirHand Hand, float Elapsed) const;
	void ResolveShoveStrike();

	bool CanStartKick() const;
	bool TryStartKick();
	void BeginKick();
	void UpdateKick(float DeltaTime);
	void ResolveKickSweep(const FVector& From, const FVector& To, float DeltaTime);
	void ApplyKickImpact(AAedirCreature* Victim, const FHitResult& Hit, const FVector& FootMotion, float FootSpeed);
	FVector GetFootBoneWorld(bool bLeftLeg) const;
	FVector GetPelvisWorld() const;

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
	void ApplyCreatureImpact(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Victim, const FHitResult& Hit, const FVector& TipVelocity, float RelativeSpeed, float MassEff, float AxialAlignment);
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

	UFUNCTION(Server, Reliable)
	void ServerSetThrustMode(bool bActive);

	// True while the hand holds a weapon up in the stance, where the mouse moves the blade and not a fist.
	bool IsWeaponStance(EAedirHand Hand) const;
	// Both hands stay on a two-handed weapon also with no button held, except while a hand goes to the belt.
	bool IsGripHeld() const;
	bool IsObjectGrip() const;
	// Long enough for a second hand on it: not knives.
	static bool CanHalfSword(const AAedirItemActor* Item);
	void UpdateGrip(float DeltaTime);
	void UpdateObjectCarry();
	float GetSecondaryGripLocalCm(const AAedirItemActor* Item) const;
	FVector ComputeHalfSwordDir(EAedirHand Hand, const FAedirArmDynamics& Arm) const;
	float GetHalfSwordForwardDegrees(EAedirHand Hand, const FAedirArmDynamics& Arm) const;
	FVector ComputeCarriedDir(EAedirHand Hand) const;
	float GetFightShare() const;
	static FCollisionObjectQueryParams GetHeldBlockObjects();
	bool SweepHeldBlade(const AAedirItemActor* Item, const FVector& HandWorld, const FVector& Direction, float LengthCm, FHitResult& OutHit) const;
	void ResolveHeldCollision(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& PrevHand, const FVector& PrevDir, float DeltaTime);
	FVector ComputeHalfSwordHand(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const;
	FVector ComputeObjectCenter(const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const;
	FVector ComputeWeaponTargetDir(EAedirHand Hand, const FAedirArmDynamics& Arm) const;
	void UpdateWeaponDir(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime, float StrengthFactor, float WeaponWeight);

	bool bThrustMode = false;
	float DevKickHoldShare = -1.f;
	float HalfSwordAlpha = 0.f;
	bool bHalfSwordWantedLast = false;
	bool bObjectGripLast = false;
	float SecondaryGripBlend = 0.f;
	EAedirHand LastGripHand = EAedirHand::Right;

	UFUNCTION(Server, Reliable)
	void ServerStartShove();

	UFUNCTION(Server, Reliable)
	void ServerStartKick();

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

	// Shove timeline. Runs on the server and on the owning client; only the server applies the strike.
	bool bShoving = false;
	float ShoveElapsed = 0.f;
	bool bShoveStruck = false;
	float ShoveCooldownRemaining = 0.f;

	// Kick timeline. Runs on every machine; only the server sweeps for the strike.
	float KickCooldownRemaining = 0.f;
	float KickPoseAlpha = 0.f;
	bool bKickStruck = false;
	FVector KickPrevFootWorld = FVector::ZeroVector;

	float AIRetargetTimer = 0.f;
	float AIAttackTimer = 0.f;
	double AILastSeenTime = 0.0;
	bool bAIApproaching = false;
	bool bAINextLeft = true;
};
