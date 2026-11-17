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

// How a body that is down gets back on its feet, which depends on how it lies.
UENUM(BlueprintType)
enum class EAedirGetUpStyle : uint8
{
	None,
	// Face down: pushed up onto the hands and knees, one foot brought forward, then up off it.
	FromFront,
	// On the back: propped up on the hands, sat on the hips, the feet drawn in to them, then up over the feet.
	FromBack
};

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

	// Off balance, the hips also turn this far towards the side the body goes over to (degrees, at the most).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float LeanTwistDegrees = 18.f;

	// Driven off balance below StumbleWalkBelowBalance, or staggering, the body is carried the way it goes over and walks
	// after it in hurried steps until it has its feet back under it: at StumbleWalkMinShare of StumbleWalkSpeed (cm/s) just
	// below it, at all of it as it nears falling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float StumbleWalkBelowBalance = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float StumbleWalkMinShare = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float StumbleWalkSpeed = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float RecoveryPerSecond = 0.5f;

	// A blow slows getting the balance back for a moment, the more the more balance it cost: down to HardHitRecoveryShare
	// of the usual pace after one that takes it all, and HeadHitRecoveryShare as much again after one to the head; the pace
	// comes back by HitRecoveryReturnPerSecond of the way a second. A blow to the head also costs HeadBalanceLossShare times
	// the balance a blow of the same energy would elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HardHitRecoveryShare = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HeadHitRecoveryShare = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HitRecoveryReturnPerSecond = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HeadBalanceLossShare = 1.25f;

	// Where the weight is: the bodies as they are pushed about, ComPhysicalShare of the way from where the pose has them,
	// carried on by how fast the trunk is thrown off the feet for ComCarrySeconds (the point it is falling towards).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float ComPhysicalShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float ComCarrySeconds = 0.3f;

	// With the weight beyond the feet by more than FallChanceFromCm, or the balance gone below FallBalanceThreshold, the body
	// may go down at any moment it cannot catch itself: at most FallChancePerSecond, from FallChanceFullCm beyond the feet or
	// with no balance left, twice that at no health left.
	// A blow taking at least GetUpKnockLoss of the balance while the body is getting up puts it down again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float GetUpKnockLoss = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChanceFromCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChanceFullCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChancePerSecond = 0.8f;

	// A blow on a shoulder or an upper arm moves the arm only this share of its push; the rest goes into the trunk, so the
	// arm is not thrown up beside the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float ShoulderBlowArmShare = 0.3f;

	// A blow slackens the muscles of the trunk, head and arms by up to BlowSoftMaxShare, all of it from a push of
	// BlowSoftFullPush, for a moment that passes off over BlowSoftSeconds, so it is felt through the body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float BlowSoftMaxShare = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSoftFullPush = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSoftSeconds = 0.6f;

	// Hitting the ground or something that does not give, a part of the body faster than ImpactHarmlessSpeed (cm/s) is
	// hurt: ImpactWoundShare of its weight's energy wears the place as a blow does (a bruise, and with more, blood), and
	// it takes ImpactDamagePerSpeed of health for each cm/s past it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float ImpactHarmlessSpeed = 380.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float ImpactWoundShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float ImpactDamagePerSpeed = 0.02f;

	// For the free camera, whose time all but stands still: what every loose body is doing as it starts, put back as it ends.
	void HoldBodiesStill(bool bHold);

	// A body worn down weakens: below LowHealthFrom of its health, the lower it gets the harder each blow throws it (up to
	// 1 + LowHealthHitGain times, at none left), the less it takes to knock it down (down to LowHealthKnockdownShare of a
	// whole body's), and the slower it gets its balance back (down to LowHealthRecoveryShare).
	// A whole body already gives this much to blows: they throw it HealthyHitShare times as hard, knock it down from
	// HealthyKnockdownShare of the energy and let its balance come back at HealthyRecoveryShare. Worn down, it goes on from
	// there to the LowHealth values at none left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HealthyHitShare = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HealthyKnockdownShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HealthyRecoveryShare = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float LowHealthFrom = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float LowHealthHitGain = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float LowHealthKnockdownShare = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float LowHealthRecoveryShare = 0.35f;

	// Out of breath the body weakens too, but only so far: below TiredFrom of its stamina, the lower it gets the harder
	// blows throw it (up to 1 + TiredHitGain times at none left), the less it takes to knock it down (down to
	// TiredKnockdownShare), the slower its balance comes back (TiredRecoveryShare) and the weaker its muscles hold
	// (TiredStrengthShare).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float TiredFrom = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float TiredHitGain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float TiredKnockdownShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float TiredRecoveryShare = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float TiredStrengthShare = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallBalanceThreshold = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallDelaySeconds = 1.8f;

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

	// The whole body is simulated, the hips too: they are tied to the capsule by a joint free in every direction, whose
	// motors pull them where the legs would carry them, as hard as the body is strong. Every other part is held by its joint
	// to the part above it, so weight and blows travel through the body. Off, the hips are a simulated mass moved by hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	bool bPhysicalHips = false;

	// The whole body on the engine's physics control (see UAedirBodyControlComponent): every part simulated and held by its
	// joint, the trunk kept upright and the feet led to their steps in the world, the hips by a control to the capsule.
	// Off, the older motors and hand-moved hips are used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	bool bUseBodyControl = true;

	// The hip joint's pull: how stiff the spring to where the hips belong is and how hard it damps, for position and for
	// turning, at full strength. Force per cm and per degree, in the units of a physics constraint drive.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float HipJointLinearStiffness = 30000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float HipJointLinearDamping = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float HipJointAngularStiffness = 60000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float HipJointAngularDamping = 4000.f;

	// How quickly the hips are pulled back to where they belong (rad/s) and how much that pull is damped (1 = no overshoot).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisHoldFrequency = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisHoldDamping = 1.f;

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

	// A staggering blow drives the feet back this share of its speed along the ground, and the hips this share.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float StaggerFeetShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float StaggerHipsShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisMinHoldShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Upright")
	float PelvisTipLeverCm = 90.f;

	// Seconds a fresh body takes to get up off the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBaseSeconds = 3.f;

	// A player's body stays down until it is told to get up: S face down, W on the back. Bodies nobody plays get up by themselves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	bool bPlayerChoosesGetUp = true;

	// The chest tells which way a body lies, and the head which way it is stretched out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	FName ChestBoneName = FName(TEXT("spine_03"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	FName HeadBoneName = FName(TEXT("head"));

	// A body with a leg gone still tries to get up, and falls again this far through it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float LegLostGetUpGivesWayAt = 0.55f;

	// Over this first share of getting up the hips, hands and feet go from where they lay to where the moves start.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpSetOutShare = 0.15f;

	// Getting up off the back, the body stands up over its drawn-in feet, this far past where its hips lay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpFromBackStandCm = 32.f;

	// How much longer it takes with no stamina left, as a share.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpTiredShare = 0.6f;

	// How much longer it takes with no health left, as a share.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpHurtShare = 0.9f;

	// How weak the muscles are at the start of getting up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpMuscleShare = 0.7f;

	// A knockdown tips the body over the way the blow went: the head starts off at this share of the speed a stagger would give.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float KnockdownToppleShare = 0.6f;

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

	// How hard each joint resists turning at all, as a rate per second: a joint's own drag, with no pose it is pulled to. The
	// physics asset's joints came with stiff drives to its reference pose, which held a dead body's arms and legs up in it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Motors")
	float JointDragPerSecond = 2.f;

	// Running into a wall, a tree or anything that does not give (a loose thing of at least StillObjectWallMassKg lying
	// still counts): from WallImpactSpeedThreshold (cm/s, into it) the body is thrown off its balance, all of what
	// WallImpactFullLossSpeed past it would cost at most, and knocked back off it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float WallImpactSpeedThreshold = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float WallImpactFullLossSpeed = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float StillObjectWallMassKg = 25.f;

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
	// A leg's bone broke under the body: it goes down now, and gets up again in its time.
	void BuckleLeg() { if (IsAuthoritative() && !bIsRagdoll) { EnterRagdoll(MinRagdollSeconds); } }

	// A head thrown round past where a neck turns is set back on it.
	void KeepHeadOnNeck(float DeltaTime);
	float HeadTwistedSeconds = 0.f;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsRagdoll() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsStaggering() const;

	// While a body that was down gets back on its feet: how much of the way it still has to come, 1 just off the ground.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetGetUpShare() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsGettingUp() const { return GetUpLeft > 0.f; }

	// How far through getting up the body is, 0 just off the ground to 1 standing.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetGetUpProgress() const { return GetUpTotal > 0.f ? 1.f - GetUpLeft / GetUpTotal : 1.f; }

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	EAedirGetUpStyle GetGetUpStyle() const { return GetUpStyle; }

	// Down and able to get up again, and how it lies, which says what it takes.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsReadyToGetUp() const { return bIsRagdoll && bReadyToGetUp; }

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	EAedirGetUpStyle GetLyingStyle() const { return LyingStyle; }

	// The player's movement keys while down: forward (W) gets a body up off its back, back (S) one lying face down.
	void RequestGetUp(bool bForwardKey);

	// Height of the hips above the ground when standing.
	float GetPelvisStandHeight() const;

	// Where the hips lay when getting up started, as the pose puts hips: moved from standing in the frame of the body, and turned.
	bool GetGetUpStartHips(FVector& OutOffset, FQuat& OutTurn) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	bool IsDazed() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetDazeSeconds() const;

	// 0 when not dazed, 1 at the full daze duration.
	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	float GetDazeFactor() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Balance")
	FRotator GetPelvisLean() const;

	// How far the trunk is bent by the blows it took, in degrees: back (pitch), aside (roll) and turned (yaw).
	FRotator GetHitSway() const { return FRotator(HitSway.X, HitSway.Z, HitSway.Y); }
	// How worn down the body is, for its balance: 0 above LowHealthFrom of its health, 1 at none left.
	float GetHurtShare() const;
	// How out of breath the body is, for its balance and strength: 0 above TiredFrom of its stamina, 1 at none left.
	float GetTiredShare() const;

	// A blow bends the trunk the way it drove, at the place it struck, and the trunk springs back: how fast it springs, in
	// Hz, and how much the spring is damped (1 for no overshoot).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitSwayHz = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitSwayDampingRatio = 0.45f;

	// How fast a blow sets the trunk bending, in degrees a second for each unit of the blow's push (kg cm/s, scaled).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitSwayDegreesPerSecondPerPush = 1.1f;

	// A blow off the middle turns the trunk too: degrees a second for each unit of push and cm off the middle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitSwayTwistPerPushCm = 0.02f;

	// The furthest the trunk is bent by blows, in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitSwayMaxDegrees = 28.f;

	// A blow does not push all at once: HitPushNowShare of it lands at once and the rest keeps pushing over
	// HitPushSpreadSeconds after it, falling off, so the body is seen carried on by it. The whole push is HitPushScale of
	// what the blow's weight and speed give.
	// An arm caught somewhere for a while is let pass through things this long, its muscles pulling it home harder, rather
	// than being put back on its pose at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float ArmFreeSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitPushScale = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitPushNowShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitPushSpreadSeconds = 0.15f;

	// A blow to the head knocks the head on its neck as a weight on a spring: it is left behind the way the blow drove it
	// and comes back. How fast it springs (Hz), how damped, degrees a second for each unit of push, at most so far.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HeadSwayHz = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HeadSwayDampingRatio = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HeadSwayDegreesPerSecondPerPush = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HeadSwayMaxDegrees = 35.f;

	// How far the head is knocked on its neck by the blows it took: back (pitch), aside (roll) and turned (yaw).
	FRotator GetHeadSway() const { return FRotator(HeadSway.X, HeadSway.Z, HeadSway.Y); }

	// The last blow that landed on this body: the bone, where on it (in the bone's frame), when (world seconds) and how hard
	// it pushed. False if none has yet.
	bool GetLastBlow(FName& OutBone, FVector& OutLocalPoint, double& OutWorldTime, float& OutPush) const;

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

	// Whether a dying body has already let go of what it held.
	bool bDroppedOnDeath = false;
	void SetBalance(float NewBalance);
	void AddLeanDirection(const FVector& WorldDirection, float Magnitude);
