#pragma once

#include "CoreMinimal.h"
#include "Audio/AedirSfxSubsystem.h"
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

// One step of getting up off the ground: where the hips are and how the body is bent there. Heights and distances are in
// cm for a body whose hips stand 95 cm high; back is from where the hips lay towards the feet; the trunk's angle is from
// lying flat with the head the way it lay (0) to upright (90) and on over the feet (past 90). Bends are in degrees:
// the hips and knees forward, the back bowed forward, the arms raised forward from hanging, the elbows bent.
USTRUCT(BlueprintType)
struct FAedirGetUpStage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HipHeightCm = 13.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HipBackCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float TrunkDegrees = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HipBend = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float KneeBend = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float BackBow = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float ArmRaise = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float ElbowBend = 0.f;

	// How much the hands lean on the ground here, 0 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float Hands = 1.f;

	// How much the knees (face down) or the feet (on the back) are held to the ground at their spot, 0 to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float Legs = 0.f;

	// Seconds this step takes to come to from the one before, for a body fresh and whole.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float Seconds = 1.f;

	// How far one leg, picked at random as the body starts up, is put forward of the other, 0 to 1: its hip and knee bent
	// as LeadHipBend and LeadKneeBend, its foot set down flat LeadFootAheadCm ahead of where the step has the hips (face
	// down: kneeling on the other knee, the body pushes up off that foot).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float Lead = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float LeadHipBend = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float LeadKneeBend = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float LeadFootAheadCm = 0.f;
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

// Where a hand is told by its side (0 the left, 1 the right): both of them.
inline constexpr int32 AedirBothHands = 2;

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
	float StumbleWalkBelowBalance = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float StumbleWalkMinShare = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float StumbleWalkSpeed = 420.f;

	// The hurried walk dies away over this long after the blow that started it, however long the balance takes to come
	// back, so a hard blow throws the body a few steps, not across the field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float StumbleWalkSeconds = 1.f;

	// Whether the body is walking after its fall just now: it neither turns to whoever it fights nor is held to them.
	bool IsStumbling() const { return bStumbling; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float RecoveryPerSecond = 0.5f;

	// A blow slows getting the balance back for a moment, the more the more balance it cost: down to HardHitRecoveryShare
	// of the usual pace after one that takes it all, and HeadHitRecoveryShare as much again after one to the head; the pace
	// comes back by HitRecoveryReturnPerSecond of the way a second. A blow to the head also costs HeadBalanceLossShare times
	// the balance a blow of the same energy would elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HardHitRecoveryShare = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float HeadHitRecoveryShare = 0.85f;

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
	// A blow taking at least GetUpKnockLoss of the balance while the body is getting up puts it down again; a lesser one sets
	// it back GetUpBlowStepLoss steps of getting up for all its balance (a tenth of it, a tenth of that).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float GetUpKnockLoss = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBlowStepLoss = 3.f;

	// Down but awake, the head turns slackly to where the player looks (anyone else, to whom it fights): at DownLookHz, with
	// DownLookShare of the strength, never more than DownLookMaxDegrees at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down")
	float DownLookHz = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownLookShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down")
	float DownLookMaxDegrees = 60.f;

	// And the trunk turns with it, from the waist, as one lying turns to look: the chest twisted about the spine towards
	// where the head looks, DownTwistShare as hard as the head (the belly DownTwistBellyShare of that), at most
	// DownTwistMaxDegrees at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownTwistShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownTwistBellyShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down")
	float DownTwistMaxDegrees = 50.f;

	// Down but awake, the muscles keep this much of their strength (1 as standing): enough to hold and move the limbs,
	// loose enough for the body to lie on the ground rather than stand stiff on it like a statue; dead or senseless they
	// keep only RagdollMotorShare.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownAwakeMotorShare = 0.2f;

	// Down but awake, the arms keep this much (1 as standing), to guard and strike from the ground as they do standing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownAwakeArmShare = 0.35f;

	// Come round after lying senseless, the muscles, the arms' joints and the look come back to what an awake body has over
	// this long: the first for a body with all its breath and health, the second for one with none. It gets up only once this
	// share of it is back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0.1", Units="s"))
	float WakeSecondsMin = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0.1", Units="s"))
	float WakeSecondsMax = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float WakeGetUpShare = 0.9f;

	// Lying awake, an arm meeting the ground is hurt only when stopped faster than this (a punch or a guard thrown from
	// the ground is not), as an arm thrown on it by a fall would be.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", Units="cm/s"))
	float DownArmHarmlessSpeed = 1600.f;

	// Giving up while getting up, the hands leave the ground once the body leans on them no more than this: before, without
	// them, the trunk stays folded over the hips and it falls back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float HoistSurrenderHandsBelow = 0.5f;

	// Until then one of them is held up all the same, and the other alone bears the body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	bool bHoistSurrenderOneHand = true;

	// And the trunk is held up to its step this much the quicker.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="1"))
	float HoistSurrenderTrunkScale = 1.8f;

	// The share of the arms' weight the trunk bears for them then, and how upright it has to be to stand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float HoistSurrenderArmBearShare = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float HoistSurrenderUpright = 0.55f;

	// Giving up lying on the ground, a body rolls slowly over onto its back first, at this speed (degrees/s), until its belly
	// faces this far up (-1 to 1); not getting up meanwhile for longer than SurrenderRollMostSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Surrender", meta=(ClampMin="0", Units="deg"))
	float SurrenderRollDegrees = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Surrender", meta=(ClampMin="-1", ClampMax="1"))
	float SurrenderRollBellyUp = 0.75f;

	// A body is looked at this often (s) for having gone through the ground: no ground within this far below it (cm) and
	// ground above it. It is put back this far over the ground (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fall", meta=(ClampMin="0.05", Units="s"))
	float UnderWorldCheckSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fall", meta=(ClampMin="100", Units="cm"))
	float UnderWorldLookCm = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fall", meta=(ClampMin="0", Units="cm"))
	float UnderWorldLiftCm = 40.f;

	// Walking into somebody standing at more than BodyPushLeastSpeed (cm/s), within this of touching (cm), gives them this
	// share of the speed it comes into them with (more for a heavier body, less for a lighter), and pushes at their balance
	// this much a second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Push", meta=(ClampMin="0"))
	float BodyPushLeastSpeed = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Push", meta=(ClampMin="0", Units="cm"))
	float BodyPushReachCm = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Push", meta=(ClampMin="0", ClampMax="1"))
	float BodyPushShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Push", meta=(ClampMin="0"))
	float BodyPushBalancePerSecond = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Surrender", meta=(ClampMin="0", Units="s"))
	float SurrenderRollMostSeconds = 5.f;

	// How much of the way to that speed the trunk is brought each second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Surrender", meta=(ClampMin="0"))
	float SurrenderRollGrip = 15.f;

	// How far an awake body's elbows give while it is down, sideways and along their bend either way of its middle (degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="5", ClampMax="150"))
	float DownElbowSideDegrees = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="30", ClampMax="150"))
	float DownElbowBendDegrees = 85.f;

	// A fist (or a hand) driven into something fixed, a wall, a tree, a rock, standing: it sounds as a fist on a forearm
	// from this speed on, the slow take up to the middle of these two and the hard one past it (cm/s); it is hurt past the
	// harmless speed, the wound this share of the blow's energy and this much damage for each cm/s past it; and it springs
	// back off the thing with this share of the speed it came in at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", Units="cm/s"))
	float FistWorldSoundMinSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", Units="cm/s"))
	float FistWorldSoundMaxSpeed = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", Units="cm/s"))
	float FistWorldHarmlessSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float FistWorldWoundShare = 1.f;

	// The weight behind it (kg), and how far out from the knuckles' or the toes' bone towards the thing it is marked (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float FistWorldWoundKg = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float FistWorldMarkOutCm = 1.5f;

	// A part of the body that strikes the ground or something fixed faster than this breaks rather than knocks itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts", meta=(ClampMin="0", Units="cm/s"))
	float ImpactBreakSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float FistWorldDamagePerSpeed = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", ClampMax="1"))
	float FistWorldBounceShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", Units="cm/s"))
	float FistWorldBounceMaxSpeed = 420.f;

	// Called as a fist meets something fixed, at the speed it came in with; gives back the harm done to the hand.
	float KnockFist(FName Bone, float Speed, const FHitResult& Hit);

	// The same for a foot kicked into something fixed, sprung back by its own share of the speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", ClampMax="1"))
	float KickWorldBounceShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0", Units="cm/s"))
	float KickWorldBounceMaxSpeed = 350.f;

	float KnockFoot(FName Bone, float Speed, const FHitResult& Hit);

	// The head driven into something fixed, heard as that take of a fist on the body, and marked where it struck (MarkAt, on
	// the skin of the forehead).
	float KnockHead(float Speed, const FHitResult& Hit, float SoundTake, const FVector& MarkAt);

	// The head knocked on something fixed takes this many times a fist's harm, and as many times its wear towards a mark.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float HeadKnockDamageShare = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Fist", meta=(ClampMin="0"))
	float HeadKnockWoundShare = 0.55f;

	// A body already lying on the ground takes this share of the push of a blow or a kick a standing one would: it has
	// the ground under it and nothing to be thrown off, so it is shoved, not sent flying.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", ClampMax="1"))
	float DownBlowPushShare = 0.35f;

	// A foot on the ground is held still by the pose only while the body stands: slower than the Min it is held,
	// faster than the Max it is let go to its muscles, so a walking foot rolls off the ground instead of skating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Feet", meta=(ClampMin="0", Units="cm/s"))
	float PlantedFootSpeedMinCm = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Feet", meta=(ClampMin="0", Units="cm/s"))
	float PlantedFootSpeedMaxCm = 6.f;

	// And only once it has come within this of where the pose sets it (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Feet", meta=(ClampMin="0"))
	float PlantedFootCatchCm = 4.f;

	// Getting up, the mesh put back on the capsule further than this (or turned more than 20 degrees) starts the hair's
	// simulation afresh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Down", meta=(ClampMin="0", Units="cm"))
	float HairResetMeshMoveCm = 20.f;

	// Getting up, the hands rest this high over the ground (their middle, in cm for a body of size 1): pulled lower, into
	// it, they shook against it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandAboveGroundCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChanceFromCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChanceFullCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance")
	float FallChancePerSecond = 0.8f;

	// The weight beyond the feet counts towards going over only once the balance is going: none of it at full balance, all
	// of it from this much balance down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0", ClampMax="1"))
	float FallTipBelowBalance = 0.5f;

	// Going over, the legs give and the hips sink, faster and faster: below SinkFromBalance of its balance they drop with up
	// to SinkGravityShare of a fall's pull (all of it at no balance left, as the balance lost between to the power
	// SinkLossPower); the body goes down once the hips have sunk FallSinkCm (for a body of size 1). Its balance coming back,
	// the drop is taken hold of at SinkRecoverBrake a second; back above SinkFromBalance the hips rise again at SinkRiseSpeed
	// (cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink", meta=(ClampMin="0", ClampMax="1"))
	float SinkFromBalance = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink", meta=(ClampMin="0", ClampMax="1"))
	float SinkGravityShare = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink", meta=(ClampMin="0.5"))
	float SinkLossPower = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink", meta=(ClampMin="0"))
	float SinkRecoverBrake = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink")
	float FallSinkCm = 48.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Sink")
	float SinkRiseSpeed = 60.f;

	// How far the hips have sunk under a lost balance, in the world's cm.
	float GetBalanceSinkCm() const { return SinkCm; }

	// Where the weight settles over the feet as the body stands is learnt over ComSettleSeconds while it is steady. The hips
	// tip after the weight gone off from there however it got there: more than ComLeanFromCm off, ComLeanDegreesPerCm for
	// every cm further, at most ComLeanMaxDegrees, that way; walking, ComLeanWalkingShare of that at ComLeanWalkSpeed and
	// over, since walking always carries the weight ahead of the feet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComSettleSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComLeanFromCm = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComLeanDegreesPerCm = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComLeanMaxDegrees = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight", meta=(ClampMin="0", ClampMax="1"))
	float ComLeanWalkingShare = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComLeanWalkSpeed = 60.f;

	// After the body has moved or shifted its feet, where the weight settles is taken afresh for this long, so a step
	// that ends a little off is a new stance, not the weight gone off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComResettleSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComFeetShiftCm = 2.f;

	// Not walking, the balance goes with the weight more than ComDriftLossFromCm off where it settles, by up to
	// ComDriftLossPerSecond from ComDriftLossFullCm off; it comes back by about RecoveryPerSecond at the same time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComDriftLossFromCm = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComDriftLossFullCm = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Weight")
	float ComDriftLossPerSecond = 0.7f;

	// A blow costs this share of the balance it would by its energy alone; the weight thrown off the feet by it does the
	// rest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float HitBalanceLossShare = 0.45f;

	// A blow on a shoulder or an upper arm moves the arm only this share of its push; the rest goes into the trunk, so the
	// arm is not thrown up beside the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float ShoulderBlowArmShare = 0.3f;

	// A blow slackens the muscles of the trunk, head and arms for a moment, so it is felt through the body: a fresh, unhurt
	// body by BlowSlackFitShare for BlowSlackFitSeconds, a worn one (no health and no breath left) by BlowSlackWornShare for
	// BlowSlackWornSeconds, half worn halfway between; all of it from a push of BlowSoftFullPush, less from a smaller one.
	// The slack lasts as long as that for a blow pushing BlowSoftFullPush, in proportion for a lighter one, at most
	// BlowSlackMaxScale times as long for a harder one. After that the muscles take hold again over BlowSlackReturnSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSlackMaxScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float BlowSlackFitShare = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit", meta=(ClampMin="0", ClampMax="1"))
	float BlowSlackWornShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSlackFitSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSlackWornSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSlackReturnSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float BlowSoftFullPush = 250.f;

	// How worn the body is for that: half from health gone, half from stamina gone, 0 to 1.
	float GetWornShare() const;

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
	// TiredKnockdownShare) and the slower its balance comes back (TiredRecoveryShare). Its arms hold the weaker the less
	// stamina it has, from full to none, down to TiredStrengthShare; its legs and trunk likewise down to UprightExhaustedShare.
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

	// Seconds it takes to get up off the ground: Min for a body fresh and whole, Max for one with no stamina and no health left,
	// and in between by how much of each is gone (GetUpTiredShare and GetUpHurtShare of the way each).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpSecondsMin = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpSecondsMax = 4.5f;

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

	// How far towards GetUpSecondsMax getting up slows with no stamina left, and with no health left; the two add up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float GetUpTiredShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float GetUpHurtShare = 0.5f;

	// Getting up is the body lifting its own hips, as long as the player holds W, A, S or D (anyone else, always): up, and
	// over to the way the key goes, the body turning round to face that way as it rises (GetUpTurnDegreesPerSecond). Let go
	// it sinks back down at GetUpSinkPace of its rising pace, and lies down again once it is back on the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpTurnDegreesPerSecond = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float GetUpSinkPace = 0.6f;

	// The hips go this far over towards the way of the keys while lifting, and the trunk leans after them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpLeanCm = 10.f;

	// How hard the body lifts itself, 0 to 1: full at the start; a blow on the way up takes GetUpBlowLiftLoss times the
	// balance it cost off it, and it comes back at GetUpLiftRecoverPerSecond. The rise goes at the pace of the lift, and with
	// none left the body sinks even with the key held; its muscles hold it with no less than GetUpWeakMuscleShare.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBlowLiftLoss = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpLiftRecoverPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float GetUpWeakMuscleShare = 0.4f;

	// Getting up the body is always physical, held to the moves by its muscles, even while aedir.DirectPose shows it posed
	// on its feet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	bool bPhysicalGetUp = true;

	// Just come into the world, the body is held in its pose for this long, its physics off, and only then let into it: from
	// its first frame it stands as it should instead of its arms and legs flying out of the rest pose it was made in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance", meta=(ClampMin="0"))
	float SpawnPosedSeconds = 0.4f;

	// The moves of getting up wait for the physical hips: trailing the pose by up to GetUpHipTrailFreeCm they go on at full
	// pace, slower the further, and stop at GetUpHipTrailStopCm until the muscles have brought the hips up after them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpHipTrailFreeCm = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpHipTrailStopCm = 14.f;

	// Sunk back to the ground, the body can start getting up again after this many seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpRetrySeconds = 0.6f;

	// Getting up hauled up by the hips (bHoistGetUp): the body stays loose, a ragdoll with weak muscles, and pulls on the
	// pelvis and the chest take it through the steps of getting up (FrontGetUpStages face down, BackGetUpStages on the
	// back), one after the other as long as the key towards the feet is held. The pull carries HoistWeightShare of the
	// body's weight times the lift left after the blows (GetUpLift), plus a spring of HoistSpringPerSecondSq towards where
	// the step has the hips, damped by HoistDampingPerSecond; struck, the lift drops and the body sinks under its own weight
	// until it comes back. Let go, the steps go back at GetUpSinkPace and the body sinks down again. Tired or hurt, the
	// springs, the steps and the recovery go at down to HoistWeakestShare. The trunk is turned to the step's angle at
	// HoistUprightHz. Past the last but one step, up to HoistDoneHeightShare of the standing height, upright to
	// HoistDoneUpright and slower than HoistDoneSpeedCm for HoistDoneHoldSeconds, the body is on its feet: standing and
	// stepping take it over, its muscles bringing it into the standing pose over HoistSettleSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	bool bHoistGetUp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistWeightShare = 1.f;

	// Face down: the hands go down beside the shoulders; up onto the hands and knees, the back flat; the hips back over the
	// heels as the trunk comes upright, sitting on the knees; then up. The first step is the body lying.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	TArray<FAedirGetUpStage> FrontGetUpStages;

	// On the back: the hands down beside the hips, the belly tightened, back and head a little up, the feet drawn in to the
	// hips with the knees up; the trunk up and over the feet, the hips with it, crouched on them; then up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	TArray<FAedirGetUpStage> BackGetUpStages;

	// A player's body gets up only with the keys held towards its feet (as seen from the camera), within this cosine of
	// that way: face down the other way is for crawling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="-1", ClampMax="1"))
	float GetUpKeyFeetCosine = 0.4f;

	// Face down the knees, on the back the feet, are held to their spot on the ground this far towards the feet from where
	// the hips lay (in cm for a 95 cm hip height), this far out to each side, by a spring of GetUpLegSpringPerSecondSq.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpKneeBackCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpFeetBackCm = 34.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpLegOutCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpLegSpringPerSecondSq = 120.f;

	// On the back the hands go down beside the hips, this far out and this far towards the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistBackHandOutCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistBackHandAheadCm = 8.f;

	// The steps go on only while the hips keep up with them: trailing where the step has them by up to HoistTrailFreeCm at
	// full pace, slower the further, not at all from HoistTrailStopCm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistTrailFreeCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistTrailStopCm = 40.f;

	// The hips come within this much of where a step has them before the next step begins (in cm for a 95 cm hip height).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistStepArriveCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistStepWaitSeconds = 0.45f;

	// How much faster than the steps' own seconds a body hauls itself up: the steps, the hands going down and the waits all
	// go this many times as fast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0.25", ClampMax="3"))
	float HoistPace = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistSpringPerSecondSq = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDampingPerSecond = 14.f;

	// The hips carry less of the weight the higher they are above where the step has them, none this far above (in cm for
	// a 95 cm hip height).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistCarryEaseCm = 15.f;

	// The pull never speeds the hips up by more than this beyond carrying the weight, in cm/s².
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistMaxExtraAccel = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistRampSeconds = 1.f;

	// With the keys held, first the neck takes up the head, harder over HoistHeadSeconds, turning the face to the way the
	// camera looks (HoistHeadHz, lifted HoistHeadLiftShare of the way up). After HoistHandDelaySeconds the hand nearer the
	// ground reaches over HoistHandReachSeconds to its spot on the ground and leans on it (a spring of
	// HoistHandSpringPerSecondSq) for as long as the steps have the hands down; the steps start once both are down. The pull
	// comes in over HoistRampSeconds, and let go it fades over that.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHeadSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHeadHz = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistHeadLiftShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandDelaySeconds = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandReachSeconds = 0.6f;

	// The second hand goes down this long after the first; both palms are turned flat to the ground at HoistPalmHz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistSecondHandLateSeconds = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistPalmHz = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandSpringPerSecondSq = 250.f;

	// The hand is pulled to the ground no harder than this, in cm/s² for the whole arm; leaning on it lifts this share of
	// the weight above the belly, until the hips are hauled up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandMaxAccel = 3500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistHandPushShare = 0.45f;

	// Where the hand goes: this far out from the shoulder, away from the chest, and this far on towards the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandOutCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistHandAheadCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0.1", ClampMax="1"))
	float HoistWeakestShare = 0.5f;

	// How hard the hips are pulled across the ground to where the step has them (1/s²).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistAcrossSpringPerSecondSq = 10.f;

	// The chest is drawn out from the hips the way the step has the trunk by a spring of HoistChestSpringPerSecondSq, carrying HoistChestCarryShare of the
	// weight above the belly (the hips carry the rest).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistChestSpringPerSecondSq = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistChestCarryShare = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistUprightHz = 2.4f;

	// The hips are turned at this pace to face the way the body will stand: the head's way face down, the feet's on the back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistTurnHz = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDoneHeightShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistDoneUpright = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDoneSpeedCm = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDoneHoldSeconds = 0.15f;

	// Hauled up onto its feet, the body is handed back to its capsule; the hold of its hips and feet on the capsule and the
	// ground, and the muscles' strength, come in over HoistSettleSeconds, so it is taken into the standing pose softly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistSettleSeconds = 1.2f;

	// From this share of it on it can walk again, and its feet are held where they stand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistSettleMoveShare = 0.5f;

	// Coming up the last of the way the body finds its feet: from HoistFeetFromSteps before the end of the steps on (coming in
	// over HoistFeetEaseSteps) each foot is held flat to the ground where it stands, by a spring of HoistFootSpringPerSecondSq
	// and turned flat at HoistFootHz, and the hips are lifted no higher than the legs reach from them (HoistLegReachShare of
	// their length). A foot further than HoistFootTriggerCm from under its hip is stepped there in HoistFootStepSeconds,
	// HoistFootStepCm over the ground, one at a time, set down HoistFootAheadCm before the hips.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="3"))
	float HoistFeetFromSteps = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0.05", ClampMax="1"))
	float HoistFeetEaseSteps = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistFootSpringPerSecondSq = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistFootHz = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0.5", ClampMax="1"))
	float HoistLegReachShare = 0.98f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="1"))
	float HoistFootTriggerCm = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0.05", Units="s"))
	float HoistFootStepSeconds = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistFootStepCm = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistFootAheadCm = 3.f;

	// With both feet found (no further than HoistFeetFoundCm from where they stand) it counts as up sooner: from
	// HoistDoneFromSteps before the end of the steps on, this high, this upright and after this long.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistFeetFoundCm = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="2"))
	float HoistDoneFromSteps = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistDoneFeetHeightShare = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistDoneFeetUpright = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", Units="s"))
	float HoistDoneFeetHoldSeconds = 0.08f;

	// In the last step and still not up after HoistDoneEasySeconds, the body counts as up from HoistDoneEasyHeightShare of
	// the standing height and HoistDoneEasyUpright; still not after HoistGiveUpSeconds, it gives up and lies down again to
	// try anew, never left hanging half up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDoneEasySeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistDoneEasyHeightShare = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistDoneEasyUpright = 0.85f;

	// From HoistDoneEasySeconds to this long into the last step, as upright as this is enough in the end.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistDoneLateSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="-1", ClampMax="1"))
	float HoistDoneLateUpright = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistGiveUpSeconds = 5.f;

	// Getting up the body stands on what it has on the ground, and is only as steady as its lift and strength: struck or
	// worn, the chest leaning off where the step has it is drawn on over by its weight (HoistTipShare of what gravity would
	// do to it there), and the trunk is held up the less, down to HoistUnsteadyHoldShare. The walking keys lean it back:
	// pressed off the way they were first held to get up, the chest and the hips are pushed that way at up to
	// HoistKeyBalanceAccel (cm/s2); a body nobody plays leans back by itself, HoistSelfBalanceShare as well. Leaning over by
	// more than HoistTipOverCm (for a 95 cm hip height) it goes over and lies down again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistTipShare = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistUnsteadyHoldShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistKeyBalanceAccel = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistSelfBalanceShare = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistTipOverCm = 32.f;

	// Only a body less steady than this can go over: one at its full strength holds whatever its trunk does on the way up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0", ClampMax="1"))
	float HoistTipUnsteady = 0.8f;

	// Walked or run into while getting up, the body loses this much of its lift per cm/s it was met at (beyond BumpMinSpeed,
	// and a little from the touch itself), and its trunk is shoved as a standing one's is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float GetUpBumpLiftPerSpeed = 0.006f;

	// Getting up even a walk into the body counts: from this closing speed on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpBumpMinSpeed = 60.f;

	// The leg put forward getting up is lifted over the ground as it comes: its foot held up by this share of how far it
	// still has to go, at most HoistStepLiftMaxCm (for a 95 cm hip height), and set down flat once there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp", meta=(ClampMin="0"))
	float HoistStepLiftShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float HoistStepLiftMaxCm = 16.f;

	// Getting up, the hand that is in use (its button held, or holding something) stays off the ground, free to fight or hold
	// with: only one may; the other takes the weight alone. -1 for both on the ground.
	int32 GetHoistFreeHand() const { return bHoisting ? HoistFreeHand : -1; }

	// For this long an arm (0 left, 1 right) touches nothing: it holds somebody and would be thrown about by what it holds.
	void LetArmThrough(int32 Side, float Seconds);
	// The same for the whole body: what it carries close to it would be pinned by it.
	void LetBodyThrough(float Seconds);
	// Somebody has this body in their hands and lifts it: for this long it does not try to get up.
	void SetHeldUp(float Seconds);
	// Lifted off its feet by hands that carry its whole weight: it hangs from them, loose but awake.
	void LiftOffFeet();
	// The body's whole weight in kilos.
	float GetBodyKilos() const;

	// Getting up, this hand (0 left, 1 right) is needed on the ground and not the body's own to fight with.
	bool IsHandOnGround(int32 Side) const { return bHoisting ? HoistFreeHand != Side && HoistFreeHand != AedirBothHands : IsGettingUp(); }
	// Getting up, how far this hand is on its way down to the ground, 0 to 1; until it starts it may stay where it was (on
	// a wound, say).
	float GetHoistHandReach(int32 Side) const;
	bool IsHandGoingDown(int32 Side) const { return bHoisting ? GetHoistHandReach(Side) > 0.f : IsGettingUp(); }

	// Walked or run into by someone while getting up.
	void BumpedGettingUp(const class AAedirCreature* By, const FVector& Point, FName Part = NAME_None);

	// While the hips are being hauled up: how far the pull has come in, 0 to 1.
	float GetHoistShare() const { return bHoisting ? HoistDrive : 0.f; }
	// The leg put forward getting up: 0 the left, 1 the right.
	int32 GetHoistLeadSide() const { return HoistLeadSide; }
	bool IsHoisting() const { return bHoisting; }
	// Just hauled up onto its feet, being taken into the standing pose.
	bool IsSettling() const { return HoistSettleLeft > 0.f; }
	// How far an awake body has come round from lying senseless, 0 to 1.
	float GetWakeShare() const { return WakeShare; }
	// The step of getting up the body is at (between two, mixed), and how far along the steps it is (0 lying, 1 the first
	// step, and on); false unless being hauled up.
	bool GetHoistStage(FAedirGetUpStage& OutStage, float& OutProgress) const;

	// How far the hips are over towards the way of the keys, in cm in the body frame (X forward, Y right); zero unless
	// getting up.
	FVector GetGetUpLeanLocal() const;
	// How hard the body still lifts itself getting up, 0 to 1.
	float GetGetUpLift() const { return GetUpLift; }

	// How much further than the physics asset's knee joints have it (to about 115 degrees) the knees fold, so a body kneeling
	// can sit right back on its heels; the straight end stays as it is. KneeBendTwistSign is which way of the joint's twist
	// the bend lies. 0 keeps the asset's.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float KneeExtraBendDegrees = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float KneeBendTwistSign = -1.f;

	// How weak the muscles are at the start of getting up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|GetUp")
	float GetUpMuscleShare = 0.7f;

	// A knockdown tips the body over the way the blow went: the head starts off at this share of the speed a stagger would give.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float KnockdownToppleShare = 0.6f;

	// Going down, no part of the body is thrown upwards faster than this, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntryMaxRiseSpeed = 60.f;

	// For this long after going down that limit holds on every part, whatever pushes it: the joints and the ground taking
	// hold of a body caught mid-step.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntrySettleSeconds = 0.4f;

	// The fastest a part found in the ground is let out of it, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntryOutOfGroundSpeed = 40.f;

	// Over that moment no part of a leg moves faster than this against the speed the body went down at, in cm/s.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntryLegSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts")
	float RagdollEntryLegSpinDegrees = 540.f;

	// Run into a wall, the part of the body that met it is thrown back off it by WallImpactPartRebound of the speed it
	// came in at past WallImpactSpeedThreshold, and the trunk by WallImpactTrunkRebound of it, each for its own weight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts", meta=(ClampMin="0"))
	float WallImpactPartRebound = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Impacts", meta=(ClampMin="0"))
	float WallImpactTrunkRebound = 0.25f;

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
	// The keys held while down or getting up, as a way across the ground (zero for none in particular): down, they start
	// getting up; on the way up they keep the body lifting, towards that way.
	void RequestGetUp(const FVector& KeyWay);

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
	float HitPushScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitPushNowShare = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Balance|Hit")
	float HitPushSpreadSeconds = 0.2f;

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

	// A blow that landed on this body: the bone, where on it (in the bone's frame), when (world seconds) and how hard it
	// pushed.
	struct FAedirBlowLanded
	{
		FName Bone;
		FVector Local = FVector::ZeroVector;
		FVector World = FVector::ZeroVector;
		double Time = -1000.0;
		float Push = 0.f;
	};
	// The last few blows, the oldest first: two landing at once on two places are two blows.
	const TArray<FAedirBlowLanded, TInlineAllocator<4>>& GetRecentBlows() const { return RecentBlows; }

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
	bool bDroppedSenseless = false;
	void SetBalance(float NewBalance);
	void AddLeanDirection(const FVector& WorldDirection, float Magnitude);