public:
	// A step taken to catch the body has landed.
	void OnRecoveryStepTaken();
private:
	void DecayLeanDirection(float Amount);
	void UpdateFallState();
	void UpdateServerState(float DeltaTime);
	void ApplyCenterOfMassLoss(float DeltaTime);
	void EnterRagdoll(float MinimumSeconds);
	void UpdatePelvisLean(float DeltaTime);
	void UpdateStumbleWalk(float DeltaTime);
	float HitRecoveryScale = 1.f;
	float BlowSoftness = 0.f;
	double RecoveryWatchSince = -1.0;
	float RecoveryWatchLowest = 1.f;
	TMap<FName, double> LastPartImpactTime;
	TArray<TPair<FVector, FVector>> HeldBodyMotion;
	void HurtPartOnImpact(FName Bone, const FVector& NormalImpulse, const FHitResult& Hit);
	void RunIntoWall(const FVector& OwnerVelocity, const FHitResult& Hit, class UAedirStatsComponent* StatsComponent, float Athletism);
	// Lying loose, each part is watched for being stopped short against the ground or something fixed.
	void WatchPartImpacts();
	TArray<FVector> LastPartVelocity;
	double NextStumbleLogTime = 0.0;
	void UpdateHitSway(float DeltaTime);
	// Back, aside and turned, in degrees, and how fast each is changing.
	FVector HitSway = FVector::ZeroVector;
	FVector HitSwayVelocity = FVector::ZeroVector;
	FVector HeadSway = FVector::ZeroVector;
	FVector HeadSwayVelocity = FVector::ZeroVector;
	// What is left of blows still pushing.
	struct FPendingPush
	{
		FName Bone;
		FVector Impulse;
		FVector Location;
		float Left = 0.f;
	};
	TArray<FPendingPush> PendingPushes;
	FName LastBlowBone = NAME_None;
	FVector LastBlowLocal = FVector::ZeroVector;
	double LastBlowTime = -1000.0;
	float LastBlowPush = 0.f;
	// One part of a blow's push landing: on the physical body, the trunk's and head's sway and the hips.
	void LandPush(FName Bone, const FVector& Impulse, const FVector& Location);
	void UpdatePendingPushes(float DeltaTime);
	void DrivePelvis();
	void SetRagdollPresentation(bool bRagdoll);
	void ApplyRagdollPhysics(bool bRagdoll);
	void ApplyBodyContacts(bool bRagdoll);
	void ApplyLegPhysics(bool bRagdoll);
	void UpdateGetUp(float DeltaTime);
	// Pulls the physical hips towards where the pose and the capsule want them, as hard as the body is strong.
	void HoldPelvis(float DeltaTime);
	// The body on physics control: whether it is used, and its update every frame while standing.
	bool UsesBodyControl() const;
	void UpdateBodyControl(float DeltaTime);
	UPROPERTY(Transient)
	TObjectPtr<class UAedirBodyControlComponent> BodyControl;

	// The capsule size the hips were last tied to.
	float BoundCapsuleHalfHeight = -1.f;

	// Time left for a body just carried over to arrive before it is judged again.
	float BodyTeleportGrace = 0.f;

	// How the mesh sat on the capsule when the body went down, to put it back exactly so when it gets up.
	FTransform MeshRelativeAtFall = FTransform::Identity;
	// Physical hips: the joint to the capsule, made once, and its pull set every frame.
	bool UsesPhysicalHips() const;
	void SetupHipJoint();
	void DriveHipJoint(const FVector& TargetLocation, const FQuat& TargetRotation, float Strength, float DeltaTime);
	FTransform HipJointFrame = FTransform::Identity;
	bool bHipJointActive = false;
	UPROPERTY(Transient)
	TObjectPtr<class UPhysicsConstraintComponent> HipJoint;
	void PushHips(const FVector& Impulse);
	bool bHipsInitialized = false;
	FVector HipsLocation = FVector::ZeroVector;
	FVector HipsVelocity = FVector::ZeroVector;
	float BodyMassKg = 70.f;
	void BumpInto(class AAedirCreature* Other, const FHitResult& Hit);
	float GetUpLeft = 0.f;
	float GetUpTotal = 0.f;
	// Where the hips lay when getting up started.
	FTransform GetUpStartPelvis = FTransform::Identity;
	EAedirGetUpStyle ReadLyingStyle() const;
	UFUNCTION(Server, Unreliable)
	void ServerRequestGetUp(bool bForwardKey);
	double GetUpRequestTime = -100.0;
	EAedirGetUpStyle RequestedGetUpStyle = EAedirGetUpStyle::None;
	double LastGetUpRequestSent = -100.0;
	UPROPERTY(Replicated)
	EAedirGetUpStyle GetUpStyle = EAedirGetUpStyle::None;
	UPROPERTY(Replicated)
	EAedirGetUpStyle LyingStyle = EAedirGetUpStyle::None;
	UPROPERTY(Replicated)
	bool bReadyToGetUp = false;
	// How the body was moving the frame before, since by the time a hit is reported the movement has already stopped it.
	FVector LastVelocity = FVector::ZeroVector;
	// When this body last ran into another, so one shove is not counted on every frame of the contact.
	double LastBumpTime = -100.0;
	bool bLegLost = false;
	void ReshapeBodyPhysics();
	void PutMeshOnCapsule();
	float PhysicsShapeScale = 0.f;
	// The joints of the physics asset were laid for another skeleton, each a few centimetres short of where the bone it
	// holds begins, and drew every limb and the trunk shorter once the physics held the body: each joint is put back on
	// the child bone's own origin, as the skeleton has it.
	void FitJointsToSkeleton();
	void ApplyJointLimits(bool bRagdoll);
	TArray<FConstraintProfileProperties> LooseJointLimits;
	FString AppliedFreeJoint;
	FVector AppliedArmTuning = FVector::ZeroVector;
	FTransform MeshRestRelative = FTransform::Identity;
	const FAedirBoneMotor& GetBoneMotor(FName BoneName) const;
	void ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit);
	void SetupMotors();
	void UpdateMotors(float DeltaTime, bool bForce);
	void UpdateArmTraps(float DeltaTime);
	void LimitArmSpin();
	void UpdatePosedArms();
	float ArmTrapSeconds[2] = { 0.f, 0.f };
	float ArmTrapCooldown[2] = { 0.f, 0.f };
	// An arm let go of what caught it: how long it still passes through things and pulls harder to its pose.
	float ArmFreeLeft[2] = { 0.f, 0.f };
	void SetArmFree(int32 Side, bool bFree);
	void UpdateArmFree(float DeltaTime);
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

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastTopple(FVector TopVelocity);
	void ApplyTopple(const FVector& TopVelocity);
	FVector PendingTopple = FVector::ZeroVector;
	int32 PendingToppleFrames = 0;

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
	// How slack the body still is from the last blow it took, from 1 just struck down to 0.
	float Flinch = 0.f;

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