public:
	// A step taken to catch the body has landed.
	void OnRecoveryStepTaken();
private:
	// Whether the body stands still enough for its feet on the ground to be held by the pose.
	bool bStandingForPlant = true;

	// Hands the body control what the body is down: dead, senseless or awake, and how much muscle it keeps.
	void UpdateDownMuscles(float DeltaTime);
	float GetWakeStrength() const { return FMath::Square(FMath::Clamp(WakeShare, 0.f, 1.f)); }
	float WakeShare = 1.f;
	float JointsWake = 1.f;
	// Coming up the last of the way: where each foot stands, and the step it takes there.
	// Holds and steps the feet on the last of the way up; whether both are found.
	bool UpdateHoistFeet(float Share, const FVector& Hips, const FVector& Right, double GroundZ, float Size, float Strength, float DeltaTime);
	bool bHoistFeetSet = false;
	bool bHoistFootStepping[2] = { false, false };
	FVector HoistFootSpot[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector HoistFootFrom[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float HoistFootElapsed[2] = { 0.f, 0.f };
	FQuat HoistFootRest[2] = { FQuat::Identity, FQuat::Identity };
	float HoistAnkleCm = 9.f;
	// A foot whose toes are this near the ground stands on them.
	static constexpr float HoistToesDownCm = 3.f;
	float HoistBallCm = 3.f;
	// For aedir.GetUpWatch: how long and how high both feet were off the ground coming up the last of the way and after.
	void WatchRisingFeet(float DeltaTime);
	float FeetWatchLeft = 0.f;
	float FeetWatchSeconds[2] = { 0.f, 0.f };
	float FeetWatchAir[2] = { 0.f, 0.f };
	float FeetWatchMost[2] = { 0.f, 0.f };

	void DecayLeanDirection(float Amount);
	void UpdateFallState();
	void UpdateServerState(float DeltaTime);
	void ApplyCenterOfMassLoss(float DeltaTime);
	void EnterRagdoll(float MinimumSeconds);
	void UpdatePelvisLean(float DeltaTime);
	void UpdateStumbleWalk(float DeltaTime);
	bool bStumbling = false;
	double StumbleBlowTime = -100.0;
	FVector RecoveryWatchFrom = FVector::ZeroVector;
	float HitRecoveryScale = 1.f;
	float BlowSoftness = 0.f;
	double BlowSoftUntil = -100.0;
	double NextSlackLogTime = 0.0;
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
	TArray<FAedirBlowLanded, TInlineAllocator<4>> RecentBlows;
	// One part of a blow's push landing: on the physical body, the trunk's and head's sway and the hips.
	void LandPush(FName Bone, const FVector& Impulse, const FVector& Location);
	void UpdatePendingPushes(float DeltaTime);
	void DrivePelvis();
	void SetRagdollPresentation(bool bRagdoll);
	void ApplyRagdollPhysics(bool bRagdoll);
	void ApplyBodyContacts(bool bRagdoll);
	void ApplyLegPhysics(bool bRagdoll);
	void UpdateGetUp(float DeltaTime);
	void StartHoist();
	void UpdateHoist(float DeltaTime);
	void StopHoist(const TCHAR* Why);
	void LookWhileDown();
	void WidenKnees();
	// The knee joints' own twist range either way, as the physics asset has it, once read.
	float KneeAssetTwistHalf = 0.f;
	// Whether the keys asking the body up are held now (a body nobody plays always holds them), towards its feet.
	bool IsGetUpKeyHeld() const;
	const TArray<FAedirGetUpStage>& HoistStages() const;
	FAedirGetUpStage SampleHoistStage(float Progress) const;
	bool bHoisting = false;
	// How far along the steps of getting up the body is: 0 lying, 1 at the first step, and on.
	float HoistProgress = 0.f;
	// The way the head lay from the hips when the body started getting up, flat.
	FVector HoistHeadward = FVector::ForwardVector;
	float HoistGroundZ = 0.f;
	float HoistTrailLogged = 0.f;
	float HoistStepWaited = 0.f;
	bool bHoistFinished = false;
	float HoistDrive = 0.f;
	float HoistDoneFor = 0.f;
	float HoistRestFor = 0.f;
	float HoistHeldSeconds = 0.f;
	int32 HoistHand = -1;
	int32 HoistFreeHand = -1;
	// The leg of a kick goes through other bodies: what it strikes is moved by the blow alone, not by the force that drives
	// the leg, which threw bodies about.
	void UpdateKickLegContacts();
	int32 KickThroughLeg = -1;
	FVector ChestFaceLocal = FVector::ZeroVector;
	float SpawnPosedLeft = 0.f;
	float SpawnAge = 0.f;
	int32 HoistLeadSide = 0;
	// The way the keys were held to get up, against which the keys lean the body back, and how far it leans off its step.
	FVector HoistNeutralKey = FVector::ZeroVector;
	float HoistLeanLogged = 0.f;
	float HoistLastStepFor = 0.f;
	FVector HoistSelfLean = FVector::ZeroVector;
	FVector HoistLeanSmoothed = FVector::ZeroVector;
	// How the body lay when it started getting up.
	EAedirGetUpStyle HoistStyle = EAedirGetUpStyle::None;
	FVector HoistFaceLocal = FVector(0.f, 1.f, 0.f);
	FVector HoistPalmLocal[2] = { FVector(-1.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f) };
	float HoistPalmDownLogged = 0.f;
	float ChestAboveHips = 40.f;
	float HoistSeconds = 0.f;
	float HoistHighest = 0.f;
	float HoistSettleLeft = 0.f;
	double HeldUpUntil = 0.0;
	float BodyThroughLeft = 0.f;
	void SetBodyThrough(bool bThrough);
	// The sound, the spring back and the harm of a part driven into something fixed, and the part behind it.
	float KnockPart(FName Bone, FName BehindBone, float Speed, float BounceShare, float BounceMax, const FHitResult& Hit, EAedirSfx Sound = EAedirSfx::PunchForearm, float SoundTake = -1.f,
		const FVector* MarkAt = nullptr, float DamageShare = 1.f, float WoundShare = 1.f);
	void WatchKnock(float DeltaTime);
	FName KnockWatchBone;
	FVector KnockWatchFrom = FVector::ZeroVector;
	FVector KnockWatchAway = FVector::ZeroVector;
	float KnockWatchLeft = 0.f;
	float KnockWatchMost = 0.f;
	bool bFootPlanted[2] = { false, false };
	// How far a body just hauled up has found its feet, 1 standing as usual.
	float GetFoundFeetShare() const;
	FVector HoistAnchor = FVector::ZeroVector;
	// Dev:for a moment after going down, how high the hips rise and how fast the parts move ([Fall] in the log).
	void WatchFall(float DeltaTime);
	void SettleRagdollEntry(float DeltaTime);
	void LiftLegsOutOfGround();
	// The ground a body gets up on, under anything loose, any body and anything held or worn.
	bool TraceStandingGround(const FVector& Start, const FVector& End, FVector& OutHitPoint, bool bLog = true) const;
	// Loose things a body stood up in the middle of, passed through until it is off them.
	void IgnoreLooseThingsAtFeet();
	void ForgetThingsSteppedOff();
	TArray<TWeakObjectPtr<AActor>> SteppedOffThings;
	void CapRagdollEntrySpeeds();
	float RagdollSettleLeft = 0.f;
	float RagdollRiseLimit = 0.f;
	FVector RagdollCarried = FVector::ZeroVector;
	float FallWatchLeft = 0.f;
	float FallStartZ = 0.f;
	float FallMaxRise = 0.f;
	float FallMaxUpSpeed = 0.f;
	float FallMaxSpeed = 0.f;
	float FallEntrySpeed = 0.f;
	FName FallFastestBone;
	float FallFastestAt = 0.f;
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
	FVector GetUpKeyWay = FVector::ZeroVector;
	FVector2D GetUpLean = FVector2D::ZeroVector;
	float GetUpLift = 1.f;
	float GetUpHipTrailMost = 0.f;
	float GetUpWaitedSeconds = 0.f;
	float GetUpHipTrailSum = 0.f;
	float GetUpRiseSeconds = 0.f;
	// Where the hips lay when getting up started.
	FTransform GetUpStartPelvis = FTransform::Identity;
	EAedirGetUpStyle ReadLyingStyle() const;
	// Puts a body that went through the ground back on top of it.
	void UpdateUnderWorld(float DeltaTime);
	// Shoves along somebody standing that this body walks into.
	void UpdateBodyPush(float DeltaTime);
	float UnderWorldCheckIn = 0.f;
	// The way the belly faces in the world.
	FVector ReadBellyWay() const;
	// Rolls a body giving up on the ground over onto its back; true while it still rolls.
	bool UpdateSurrenderRoll(float DeltaTime);
	float SurrenderRollSeconds = 0.f;
	UFUNCTION(Server, Unreliable)
	void ServerRequestGetUp(FVector KeyWay);

	double GetUpRequestTime = -100.0;
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
	void ApplyJointLimits(bool bRagdoll, bool bArmsOnly = false);
	// Lying awake, the arms' joints have the room they have standing.
	bool bDownArmsAwake = false;
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
	// The weight off the middle of the feet in the body's own frame (cm), how near it is to their edge (cm, under 0 past
	// it), and whether the body stands so it can lose its balance by it.
	FVector2D ComFromFeetLocal = FVector2D::ZeroVector;
	// Where it settles as the body stands, and how far off from there it is now.
	FVector2D ComSettledLocal = FVector2D::ZeroVector;
	FVector2D ComDriftLocal = FVector2D::ZeroVector;
	bool bComSettledKnown = false;
	FVector2D LastFeetCentre = FVector2D::ZeroVector;
	double ComResettleUntil = 0.0;
	float ComEdgeCm = 100.f;
	bool bComEdgeCounts = false;
	double NextComLogTime = 0.0;

	// Protective reflex. The target is sampled every 0.1 s, the blend is smoothed every tick.
	float TargetProtectiveAlpha = 0.f;
	float ProtectiveAlpha = 0.f;
	FVector ProtectiveGroundWorld = FVector::ZeroVector;
	// Alpha at the last tick the reflex was active, used to soften the fall damage on landing.
	float LastProtectiveReflexAlpha = 0.f;

	FRotator CurrentLean = FRotator::ZeroRotator;
	float TimeBelowThreshold = 0.f;
	float SinkCm = 0.f;
	float SinkSpeed = 0.f;
	float SinkLastBalance = 1.f;
	double NextSinkLogTime = 0.0;
	void UpdateBalanceSink(float DeltaTime);

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

	// The arms whose joints were last given the limp limits for being broken (1 left, 2 right).
	uint8 BrokenArmJoints = 0;
};
