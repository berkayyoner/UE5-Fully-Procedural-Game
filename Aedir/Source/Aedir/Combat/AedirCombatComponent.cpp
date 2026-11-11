#include "Combat/AedirCombatComponent.h"
#include "Audio/AedirSfxSubsystem.h"

#include "Aedir.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirGroupSubsystem.h"
#include "Creature/AedirStatsComponent.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Net/UnrealNetwork.h"
#include "Npc/AedirNpc.h"
#include "Npc/AedirTrainingDummy.h"
#include "Physics/AedirBalanceComponent.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	constexpr float AedirCombatStatMax = 100.f;
	constexpr float AedirDefaultStat = 10.f;
	constexpr float AedirGuardBlendSpeed = 6.f;
	// How long a weapon must be off a body before touching it again counts as a new blow.
	constexpr float AedirFreshContactSeconds = 0.12f;
	// The closed loop between the pose and what is shown: how quickly it learns, how far it may ask beyond the pose, how still
	// the pose must be for it to learn, and how quickly it forgets out of the stance.
	constexpr float AedirServoGainPerSecond = 2.5f;
	constexpr float AedirServoMaxHandCm = 18.f;
	constexpr float AedirServoMaxBladeDegrees = 70.f;
	constexpr float AedirServoSamePoseDegrees = 8.f;
	constexpr float AedirServoStillHandSpeed = 25.f;
	constexpr float AedirServoStillAimSpeed = 40.f;
	constexpr float AedirServoForgetPerSecond = 2.f;
	constexpr float AedirServoFightShare = 0.95f;
	TAutoConsoleVariable<int32> CVarAedirPosedArms(TEXT("aedir.PosedArms"), 1, TEXT("A weapon's arms in the stance move exactly as the pose has them (1), or are left to their muscles (0)."));
	TAutoConsoleVariable<int32> CVarAedirPoseServo(TEXT("aedir.PoseServo"), 1, TEXT("Feed back where the hand and blade really are, so a held pose settles exactly where it is meant to (1)."));
	// The fixed corrections measured once for each key pose were right only for the way they were measured coming to it; with
	// the closed loop the poses are asked exactly as the reference stills have them.
	float AedirFixShare() { return CVarAedirPoseServo.GetValueOnGameThread() != 0 ? 0.f : 1.f; }
	// A bare fist lands a blow from this speed into the body, slower than a weapon: it weighs little but is meant to hit.
	constexpr float AedirFistMinImpactSpeed = 180.f;
	// A blunt weapon longer than this carries its weight in the head: it turns as if this much heavier, and its swing is
	// damped by this share, so it carries on past the end of the arm's movement.
	constexpr float AedirHeadHeavyMinLengthCm = 40.f;
	constexpr float AedirHeadHeavyTurnMass = 1.8f;
	constexpr float AedirHeadHeavyDamping = 0.7f;
	// A blade moving across itself more than this share along its flat strikes with the flat.
	constexpr float AedirFlatStrikeShare = 0.7f;
	// Against the body below this share of the blow speed, a fist or blade is resting there; driven in again from that, it lands anew.
	constexpr float AedirRestingIntoShare = 0.4f;
	TAutoConsoleVariable<int32> CVarAedirHitLog(TEXT("aedir.HitLog"), 0, TEXT("Log every quick touch on a body that did not count as a blow, and why (1)."));
	constexpr float AedirArmRampInSeconds = 0.1f;
	constexpr float AedirArmRampOutSeconds = 0.2f;
	constexpr float AedirMinSwingDuration = 0.05f;
	constexpr float AedirWeightSlowdownPerRatio = 0.35f;
	constexpr float AedirArmedSkillExperience = 0.05f;

	constexpr float AedirSwingWindupEnd = 0.3f;
	constexpr float AedirSwingStrikeEnd = 0.65f;
	constexpr float AedirWindupPullBack = 20.f;
	constexpr float AedirArcBulge = 15.f;
	constexpr float AedirShoulderLateral = 22.f;
	constexpr float AedirShoulderHeight = 30.f;
	constexpr float AedirGuardForward = 30.f;
	constexpr float AedirGuardHeight = 20.f;
	constexpr float AedirGuardInward = 8.f;
	constexpr float AedirPunchLateral = 14.f;
	constexpr float AedirHookOutsideLateral = 55.f;
	constexpr float AedirHookAcrossLateral = 30.f;
	constexpr float AedirChestHeight = 10.f;

	constexpr float AedirAIRetargetInterval = 0.2f;
	constexpr float AedirAIMemorySeconds = 3.f;
	constexpr float AedirAITurnSpeed = 220.f;
	constexpr float AedirAICloseRange = 90.f;

	constexpr float AedirArmDampingRatio = 0.7f;
	constexpr float AedirArmStrengthBase = 0.6f;
	constexpr float AedirArmStaminaFloor = 0.35f;
	constexpr float AedirArmInjuryStiffnessLoss = 0.7f;
	constexpr float AedirArmDazeStiffnessLoss = 0.6f;
	constexpr float AedirArmMassStrengthScale = 0.6f;
	constexpr float AedirArmSubstepSeconds = 1.f / 120.f;
	constexpr float AedirArmReachMinScale = 0.45f;
	constexpr float AedirArmReachHardLimit = 1.15f;
	constexpr float AedirArmFallbackLength = 12.f;
	constexpr float AedirArmMinSpeed = 50.f;
	// What a held thing strikes with is its blade or haft, as wide as its box across the edge and only a little more: the
	// model's own bounds take in the guard, and a blade that thick would stop a hand's breadth short of what it meets. A haft
	// is widened for the head at its end.
	constexpr float AedirStrikeSkinCm = 0.5f;
	constexpr float AedirStrikeMinRadiusCm = 1.5f;
	constexpr float AedirStrikeMaxRadiusCm = 3.5f;
	constexpr float AedirStrikeHaftRadiusCm = 3.5f;
	constexpr double AedirStrikeHaftBoxY = 0.035;
	// The swept weapon is looked at every so many of its own thicknesses along its way, at most this many times a frame.
	constexpr float AedirStrikeStepRadii = 1.5f;
	constexpr int32 AedirStrikeMaxSteps = 12;
	constexpr float AedirArmReconcileDistance = 40.f;
	constexpr float AedirArmProxyInterpSpeed = 12.f;
	constexpr float AedirArmNetSendInterval = 1.f / 30.f;
	constexpr float AedirArmAimYawLimit = 110.f;
	constexpr float AedirArmOutwardYawLimit = 80.f;
	constexpr float AedirArmAcrossYawLimit = 0.f;
	constexpr float AedirArmConvergeYawDegrees = 4.f;
	constexpr float AedirArmMouseSpeedSmoothing = 10.f;
	constexpr float AedirArmGuardSpeedLow = 250.f;
	constexpr float AedirArmGuardSpeedHigh = 800.f;
	// Bare fists, as in Half Sword: the mouse moved slowly moves the guard, moved quickly throws a punch out, from this speed
	// on to fully at this one (mouse axis units a second; at the project's 0.07 a unit, about 700 to 2600 pixels a second).
	constexpr float AedirFistPunchSpeedLow = 50.f;
	constexpr float AedirFistPunchSpeedHigh = 180.f;
	constexpr float AedirArmPunchRiseSpeed = 30.f;
	constexpr float AedirArmPunchFallSpeed = 8.f;
	// Guard: both fists meet in front of the face, at nose height measured from the capsule centre.
	constexpr float AedirGuardForwardCm = 32.f;
	constexpr float AedirGuardInwardCm = 1.5f;
	constexpr float AedirGuardFaceHeightCm = 62.f;
	// The guard fists are this far below the head bone: before the chin.
	constexpr float AedirGuardBelowHeadCm = 10.f;
	constexpr float AedirArmSideHoldStart = 0.35f;
	constexpr float AedirArmSideHoldGain = 3.f;
	constexpr float AedirArmThrowReachShare = 1.f;
	constexpr float AedirGuardHeightPerDegree = 0.45f;
	constexpr float AedirArmDominanceMargin = 0.1f;
	constexpr float AedirArmGuardKeepShare = 0.15f;
	constexpr float AedirArmHeadHeightPitchDegrees = 2.f;
	constexpr float AedirArmAimPitchMin = -70.f;
	constexpr float AedirArmAimPitchMax = 110.f;
	constexpr float AedirFistAimPitchMax = 85.f;
	// Weapon stance. The hand stays in front of the chest and moves little; the blade sweeps much further around it.
	constexpr float AedirWeaponYawLimit = 100.f;
	constexpr float AedirWeaponPitchMin = -95.f;
	// All the way down the point goes to the ground in front of the feet, for a blow at someone lying there.
	constexpr float AedirWeaponDownPitch = -82.f;
	// The trunk bends into a downward blow and arches back under a raised one.
	constexpr float AedirSwingBendDownDegrees = 34.f;
	constexpr float AedirSwingBendUpDegrees = 9.f;
	// Measured in Half Sword with raw mouse counts, the same input both games read: from one end of a weapon's travel to the
	// other is about 3750 counts across and 3500 up and down, so from the middle about 1875 to the far side (100 degrees here)
	// and 1750 to the top or the bottom. At the project's 0.07 a count and the aim's 0.3 a unit, these shares. They had been
	// set five times higher from cursor moves that were not raw counts, and every small hand movement threw the arms round.
	constexpr float AedirWeaponMouseShare = 2.5f;
	constexpr float AedirWeaponMouseUpShare = 2.4f;
	// Bare fists move round their guard by this share of the same.
	constexpr float AedirFistMouseShare = 4.f;
	// With no button held the point hangs down and across the body.
	constexpr float AedirWeaponGuardPitch = -14.f;
	constexpr float AedirWeaponGuardInwardYaw = 34.f;
	constexpr float AedirWeaponChestHeightCm = 28.f;
	constexpr float AedirWeaponChestSideCm = 10.f;
	constexpr float AedirWeaponHandReachCm = 44.f;
	constexpr float AedirWeaponHandThrowReachCm = 58.f;
	constexpr float AedirWeaponHandPitch = -18.f;
	constexpr float AedirWeaponFightHandPitch = -5.f;
	constexpr float AedirWeaponHandPitchShare = 0.7f;
	constexpr float AedirWeaponTurnRate = 48.f;
	constexpr float AedirWeaponTurnFrequencyShare = 1.8f;
	constexpr float AedirWeaponTurnDampingLight = 0.78f;
	constexpr float AedirWeaponTurnDampingHeavy = 0.65f;
	constexpr float AedirWeaponTurnDampingPerKg = 0.05f;
	constexpr float AedirWeaponTurnSubstep = 1.f / 120.f;
	constexpr float AedirWeaponWeightDroopPerKg = 1.5f;
	// How the arms get to where the mouse wants them: the muscles push with a limited force, so a swing gathers speed, peaks
	// and is reined in again, a heavier weapon running on a little further. Weight slows them, and so does being out of breath.
	constexpr float AedirPoseMaxSpeed = 1500.f;
	constexpr float AedirPoseMaxAccel = 14000.f;
	constexpr float AedirPoseFrequency = 17.f;
	constexpr float AedirPoseDampingLight = 0.85f;
	constexpr float AedirPoseDampingHeavy = 0.6f;
	constexpr float AedirPoseDampingPerKg = 0.06f;
	constexpr float AedirPoseSubstep = 1.f / 120.f;
	constexpr float AedirPoseSpeedMassShare = 0.15f;
	constexpr float AedirPoseStrengthReference = 0.7f;
	constexpr float AedirFatigueSpeedLoss = 0.55f;
	constexpr float AedirFatigueDroopCm = 8.f;
	constexpr float AedirFatigueDroopPitch = 12.f;
	// A swing costs breath and builds the arm, a little per unit of travel. Slow aiming costs nothing.
	constexpr float AedirSwingEffortMinSpeed = 220.f;
	constexpr float AedirSwingStaminaPerTravel = 0.0025f;
	constexpr float AedirSwingStaminaMassShare = 0.2f;
	constexpr float AedirSwingStrengthPerTravel = 0.000015f;
	constexpr float AedirSwingStrengthMassShare = 0.25f;
	// With a button held the blade stands up in front of the body. The first half of the mouse travel takes the hands to the side with the point slowly following them.
	constexpr float AedirWeaponFightPitch = 52.f;
	constexpr float AedirWeaponOpenStart = 0.22f;
	constexpr float AedirWeaponTipOutYaw = 36.f;
	constexpr float AedirWeaponMidPitch = 40.f;
	// The hand travels a wide arc around the body: out to the side first, then back and up beside the shoulder.
	constexpr float AedirSwingMidYaw = 50.f;
	constexpr float AedirSwingEndYaw = 64.f;
	constexpr float AedirSwingMidRadiusCm = 52.f;
	constexpr float AedirSwingMidRadiusTwoHandCm = 44.f;
	constexpr float AedirSwingEndRadiusCm = 36.f;
	constexpr float AedirSwingMidHeightCm = 28.f;
	constexpr float AedirSwingEndHeightCm = 44.f;
	constexpr float AedirSwingAcrossForwardCm = 12.f;
	constexpr float AedirSwingThrowCm = 12.f;
	constexpr float AedirWeaponLowerHandPerDegree = 0.42f;
	// The second half opens the blade outwards and takes it round behind the body, with the hand up beside the shoulder.
	constexpr float AedirWeaponBackYaw = 165.f;
	constexpr float AedirWeaponBackPitch = 18.f;
	// On its way it stands up over the hand. Through the level it would cross the line of the forearm and the wrist would turn over.
	constexpr float AedirWeaponOpenOverPitch = 40.f;
	constexpr float AedirWeaponOpenOverAcrossPitch = 66.f;
	constexpr float AedirWeaponOpenMaxPitch = 86.f;
	constexpr float AedirWeaponMaxTurnDegrees = 2400.f;
	constexpr float AedirWeaponMaxTurnMassShare = 0.08f;
	constexpr float AedirWeaponLowerPitchShare = 0.9f;
	constexpr float AedirWeaponSpineShare = 0.5f;
	constexpr float AedirWeaponSpineMaxDegrees = 40.f;
	// A punch drives the shoulder of its arm forward.
	constexpr float AedirPunchSpineDegrees = 50.f;
	constexpr float AedirPunchSpineAimShare = 0.3f;
	constexpr float AedirPunchSpineMaxDegrees = 62.f;
	constexpr float AedirPunchLeanDegrees = 11.f;
	// Fists aimed further down than this (the view and the arm together) bend the trunk over, all of this much once aimed this
	// much further down.
	// Thrown hard, this share of the mouse's movement no longer moves the fist's aim.
	constexpr float AedirPunchAimHold = 0.85f;
	constexpr float AedirPunchBendFromPitch = 25.f;
	constexpr float AedirPunchBendSpanPitch = 40.f;
	constexpr float AedirPunchBendDownDegrees = 45.f;
	// Raising the mouse takes the hands just over the head with the elbows bent, the blade straight back behind them along the middle of the body.
	constexpr float AedirWeaponRaiseFullDegrees = 80.f;
	// Below this aim up the elbows stay down by the body.
	constexpr float AedirElbowRaiseFromDegrees = 30.f;
	constexpr float AedirWeaponRaisedForwardCm = 20.f;
	constexpr float AedirWeaponRaisedSideCm = 8.f;
	constexpr float AedirWeaponRaisedSideTwoHandCm = 4.f;
	constexpr float AedirWeaponRaisedHeightCm = 88.f;
	constexpr float AedirWeaponRaisedBack = 0.96f;
	constexpr float AedirWeaponRaisedDown = 0.28f;
	constexpr float AedirWeaponRaisedYawShare = 0.3f;
	// A weapon carried in the loose arm hangs from the hand. A long one leans forward so its point stays off the ground.
	constexpr float AedirCarryHangPitch = 30.f;
	// On the move the carried blade leans this much towards the line of the forearm, fading in between these speeds.
	constexpr float AedirCarryForearmShare = 1.5f;
	// Rest poses out of the fight stance: how far below level and how far out to the carrying hand's side the point lies.
	constexpr float AedirRestOneHandPitch = -62.f;
	constexpr float AedirRestOneHandOutYaw = 20.f;
	// In both hands, as in Half Sword, the hands rest before the belly and the blade goes down and forward out to the carrying
	// hand's side. Asked no further round than that: asked round towards the back, the blade lay back along the forearm,
	// where the wrist has to choose a side, and it turned over in the fist.
	// The blade lies low out to the right before the hips, the hands a little to the left of the middle (still 17); further
	// round, or the hands lower or more to the right, and the second hand no longer reaches its place on the grip.
	constexpr float AedirRestTwoHandPitch = -25.f;
	constexpr float AedirRestTwoHandOutYaw = 45.f;
	// Measured in the game: asked this much further, the blade comes to rest at the angles above.
	constexpr float AedirRestTwoHandFixPitch = 0.f;
	constexpr float AedirRestTwoHandFixYaw = 0.f;
	constexpr float AedirRestPolePitch = 72.f;
	constexpr float AedirCarryPoleWalkingPitch = -5.f;
	constexpr float AedirCarryPoleWalkingInYaw = 10.f;
	// From this walking speed the pole is brought down level, all of it at a walk.
	constexpr float AedirCarryPoleWalkFromSpeed = 20.f;
	constexpr float AedirCarryPoleWalkFullSpeed = 100.f;
	constexpr float AedirCarryHaftedMaxCm = 100.f;
	constexpr float AedirCarryHaftedPitch = 0.f;
	constexpr float AedirCarryHaftedInYaw = 80.f;
	constexpr float AedirRestPoleOutYaw = 10.f;
	// The hand at rest is about this high over the ground, which keeps a long blade's point off it.
	constexpr float AedirRestHandHeightCm = 78.f;
	// Where the carrying hand of a weapon held in both hands rests out of the stance, from the middle of the shoulders: low
	// before the hips. A pole is held out to the side and higher, upright.
	constexpr float AedirRestTwoHandForwardCm = 24.f;
	constexpr float AedirRestTwoHandSideCm = -10.f;
	constexpr float AedirRestTwoHandHeightCm = 6.f;
	constexpr float AedirRestPoleSideCm = 14.f;
	constexpr float AedirRestPoleRaiseCm = 8.f;
	constexpr float AedirCarryAimFadeFromSpeed = 60.f;
	constexpr float AedirCarryAimFadeFullSpeed = 250.f;
	constexpr float AedirCarryHandHeightCm = 72.f;
	// A two-handed weapon in one hand: the arm has to bear all of it, far from the hand.
	constexpr float AedirOneHandedStrainMass = 3.4f;
	constexpr float AedirOneHandedDroopCm = 8.f;
	constexpr float AedirOneHandedDroopPitch = 30.f;
	// Held items stop at what they hit. Only what is light enough gives way.
	constexpr float AedirBladeGuardCm = 10.f;
	constexpr float AedirBladeRadiusCm = 2.5f;
	constexpr int32 AedirBladeSweepTries = 4;
	// The middle of the upper fist of a two-handed hold lies this far below the guard.
	constexpr float AedirUpperHandBelowGuardCm = 5.5f;
	// Two fists on a haft: the upper one this far above the lower.
	constexpr float AedirTwoHandHaftSpanCm = 18.f;
	constexpr float AedirPoleHandsApartCm = 32.f;
	// A weapon at least this long is held in both hands by a player whose other hand is free; shorter ones (knives, daggers,
	// tools) in one.
	constexpr float AedirTwoHandMinLengthCm = 50.f;
	// The side of the weapon is read this far along the blade from the hands, and the leading foot changes over once that
	// point is this far across the middle.
	constexpr float AedirLeadFootBladeReachCm = 30.f;
	constexpr float AedirLeadFootSwitchCm = 10.f;
	constexpr float AedirBladeBlockMassKg = 40.f;
	constexpr float AedirBladePushMaxSpeed = 500.f;
	// A clash of weapons: the fastest a blade's tip counts, and how much of the push the struck hand takes.
	constexpr float AedirClashMaxSpeed = 1500.f;
	constexpr float AedirClashHandShare = 0.6f;

	// The mouse moves the hand: around its own shoulder (two hands on one grip around the middle of the chest), the mouse's
	// side to side turning it around the body and its up and down raising it from low to over the head.
	static TAutoConsoleVariable<int32> CVarAedirHandLedSwing(TEXT("aedir.HandLedSwing"), 1, TEXT("The mouse moves the hand and the blade follows it by its weight (1), or swings go through set poses (0)."));
	// The whole of the mouse's travel moves the hand over the room the arm really has, in front of the body: no part of
	// the travel is spent pressing against a limit.
	constexpr float AedirHandLedYawShare = 1.f;
	constexpr float AedirHandLedUpShare = 0.6f;
	constexpr float AedirHandLedDownShare = 0.95f;
	constexpr float AedirHandLedInwardYaw = 12.f;
	constexpr float AedirHandLedRestPitch = 18.f;
	constexpr float AedirHandLedPitchMin = -50.f;
	constexpr float AedirHandLedPitchMax = 82.f;
	constexpr float AedirHandLedNearCm = 32.f;
	constexpr float AedirHandLedFarCm = 50.f;
	constexpr float AedirHandLedBehindShorterCm = 12.f;
	constexpr float AedirHandLedBehindFromYaw = 60.f;
	constexpr float AedirHandLedBehindSpanYaw = 45.f;
	// How far round a hand can be held: out past its own shoulder, and across in front of the body to the far one. The
	// turning chest takes the shoulder along, so the hand ends up beside and behind the head.
	constexpr float AedirHandLedOutwardYaw = 105.f;
	constexpr float AedirHandLedAcrossYaw = 80.f;
	constexpr float AedirHandLedTwoHandSideCm = 6.f;
	constexpr float AedirHandLedTwoHandOutwardYaw = 85.f;
	constexpr float AedirHandLedTwoHandAcrossYaw = 50.f;
	constexpr float AedirHandLedTwoHandLowAcrossYaw = 30.f;
	constexpr float AedirHandLedRaisedAcrossYaw = 45.f;
	constexpr float AedirHandLedRaisedFromPitch = 30.f;
	constexpr float AedirHandLedRaisedFullPitch = 55.f;	// Out to its own side a hand held this low goes round no further than this; from the low to the high height the full way.
	constexpr float AedirHandLedLowOutwardYaw = 55.f;
	constexpr float AedirHandLedLowPitch = -25.f;
	constexpr float AedirHandLedHighPitch = 20.f;
	// The point of a blade held still is kept this far above the ground under the feet.
	constexpr float AedirBladeAboveGroundCm = 12.f;
	// The blade stands out of the fist this far above the line of the arm; weight and tiredness let it sink.
	constexpr float AedirHandLedBladeRaise = 50.f;
	// Drawn back for a blow the wrist cocks and the blade lies back over the shoulder, behind the head when raised, or
	// trailing low behind the hip; brought forward the wrist opens and the blade goes on the line of the arm. The cock grows
	// from these angles of the hand round the body and up.
	constexpr float AedirCockFromYaw = 25.f;
	constexpr float AedirCockFullYaw = 85.f;
	constexpr float AedirCockFromPitch = 35.f;
	constexpr float AedirCockFullPitch = 78.f;
	// How high the cocked blade points behind: low from a low hand, up over the shoulder from one at the shoulder, down
	// behind the head from one over it.
	constexpr float AedirCockLowElevation = -30.f;
	constexpr float AedirCockShoulderElevation = 42.f;
	constexpr float AedirCockOverheadElevation = -32.f;
	constexpr float AedirCockLowPitch = -45.f;
	constexpr float AedirCockShoulderPitch = 10.f;
	constexpr float AedirCockOverheadFromPitch = 40.f;
	// The cocked blade points this much out to the side the hand is on, as well as back.
	constexpr float AedirCockSideShare = 0.45f;	// Within this much of the middle of its travel the mouse keeps the point on the enemy, from that much out it is the
	// swing; the point then stands this far over level and leans this share of the mouse's way.
	constexpr float AedirPointOnEnemyToDeg = 18.f;
	constexpr float AedirPointOnEnemyFromDeg = 42.f;
	constexpr float AedirPointOnEnemyPitch = 22.f;
	constexpr float AedirPointOnEnemyLeanShare = 0.45f;
	static TAutoConsoleVariable<int32> CVarAedirArmLedWithHand(TEXT("aedir.ArmLedWithHand"), 1, TEXT("The arm also turns about the blade when the mouse leads the hand (1)."));
	static TAutoConsoleVariable<int32> CVarAedirPointZone(TEXT("aedir.PointZone"), 1, TEXT("Near the middle of the mouse's travel the point stays on the enemy (1)."));
	static TAutoConsoleVariable<int32> CVarAedirSwingCock(TEXT("aedir.SwingCock"), 1, TEXT("Drawn back, the wrist cocks the blade back over the shoulder (1), or the blade always stands over the arm (0)."));
	// Between these heights of the arm's line, "above the arm" turns from straight up to back over the head.
	constexpr float AedirHandLedUprightFromZ = 0.5f;
	constexpr float AedirHandLedUprightFullZ = 0.95f;
	// Blades only resting on each other are not worth a line in the log.
	constexpr float AedirClashLogSpeed = 200.f;
	constexpr float AedirBladePushShare = 0.15f;
	constexpr float AedirBladeMaxPullBackCm = 30.f;
	constexpr int32 AedirBladeBackOffSteps = 4;
	// A big object in both hands.
	constexpr float AedirObjectChestHeightCm = 34.f;
	constexpr float AedirObjectRestPitch = -15.f;
	constexpr float AedirObjectMaxPitch = 70.f;
	constexpr float AedirObjectBodyGapCm = 8.f;
	constexpr float AedirObjectThrowGapCm = 26.f;
	constexpr float AedirObjectFistAlpha = 0.3f;
	constexpr float AedirObjectMouseShare = 0.8f;
	constexpr float AedirObjectMouseWeightKg = 40.f;
	constexpr float AedirObjectYawLimit = 70.f;
	constexpr float AedirObjectPitchMin = -25.f;
	constexpr float AedirObjectPitchMax = 100.f;
	constexpr float AedirPropMaxSpeedChange = 220.f;
	constexpr float AedirWeaponTurnMassShare = 0.3f;
	// Two hands hold the weapon low in front of the groin.
	const FVector AedirTwoHandRest(31.f, 0.f, 2.f);
	const FVector AedirTwoHandFight(40.f, 0.f, 21.f);
	// A polearm turns around a point between the hands. The lower hand stays beside the waist, the shaft levers around it and the whole chest turns along.
	constexpr float AedirPolearmPivotForwardCm = 26.f;
	constexpr float AedirPolearmPivotSideCm = 17.f;
	constexpr float AedirPolearmPivotHeightCm = 16.f;
	constexpr float AedirPolearmPivotFightHeightCm = 20.f;
	constexpr float AedirPolearmPivotYaw = 62.f;
	constexpr float AedirPolearmPivotSwingCm = 23.f;
	constexpr float AedirPolearmPivotThrowCm = 10.f;
	constexpr float AedirPolearmPivotLowerPerDegree = 0.22f;
	constexpr float AedirPolearmSwingYaw = 135.f;
	constexpr float AedirPolearmGuardPitch = -8.f;
	constexpr float AedirPolearmFightPitch = 22.f;
	constexpr float AedirPolearmSwingPitch = 14.f;
	constexpr float AedirPolearmLowerPitchShare = 0.7f;
	constexpr float AedirPolearmInwardYaw = 14.f;
	constexpr float AedirPolearmSpineDegrees = 30.f;
	constexpr float AedirPolearmSpineShare = 0.6f;
	constexpr float AedirPolearmSpineMaxDegrees = 58.f;
	constexpr float AedirPolearmThrustHandCm = 30.f;
	// Mouse up lifts it over the head: the head of the weapon goes back past the vertical, the upper hand above the forehead and the lower one in front of the chest.
	constexpr float AedirPolearmRaisedPitch = 114.f;
	constexpr float AedirPolearmRaisedYawShare = 0.25f;
	constexpr float AedirPolearmRaisedForwardCm = 17.f;
	constexpr float AedirPolearmRaisedHeightCm = 58.f;
	// Half-swording: the weapon lies across the chest with a hand at each end, and the mouse swings both arms around the middle between them.
	constexpr float AedirHalfSwordCenterForwardCm = 43.f;
	// The middle of the weapon travels a wide flat arc around the body along with the blade, so both arms sweep and the wrists keep still.
	constexpr float AedirHalfSwordArcShare = 0.65f;
	constexpr float AedirHalfSwordArcEndCm = 42.f;
	// However it turns, the hand on the handle stays this clear of the body.
	constexpr float AedirHalfSwordBodyClearCm = 34.f;
	constexpr float AedirHalfSwordBodyClearSoftCm = 8.f;
	constexpr float AedirHalfSwordWristLoose = 0.7f;
	constexpr float AedirHalfSwordRaisedForwardCm = 16.f;
	constexpr float AedirHalfSwordCenterHeightCm = 30.f;
	constexpr float AedirHalfSwordRaisePerDegree = 0.85f;
	constexpr float AedirHalfSwordLowerPerDegree = 0.35f;
	constexpr float AedirHalfSwordRaisedPitch = 70.f;
	constexpr float AedirHalfSwordSwingShare = 1.15f;
	constexpr float AedirHalfSwordSpineShare = 0.55f;
	constexpr float AedirHalfSwordThrustCm = 30.f;
	constexpr float AedirHalfSwordRestForward = 8.f;
	constexpr float AedirHalfSwordForwardPerDownDegree = 0.9f;
	constexpr float AedirHalfSwordMinForward = -105.f;
	constexpr float AedirHalfSwordMaxForward = 118.f;
	constexpr float AedirHalfSwordRestRise = 4.f;
	constexpr float AedirHalfSwordRisePerForward = 0.22f;
	constexpr float AedirHalfSwordRisePerBack = 0.15f;
	constexpr float AedirHalfSwordRaisedRise = 15.f;
	constexpr float AedirHalfSwordYawLimit = 100.f;
	constexpr float AedirHalfSwordPitchMin = -45.f;
	constexpr float AedirHalfSwordPitchMax = 75.f;
	// The hands lie well apart on the weapon, a long one more so, as far as both arms still reach.
	constexpr float AedirHalfSwordHandsApartShare = 0.6f;
	constexpr float AedirHalfSwordHandsApartMinCm = 42.f;
	constexpr float AedirHalfSwordHandsApartMaxCm = 58.f;
	constexpr float AedirHalfSwordMinLengthCm = 45.f;
	constexpr float AedirHalfSwordSlideSeconds = 0.32f;
	constexpr float AedirHalfSwordHopCm = 8.f;
	constexpr float AedirSecondaryGripBlendSeconds = 0.3f;
	constexpr float AedirGripRestInterpSpeed = 3.f;
	// Thrust mode.
	// A thrust goes all the way out for about 400 pixels of the mouse pushed forward.
	constexpr float AedirThrustMouseExtension = 0.035f;
	constexpr float AedirThrustSteerShare = 0.25f;
	constexpr float AedirThrustHandMaxCm = 56.f;
	// A drawn-back thrust waits with the hands up by the shoulder on the weapon's side, the point forward on the enemy.
	constexpr float AedirThrustWaistForwardCm = 24.f;
	constexpr float AedirThrustWaistSideCm = 20.f;
	constexpr float AedirThrustWaistSideTwoHandCm = 14.f;
	constexpr float AedirThrustWaistUpCm = 26.f;
	// It goes out a shoulder's width from the middle with the point converging on the middle ahead, the elbow pointing back;
	// held in both hands, nearly from the middle with the arms straight out.
	constexpr float AedirThrustSideCm = 24.f;
	constexpr float AedirThrustSideTwoHandCm = 8.f;
	// The view looks down on the body, and the weight of the blade takes its point lower still: the thrust is asked this much above where the view looks, so it goes level at a man ahead.
	constexpr float AedirThrustViewPitchOffset = 40.f;
	constexpr float AedirThrustPitchMin = -45.f;
	constexpr float AedirThrustPitchMax = 35.f;
	constexpr float AedirThrustChestHeightCm = 30.f;
	constexpr float AedirThrustInwardYaw = 8.f;
	constexpr float AedirArmGuardRestYaw = 12.f;
	constexpr float AedirArmGuardRestPitch = 15.f;
	constexpr float AedirArmExtensionStep = 0.08f;
	constexpr int32 AedirArmorZoneCount = 4;
	constexpr int32 AedirLimbCount = 4;
	constexpr float AedirArmorMaxProtection = 0.9f;

	// A graze does little of what a weapon can do; the full damage takes a blow with this much energy behind it.
	constexpr float AedirImpactFullFlatJoules = 60.f;
	constexpr float AedirImpactMaxHealthShare = 0.25f;
	constexpr float AedirImpactMaxHealthShareHead = 0.4f;
	constexpr float AedirAIRestStaminaShare = 0.15f;
	constexpr float AedirAIFleeCm = 1500.f;
	constexpr float AedirImpactMaxSpeed = 5000.f;
	// The fastest a blow counts: a strong man's sword point at the end of a full swing.
	constexpr float AedirImpactCountedMaxSpeed = 3000.f;
	// Room the own body takes up, which a held blade keeps out of.
	constexpr float AedirOwnTrunkRadiusCm = 17.f;
	constexpr float AedirOwnHeadRadiusCm = 13.f;
	constexpr float AedirOwnThighRadiusCm = 9.f;
	constexpr float AedirOwnBladeGuardCm = 12.f;
	// Where the blade is wanted stays this much further clear of the own body than the body itself.
	constexpr float AedirOwnClearanceCm = 4.f;
	// How a blow that does not get through comes off the body, and how long a clean cut may carry on through.
	constexpr float AedirBluntBounce = 0.2f;
	constexpr double AedirPassThroughSeconds = 0.35;
	// How fast a caught blade has to have been going to tear itself out of the hand.
	constexpr float AedirWrenchSpeedBase = 2600.f;
	constexpr float AedirWrenchSpeedPerStrength = 12.f;
	constexpr float AedirWrenchTwoHandShare = 1.6f;
	// An edge needs this much energy, before armour, to go right through each part of the body. A blow well short of it
	// glances off, one in between bites in and sticks, and anything past it carries on.
	constexpr float AedirCutHeadJoules = 170.f;
	constexpr float AedirCutTorsoJoules = 180.f;
	constexpr float AedirCutArmJoules = 65.f;
	constexpr float AedirCutLegJoules = 100.f;
	constexpr float AedirCutGlanceShare = 0.3f;
	constexpr float AedirCutLuck = 0.2f;
	constexpr float AedirGlanceBounce = 0.35f;
	constexpr float AedirGlanceDamageShare = 0.4f;
	constexpr float AedirCutThroughKeep = 0.7f;
	// A stuck blade stays in until the hand pulls this far from it, more for every cm it went in, or until this long has
	// passed; torn out it opens the wound further.
	constexpr float AedirLodgedHoldSeconds = 4.f;
	constexpr float AedirLodgedTearCm = 14.f;
	constexpr double AedirLodgedTearPerDepth = 0.5;
	constexpr float AedirLodgedTearJoules = 30.f;
	constexpr float AedirLodgedTearJoulesPerCm = 4.f;
	// Pulled on, past a little give, it drags the body it is in and costs it its balance, at most this much each second.
	constexpr double AedirLodgedPullFreeCm = 4.0;
	constexpr float AedirLodgedDragImpulsePerSecond = 9000.f;
	constexpr float AedirLodgedBalancePerSecond = 1.2f;
	// How deep a point goes in: a bite at least, at most this share of the weapon, reached by a thrust this many times
	// what it takes to go through.
	constexpr float AedirLodgeMinDepthCm = 4.f;
	constexpr float AedirLodgeMaxDepthShare = 0.5f;
	constexpr float AedirLodgeDeepShare = 2.f;
	// A point driven in along the blade, or an axe's bit chopped this deep into its cut, stays in; any other edge drags on through.
	constexpr float AedirLodgeThrustAlignment = 0.75f;
	// What a blow to the head takes of the senses: nothing below a tap, then a point for every few joules that get through, so
	// a hard bare-headed blow takes most of them and a second one knocks a man down senseless.
	constexpr float AedirSensesFreeJoules = 12.f;
	constexpr float AedirSensesPerJoule = 0.32f;
	// A dent shows on plate from a blow this hard; a helmet is knocked off by one harder than this, the more likely the harder,
	// up to this chance, and flies off at this speed and lift.
	// What gets through to the bone breaks an arm from this many joules and a leg from this many, for a body of middling
	// strength; a blow on a hand, a foot or a collarbone counts this share of itself.
	constexpr float AedirBreakArmJoules = 210.f;
	constexpr float AedirBreakLegJoules = 290.f;
	constexpr float AedirBreakEndShare = 0.5f;
	constexpr float AedirDentShowJoules = 40.f;
	constexpr float AedirHelmetOffFromJoules = 100.f;
	constexpr float AedirHelmetOffSpreadJoules = 280.f;
	constexpr float AedirHelmetOffMaxChance = 0.6f;
	constexpr float AedirHelmetOffSpeed = 250.f;
	constexpr float AedirHelmetOffLift = 150.f;
	constexpr float AedirDentDepthCm = 4.f;
	constexpr float AedirDentRadiusMinCm = 2.5f;
	constexpr float AedirDentRadiusMaxCm = 6.f;
	constexpr float AedirDentJoulesPerCm = 120.f;
	constexpr float AedirLodgeChopShare = 0.6f;
	constexpr double AedirLodgeHaftedBoxY = 0.035;
	constexpr float AedirCutBiteKeep = 0.4f;
	// Below this many joules a blow does no harm at all.
	constexpr float AedirImpactNoHarmJoules = 6.f;
	// Most of a blow goes into the wound. This share of it is what pushes the body off its feet.
	constexpr float AedirImpactBodyEnergyShare = 0.45f;
	// A weapon no button holds up any more strikes only while it is still really flying.
	constexpr double AedirLooseStrikeMinSpeed = 400.0;
	// A blow this hard is heard at full; a harder edged one also thumps the body it lands on.
	constexpr float AedirSfxFullJoules = 250.f;
	constexpr float AedirSfxHeavyJoules = 300.f;
	// A clash or a knock on the world is heard from this speed on, at full from this one.
	constexpr float AedirSfxHitMinSpeed = 250.f;
	constexpr float AedirSfxHitFullSpeed = 1500.f;
	// The point of a swing hissing through the air: from this speed, at most once in this long.
	constexpr float AedirSfxWhooshSpeed = 1100.f;
	constexpr float AedirSfxWhooshFullSpeed = 2600.f;
	constexpr double AedirSfxWhooshSeconds = 0.3;
	constexpr float AedirImpactInjuryPerDamage = 0.012f;
	constexpr float AedirImpactLimbHealPerSecond = 0.002f;
	constexpr float AedirImpactRecoilScale = 0.35f;
	constexpr float AedirImpactStaminaPerEnergy = 0.01f;
	// However hard the blow, landing it costs no more breath than a few swings.
	constexpr float AedirImpactStaminaMax = 6.f;
	constexpr float AedirImpactPropImpulseScale = 0.6f;

	constexpr float AedirClashCacheInterval = 0.1f;
	constexpr float AedirClashSelfReflection = -0.3f;
	constexpr float AedirClashOtherReflection = 0.1f;
	constexpr float AedirClashBalanceScale = 0.1f;
	constexpr float AedirClashEnergyShare = 0.5f;

	constexpr float AedirShoveStrikeSeconds = 0.12f;
	constexpr float AedirShoveDuration = 0.35f;
	constexpr float AedirShoveReachCm = 70.f;
	constexpr float AedirShoveConeCos = 0.5f;
	constexpr float AedirShoveRecoilBalance = 0.06f;
	constexpr float AedirShoveStiffnessScale = 2.f;
	constexpr float AedirShoveHeavyItemKg = 6.f;
	constexpr float AedirShoveArmInjuryLimit = 0.6f;
	constexpr float AedirShoveBaseJoules = 20.f;
	constexpr float AedirShoveStrengthJoules = 1.8f;
	constexpr float AedirShoveMinStaminaScale = 0.5f;

	// A slow chamber (knee up to the belly), then a snapping extension.
	constexpr float AedirKickDuration = 1.2f;
	constexpr float AedirKickStrikeStart = 0.56f;
	constexpr float AedirKickStrikeEnd = 0.8f;
	constexpr float AedirKickHitRadius = 8.f;
	constexpr float AedirKickMassKg = 10.f;
	constexpr float AedirKickEnergyScale = 1.f;
	constexpr float AedirKickMaxFootSpeed = 1200.f;
	constexpr float AedirKickRecoilBalance = 0.04f;
	constexpr float AedirKickMinBalance = 0.4f;
	constexpr float AedirKickPoseRiseSeconds = 0.3f;
	constexpr float AedirKickPoseFallSeconds = 0.35f;
	constexpr float AedirKickChamberTime = 0.5f;
	constexpr float AedirKickExtendTime = 0.62f;
	constexpr float AedirKickChamberAheadCm = 20.f;
	constexpr float AedirKickChamberRightCm = 6.f;
	constexpr float AedirKickChamberBelowHipCm = -40.f;
	constexpr float AedirKickExtendCm = 85.f;
	constexpr float AedirKickMaxYawDegrees = 70.f;
	constexpr float AedirKickMinLiftCm = -15.f;
	constexpr float AedirKickMaxLiftCm = 55.f;
	constexpr float AedirKickSoleStartTime = 0.2f;
	constexpr float AedirKickSoleFullTime = 0.5f;
	constexpr float AedirKickSoleEndTime = 0.9f;
	constexpr float AedirKickGroundBelowHipCm = 90.f;
	constexpr float AedirKickExtendHeightCm = 55.f;

	// A fight starts some way off: first the stance, then closing to the length of the arm and the weapon, then the blow.
	constexpr float AedirAIArmReachCm = 62.f;
	constexpr float AedirAIWeaponReachShare = 0.85f;
	constexpr float AedirAIStanceExtraCm = 280.f;
	constexpr float AedirAICloseInShare = 0.9f;
	// Armed, it keeps the length of its weapon: closer than this share of its reach it steps back, before the bodies touch.
	constexpr float AedirAIBackOffShare = 0.75f;
	constexpr float AedirAIBackOffInput = 0.85f;
	// Room kept between the bodies, armed and bare-handed, and how wide the band is in which it neither closes nor gives way.
	constexpr float AedirAIArmedRoomCm = 30.f;
	constexpr float AedirAIUnarmedRoomCm = 8.f;
	constexpr float AedirAIKeepBandCm = 15.f;
	// A bare fist: how long a hook or an uppercut is drawn, a jab barely; how long the throw lasts and the fist comes back.
	constexpr float AedirAIFistWindupSeconds = 0.22f;
	constexpr float AedirAIJabWindupSeconds = 0.05f;
	constexpr float AedirAIFistThrowSeconds = 0.22f;
	constexpr float AedirAIFistRecoverSeconds = 0.3f;
	// How much of the rise or fall from the shoulder to the part aimed at a weapon blow follows.
	constexpr float AedirAIStrikeRiseShare = 0.8f;
	constexpr float AedirAIRunBeyondCm = 500.f;
	constexpr float AedirAIWindupSeconds = 0.4f;
	constexpr float AedirAIStrikeSeconds = 0.35f;
	constexpr float AedirAIRecoverSeconds = 0.35f;
	// One on the ground is struck down at this steeply, from as close as this, bent over it.
	constexpr float AedirAIGroundStrikePitch = -70.f;
	constexpr float AedirAIStoopCm = 200.f;
	constexpr float AedirAIGrudgeSeconds = 25.f;
	constexpr float AedirAIContactRange = 110.f;
	constexpr float AedirAIShoveChance = 0.12f;
	constexpr float AedirAIKickChance = 0.08f;
	// Hearing: the noise of a body standing still, how much each cm/s of speed adds, crouching, a fight, and how much of it
	// gets through a wall.
	constexpr float AedirAIHearBaseCm = 250.f;
	constexpr float AedirAIHearPerSpeed = 3.5f;
	constexpr float AedirAIHearCrouchShare = 0.4f;
	constexpr float AedirAIHearFightCm = 600.f;
	constexpr float AedirAIHearMuffledShare = 0.55f;

	const FName AedirKickFootLeftBone(TEXT("foot_l"));
	const FName AedirKickFootRightBone(TEXT("foot_r"));
	const FName AedirKickPelvisBone(TEXT("pelvis"));

	float CombatSmoothStep01(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	// How far the mouse has raised a weapon. Half of it runs straight, so the arm never stands still on its way up or down.
	float CombatRaiseShare(float AimPitch, float FullDegrees)
	{
		const float Linear = FMath::Clamp(AimPitch / FullDegrees, 0.f, 1.f);
		return (Linear + CombatSmoothStep01(Linear)) * 0.5f;
	}

	// A swing runs through its poses without slowing at any of them. Keys lie a quarter of the mouse travel apart from one end to the other.
	constexpr int32 CombatSwingKeysPerSide = 4;

	template <typename ValueType, typename KeyFunction>
	ValueType CombatSwingSpline(float Travel, const KeyFunction& Key)
	{
		const float Position = (FMath::Clamp(Travel, -1.f, 1.f) + 1.f) * CombatSwingKeysPerSide;
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Position), 0, CombatSwingKeysPerSide * 2 - 1);
		const float T = Position - Index;
		const auto KeyAt = [&Key](int32 At) { return Key(FMath::Clamp(static_cast<float>(At) / CombatSwingKeysPerSide - 1.f, -1.f, 1.f)); };
		const ValueType P0 = KeyAt(Index - 1);
		const ValueType P1 = KeyAt(Index);
		const ValueType P2 = KeyAt(Index + 1);
		const ValueType P3 = KeyAt(Index + 2);
		return ((P1 * 2.f) + (P2 - P0) * T + (P0 * 2.f - P1 * 5.f + P2 * 4.f - P3) * (T * T) + (P1 * 3.f - P0 - P2 * 3.f + P3) * (T * T * T)) * 0.5f;
	}

	FVector QuadraticBezier(const FVector& A, const FVector& Control, const FVector& B, float T)
	{
		const float Inverse = 1.f - T;
		return A * (Inverse * Inverse) + Control * (2.f * Inverse * T) + B * (T * T);
	}

	// A weapon made of wood: a club, a staff. The rest of what is held to strike with is steel.
	bool CombatIsWooden(const AAedirItemActor* Item)
	{
		if (!Item)
		{
			return false;
		}
		const FString Name = Item->ItemName.ToString();
		return Name.Contains(TEXT("Club")) || Name.Contains(TEXT("Staff")) || Name.Contains(TEXT("Cudgel")) || Name.Contains(TEXT("Stick"));
	}

	bool CombatBoneNameContainsAny(const FString& BoneName, const TCHAR* const* Tokens, int32 TokenCount)
	{
		for (int32 Index = 0; Index < TokenCount; ++Index)
		{
			if (BoneName.Contains(Tokens[Index], ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

UAedirCombatComponent::UAedirCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);

	ArmorProtection.Init(0.f, AedirArmorZoneCount);
	LimbInjury.Init(0.f, AedirLimbCount);
}

void UAedirCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	Creature = Cast<AAedirCreature>(GetOwner());
}

void UAedirCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Creature)
	{
		return;
	}

	const bool bAuthority = IsAuthoritative();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		GuardAlpha[Index] = FMath::FInterpConstantTo(GuardAlpha[Index], IsGuardActive(Hand) ? 1.f : 0.f, DeltaTime, AedirGuardBlendSpeed);
		SwingCooldownRemaining[Index] = FMath::Max(0.f, SwingCooldownRemaining[Index] - DeltaTime);
	}

	ShoveCooldownRemaining = FMath::Max(0.f, ShoveCooldownRemaining - DeltaTime);
	KickCooldownRemaining = FMath::Max(0.f, KickCooldownRemaining - DeltaTime);

	UpdateSwings(DeltaTime);

	if (bAuthority || IsAutonomousProxy())
	{
		UpdateTwoHandGrip();
		UpdateShove(DeltaTime);
	}

	UpdateGrip(DeltaTime);

	UpdateKick(DeltaTime);

	if (bAuthority && !Creature->IsPlayerControlled())
	{
		UpdateAI(DeltaTime);
	}

	UpdateArms(DeltaTime);
	UpdateLeadFoot();
	UpdateObjectCarry();
	UpdateHeldWeight(DeltaTime);
	UpdateGripJoint(DeltaTime);

	if (bAuthority)
	{
		ClashedThisTick.Reset();
		UpdateClashes(DeltaTime);
		HealLimbInjuries(DeltaTime);

		for (int32 Index = 0; Index < 2; ++Index)
		{
			UpdateImpacts(static_cast<EAedirHand>(Index), Arms[Index], DeltaTime);
		}

		for (int32 Index = 0; Index < 2; ++Index)
		{
			PublishArmState(static_cast<EAedirHand>(Index), Arms[Index]);
		}
	}
	else if (IsAutonomousProxy())
	{
		SendArmAimToServer(DeltaTime);
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAedirArmDynamics& Arm = Arms[Index];
		Arm.PrevHandWorld = Arm.HandWorld;
		Arm.PrevTipWorld = ComputeArmTip(static_cast<EAedirHand>(Index), Arm);
	}
}

void UAedirCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirCombatComponent, bLeftGuard);
	DOREPLIFETIME(UAedirCombatComponent, bOffHandFree);
	DOREPLIFETIME(UAedirCombatComponent, bHalfSwordToggled);
	DOREPLIFETIME(UAedirCombatComponent, bRightGuard);
	DOREPLIFETIME(UAedirCombatComponent, LeftSwing);
	DOREPLIFETIME(UAedirCombatComponent, RightSwing);
	DOREPLIFETIME(UAedirCombatComponent, LeftArmNet);
	DOREPLIFETIME(UAedirCombatComponent, RightArmNet);
	DOREPLIFETIME(UAedirCombatComponent, bTwoHandGrip);
	DOREPLIFETIME(UAedirCombatComponent, KickNet);
	DOREPLIFETIME(UAedirCombatComponent, ArmorProtection);
	DOREPLIFETIME(UAedirCombatComponent, LimbInjury);
}

void UAedirCombatComponent::SetGuard(EAedirHand Hand, bool bActive)
{
	if (!Creature)
	{
		return;
	}
	// A broken or lost arm raises no guard.
	if (bActive && Creature->Wounds && Creature->Wounds->HasUselessArm(Hand == EAedirHand::Left))
	{
		return;
	}

	if (IsAuthoritative())
	{
		SetGuardInternal(Hand, bActive);
	}
	else if (Creature->IsLocallyControlled())
	{
		SetGuardInternal(Hand, bActive);
		ServerSetGuard(Hand, bActive);
	}
}

void UAedirCombatComponent::AddSwingInput(EAedirHand Hand, FVector2D MouseDeltaThisFrame, float DeltaTime)
{
	if (!Creature || !Creature->IsLocallyControlled() || DeltaTime <= 0.f || !IsGuardActive(Hand))
	{
		return;
	}

	// The second hand of a two-hand grip is placed by the grip, so its button steers the primary hand unless that has its own.
	if (IsSecondaryGripHand(Hand))
	{
		const EAedirHand PrimaryHand = GetGripHand();
		if (IsGuardActive(PrimaryHand))
		{
			return;
		}
		Hand = PrimaryHand;
	}

	FAedirArmDynamics& Arm = GetArm(Hand);
	if (Arm.bScriptedControl)
	{
		return;
	}

	if (IsObjectGrip())
	{
		const AAedirItemActor* Object = GetHeldItem(Hand);
		const float MouseShare = AedirObjectMouseShare / (1.f + (Object ? Object->Weight : 0.f) / AedirObjectMouseWeightKg);
		const float ObjectThrow = FMath::Clamp((static_cast<float>(MouseDeltaThisFrame.Size()) / DeltaTime - AedirArmGuardSpeedLow) / (AedirArmGuardSpeedHigh - AedirArmGuardSpeedLow), 0.f, 1.f);
		Arm.PunchBlend = FMath::FInterpTo(Arm.PunchBlend, ObjectThrow, DeltaTime, ObjectThrow > Arm.PunchBlend ? AedirArmPunchRiseSpeed : AedirArmPunchFallSpeed);
		Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X + MouseDeltaThisFrame.X * AimSensitivity * MouseShare, -AedirObjectYawLimit, AedirObjectYawLimit);
		Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity * MouseShare, AedirObjectPitchMin, AedirObjectPitchMax);
		return;
	}

	if (IsWeaponStance(Hand))
	{
		const float WeaponMouseSpeed = static_cast<float>(MouseDeltaThisFrame.Size()) / DeltaTime;
		Arm.MouseSpeed = FMath::Max(WeaponMouseSpeed, FMath::FInterpTo(Arm.MouseSpeed, WeaponMouseSpeed, DeltaTime, AedirArmMouseSpeedSmoothing));
		const float WeaponThrow = FMath::Clamp((Arm.MouseSpeed - AedirArmGuardSpeedLow) / (AedirArmGuardSpeedHigh - AedirArmGuardSpeedLow), 0.f, 1.f);
		Arm.PunchBlend = FMath::FInterpTo(Arm.PunchBlend, WeaponThrow, DeltaTime, WeaponThrow > Arm.PunchBlend ? AedirArmPunchRiseSpeed : AedirArmPunchFallSpeed);

		if (bThrustMode)
		{
			// Forward and back on the mouse drive the blade along its own line; sideways only steers the point.
			Arm.Extension = FMath::Clamp(Arm.Extension + static_cast<float>(MouseDeltaThisFrame.Y) * AedirThrustMouseExtension, 0.f, 1.f);
			Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X + MouseDeltaThisFrame.X * AimSensitivity * AedirFistMouseShare, -AedirWeaponYawLimit, AedirWeaponYawLimit);
			return;
		}

		Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X + MouseDeltaThisFrame.X * AimSensitivity * AedirWeaponMouseShare, -AedirWeaponYawLimit, AedirWeaponYawLimit);
		Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity * AedirWeaponMouseUpShare, AedirWeaponPitchMin, AedirWeaponRaiseFullDegrees);
		return;
	}

	// A quick mouse movement throws the arm out along the aim; a slow one pulls it back to the guard in front of the face.
	const float RawSpeed = static_cast<float>(MouseDeltaThisFrame.Size()) / DeltaTime;
	// Attack is instant so a flick counts at once; the speed then decays smoothly.
	Arm.MouseSpeed = FMath::Max(RawSpeed, FMath::FInterpTo(Arm.MouseSpeed, RawSpeed, DeltaTime, AedirArmMouseSpeedSmoothing));
	const float PunchTarget = FMath::Clamp((Arm.MouseSpeed - AedirFistPunchSpeedLow) / (AedirFistPunchSpeedHigh - AedirFistPunchSpeedLow), 0.f, 1.f);
	Arm.PunchBlend = FMath::FInterpTo(Arm.PunchBlend, PunchTarget, DeltaTime, PunchTarget > Arm.PunchBlend ? AedirArmPunchRiseSpeed : AedirArmPunchFallSpeed);

	// Mouse Y input is positive when the mouse moves up, so moving the mouse up raises the arm.
	// Outwards the arm sweeps a 90 degree arc, across the body it only reaches a little past the centre line.
	// A quick throw drives the fist out along where it was aimed: the throw itself moves the aim only a little, or every punch
	// would rise from low to high as the mouse goes up and pass over the guard instead of into it.
	const float AimShare = 1.f - AedirPunchAimHold * PunchTarget;
	const float OutwardSign = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float OutwardYaw = FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSign + MouseDeltaThisFrame.X * OutwardSign * AimSensitivity * AedirFistMouseShare * AimShare, -AedirArmAcrossYawLimit, AedirArmOutwardYawLimit);
	Arm.AimOffsetDeg.X = OutwardYaw * OutwardSign;
	Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity * AedirFistMouseShare * AimShare, AedirArmAimPitchMin, AedirFistAimPitchMax);
}

void UAedirCombatComponent::AddExtensionInput(EAedirHand Hand, float WheelDelta)
{
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.Extension = FMath::Clamp(Arm.Extension + WheelDelta * AedirArmExtensionStep, 0.f, 1.f);
}

bool UAedirCombatComponent::StartSwing(EAedirHand Hand, EAedirSwingType Type)
{
	return TryStartSwing(Hand, Type, FVector2D::ZeroVector);
}

void UAedirCombatComponent::SetArmorProtection(EAedirBodyZone Zone, float Protection)
{
	const int32 Index = static_cast<int32>(Zone);
	if (!IsAuthoritative() || !ArmorProtection.IsValidIndex(Index))
	{
		return;
	}

	ArmorProtection[Index] = FMath::Clamp(Protection, 0.f, AedirArmorMaxProtection);
}

float UAedirCombatComponent::GetArmorProtection(EAedirBodyZone Zone) const
{
	const int32 Index = static_cast<int32>(Zone);
	return ArmorProtection.IsValidIndex(Index) ? ArmorProtection[Index] : 0.f;
}

float UAedirCombatComponent::GetLimbInjury(EAedirLimb Limb) const
{
	const int32 Index = static_cast<int32>(Limb);
	return LimbInjury.IsValidIndex(Index) ? LimbInjury[Index] : 0.f;
}

FVector UAedirCombatComponent::GetHandVelocity(EAedirHand Hand) const
{
	return GetArm(Hand).HandVelocity;
}

FVector UAedirCombatComponent::GetTipWorld(EAedirHand Hand) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	return ComputeArmTip(Hand, GetArm(Hand));
}

bool UAedirCombatComponent::IsGuardActive(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? bLeftGuard : bRightGuard;
}

bool UAedirCombatComponent::IsInFightStance() const
{
	return bLeftGuard || bRightGuard;
}

bool UAedirCombatComponent::GetGuardPose(EAedirHand Hand, float& OutAlpha) const
{
	OutAlpha = GuardAlpha[HandIndex(Hand)];
	return OutAlpha > 0.001f;
}

bool UAedirCombatComponent::GetSwingPose(EAedirHand Hand, FVector& OutHandTargetWorld, float& OutAlpha) const
{
	const FAedirArmDynamics& Arm = GetArm(Hand);
	if (IsSecondaryGripHand(Hand) && IsGripHeld() && !IsObjectGrip())
	{
		OutAlpha = 1.f;
		OutHandTargetWorld = GetGripPointWorld();
		return true;
	}

	OutAlpha = Arm.ActiveAlpha;
	if (OutAlpha <= 0.001f)
	{
		return false;
	}

	OutHandTargetWorld = Arm.HandWorld;
	return true;
}

float UAedirCombatComponent::GetFistAlpha(EAedirHand Hand) const
{
	// A hand around a handle is closed. A hand on a big object lies open on its surface.
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	if (HeldItem)
	{
		return HeldItem->ItemClass == EAedirItemClass::Object ? AedirObjectFistAlpha : 1.f;
	}
	if (Creature && Creature->Interaction && Creature->Interaction->IsGrabbingBody(Hand))
	{
		return 1.f;
	}
	// The second hand of a two-handed hold closes on the grip as it comes onto it, whether or not the weapon counts as held
	// by it (a sword that is only one hand's but held in both).
	if (IsSecondaryGripHand(Hand) && !IsObjectGrip())
	{
		return FMath::Max(CombatSmoothStep01(SecondaryGripBlend), GuardAlpha[HandIndex(Hand)]);
	}

	return GuardAlpha[HandIndex(Hand)];
}

FString UAedirCombatComponent::DevDescribeArm(EAedirHand Hand) const
{
	const FAedirArmDynamics& Arm = GetArm(Hand);
	const FTransform& Body = Creature->GetActorTransform();
	const FVector Target = IsWeaponStance(Hand) ? ComputeWeaponTargetDir(Hand, Arm) : ComputeCarriedDir(Hand);
	const FRotator Want = Body.InverseTransformVectorNoScale(Target).Rotation();
	const FRotator Has = Body.InverseTransformVectorNoScale(Arm.WeaponDir).Rotation();
	const FVector HandLocal = Body.InverseTransformPosition(Arm.HandWorld);
	const FVector Wanted = Body.InverseTransformPosition(ComputeArmTarget(Hand, Arm));
	return FString::Printf(TEXT("fist aim %.0f %.0f punch %.2f, aim %.0f %.0f stance %d grip %d blade want p%.0f y%.0f has p%.0f y%.0f hand %.0f %.0f %.0f wants %.0f %.0f %.0f, held %d blend %.2f active %.2f gripper %d item len %.0f hafted %d pole %d"), Arm.AimOffsetDeg.X, Arm.AimOffsetDeg.Y, Arm.PunchBlend, Arm.PoseAimDeg.X, Arm.PoseAimDeg.Y, IsWeaponStance(Hand) ? 1 : 0, bTwoHandGrip ? 1 : 0,
		Want.Pitch, Want.Yaw, Has.Pitch, Has.Yaw, HandLocal.X, HandLocal.Y, HandLocal.Z, Wanted.X, Wanted.Y, Wanted.Z, IsGripHeld() ? 1 : 0, SecondaryGripBlend, Arm.ActiveAlpha, Hand == GetGripHand() ? 1 : 0, GetHeldItem(Hand) ? GetHeldItem(Hand)->GetWeaponLength() : 0.f, GetHeldItem(Hand) && GetHeldItem(Hand)->IsHafted() ? 1 : 0, IsPolearm(GetHeldItem(Hand)) ? 1 : 0);
}

bool UAedirCombatComponent::WantsPosedArm(EAedirHand Hand) const
{
	if (CVarAedirPosedArms.GetValueOnGameThread() == 0 || !Creature)
	{
		return false;
	}
	const EAedirHand Holding = IsSecondaryGripHand(Hand) ? GetGripHand() : Hand;
	const FAedirArmDynamics& Arm = GetArm(Holding);
	const AAedirItemActor* Item = GetHeldItem(Holding);
	// Up in the stance, or a weapon carried at rest: both are poses read off the stills, shown as they are.
	const bool bWeapon = Item && (IsWeaponStance(Holding) || Item->ItemClass == EAedirItemClass::Weapon);
	const UAedirInteractionComponent* Interaction = Creature->Interaction.Get();
	FVector HolsterDir;
	float HolsterShare = 0.f;
	const bool bStowing = Interaction && (Interaction->GetHolsterAim(Holding, HolsterDir, HolsterShare) || Interaction->GetHandState(Holding) == EAedirHandState::Reaching);
	return bWeapon && !bStowing && !Arm.bScriptedControl && !Arm.LodgedIn.IsValid();
}

FString UAedirCombatComponent::DevMeasurePose(EAedirHand Hand) const
{
	// What the pose asks of the hand and the blade, and where the hand and the held weapon really are: in the body's own frame
	// (forward, right, up from the middle of the capsule; pitch up, yaw right).
	const FAedirArmDynamics& Arm = GetArm(Hand);
	const FTransform& Body = Creature->GetActorTransform();
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const FRotator Want = Body.InverseTransformVectorNoScale(ComputeWeaponModelDir(Hand, Arm)).Rotation();
	const FVector WantHand = ComputeStanceHandLocal(Hand, Arm) * static_cast<float>(Creature->GetActorScale3D().X);
	const FRotator Has = Item ? Body.InverseTransformVectorNoScale(Item->GetActorForwardVector()).Rotation() : FRotator::ZeroRotator;
	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	const FVector HasHand = Mesh ? Body.InverseTransformPositionNoScale(Mesh->GetBoneLocation(Hand == EAedirHand::Right ? TEXT("hand_r") : TEXT("hand_l"))) : FVector::ZeroVector;
	const float BladeOff = Item ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Want.Vector(), Has.Vector())), -1.f, 1.f))) : 0.f;
	return FString::Printf(TEXT("aim %.0f %.0f want blade p%.0f y%.0f hand %.0f %.0f %.0f | has blade p%.0f y%.0f hand %.0f %.0f %.0f | blade off %.0f deg, hand off %.0f cm | servo %.0f deg %.0f cm"),
		Arm.PoseAimDeg.X, Arm.PoseAimDeg.Y, Want.Pitch, Want.Yaw, WantHand.X, WantHand.Y, WantHand.Z, Has.Pitch, Has.Yaw, HasHand.X, HasHand.Y, HasHand.Z, BladeOff, FVector::Dist(WantHand, HasHand),
		FMath::RadiansToDegrees(static_cast<float>(Arm.ServoBladeLocal.GetAngle())), Arm.ServoHandLocal.Size());
}

void UAedirCombatComponent::DevSetArmAim(EAedirHand Hand, FVector2D AimDegrees, float Extension)
{
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.AimOffsetDeg = AimDegrees;
	Arm.PoseAimDeg = AimDegrees;
	Arm.PoseAimSpeed = FVector2D::ZeroVector;
	Arm.Extension = FMath::Clamp(Extension, 0.f, 1.f);
}

void UAedirCombatComponent::DevHoldKick(float Share)
{
	DevKickHoldShare = Share;
}

void UAedirCombatComponent::SetThrustMode(bool bActive)
{
	bThrustMode = bActive;
	if (Creature && !IsAuthoritative() && Creature->IsLocallyControlled())
	{
		ServerSetThrustMode(bActive);
	}
}

void UAedirCombatComponent::ServerSetThrustMode_Implementation(bool bActive)
{
	bThrustMode = bActive;
}

bool UAedirCombatComponent::IsWeaponStance(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	// The carrying hand of a two-handed hold keeps its place while the other hand lets go for a moment, so that hand can take
	// hold again where it let go; dropped to the side, the grip would be out of the other arm's reach for good.
	return HeldItem && HeldItem->ItemClass == EAedirItemClass::Weapon && (IsArmActive(Hand) || (bTwoHandGrip && Hand == GetGripHand()));
}

bool UAedirCombatComponent::IsGripHeld() const
{
	return bTwoHandGrip && GripLostSeconds <= 0.f && !(Creature && Creature->Interaction && Creature->Interaction->IsHolsterMoving());
}

bool UAedirCombatComponent::IsHalfSwording() const
{
	return HalfSwordAlpha > 0.f;
}

bool UAedirCombatComponent::IsObjectGrip() const
{
	const AAedirItemActor* Item = GetHeldItem(EAedirHand::Left);
	return bTwoHandGrip && Item && Item->ItemClass == EAedirItemClass::Object && Item == GetHeldItem(EAedirHand::Right);
}

bool UAedirCombatComponent::IsHangingWeapon(EAedirHand Hand) const
{
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	FVector HolsterDir;
	float HolsterShare = 0.f;
	// An empty hand on its way to the belt turns as the belt wants it.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	return Item && Item->ItemClass == EAedirItemClass::Weapon && !IsWeaponStance(Hand) && Interaction && !Interaction->GetHolsterAim(Hand, HolsterDir, HolsterShare) && !Interaction->IsPassingHeld(Hand);
}

float UAedirCombatComponent::GetHalfSwordShare() const
{
	return CombatSmoothStep01(HalfSwordAlpha);
}

bool UAedirCombatComponent::CanHalfSword(const AAedirItemActor* Item)
{
	return Item && Item->ItemClass == EAedirItemClass::Weapon && Item->GetWeaponLength() >= AedirHalfSwordMinLengthCm;
}

void UAedirCombatComponent::UpdateObjectCarry()
{
	const bool bObjectGrip = IsObjectGrip();
	AAedirItemActor* Item = bObjectGrip ? GetHeldItem(EAedirHand::Left) : nullptr;
	if (!Item)
	{
		return;
	}

	// The middle of the object is the hand of the arm dynamics, so its weight slows the swing. What it runs into stops it.
	FAedirArmDynamics& Arm = GetArm(GetGripHand());
	const FVector From = Item->GetBoundsCenterWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCarriedObject), false);
	Params.AddIgnoredActor(Creature);
	Params.AddIgnoredActor(Item);
	FHitResult Hit;
	if (GetWorld() && !Arm.bScriptedControl && GetWorld()->SweepSingleByObjectType(Hit, From, Arm.HandWorld, FQuat::Identity, GetHeldBlockObjects(), FCollisionShape::MakeSphere(Item->GetHoldRadiusCm()), Params) && !Hit.bStartPenetrating)
	{
		UPrimitiveComponent* Other = Hit.GetComponent();
		if (Other && Other->IsSimulatingPhysics() && Other->GetMass() <= AedirBladeBlockMassKg)
		{
			if (IsAuthoritative())
			{
				Other->AddImpulseAtLocation(Arm.HandVelocity.GetClampedToMaxSize(AedirBladePushMaxSpeed) * (Other->GetMass() * AedirBladePushShare), Hit.ImpactPoint);
			}
		}
		else
		{
			Arm.HandWorld = Hit.Location;
			Arm.HandVelocity = FVector::ZeroVector;
		}
	}

	Item->SetActorLocation(Item->GetActorLocation() + (Arm.HandWorld - From));
}

FCollisionObjectQueryParams UAedirCombatComponent::GetHeldBlockObjects()
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	return Objects;
}

bool UAedirCombatComponent::SweepHeldBlade(const AAedirItemActor* Item, const FVector& HandWorld, const FVector& Direction, float LengthCm, FHitResult& OutHit) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirHeldBlade), false);
	Params.AddIgnoredActor(Creature);
	Params.AddIgnoredActor(Item);
	// What hangs on the belt or lies in the other hand does not stop a swing.
	TArray<AActor*> Carried;
	Creature->GetAttachedActors(Carried);
	Params.AddIgnoredActors(Carried);
	if (!GetWorld())
	{
		return false;
	}
	// Another body does not stop the blade here short of it: the blade goes on to meet it, and the blow decides whether it
	// stops on it, bites or goes through. Steel and the world still stop it.
	for (int32 Try = 0; Try < AedirBladeSweepTries; ++Try)
	{
		if (!GetWorld()->SweepSingleByObjectType(OutHit, HandWorld + Direction * AedirBladeGuardCm, HandWorld + Direction * LengthCm, FQuat::Identity, GetHeldBlockObjects(), FCollisionShape::MakeSphere(AedirBladeRadiusCm), Params))
		{
			return false;
		}
		AAedirCreature* Body = Cast<AAedirCreature>(OutHit.GetActor());
		if (!Body || OutHit.GetComponent() != Body->GetMesh())
		{
			return true;
		}
		Params.AddIgnoredActor(Body);
	}
	return false;
}

void UAedirCombatComponent::ResolveHeldCollision(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& PrevHand, const FVector& PrevDir, float DeltaTime)
{
	// Every blade meets what is in its way, an attack an NPC plays out as much as one the player swings.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Item || Item->ItemClass != EAedirItemClass::Weapon || !(IsWeaponStance(Hand) || Arm.bScriptedControl) || PrevDir.IsNearlyZero())
	{
		return;
	}

	const float Length = Item->GetWeaponLength() * static_cast<float>(Creature->GetActorScale3D().X);
	FHitResult Hit;
	if (!SweepHeldBlade(Item, Arm.HandWorld, Arm.WeaponDir, Length, Hit))
	{
		return;
	}

	// Steel on steel: the other weapon is knocked aside by what this one carried into it, as the two arms stand behind them.
	if (DeltaTime > KINDA_SMALL_NUMBER)
	{
		AAedirCreature* Holder = Cast<AAedirCreature>(Hit.GetActor());
		if (!Holder && Hit.GetActor())
		{
			Holder = Cast<AAedirCreature>(Hit.GetActor()->GetAttachParentActor());
		}
		UAedirCombatComponent* OtherCombat = Holder && Holder != Creature ? Holder->Combat.Get() : nullptr;
		const EAedirHand OtherHand = OtherCombat && OtherCombat->GetHeldItem(EAedirHand::Left) == Hit.GetActor() ? EAedirHand::Left : EAedirHand::Right;
		const AAedirItemActor* OtherItem = OtherCombat ? OtherCombat->GetHeldItem(OtherHand) : nullptr;
		const bool bOnWeapon = OtherItem && Hit.GetActor() == OtherItem;
		if (bOnWeapon)
		{
			const FVector TipMotion = ((Arm.HandWorld + Arm.WeaponDir * Length) - (PrevHand + PrevDir * Length)) / DeltaTime;
			const float MyMass = Item->Weight + ForearmMass;
			const float TheirMass = OtherItem->Weight + ForearmMass;
			const FVector Push = TipMotion.GetClampedToMaxSize(AedirClashMaxSpeed) * (MyMass / (MyMass + TheirMass));
			OtherCombat->ReceiveClash(OtherHand, Push);
			const float ClashSpeed = static_cast<float>(TipMotion.Size());
			if (ClashSpeed > AedirSfxHitMinSpeed && GetWorld()->GetTimeSeconds() - LastClashSoundTime > 0.12)
			{
				LastClashSoundTime = GetWorld()->GetTimeSeconds();
				const bool bWood = CombatIsWooden(Item) || CombatIsWooden(OtherItem);
				UAedirSfxSubsystem::Play(this, bWood ? EAedirSfx::WoodHit : EAedirSfx::Clash, Hit.ImpactPoint, FMath::GetRangePct(AedirSfxHitMinSpeed, AedirSfxHitFullSpeed, ClashSpeed));
			}
			UE_CLOG(TipMotion.Size() > AedirClashLogSpeed, LogAedir, Log, TEXT("[Clash] %s's %s met %s's %s at %.0f cm/s"), *Creature->GetName(), *Item->GetName(), *Holder->GetName(), *OtherItem->GetName(), TipMotion.Size());
		}
	}

	// Something loose gets pushed along by the blade.
	UPrimitiveComponent* Other = Hit.GetComponent();
	if (Other && Other->IsSimulatingPhysics() && Other->GetMass() <= AedirBladeBlockMassKg)
	{
		if (IsAuthoritative() && DeltaTime > KINDA_SMALL_NUMBER)
		{
			const FVector TipMotion = (Arm.HandWorld + Arm.WeaponDir * Length) - (PrevHand + PrevDir * Length);
			Other->AddImpulseAtLocation((TipMotion / DeltaTime).GetClampedToMaxSize(AedirBladePushMaxSpeed) * (Other->GetMass() * AedirBladePushShare), Hit.ImpactPoint);
		}
		return;
	}

	// Steel or wood knocking on the world: a wall, a tree, the ground, a table.
	if (DeltaTime > KINDA_SMALL_NUMBER && !Cast<AAedirCreature>(Hit.GetActor()) && !Cast<AAedirItemActor>(Hit.GetActor()))
	{
		const float KnockSpeed = static_cast<float>(((Arm.HandWorld + Arm.WeaponDir * Length) - (PrevHand + PrevDir * Length)).Size() / DeltaTime);
		if (KnockSpeed > AedirSfxHitMinSpeed && GetWorld()->GetTimeSeconds() - LastClashSoundTime > 0.15)
		{
			LastClashSoundTime = GetWorld()->GetTimeSeconds();
			const bool bWoodenThing = Hit.GetActor() && (Hit.GetActor()->GetName().Contains(TEXT("Wood")) || Hit.GetActor()->GetName().Contains(TEXT("Log")) || Hit.GetActor()->GetName().Contains(TEXT("Tree")));
			const EAedirSfx Kind = CombatIsWooden(Item) ? EAedirSfx::WoodHit : (bWoodenThing && Item->GetDamageType() == EAedirDamageType::Slash ? EAedirSfx::WoodCut : EAedirSfx::MetalHit);
			UAedirSfxSubsystem::Play(this, Kind, Hit.ImpactPoint, FMath::GetRangePct(AedirSfxHitMinSpeed, AedirSfxHitFullSpeed, KnockSpeed));
		}
	}

	// The body walked the blade into it: the hand gives way along the blade and the elbow bends.
	FHitResult PrevHit;
	if (SweepHeldBlade(Item, PrevHand, PrevDir, Length, PrevHit))
	{
		const float Over = Length - AedirBladeGuardCm - Hit.Distance;
		Arm.HandWorld -= Arm.WeaponDir * FMath::Min(Over, AedirBladeMaxPullBackCm);
		Arm.HandVelocity = FVector::ZeroVector;
		Arm.WeaponDirSpeed = FVector::ZeroVector;
		return;
	}

	// Caught fast enough, the weapon is wrenched out of the hand instead of stopping the arm.
	const float CaughtSpeed = static_cast<float>((Arm.HandVelocity + (Arm.WeaponDir - PrevDir) * (Length / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER))).Size());
	const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
	const float WrenchSpeed = (AedirWrenchSpeedBase + Strength * AedirWrenchSpeedPerStrength) * (IsGripHeld() ? AedirWrenchTwoHandShare : 1.f);
	if (CaughtSpeed > WrenchSpeed && IsAuthoritative() && Creature->Interaction)
	{
		UE_LOG(LogAedir, Log, TEXT("%s had %s wrenched from the hand at %.0f cm/s"), *Creature->GetName(), *Item->GetName(), CaughtSpeed);
		Creature->Interaction->DropHeld(Hand);
		return;
	}

	// The swing stops at the last pose that was still clear.
	float Clear = 0.f;
	float Step = 0.5f;
	for (int32 Index = 0; Index < AedirBladeBackOffSteps; ++Index, Step *= 0.5f)
	{
		const float Try = Clear + Step;
		FHitResult TryHit;
		if (!SweepHeldBlade(Item, FMath::Lerp(PrevHand, Arm.HandWorld, Try), FMath::Lerp(PrevDir, Arm.WeaponDir, Try).GetSafeNormal(), Length, TryHit))
		{
			Clear = Try;
		}
	}
	Arm.HandWorld = FMath::Lerp(PrevHand, Arm.HandWorld, Clear);
	Arm.WeaponDir = FMath::Lerp(PrevDir, Arm.WeaponDir, Clear).GetSafeNormal();
	Arm.HandVelocity = FVector::ZeroVector;
	Arm.WeaponDirSpeed = FVector::ZeroVector;
}

void UAedirCombatComponent::ReceiveClash(EAedirHand Hand, const FVector& Push)
{
	// Struck on the weapon, the hand is driven along with the blow and the blade turns away with it; the arm then pulls it back.
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.HandVelocity += Push * AedirClashHandShare;
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const float Length = Item ? FMath::Max(Item->GetWeaponLength(), 1.f) : 1.f;
	Arm.WeaponDirSpeed += FVector::VectorPlaneProject(Push, Arm.WeaponDir) / Length;
}

void UAedirCombatComponent::SetWeaponDirNow(EAedirHand Hand, const FVector& DirectionWorld)
{
	GetArm(Hand).WeaponDir = DirectionWorld;
	GetArm(Hand).WeaponDirSpeed = FVector::ZeroVector;
}

float UAedirCombatComponent::GetSwingBendDegrees() const
{
	// Forward is positive: the trunk follows the weapon arm down, and leans back a little as it goes up overhead.
	float Bend = 0.f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!GetHeldItem(Hand) && IsGuardActive(Hand))
		{
			// A fist thrown down at one on the ground bends the trunk over it: standing straight the arm stops well above him.
			const float WorldPitch = static_cast<float>(FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch) + GetArm(Hand).AimOffsetDeg.Y);
			const float FistDown = FMath::Clamp((-WorldPitch - AedirPunchBendFromPitch) / AedirPunchBendSpanPitch, 0.f, 1.f);
			Bend = FMath::Max(Bend, FistDown * AedirPunchBendDownDegrees);
			continue;
		}
		if (!IsWeaponStance(Hand) || IsSecondaryGripHand(Hand))
		{
			continue;
		}
		const float AimY = static_cast<float>(GetArm(Hand).PoseAimDeg.Y);
		const float Down = FMath::Clamp(-AimY / -AedirWeaponPitchMin, 0.f, 1.f);
		const float Up = CombatRaiseShare(AimY, AedirWeaponRaiseFullDegrees);
		Bend = FMath::Max(Bend, Down * AedirSwingBendDownDegrees - Up * AedirSwingBendUpDegrees);
		if (Bend <= 0.f)
		{
			Bend = FMath::Min(Bend, Down * AedirSwingBendDownDegrees - Up * AedirSwingBendUpDegrees);
		}
	}
	const bool bFists = !GetHeldItem(EAedirHand::Left) && !GetHeldItem(EAedirHand::Right);
	return bFists ? Bend : Bend * GetFightShare();
}

float UAedirCombatComponent::GetSwingTwistDegrees() const
{
	// The spine turns with a swing, as far as the weapon is pulled to the side, and with a punch, as hard as it is thrown.
	float Twist = 0.f;
	float PunchTwist = 0.f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		const FAedirArmDynamics& Arm = GetArm(Hand);
		if (IsWeaponStance(Hand))
		{
			if (!IsSecondaryGripHand(Hand) && FMath::Abs(Arm.PoseAimDeg.X) > FMath::Abs(Twist))
			{
				Twist = static_cast<float>(Arm.PoseAimDeg.X);
			}
		}
		else if (!GetHeldItem(Hand) && IsGuardActive(Hand))
		{
			PunchTwist += (Hand == EAedirHand::Right ? -1.f : 1.f) * Arm.PunchBlend * AedirPunchSpineDegrees + static_cast<float>(Arm.AimOffsetDeg.X) * AedirPunchSpineAimShare;
		}
	}
	const EAedirHand GripHand = GetGripHand();
	const bool bPolearmGrip = IsGripHeld() && IsPolearm(GetHeldItem(GripHand));
	const float SpineShare = bPolearmGrip ? AedirPolearmSpineShare : FMath::Lerp(AedirWeaponSpineShare, AedirHalfSwordSpineShare, HalfSwordAlpha);
	const float SpineMax = bPolearmGrip ? AedirPolearmSpineMaxDegrees : AedirWeaponSpineMaxDegrees;
	const float SwingTwist = FMath::Clamp(Twist * SpineShare, -SpineMax, SpineMax) * GetFightShare();
	const float PolearmTwist = bPolearmGrip ? (GripHand == EAedirHand::Right ? AedirPolearmSpineDegrees : -AedirPolearmSpineDegrees) * (1.f - FMath::Min(FMath::Abs(Twist) / AedirWeaponYawLimit, 1.f)) : 0.f;
	return SwingTwist + PolearmTwist + FMath::Clamp(PunchTwist, -AedirPunchSpineMaxDegrees, AedirPunchSpineMaxDegrees);
}

float UAedirCombatComponent::GetPunchLeanDegrees() const
{
	// A thrown fist takes the chest forward along with it.
	float Lean = 0.f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!GetHeldItem(Hand) && IsGuardActive(Hand))
		{
			Lean = FMath::Max(Lean, GetArm(Hand).PunchBlend * AedirPunchLeanDegrees);
		}
	}
	return Lean;
}

float UAedirCombatComponent::GetFightShare() const
{
	return CombatSmoothStep01(FMath::Max(GuardAlpha[0], GuardAlpha[1]));
}

bool UAedirCombatComponent::GetObjectCarry(FVector& OutCenterWorld, float& OutRadiusCm, float& OutAlpha) const
{
	const AAedirItemActor* Item = GetHeldItem(EAedirHand::Left);
	if (!IsObjectGrip() || SecondaryGripBlend <= 0.f)
	{
		return false;
	}

	OutCenterWorld = GetArm(GetGripHand()).HandWorld;
	OutRadiusCm = Item->GetHoldRadiusCm();
	OutAlpha = CombatSmoothStep01(SecondaryGripBlend);
	return true;
}

void UAedirCombatComponent::UpdateGrip(float DeltaTime)
{
	const EAedirHand PrimaryHand = GetGripHand();
	const EAedirHand OtherHand = PrimaryHand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	AAedirItemActor* Item = GetHeldItem(PrimaryHand);
	const bool bBothOnItem = bTwoHandGrip && Item && GetHeldItem(OtherHand) == Item;
	const bool bRagdoll = Creature->Balance && Creature->Balance->IsRagdoll();

	SecondaryGripBlend = FMath::FInterpConstantTo(SecondaryGripBlend, IsGripHeld() && !bRagdoll ? 1.f : 0.f, DeltaTime, 1.f / AedirSecondaryGripBlendSeconds);

	// A weapon that is in one hand only gets the second one on its handle first; the hand moves up the blade from a proper two-handed hold.
	// A player slides the hand up the blade with a click of the right button and back with another; others while they hold it.
	const bool bHalfSwordAsked = Creature->IsPlayerControlled() ? bHalfSwordToggled : bLeftGuard;
	const bool bHalfSwordWanted = bTwoHandGrip && bHalfSwordAsked && CanHalfSword(Item) && IsGripHeld() && bBothOnItem;
	HalfSwordAlpha = FMath::FInterpConstantTo(HalfSwordAlpha, bHalfSwordWanted ? 1.f : 0.f, DeltaTime, 1.f / AedirHalfSwordSlideSeconds);

	// The mouse means something else in this hold, so it starts from the middle.
	if (bHalfSwordWanted != bHalfSwordWantedLast)
	{
		GetArm(PrimaryHand).AimOffsetDeg = FVector2D::ZeroVector;
		bHalfSwordWantedLast = bHalfSwordWanted;
	}

	// The object comes from where the first hand holds it.
	const bool bObjectGrip = IsObjectGrip();
	if (bObjectGrip && !bObjectGripLast)
	{
		FAedirArmDynamics& CarryArm = GetArm(PrimaryHand);
		CarryArm.HandWorld = Item->GetBoundsCenterWorld();
		CarryArm.PrevHandWorld = CarryArm.HandWorld;
		CarryArm.HandVelocity = FVector::ZeroVector;
		CarryArm.AimOffsetDeg = FVector2D::ZeroVector;
		CarryArm.PoseAimDeg = FVector2D::ZeroVector;
	}
	bObjectGripLast = bObjectGrip;

	// The blade stays where it was when the other hand becomes the carrying one.
	const EAedirHand NewPrimary = GetGripHand();
	if (NewPrimary != LastGripHand && bTwoHandGrip && !bObjectGrip)
	{
		FAedirArmDynamics& NewArm = GetArm(NewPrimary);
		const FAedirArmDynamics& OldArm = GetArm(LastGripHand);
		NewArm.AimOffsetDeg = OldArm.AimOffsetDeg;
		NewArm.PoseAimDeg = OldArm.PoseAimDeg;
		NewArm.PoseAimSpeed = OldArm.PoseAimSpeed;
		NewArm.Extension = OldArm.Extension;
		NewArm.PunchBlend = OldArm.PunchBlend;
		NewArm.WeaponDir = OldArm.WeaponDir;
		NewArm.WeaponDirSpeed = OldArm.WeaponDirSpeed;
		if (const USkeletalMeshComponent* BodyMesh = Creature->GetMesh())
		{
			NewArm.HandWorld = BodyMesh->GetBoneLocation(NewPrimary == EAedirHand::Left ? Creature->Interaction->LeftHandBone : Creature->Interaction->RightHandBone);
			NewArm.PrevHandWorld = NewArm.HandWorld;
			NewArm.HandVelocity = FVector::ZeroVector;
		}
	}
	LastGripHand = NewPrimary;

	if (bTwoHandGrip)
	{
		FAedirArmDynamics& Arm = GetArm(NewPrimary);
		if (!IsInFightStance())
		{
			// With no button held the weapon settles back into the low guard.
			Arm.AimOffsetDeg = FMath::Vector2DInterpTo(Arm.AimOffsetDeg, FVector2D::ZeroVector, DeltaTime, AedirGripRestInterpSpeed);
			Arm.PunchBlend = FMath::FInterpTo(Arm.PunchBlend, 0.f, DeltaTime, AedirArmPunchFallSpeed);
		}
		else if (HalfSwordAlpha > 0.f && !bObjectGrip)
		{
			Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X, -AedirHalfSwordYawLimit, AedirHalfSwordYawLimit);
			Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y, AedirHalfSwordPitchMin, AedirHalfSwordPitchMax);
		}
	}
}

float UAedirCombatComponent::GetUpperHandleCm(const AAedirItemActor* Item)
{
	// On a hilted weapon the handle ends at the guard, where the body's box begins: the upper hand closes below it, on the
	// handle, however short the handle is.
	float HandleCm = Item->GripLocalCm + Item->GripSpanCm;
	// On the haft of an axe or a hammer the two fists close near each other low on it, as one swings it, not spread up to
	// the head as if carrying a banner.
	if (Item->BodyScale.Y <= AedirLodgeHaftedBoxY && !Item->bPolearm)
	{
		HandleCm = Item->GripLocalCm + FMath::Min(Item->GripSpanCm, AedirTwoHandHaftSpanCm);
	}
	// On a pole the hands are about a forearm apart, the upper one before the chest when the pole stands up (stills
	// 34, 35, 38), not reaching up the shaft past the head.
	if (Item->bPolearm)
	{
		HandleCm = Item->GripLocalCm + FMath::Min(Item->GripSpanCm, AedirPoleHandsApartCm);
	}
	const float GuardCm = static_cast<float>(-Item->BodyScale.X * 50.0);
	if (Item->GripLocalCm < GuardCm)
	{
		HandleCm = FMath::Min(HandleCm, GuardCm - AedirUpperHandBelowGuardCm);
	}
	return HandleCm;
}

float UAedirCombatComponent::GetSecondaryGripLocalCm(const AAedirItemActor* Item) const
{
	// The upper hand slides from the handle up onto the blade; the carrying hand stays on the handle.
	const float BladeCm = Item->GripLocalCm + FMath::Clamp(Item->GetWeaponLength() * AedirHalfSwordHandsApartShare, AedirHalfSwordHandsApartMinCm, AedirHalfSwordHandsApartMaxCm);
	return FMath::Lerp(GetUpperHandleCm(Item), BladeCm, CombatSmoothStep01(HalfSwordAlpha));
}

bool UAedirCombatComponent::GetSecondaryGrip(EAedirHand& OutHand, FVector& OutPointInPrimaryHand, FVector& OutBladeInPrimaryHand, float& OutAlpha) const
{
	const EAedirHand PrimaryHand = GetGripHand();
	const AAedirItemActor* Item = GetHeldItem(PrimaryHand);
	if (!bTwoHandGrip || SecondaryGripBlend <= 0.f || !Item || !Item->GetRootComponent() || IsObjectGrip())
	{
		return false;
	}

	OutHand = PrimaryHand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	OutAlpha = CombatSmoothStep01(SecondaryGripBlend);

	const FTransform ItemInHand = Item->GetRootComponent()->GetRelativeTransform();
	OutBladeInPrimaryHand = ItemInHand.GetRotation().GetAxisX();
	OutPointInPrimaryHand = ItemInHand.GetLocation() + OutBladeInPrimaryHand * GetSecondaryGripLocalCm(Item);

	// On its way along the weapon the hand lifts off and passes the guard on its own side.
	if (HalfSwordAlpha > 0.f && HalfSwordAlpha < 1.f)
	{
		const FVector HopAxis = ItemInHand.GetRotation().GetAxisZ();
		const FVector HopWorld = Item->GetActorRotation().Quaternion().GetAxisZ();
		const float OwnSide = OutHand == EAedirHand::Right ? 1.f : -1.f;
		const float HopSign = FVector::DotProduct(HopWorld, Creature->GetActorRightVector()) * OwnSide >= 0.f ? 1.f : -1.f;
		OutPointInPrimaryHand += HopAxis * (HopSign * AedirHalfSwordHopCm * FMath::Sin(HalfSwordAlpha * UE_PI));
	}
	return true;
}

bool UAedirCombatComponent::GetWeaponAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	if (HeldItem && Creature && Creature->Interaction && Creature->Interaction->GetConsumableAim(Hand, OutDirectionWorld, OutAlpha))
	{
		return true;
	}
	if (HeldItem && HeldItem->IsShield())
	{
		// A shield brought up on guard has its grip bar stood upright, so the fist points ahead along the forearm and the face
		// of the shield is turned to what comes, not to the sky.
		OutAlpha = GuardAlpha[HandIndex(Hand)];
		OutDirectionWorld = Creature ? Creature->GetActorUpVector() : FVector::UpVector;
		return OutAlpha > 0.001f;
	}
	if (!HeldItem || HeldItem->ItemClass != EAedirItemClass::Weapon)
	{
		// On its way to the belt the hand already turns to how the handle hangs there.
		return !HeldItem && Creature && Creature->Interaction && Creature->Interaction->GetDrawAim(Hand, OutDirectionWorld, OutAlpha);
	}

	// The second hand follows the blade of the primary one.
	const EAedirHand AimHand = IsSecondaryGripHand(Hand) ? GetGripHand() : Hand;
	const FAedirArmDynamics& Arm = GetArm(AimHand);
	// A weapon points ahead also when it is only carried, every kind of weapon at the same angle.
	OutAlpha = 1.f;
	OutDirectionWorld = Arm.WeaponDir;
	return OutAlpha > 0.001f && !OutDirectionWorld.IsNearlyZero();
}

FVector UAedirCombatComponent::ComputeWeaponTargetDir(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	// The pose's blade, asked as much further round as the closed loop has learned it falls short.
	const FVector Model = ComputeWeaponModelDir(Hand, Arm);
	if (Arm.ServoBladeLocal.Equals(FQuat::Identity, 1e-4))
	{
		return Model;
	}
	const FQuat Facing = FRotator(0.0, Creature->GetActorRotation().Yaw, 0.0).Quaternion();
	return Facing.RotateVector(Arm.ServoBladeLocal.RotateVector(Facing.UnrotateVector(Model))).GetSafeNormal();
}

FVector UAedirCombatComponent::ComputeWeaponModelDir(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	const double ActorYaw = Creature->GetActorRotation().Yaw;
	const FVector2D Aim = Arm.PoseAimDeg;
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	if (bThrustMode && !UsesHandLedSwing(Hand) && !(bTwoHandGrip && IsPolearm(GetHeldItem(Hand))))
	{
		const double CameraPitch = FMath::Clamp(FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch) + AedirThrustViewPitchOffset, AedirThrustPitchMin, AedirThrustPitchMax);
		return FRotator(CameraPitch,ActorYaw - HandSide * AedirThrustInwardYaw + Aim.X * AedirThrustSteerShare, 0.0).Vector();
	}

	const AAedirItemActor* Item = GetHeldItem(Hand);
	FVector StanceDir;
	if (bTwoHandGrip && IsPolearm(Item))
	{
		FVector PolearmHand;
		FVector PolearmDir;
		ComputePolearmPose(Hand, Arm, Item, PolearmHand, PolearmDir);
		StanceDir = Creature->GetActorTransform().TransformVectorNoScale(PolearmDir);
	}
	else if (UsesHandLedSwing(Hand))
	{
		StanceDir = ComputeHandLedBladeDir(Hand, Arm, Item);
	}
	else
	{
		// One smooth curve through the poses of the swing, from behind the body on one side to behind it on the other.
		const FVector2D Angles = CombatSwingSpline<FVector2D>(FMath::Clamp(static_cast<float>(Aim.X) / AedirWeaponYawLimit, -1.f, 1.f), [this, Hand, Item](float Travel) { return ComputeSwingBladeKey(Hand, Item, Travel); });
		const float Droop = (Item && Item->bTwoHanded && !bTwoHandGrip ? AedirOneHandedDroopPitch : 0.f) + (Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopPitch : 0.f) + (Item ? Item->Weight * (bTwoHandGrip ? 0.5f : 1.f) * AedirWeaponWeightDroopPerKg : 0.f);
		const float DownShare = FMath::Clamp(-FMath::Min(static_cast<float>(Aim.Y), 0.f) / -AedirWeaponPitchMin, 0.f, 1.f);
		const float Pitch = FMath::Lerp(static_cast<float>(Angles.X), AedirWeaponDownPitch, FMath::Pow(DownShare, 0.8f)) - Droop;
		const FVector SwingDir = FRotator(Pitch, ActorYaw + Angles.Y, 0.0).Vector();

		// Mouse up goes through the upright blade to one lying straight back behind the raised hands.
		const float RaiseShare = CombatRaiseShare(static_cast<float>(Aim.Y), AedirWeaponRaiseFullDegrees);
		const FVector RaisedLocal = FVector(-AedirWeaponRaisedBack, 0.f, -AedirWeaponRaisedDown).GetSafeNormal();
		const FVector RaisedDir = FRotator(0.0, ActorYaw + Aim.X * AedirWeaponRaisedYawShare, 0.0).RotateVector(RaisedLocal);
		StanceDir = FMath::Lerp(SwingDir, RaisedDir, RaiseShare).GetSafeNormal();
	}
	// Held in both hands but not raised, the weapon rests as it is carried: not pointed at anyone.
	const float RestShare = bTwoHandGrip && !bThrustMode ? 1.f - GetFightShare() : 0.f;
	if (RestShare > 0.f && HalfSwordAlpha <= 0.f)
	{
		StanceDir = FMath::Lerp(StanceDir, ComputeCarriedDir(Hand), RestShare).GetSafeNormal();
	}
	return HalfSwordAlpha > 0.f ? FMath::Lerp(StanceDir, ComputeHalfSwordDir(Hand, Arm), CombatSmoothStep01(HalfSwordAlpha)).GetSafeNormal() : StanceDir;
}

bool UAedirCombatComponent::UsesHandLedSwing(EAedirHand Hand) const
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	return CVarAedirHandLedSwing.GetValueOnGameThread() != 0 && !(bTwoHandGrip && IsPolearm(Item)) && !GetArm(Hand).bScriptedControl;
}

FVector UAedirCombatComponent::ComputeHandLedPivotLocal(EAedirHand Hand) const
{
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	return FVector(0.f, Side * (bTwoHandGrip ? AedirHandLedTwoHandSideCm : AedirShoulderLateral), AedirShoulderHeight);
}

namespace
{
	// The poses of a fighter holding a sword, read off the reference stills: where the mouse is (right +, up +), where the
	// carrying hand is in the body's frame (forward, right, up, in cm from the middle of the hips) and which way the blade
	// points (pitch up +, yaw right +, in degrees from the body's forward). Written for the right hand; the left mirrors it.
	struct FAedirKeyPose
	{
		float AimX;
		float AimY;
		FVector Hand;
		float BladePitch;
		float BladeYaw;
	};
	const FAedirKeyPose AedirKeyPoses[] =
	{
		// Near the middle the point is on the enemy.
		{ 0.f, 0.f, FVector(32.f, 4.f, 10.f), 40.f, 0.f },
		{ 0.f, 20.f, FVector(32.f, 2.f, 8.f), 85.f, 0.f },
		{ 0.f, -28.f, FVector(36.f, 2.f, 4.f), 12.f, 0.f },
		{ 22.f, 0.f, FVector(30.f, 10.f, 10.f), 45.f, 12.f },
		{ -22.f, 0.f, FVector(32.f, 0.f, 10.f), 42.f, -12.f },
		// Out to the right the hands stay before the chest and down by the right hip, and the wrists stand the blade up, its
		// point forward (stills 21-25).
		{ 40.f, 40.f, FVector(26.f, 12.f, 16.f), 72.f, 10.f },
		{ 60.f, 30.f, FVector(24.f, 20.f, 18.f), 72.f, 25.f },
		{ 85.f, 15.f, FVector(24.f, 26.f, 12.f), 75.f, 25.f },
		{ 60.f, 0.f, FVector(28.f, 22.f, 8.f), 70.f, 15.f },
		{ 100.f, 0.f, FVector(30.f, 26.f, 6.f), 55.f, 20.f },
		// High on the right and over the head.
		{ 55.f, 60.f, FVector(12.f, 24.f, 62.f), 60.f, -20.f },
		{ 70.f, 75.f, FVector(10.f, 18.f, 74.f), 12.f, -150.f },
		{ 25.f, 80.f, FVector(12.f, 8.f, 78.f), 5.f, 90.f },
		{ -5.f, 80.f, FVector(14.f, 2.f, 78.f), 25.f, 180.f },
		// Low on the right: the hands by the hip, the point raised back over the right side (still 19).
		{ 60.f, -40.f, FVector(22.f, 22.f, -2.f), 35.f, 100.f },
		{ 100.f, -40.f, FVector(36.f, 44.f, 0.f), -30.f, 40.f },
		{ 0.f, -60.f, FVector(36.f, 0.f, -10.f), -34.f, 0.f },
		// The left: low, the arms out forward to the right and the point hanging down behind the hands (still 26); level, the blade rising out to the left; up by the left
		// shoulder, the point up and forward; further round, upright, and high, lying back over the head.
		{ -60.f, -35.f, FVector(44.f, 26.f, 18.f), -70.f, 180.f },
		{ -45.f, 0.f, FVector(30.f, 8.f, 6.f), 42.f, -70.f },
		{ -55.f, 45.f, FVector(14.f, -20.f, 52.f), 45.f, 30.f },
		{ -85.f, 30.f, FVector(22.f, -24.f, 14.f), 80.f, -20.f },
		{ -90.f, 75.f, FVector(10.f, -20.f, 70.f), 15.f, 150.f },
		{ -100.f, -40.f, FVector(38.f, -30.f, 6.f), -35.f, -60.f },
	};
	// The arms, the wrists and the second hand on the grip do not bring the blade quite to where a pose asks: each pose asks
	// this much further (pitch, yaw in degrees), measured in the game, so the blade comes to rest where the pose has it.
	const FVector2D AedirKeyPoseFix[] =
	{
		FVector2D(3, -1), FVector2D(0, 0), FVector2D(5, -3), FVector2D(8, -3), FVector2D(4, 0),
		FVector2D(-1, 12), FVector2D(-2, 20), FVector2D(2, -8), FVector2D(4, -4), FVector2D(1, -7),
		FVector2D(22, -90), FVector2D(-45, -13), FVector2D(-2, -27), FVector2D(1, -1), FVector2D(-5, 0),
		FVector2D(-11, -7), FVector2D(-27, 0), FVector2D(0, 0), FVector2D(3, -6), FVector2D(-5, 29),
		FVector2D(5, 2), FVector2D(-30, -17), FVector2D(10, -26),
	};
	static_assert(UE_ARRAY_COUNT(AedirKeyPoseFix) == UE_ARRAY_COUNT(AedirKeyPoses), "One correction for each key pose.");
	FVector AedirKeyPoseBlade(int32 Index)
	{
		const FAedirKeyPose& Key = AedirKeyPoses[Index];
		return FRotator(Key.BladePitch + AedirFixShare() * AedirKeyPoseFix[Index].X, Key.BladeYaw + AedirFixShare() * AedirKeyPoseFix[Index].Y, 0.f).Vector();
	}
	// A thrust, as in the reference stills 48-50: drawn back the hands wait before the belly, a little to the weapon's side, the
	// blade forward and out over the right side, its point raised (from behind it stands over the right shoulder, from the
	// front it goes up to the right); out, the arms are straight ahead from the middle and
	// the blade goes on level from them.
	const FAedirKeyPose AedirThrustKeyBack = { 0.f, 0.f, FVector(30.f, 6.f, 14.f), 35.f, 30.f };
	const FAedirKeyPose AedirThrustKeyOut = { 0.f, 0.f, FVector(52.f, 4.f, 30.f), 0.f, -2.f };
	// Measured in the game as for the other poses: drawn back (0) and out (1).
	const FVector2D AedirThrustFix[] = { FVector2D(2, -6), FVector2D(6, -6) };
	// The view at this pitch looks level at a man ahead; looking higher or lower takes the thrust along by this share.
	constexpr float AedirThrustLevelViewPitch = -15.f;
	constexpr float AedirThrustViewShare = 0.5f;
	constexpr float AedirThrustViewRangeDegrees = 40.f;
	constexpr float AedirThrustSteerHandCmPerDegree = 0.1f;
	// The poses blend by inverse distance in the mouse's travel, this sharply.
	constexpr float AedirKeyPosePower = 2.5f;
	// Pushed out with the wheel or a quick throw, the hands go this much further forward along the blade's way.
	constexpr float AedirKeyPoseReachCm = 18.f;
	// From this far out the blade comes down towards this pitch, all the way there fully out.
	constexpr float AedirKeyPoseLevelFrom = 0.6f;
	constexpr float AedirKeyPoseLevelPitch = -5.f;
	constexpr float AedirKeyPoseMinArmBladeDegrees = 28.f;
	static TAutoConsoleVariable<int32> CVarAedirKeyPoses(TEXT("aedir.KeyPoses"), 1, TEXT("Blade poses come from the reference key poses (1), or from the hand's angle round the shoulder (0)."));
	static TAutoConsoleVariable<int32> CVarAedirSteerBlade(TEXT("aedir.SteerBlade"), 3, TEXT("The mouse turns the blade round smoothly, as in Half Sword, for swords (1), pole weapons (2) or both (3); otherwise the blade blends between the key poses."));

	// As in Half Sword the mouse turns the blade round smoothly: to the side it swings round the body, up it stands and then
	// lies back over the head, down the point drops. The way never passes between two poses pointing opposite ways, so the
	// blade cannot flip over from one frame to the next. Raised high or held low it turns less to the side, so over the head
	// it lies back behind it rather than out over a shoulder.
	struct FAedirBladeSteer
	{
		float RestPitch;
		float YawPerDegree;
		float UpPerDegree;
		float DownPerDegree;
		float HighYawKeep;
		float LowYawKeep;
	};
	// Set by hand from the reference stills (0), or fitted so the key poses lie as near the smooth turn as they can (1): the
	// furthest is then 98 degrees off it for a sword, 58 for a pole weapon.
	const FAedirBladeSteer AedirSwordSteers[] = { { 40.f, 1.1f, 1.4f, 1.f, 0.2f, 0.6f }, { 56.7f, 0.61f, 0.87f, 2.06f, -0.62f, 0.45f } };
	const FAedirBladeSteer AedirPoleSteers[] = { { 35.f, 0.8f, 0.8f, 0.9f, 0.25f, 0.7f }, { 55.f, 1.35f, 1.1f, 1.71f, -0.63f, 1.72f } };
	static TAutoConsoleVariable<int32> CVarAedirSteerFit(TEXT("aedir.SteerFit"), 0, TEXT("The smooth turn of the blade as set by hand (0) or fitted to the key poses (1)."));
	static TAutoConsoleVariable<int32> CVarAedirSteerBend(TEXT("aedir.SteerBend"), 2, TEXT("The blade lies exactly as each key pose has it: its angles blended between them (2), bent off the smooth turn between them (1), or the smooth turn alone (0)."));
	// The angles of the key poses blend by distance this sharply: measured over the whole mouse travel, the blade then turns at
	// most 7 degrees for each degree the mouse moves, where the softer and sharper blends turn it faster.
	constexpr float AedirKeyAnglePower = 2.f;
	const FAedirBladeSteer& AedirSwordSteer() { return AedirSwordSteers[FMath::Clamp(CVarAedirSteerFit.GetValueOnGameThread(), 0, 1)]; }
	const FAedirBladeSteer& AedirPoleSteer() { return AedirPoleSteers[FMath::Clamp(CVarAedirSteerFit.GetValueOnGameThread(), 0, 1)]; }
	constexpr float AedirSteerHighAim = 80.f;
	constexpr float AedirSteerLowAim = -60.f;
	// The smooth turn as pitch and yaw: the pitch goes on past upright, so a blade lying back over the head has a pitch over 90.
	FVector2D AedirSteerBladeAngles(const FAedirBladeSteer& Steer, const FVector2D& Aim)
	{
		const float AimY = static_cast<float>(Aim.Y);
		const float Keep = FMath::Lerp(FMath::Lerp(1.f, Steer.HighYawKeep, FMath::SmoothStep(0.f, AedirSteerHighAim, AimY)), Steer.LowYawKeep, FMath::SmoothStep(0.f, -AedirSteerLowAim, -AimY));
		const float Pitch = Steer.RestPitch + AimY * (AimY > 0.f ? Steer.UpPerDegree : Steer.DownPerDegree);
		return FVector2D(Pitch, static_cast<float>(Aim.X) * Steer.YawPerDegree * Keep);
	}
	FVector AedirSteerBladeDir(const FAedirBladeSteer& Steer, const FVector2D& Aim)
	{
		const FVector2D Angles = AedirSteerBladeAngles(Steer, Aim);
		return FRotator(Angles.X, Angles.Y, 0.0).Vector();
	}
	// A way of pointing written as the pitch and yaw nearest to the given ones: the same way can be written as (p, y) or as
	// (180 - p, y + 180), each with any whole turn added to the yaw.
	FVector2D AedirNearestAngles(const FVector& Dir, const FVector2D& Near)
	{
		const FRotator Plain = Dir.Rotation();
		FVector2D Best(Plain.Pitch, Plain.Yaw);
		double BestCost = TNumericLimits<double>::Max();
		for (const FVector2D& Form : { FVector2D(Plain.Pitch, Plain.Yaw), FVector2D(180.0 - Plain.Pitch, Plain.Yaw + 180.0), FVector2D(-180.0 - Plain.Pitch, Plain.Yaw + 180.0) })
		{
			FVector2D Turned = Form;
			Turned.Y += 360.0 * FMath::RoundToDouble((Near.Y - Turned.Y) / 360.0);
			const double Cost = FMath::Square(Turned.X - Near.X) + FMath::Square(Turned.Y - Near.Y);
			if (Cost < BestCost)
			{
				BestCost = Cost;
				Best = Turned;
			}
		}
		return Best;
	}

	// Each key pose is a reference still: there the blade lies exactly as the pose has it. Between them the blade follows the
	// smooth turn, bent by a blend of how far each key pose lies off it. Each bend is under half a turn, so the blend of them
	// never cancels out to nothing and the blade cannot flip over between two poses pointing opposite ways.
	FVector AedirBendBlade(const FAedirBladeSteer& Steer, const FVector2D& Aim, int32 KeyCount, TFunctionRef<FVector2D(int32)> KeyAim, TFunctionRef<FVector(int32)> KeyDir)
	{
		const int32 Mode = CVarAedirSteerBend.GetValueOnGameThread();
		if (Mode == 0)
		{
			return AedirSteerBladeDir(Steer, Aim);
		}
		if (Mode == 2)
		{
			// Each key pose's pitch and yaw, written the way nearest the smooth turn there, blended by distance: the blade lies
			// exactly as the pose has it at each, and the angles change smoothly between them, so it cannot flip over.
			FVector2D AngleSum = FVector2D::ZeroVector;
			double AngleWeight = 0.0;
			for (int32 Index = 0; Index < KeyCount; ++Index)
			{
				const FVector2D At = KeyAim(Index);
				const FVector2D Angles = AedirNearestAngles(KeyDir(Index), AedirSteerBladeAngles(Steer, At));
				const double Distance = FVector2D::Distance(Aim, At);
				if (Distance < 0.5)
				{
					AngleSum = Angles;
					AngleWeight = 1.0;
					break;
				}
				const double Weight = 1.0 / FMath::Pow(Distance, static_cast<double>(AedirKeyAnglePower));
				AngleSum += Angles * Weight;
				AngleWeight += Weight;
			}
			const FVector2D Angles = AngleSum / AngleWeight;
			return FRotator(Angles.X, Angles.Y, 0.0).Vector();
		}
		FQuat Sum(0.0, 0.0, 0.0, 0.0);
		for (int32 Index = 0; Index < KeyCount; ++Index)
		{
			const FVector2D At = KeyAim(Index);
			const FQuat Bend = FQuat::FindBetweenNormals(AedirSteerBladeDir(Steer, At), KeyDir(Index));
			const double Distance = FVector2D::Distance(Aim, At);
			if (Distance < 0.5)
			{
				Sum = Bend;
				break;
			}
			const double Weight = 1.0 / FMath::Pow(Distance, static_cast<double>(AedirKeyPosePower));
			Sum.X += Bend.X * Weight;
			Sum.Y += Bend.Y * Weight;
			Sum.Z += Bend.Z * Weight;
			Sum.W += Bend.W * Weight;
		}
		Sum.Normalize();
		return Sum.RotateVector(AedirSteerBladeDir(Steer, Aim));
	}
}

namespace
{
	// The sword stance read off Half Sword, the mouse taken to each point of a 5 x 5 grid with the same raw mouse counts in
	// both games and each point filmed from behind and from the side (Saved/Measure). For each point: where the carrying
	// hand is in the body's frame (forward, right, up, cm from the middle of the hips) and which way the blade points
	// (pitch up +, yaw right +). The mouse runs 1875 counts from the middle to either side (100 degrees here) and 1750 up or
	// down; the grid points are 750 and 1500 counts from the middle, which here is a little below level.
	TAutoConsoleVariable<int32> CVarAedirStanceTable(TEXT("aedir.StanceTable"), 1, TEXT("The sword stance comes from the table measured in Half Sword (1), or from the older key poses (0)."));
	constexpr float AedirStanceGridX[5] = { -80.f, -40.f, 0.f, 40.f, 80.f };
	constexpr float AedirStanceGridY[5] = { -77.f, -42.f, -7.f, 28.f, 64.f };
	struct FAedirStanceCell
	{
		FVector Hand;
		float BladePitch;
		float BladeYaw;
	};
	// Rows from the bottom of the mouse field to the top, columns from left to right.
	const FAedirStanceCell AedirStanceTable[5][5] =
	{
		// Down: the hands low, at the left hip, before the belly, by the right hip; the point low to the left, hanging before
		// the feet, and from the right hip forward and up across.
		{ { FVector(15.f, -20.f, -2.f), -20.f, -110.f }, { FVector(25.f, 0.f, 4.f), -10.f, -90.f }, { FVector(25.f, 12.f, 0.f), -40.f, 20.f }, { FVector(20.f, 20.f, -4.f), 30.f, -25.f }, { FVector(20.f, 25.f, -4.f), 30.f, -15.f } },
		{ { FVector(18.f, -19.f, 14.f), -5.f, -115.f }, { FVector(25.f, -2.f, 16.f), 0.f, -88.f }, { FVector(27.f, 10.f, 12.f), -10.f, -20.f }, { FVector(24.f, 19.f, 8.f), 20.f, -28.f }, { FVector(22.f, 23.f, 6.f), 30.f, -18.f } },
		// The middle: the hands before the chest, the blade forward and out to the left; to the left up by the left shoulder,
		// the blade back over it; to the right down by the right side, the point forward and up.
		{ { FVector(15.f, -18.f, 32.f), 15.f, -130.f }, { FVector(22.f, -5.f, 32.f), 10.f, -110.f }, { FVector(28.f, 8.f, 25.f), 15.f, -60.f }, { FVector(28.f, 18.f, 20.f), 10.f, -30.f }, { FVector(25.f, 22.f, 15.f), 30.f, -20.f } },
		{ { FVector(14.f, -18.f, 46.f), 10.f, -115.f }, { FVector(18.f, -5.f, 50.f), 7.f, -100.f }, { FVector(22.f, 7.f, 46.f), 8.f, -80.f }, { FVector(23.f, 17.f, 40.f), 13.f, -50.f }, { FVector(22.f, 22.f, 34.f), 25.f, -35.f } },
		// Up: the hands over the head, the blade lying level across it to the left; to the right over the right shoulder,
		// the point forward and to the left.
		{ { FVector(12.f, -18.f, 58.f), 5.f, -105.f }, { FVector(15.f, -5.f, 66.f), 5.f, -95.f }, { FVector(15.f, 5.f, 68.f), 5.f, -95.f }, { FVector(18.f, 15.f, 58.f), 15.f, -70.f }, { FVector(18.f, 22.f, 52.f), 20.f, -50.f } },
	};
	void AedirGridSpan(const float (&Nodes)[5], float Value, int32& OutIndex, float& OutShare)
	{
		const float Clamped = FMath::Clamp(Value, Nodes[0], Nodes[4]);
		OutIndex = 0;
		while (OutIndex < 3 && Clamped > Nodes[OutIndex + 1])
		{
			++OutIndex;
		}
		OutShare = (Clamped - Nodes[OutIndex]) / (Nodes[OutIndex + 1] - Nodes[OutIndex]);
	}
	// Bilinear between the four grid points around the mouse.
	void AedirStanceAt(const FVector2D& Aim, FVector& OutHand, FVector& OutBlade)
	{
		int32 Col, Row;
		float U, V;
		AedirGridSpan(AedirStanceGridX, static_cast<float>(Aim.X), Col, U);
		AedirGridSpan(AedirStanceGridY, static_cast<float>(Aim.Y), Row, V);
		const auto Blade = [](const FAedirStanceCell& Cell) { return FRotator(Cell.BladePitch, Cell.BladeYaw, 0.f).Vector(); };
		const FAedirStanceCell& A = AedirStanceTable[Row][Col];
		const FAedirStanceCell& B = AedirStanceTable[Row][Col + 1];
		const FAedirStanceCell& C = AedirStanceTable[Row + 1][Col];
		const FAedirStanceCell& D = AedirStanceTable[Row + 1][Col + 1];
		OutHand = FMath::Lerp(FMath::Lerp(A.Hand, B.Hand, U), FMath::Lerp(C.Hand, D.Hand, U), V);
		OutBlade = FMath::Lerp(FMath::Lerp(Blade(A), Blade(B), U), FMath::Lerp(Blade(C), Blade(D), U), V).GetSafeNormal();
		if (OutBlade.IsNearlyZero())
		{
			OutBlade = FVector::UpVector;
		}
	}
}

bool UAedirCombatComponent::ComputeKeyPose(EAedirHand Hand, const FAedirArmDynamics& Arm, FVector& OutHandLocal, FVector& OutBladeLocal) const
{
	if (CVarAedirKeyPoses.GetValueOnGameThread() == 0)
	{
		return false;
	}
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	// The left hand leads a mirror of the right's poses.
	const FVector2D Aim(Arm.PoseAimDeg.X * Side, Arm.PoseAimDeg.Y);
	FVector HandSum = FVector::ZeroVector;
	FVector BladeSum = FVector::ZeroVector;
	double WeightSum = 0.0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AedirKeyPoses); ++Index)
	{
		const FAedirKeyPose& Key = AedirKeyPoses[Index];
		const double Distance = FVector2D::Distance(Aim, FVector2D(Key.AimX, Key.AimY));
		if (Distance < 0.5)
		{
			HandSum = Key.Hand;
			BladeSum = AedirKeyPoseBlade(Index);
			WeightSum = 1.0;
			break;
		}
		const double Weight = 1.0 / FMath::Pow(Distance, static_cast<double>(AedirKeyPosePower));
		HandSum += Key.Hand * Weight;
		BladeSum += AedirKeyPoseBlade(Index) * Weight;
		WeightSum += Weight;
	}
	OutHandLocal = HandSum / WeightSum;
	OutBladeLocal = (CVarAedirSteerBlade.GetValueOnGameThread() & 1) != 0
		? AedirBendBlade(AedirSwordSteer(), Aim, UE_ARRAY_COUNT(AedirKeyPoses), [](int32 Index) { return FVector2D(AedirKeyPoses[Index].AimX, AedirKeyPoses[Index].AimY); }, [](int32 Index) { return AedirKeyPoseBlade(Index); })
		: BladeSum.GetSafeNormal();
	if (OutBladeLocal.IsNearlyZero())
	{
		OutBladeLocal = FVector::UpVector;
	}
	const bool bTable = CVarAedirStanceTable.GetValueOnGameThread() != 0;
	if (bTable)
	{
		AedirStanceAt(Aim, OutHandLocal, OutBladeLocal);
	}
	// A thrust has its own two poses, drawn back and out, and goes between them as far as it is pushed; the mouse to the side
	// steers it, and the view up or down raises or lowers it.
	if (bThrustMode)
	{
		const float Out = FMath::Clamp(Arm.Extension, 0.f, 1.f);
		const FVector2D Pitches(AedirThrustKeyBack.BladePitch + AedirFixShare() * AedirThrustFix[0].X, AedirThrustKeyOut.BladePitch + AedirFixShare() * AedirThrustFix[1].X);
		const FVector2D Yaws(AedirThrustKeyBack.BladeYaw + AedirFixShare() * AedirThrustFix[0].Y, AedirThrustKeyOut.BladeYaw + AedirFixShare() * AedirThrustFix[1].Y);
		const double ViewPitch = FMath::Clamp(FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch) - AedirThrustLevelViewPitch, -AedirThrustViewRangeDegrees, AedirThrustViewRangeDegrees);
		OutHandLocal = FMath::Lerp(AedirThrustKeyBack.Hand, AedirThrustKeyOut.Hand, Out) + FVector(0.f, Aim.X * AedirThrustSteerHandCmPerDegree, 0.f);
		OutBladeLocal = FRotator(FMath::Lerp(Pitches.X, Pitches.Y, Out) + ViewPitch * AedirThrustViewShare, FMath::Lerp(Yaws.X, Yaws.Y, Out) + Aim.X * AedirThrustSteerShare, 0.f).Vector();
		if (!bTwoHandGrip)
		{
			OutHandLocal.Y += AedirHandLedTwoHandSideCm;
		}
		if (Side < 0.f)
		{
			OutHandLocal.Y = -OutHandLocal.Y;
			OutBladeLocal.Y = -OutBladeLocal.Y;
		}
		return true;
	}
	// A blade can only be held at a good angle across the forearm: pointing along the arm the wrist cannot bend that far. Where
	// a pose asks for that, the hands move round about the shoulder until the arm lies at least this far off the blade.
	if (!bTable)
	{
		const FVector Pivot = ComputeHandLedPivotLocal(EAedirHand::Right);
		const FVector Reach = OutHandLocal - Pivot;
		const double Length = Reach.Size();
		const FVector Along = Length > KINDA_SMALL_NUMBER ? Reach / Length : FVector::ForwardVector;
		const double MinCos = FMath::Cos(FMath::DegreesToRadians(AedirKeyPoseMinArmBladeDegrees));
		const double Dot = FVector::DotProduct(Along, OutBladeLocal);
		if (Dot > MinCos)
		{
			FVector Across = Along - OutBladeLocal * Dot;
			if (!Across.Normalize())
			{
				Across = FVector::ForwardVector;
			}
			const FVector Turned = OutBladeLocal * MinCos + Across * FMath::Sqrt(1.0 - MinCos * MinCos);
			OutHandLocal = Pivot + Turned * Length;
		}
	}
	// Held out, the hands follow the blade forward, and the blade comes down level with the arms (reference 30): pushed
	// all the way out, the point goes on straight from the hands.
	const float Out = FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend);
	const float Level = FMath::Clamp((Out - AedirKeyPoseLevelFrom) / (1.f - AedirKeyPoseLevelFrom), 0.f, 1.f);
	if (Level > 0.f)
	{
		const FRotator Was = OutBladeLocal.Rotation();
		OutBladeLocal = FRotator(FMath::Lerp(static_cast<float>(Was.Pitch), AedirKeyPoseLevelPitch, Level), Was.Yaw, 0.f).Vector();
	}
	OutHandLocal += FVector(OutBladeLocal.X, OutBladeLocal.Y, 0.f).GetSafeNormal() * (AedirKeyPoseReachCm * FMath::Max(Out - 0.5f, 0.f) * 2.f);
	// One hand alone keeps to its own side of the body a little more.
	if (!bTwoHandGrip)
	{
		OutHandLocal.Y += AedirHandLedTwoHandSideCm;
	}
	if (Side < 0.f)
	{
		OutHandLocal.Y = -OutHandLocal.Y;
		OutBladeLocal.Y = -OutBladeLocal.Y;
	}
	return true;
}

FVector UAedirCombatComponent::ComputeHandLedHandLocal(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	FVector KeyHand;
	FVector KeyBlade;
	if (ComputeKeyPose(Hand, Arm, KeyHand, KeyBlade))
	{
		KeyHand.Z -= Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopCm : 0.f;
		return KeyHand;
	}
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	const FVector2D Aim = Arm.PoseAimDeg;
	// Round to the far side the arm runs out of shoulder before the mouse runs out: the hand stops there, elbow down, rather
	// than the elbow turning over to reach further.
	const float RawYaw = static_cast<float>(Aim.X) * AedirHandLedYawShare - Side * AedirHandLedInwardYaw;
	// Both hands on one grip go round only as far as the second arm still reaches it.
	const float AimPitch = static_cast<float>(Aim.Y);
	const float Pitch = FMath::Clamp(AedirHandLedRestPitch + AimPitch * (AimPitch > 0.f ? AedirHandLedUpShare : AedirHandLedDownShare), AedirHandLedPitchMin, AedirHandLedPitchMax);
	// A shoulder takes the hand round behind the body only up at its own height or over it; down by the hip the arm cannot
	// go back that far, and asked to it would strain against its own joint and shake there.
	const float HighShare = FMath::SmoothStep(AedirHandLedLowPitch, AedirHandLedHighPitch, Pitch);
	const float Outward = FMath::Lerp(AedirHandLedLowOutwardYaw, bTwoHandGrip ? AedirHandLedTwoHandOutwardYaw : AedirHandLedOutwardYaw, HighShare);
	// Both hands low across the body cross the arms over each other: they go less far that way down there.
	// One arm raised and across comes over the face and would rub against the own head: up there it goes less far across.
	const float RaisedShare = FMath::SmoothStep(AedirHandLedRaisedFromPitch, AedirHandLedRaisedFullPitch, Pitch);
	const float Across = bTwoHandGrip ? FMath::Lerp(AedirHandLedTwoHandLowAcrossYaw, AedirHandLedTwoHandAcrossYaw, HighShare) : FMath::Lerp(AedirHandLedAcrossYaw, AedirHandLedRaisedAcrossYaw, RaisedShare);
	const float Yaw = Side > 0.f ? FMath::Clamp(RawYaw, -Across, Outward) : FMath::Clamp(RawYaw, -Outward, Across);
	// The wheel sets how far out the hand is held, a quick throw takes it all the way, and round behind the body the arm
	// cannot reach as far.
	const float Out = FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend);
	const float Behind = FMath::Clamp((FMath::Abs(Yaw) - AedirHandLedBehindFromYaw) / AedirHandLedBehindSpanYaw, 0.f, 1.f);
	const float Reach = FMath::Lerp(AedirHandLedNearCm, AedirHandLedFarCm, Out) - Behind * AedirHandLedBehindShorterCm;
	FVector Hand3 = ComputeHandLedPivotLocal(Hand) + FRotator(Pitch, Yaw, 0.f).Vector() * Reach;
	Hand3.Z -= Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopCm : 0.f;
	return Hand3;
}

FVector UAedirCombatComponent::ComputeHandLedBladeDir(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const
{
	FVector KeyHand;
	FVector KeyBlade;
	if (ComputeKeyPose(Hand, Arm, KeyHand, KeyBlade))
	{
		// A heavy or tired blade sinks a little from where the pose holds it.
		const float Sink = (Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopPitch : 0.f) + (Item ? Item->Weight * (bTwoHandGrip ? 0.5f : 1.f) * AedirWeaponWeightDroopPerKg : 0.f);
		FRotator Turn = KeyBlade.Rotation();
		Turn.Pitch -= Sink;
		return FRotator(0.0, Creature->GetActorRotation().Yaw, 0.0).RotateVector(Turn.Vector()).GetSafeNormal();
	}
	// The blade comes out of the fist at an angle above the line of the arm: up from a hand held low or to the side, back
	// over the head from a hand raised overhead. Swinging the hand round, its weight trails and swings on through the wrist.
	const FVector ArmLine = (ComputeHandLedHandLocal(Hand, Arm) - ComputeHandLedPivotLocal(Hand)).GetSafeNormal();
	// "Above the arm" is straight up for an arm held out, and back over the head for one raised: the two are blended as the
	// arm rises, so the blade never has to find a new side to stand on from one frame to the next.
	const float Upright = FMath::SmoothStep(AedirHandLedUprightFromZ, AedirHandLedUprightFullZ, static_cast<float>(ArmLine.Z));
	FVector Up = FVector::VectorPlaneProject(FVector::UpVector * (1.f - Upright) - FVector::ForwardVector * Upright, ArmLine).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = -FVector::ForwardVector;
	}
	const float Droop = (Item && Item->bTwoHanded && !bTwoHandGrip ? AedirOneHandedDroopPitch : 0.f) + (Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopPitch : 0.f)
		+ (Item ? Item->Weight * (bTwoHandGrip ? 0.5f : 1.f) * AedirWeaponWeightDroopPerKg : 0.f);
	const float Raise = FMath::DegreesToRadians(FMath::Max(AedirHandLedBladeRaise - Droop, 0.f));
	const FVector Extended = ArmLine * FMath::Cos(Raise) + Up * FMath::Sin(Raise);

	// Drawn back round the body or up over the head, the wrist cocks and the blade lies back behind the shoulder: a blow
	// from there turns it through a wide arc about the hand, not just along with it.
	const FVector HandLocal = ComputeHandLedHandLocal(Hand, Arm);
	const float HandYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(ArmLine.Y), static_cast<float>(ArmLine.X)));
	const float HandPitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(ArmLine.Z), -1.f, 1.f)));
	// Only out to its own side does the wrist cock the blade back: brought across the body the blade would cock back into the
	// own head and neck, so there it stays over the arm.
	const float OutwardYaw = HandYaw * (Hand == EAedirHand::Right ? 1.f : -1.f);
	// Raised, it cocks back behind the head however far across the hand is: that keeps it clear of the head.
	const float RaisedCock = FMath::SmoothStep(AedirCockFromPitch, AedirCockFullPitch, HandPitch);
	const float Cock = CVarAedirSwingCock.GetValueOnGameThread() == 0 ? 0.f : FMath::Max(FMath::SmoothStep(AedirCockFromYaw, AedirCockFullYaw, OutwardYaw), RaisedCock);
	float Elevation = FMath::Lerp(AedirCockLowElevation, AedirCockShoulderElevation, FMath::SmoothStep(AedirCockLowPitch, AedirCockShoulderPitch, HandPitch));
	Elevation = FMath::Lerp(Elevation, AedirCockOverheadElevation, FMath::SmoothStep(AedirCockOverheadFromPitch, AedirCockFullPitch, HandPitch)) - Droop * 0.5f;
	const float HandSideShare = static_cast<float>(HandLocal.Y / (FMath::Abs(HandLocal.Y) + 15.0));
	const FVector Back = FVector(-1.f, HandSideShare * AedirCockSideShare, 0.f).GetSafeNormal();
	const float ElevationRadians = FMath::DegreesToRadians(Elevation);
	const FVector Cocked = Back * FMath::Cos(ElevationRadians) + FVector::UpVector * FMath::Sin(ElevationRadians);
	FVector Local = Extended * (1.f - Cock) + Cocked * Cock;
	if (!Local.Normalize())
	{
		Local = FVector::UpVector;
	}

	// Near the middle of the mouse's travel the point is kept on the enemy, only leaning the way the mouse goes; past a ring
	// around it the blade turns out and back into the wide swings. The ring is the same for every direction.
	const float Travel = static_cast<float>(Arm.PoseAimDeg.Size());
	const float Inner = 1.f - FMath::SmoothStep(AedirPointOnEnemyToDeg, AedirPointOnEnemyFromDeg, Travel);
	if (Inner > 0.f && CVarAedirPointZone.GetValueOnGameThread() != 0)
	{
		const FVector OnEnemy = FRotator(AedirPointOnEnemyPitch + Arm.PoseAimDeg.Y * AedirPointOnEnemyLeanShare - Droop * 0.5f, Arm.PoseAimDeg.X * AedirPointOnEnemyLeanShare, 0.0).Vector();
		Local = FMath::Lerp(Local, OnEnemy, Inner).GetSafeNormal();
	}
	return FRotator(0.0, Creature->GetActorRotation().Yaw, 0.0).RotateVector(Local).GetSafeNormal();
}

FVector2D UAedirCombatComponent::ComputeSwingBladeKey(EAedirHand Hand, const AAedirItemActor* Item, float Travel) const
{
	// The first half of a move to the side mostly takes the hands along, then the blade opens outwards and at the end of the mouse travel it lies behind the body.
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float SwingTravel = FMath::Abs(Travel);
	const float SwingSign = FMath::Sign(Travel);
	const float MidShare = CombatSmoothStep01(SwingTravel / AedirWeaponOpenStart);
	const float Open = CombatSmoothStep01((SwingTravel - AedirWeaponOpenStart) / (1.f - AedirWeaponOpenStart));
	const bool bLong = IsPolearm(Item);
	const float FightShare = GetFightShare();
	const float Upright = FMath::Lerp(bLong ? AedirPolearmGuardPitch : AedirWeaponGuardPitch, bLong ? AedirPolearmFightPitch : AedirWeaponFightPitch, FightShare);
	const float BeforeOpen = FMath::Lerp(Upright, bLong ? AedirPolearmSwingPitch : AedirWeaponMidPitch, MidShare);
	// An arm going across the body lies along the way of the blade, so there it has to stand up further.
	const float OverPitch = SwingSign * HandSide < 0.f ? AedirWeaponOpenOverAcrossPitch : AedirWeaponOpenOverPitch;
	const float Pitch = FMath::Min(FMath::Lerp(BeforeOpen, AedirWeaponBackPitch, Open) + FMath::Sin(Open * UE_PI) * OverPitch, AedirWeaponOpenMaxPitch);
	const float RestInward = bLong ? AedirPolearmInwardYaw : AedirWeaponGuardInwardYaw * (1.f - FightShare);
	const float SwingYaw = SwingSign * FMath::Lerp(AedirWeaponTipOutYaw * MidShare, AedirWeaponBackYaw, Open) - HandSide * RestInward * (1.f - Open);
	return FVector2D(Pitch, SwingYaw);
}

namespace
{
	// The poses of a fighter holding a pole weapon in both hands, read off the reference stills: where the mouse is, where the
	// middle between the hands is in the body's frame (forward, right, up in cm) and which way the head of the weapon points
	// (pitch, yaw in degrees from the body's forward). Written for the right hand leading; the left mirrors it.
	struct FAedirPolearmKey
	{
		float AimX;
		float AimY;
		FVector Middle;
		float HeadPitch;
		float HeadYaw;
	};
	const FAedirPolearmKey AedirPolearmKeys[] =
	{
		// Before the body the head is up and on the enemy; raised it stands upright before the right side, the rear hand by
		// the hip and the upper before the chest (stills 34, 35).
		{ 0.f, 0.f, FVector(32.f, 4.f, 8.f), 35.f, 0.f },
		{ 0.f, 40.f, FVector(26.f, 4.f, 18.f), 75.f, 0.f },
		{ 20.f, 40.f, FVector(24.f, 10.f, 20.f), 75.f, 20.f },
		{ 80.f, 0.f, FVector(28.f, 16.f, 10.f), 20.f, 60.f },
		{ -80.f, 0.f, FVector(28.f, -10.f, 10.f), 20.f, -60.f },
		// Up on the right the pole rises from the right hand at the chest forward across to the left (still 36); up over the
		// left shoulder the head reaches forward and out to the left (still 37); up to the left it stands out aslant.
		{ 50.f, 60.f, FVector(20.f, 10.f, 22.f), 35.f, -35.f },
		{ -50.f, 60.f, FVector(12.f, -10.f, 38.f), 25.f, -35.f },
		{ -20.f, 80.f, FVector(18.f, 0.f, 38.f), 45.f, -90.f },
		// Over the head: upright, or reaching forward from the raised hands, steeply or flat (stills 39-41).
		{ 0.f, 95.f, FVector(14.f, 2.f, 48.f), 80.f, 180.f },
		{ 30.f, 90.f, FVector(26.f, 2.f, 32.f), 55.f, 0.f },
		{ 60.f, 85.f, FVector(16.f, 4.f, 46.f), 20.f, 10.f },
		// Both hands up over the head, the pole lying across it with the head out to the left (still 42).
		{ -70.f, 75.f, FVector(10.f, 0.f, 60.f), 20.f, -80.f },
		// Low: drawn back by the hip on either side, or brought down on the ground ahead.
		{ 80.f, -30.f, FVector(20.f, 20.f, 0.f), -15.f, 150.f },
		{ -80.f, -30.f, FVector(20.f, -16.f, 0.f), -15.f, -150.f },
		{ 0.f, -60.f, FVector(40.f, 2.f, 0.f), -45.f, 0.f },
	};
	// As for the sword: what each pose asks further (pitch, yaw), measured in the game, so the head comes to rest where the pose has it.
	const FVector2D AedirPolearmKeyFix[] =
	{
		FVector2D(0, 13), FVector2D(2, 30), FVector2D(-3, 30), FVector2D(-15, 16), FVector2D(15, 4),
		FVector2D(-15, 11), FVector2D(-14, -3), FVector2D(-11, 0), FVector2D(-10, -15), FVector2D(10, -19),
		FVector2D(-8, -9), FVector2D(-15, 29), FVector2D(-15, 28), FVector2D(11, 1), FVector2D(15, -30),
	};
	static_assert(UE_ARRAY_COUNT(AedirPolearmKeyFix) == UE_ARRAY_COUNT(AedirPolearmKeys), "One correction for each pole key pose.");
	// A pole thrust, as in Half Sword: drawn back the rear hand is behind the hip and the front one at the chest, the pole level
	// at the waist pointing ahead; out, both arms are straight ahead at shoulder height and the pole level from them (asked a
	// little higher than level: the weight of the head takes it down that far).
	// Where the rear hand of a pole weapon is (forward, right, up from the middle of the hips), and how far it moves raised,
	// right up, low, across to the left and out to the right.
	const FVector AedirPoleRearHandRest(18.f, 10.f, -2.f);
	constexpr float AedirPoleRearHandUpFrom = 30.f;
	constexpr float AedirPoleRearHandUpFull = 70.f;
	const FVector AedirPoleRearHandUp(2.f, -2.f, 24.f);
	const FVector AedirPoleRearHandHigh(-6.f, -4.f, 36.f);
	const FVector AedirPoleRearHandLow(4.f, 0.f, -4.f);
	const FVector AedirPoleRearHandAcross(8.f, -10.f, 12.f);
	const FVector AedirPoleRearHandOut(-10.f, 6.f, 0.f);
	static TAutoConsoleVariable<int32> CVarAedirPoleRearHand(TEXT("aedir.PoleRearHand"), 1, TEXT("A pole weapon turns about its rear hand held by the body (1), or both hands go where the key poses have them (0)."));
	const FAedirPolearmKey AedirPolearmThrustBack = { 0.f, 0.f, FVector(14.f, 16.f, 8.f), 30.f, -4.f };
	const FAedirPolearmKey AedirPolearmThrustOut = { 0.f, 0.f, FVector(50.f, 4.f, 34.f), 14.f, 4.f };
}

void UAedirCombatComponent::ComputePolearmPose(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item, FVector& OutHandLocal, FVector& OutDirLocal) const
{
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	if (CVarAedirKeyPoses.GetValueOnGameThread() != 0 && bThrustMode)
	{
		// A thrust with a pole: from level by the hip straight out ahead at the shoulders.
		const float Out = FMath::Clamp(Arm.Extension, 0.f, 1.f);
		FVector Middle = FMath::Lerp(AedirPolearmThrustBack.Middle, AedirPolearmThrustOut.Middle, Out);
		FVector Head = FRotator(FMath::Lerp(AedirPolearmThrustBack.HeadPitch, AedirPolearmThrustOut.HeadPitch, Out), FMath::Lerp(AedirPolearmThrustBack.HeadYaw, AedirPolearmThrustOut.HeadYaw, Out) + Arm.PoseAimDeg.X * HandSide * AedirThrustSteerShare, 0.f).Vector();
		if (HandSide < 0.f)
		{
			Middle.Y = -Middle.Y;
			Head.Y = -Head.Y;
		}
		OutDirLocal = Head;
		OutHandLocal = Middle - OutDirLocal * ((GetSecondaryGripLocalCm(Item) - Item->GripLocalCm) * 0.5f);
		return;
	}
	if (CVarAedirKeyPoses.GetValueOnGameThread() != 0 && GetFightShare() > 0.f)
	{
		// The same blend as the sword's poses, by inverse distance in the mouse's travel.
		const FVector2D Aim(Arm.PoseAimDeg.X * HandSide, Arm.PoseAimDeg.Y);
		FVector MiddleSum = FVector::ZeroVector;
		FVector HeadSum = FVector::ZeroVector;
		double WeightSum = 0.0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AedirPolearmKeys); ++Index)
		{
			const FAedirPolearmKey& Key = AedirPolearmKeys[Index];
			const double Distance = FVector2D::Distance(Aim, FVector2D(Key.AimX, Key.AimY));
			const FVector Head = FRotator(Key.HeadPitch + AedirFixShare() * AedirPolearmKeyFix[Index].X, Key.HeadYaw + AedirFixShare() * AedirPolearmKeyFix[Index].Y, 0.f).Vector();
			if (Distance < 0.5)
			{
				MiddleSum = Key.Middle;
				HeadSum = Head;
				WeightSum = 1.0;
				break;
			}
			const double Weight = 1.0 / FMath::Pow(Distance, static_cast<double>(AedirKeyPosePower));
			MiddleSum += Key.Middle * Weight;
			HeadSum += Head * Weight;
			WeightSum += Weight;
		}
		FVector Middle = MiddleSum / WeightSum;
		FVector Head = (CVarAedirSteerBlade.GetValueOnGameThread() & 2) != 0
			? AedirBendBlade(AedirPoleSteer(), Aim, UE_ARRAY_COUNT(AedirPolearmKeys), [](int32 Index) { return FVector2D(AedirPolearmKeys[Index].AimX, AedirPolearmKeys[Index].AimY); },
				[](int32 Index) { return FRotator(AedirPolearmKeys[Index].HeadPitch + AedirFixShare() * AedirPolearmKeyFix[Index].X, AedirPolearmKeys[Index].HeadYaw + AedirFixShare() * AedirPolearmKeyFix[Index].Y, 0.f).Vector(); })
			: HeadSum.GetSafeNormal();
		if (Head.IsNearlyZero())
		{
			Head = FVector::UpVector;
		}
		if (CVarAedirPoleRearHand.GetValueOnGameThread() != 0)
		{
			// As in Half Sword the rear hand stays by the body and the pole turns about it: by the hip at rest, low and stood up, up to the
			// chest raised and over the head at the top, across to the other side of the chest when the head goes over there.
			// The hands then always lie along the pole wherever it points, so neither arm is asked for a place it cannot reach.
			const float AimX = static_cast<float>(Aim.X);
			const float AimY = static_cast<float>(Aim.Y);
			// Stood up before the body the rear hand is still by the hip (stills 34, 35); it comes up to the chest only higher.
			const float Up = FMath::SmoothStep(AedirPoleRearHandUpFrom, AedirPoleRearHandUpFull, AimY);
			const float High = FMath::SmoothStep(40.f, 90.f, AimY);
			const float Low = FMath::SmoothStep(0.f, 60.f, -AimY);
			const float Across = FMath::SmoothStep(0.f, 80.f, -AimX);
			const float Out = FMath::SmoothStep(0.f, 80.f, AimX);
			const FVector Rear = AedirPoleRearHandRest + AedirPoleRearHandUp * Up + AedirPoleRearHandHigh * High + AedirPoleRearHandAcross * Across + AedirPoleRearHandOut * Out + AedirPoleRearHandLow * Low;
			Middle = Rear + Head * ((GetSecondaryGripLocalCm(Item) - Item->GripLocalCm) * 0.5f);
		}
		const float Fatigue = Creature->Stats ? Creature->Stats->GetFatigue() : 0.f;
		Middle.Z -= Fatigue * AedirFatigueDroopCm;
		Middle += FVector(Head.X, Head.Y, 0.f).GetSafeNormal() * (AedirKeyPoseReachCm * FMath::Max(FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend) - 0.5f, 0.f) * 2.f);
		if (HandSide < 0.f)
		{
			Middle.Y = -Middle.Y;
			Head.Y = -Head.Y;
		}
		// Out of the fight the pole rests as before; in it the key pose takes over as the stance comes up.
		FVector RestHand;
		FVector RestDir;
		ComputePolearmPoseFromAngles(Hand, Arm, Item, RestHand, RestDir);
		const float Share = CombatSmoothStep01(GetFightShare());
		OutDirLocal = FMath::Lerp(RestDir, Head, Share).GetSafeNormal();
		OutHandLocal = FMath::Lerp(RestHand, Middle - OutDirLocal * ((GetSecondaryGripLocalCm(Item) - Item->GripLocalCm) * 0.5f), Share);
		return;
	}
	ComputePolearmPoseFromAngles(Hand, Arm, Item, OutHandLocal, OutDirLocal);
}

void UAedirCombatComponent::ComputePolearmPoseFromAngles(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item, FVector& OutHandLocal, FVector& OutDirLocal) const
{
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float AimY = static_cast<float>(Arm.PoseAimDeg.Y);
	const float Travel = FMath::Clamp(static_cast<float>(Arm.PoseAimDeg.X) / AedirWeaponYawLimit, -1.f, 1.f);
	const float SwingShare = FMath::Abs(Travel);
	const float FightShare = GetFightShare();
	const float RaiseShare = CombatRaiseShare(AimY, AedirWeaponRaiseFullDegrees);
	const float Fatigue = Creature->Stats ? Creature->Stats->GetFatigue() : 0.f;
	const float Droop = Fatigue * AedirFatigueDroopPitch + Item->Weight * 0.5f * AedirWeaponWeightDroopPerKg;

	// The shaft swings around the body from over one shoulder to over the other, flattening as it goes out.
	const float RestPitch = FMath::Lerp(AedirPolearmGuardPitch, AedirPolearmFightPitch, FightShare);
	const float SwingPitch = FMath::Lerp(RestPitch, AedirPolearmSwingPitch, SwingShare) + FMath::Min(AimY, 0.f) * AedirPolearmLowerPitchShare - Droop;
	const float Pitch = FMath::Lerp(SwingPitch, AedirPolearmRaisedPitch, RaiseShare);
	const float SwingYaw = Travel * AedirPolearmSwingYaw - HandSide * AedirPolearmInwardYaw * (1.f - SwingShare);
	const float Yaw = SwingYaw * FMath::Lerp(1.f, AedirPolearmRaisedYawShare, RaiseShare);
	OutDirLocal = FRotator(Pitch, Yaw, 0.0).Vector();

	// The point between the hands goes along around the body on a much shorter way, and the lower hand comes from there, opposite the head of the weapon.
	const float PivotRadians = FMath::DegreesToRadians(Travel * AedirPolearmPivotYaw);
	const float Forward = FMath::Lerp(AedirPolearmPivotForwardCm, AedirPolearmPivotSwingCm, SwingShare) + Arm.PunchBlend * AedirPolearmPivotThrowCm;
	const FVector Swing(
		Forward * FMath::Cos(PivotRadians),
		Forward * FMath::Sin(PivotRadians) + HandSide * AedirPolearmPivotSideCm * (1.f - SwingShare),
		FMath::Lerp(AedirPolearmPivotHeightCm, AedirPolearmPivotFightHeightCm, FightShare) + FMath::Min(AimY, 0.f) * AedirPolearmPivotLowerPerDegree - Fatigue * AedirFatigueDroopCm);
	const FVector Raised(AedirPolearmRaisedForwardCm, HandSide * AedirWeaponRaisedSideTwoHandCm, AedirPolearmRaisedHeightCm);
	OutHandLocal = FMath::Lerp(Swing, Raised, RaiseShare) - OutDirLocal * ((GetSecondaryGripLocalCm(Item) - Item->GripLocalCm) * 0.5f);
}

FVector UAedirCombatComponent::ComputeCarriedDir(EAedirHand Hand) const
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const float Length = Item ? Item->GetWeaponLength() : 0.f;
	// Out of the fight stance a weapon is not pointed at anyone. A pole stands upright beside the body, head up; a blade in
	// both hands lies aslant before the hips, point low and out to the side of the carrying hand; one in a single hand hangs
	// point down and a little out. A long one is kept just off the ground.
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	float RestPitch = bTwoHandGrip ? AedirRestTwoHandPitch : AedirRestOneHandPitch;
	float RestYaw = Side * (bTwoHandGrip ? AedirRestTwoHandOutYaw : AedirRestOneHandOutYaw);
	if (bTwoHandGrip)
	{
		// The arms and the second hand turn the blade back towards the front: the pose asks this much further round.
		RestYaw += Side * AedirRestTwoHandFixYaw;
	}
	if (IsPolearm(Item))
	{
		RestPitch = AedirRestPolePitch;
		RestYaw = Side * AedirRestPoleOutYaw;
		// Walking, a pole is carried low and level, the head ahead (still 33); standing it is set upright (still 31).
		const float Walking = FMath::SmoothStep(AedirCarryPoleWalkFromSpeed, AedirCarryPoleWalkFullSpeed, static_cast<float>(Creature->GetVelocity().Size2D()));
		RestPitch = FMath::Lerp(RestPitch, AedirCarryPoleWalkingPitch, Walking);
		RestYaw = FMath::Lerp(RestYaw, -Side * AedirCarryPoleWalkingInYaw, Walking);
	}
	else if (Item && Item->IsHafted() && Length < AedirCarryHaftedMaxCm)
	{
		// An axe or a mace rests level by the hip, its head across before the body (still 32).
		RestPitch = AedirCarryHaftedPitch;
		RestYaw = -Side * AedirCarryHaftedInYaw;
	}
	else if (Length > KINDA_SMALL_NUMBER)
	{
		const float GroundPitch = -FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(AedirRestHandHeightCm / Length, 0.f, 1.f)));
		RestPitch = FMath::Max(RestPitch, GroundPitch);
		// The ground is kept from where the blade comes to rest, not from how far round the pose asks.
		RestPitch += bTwoHandGrip ? AedirRestTwoHandFixPitch : 0.f;
	}
	const FVector Carried = FRotator(RestPitch, Creature->GetActorRotation().Yaw + RestYaw, 0.0).Vector();
	// Walking and running the carrying arm swings, and a blade held at one angle to the world would bend the wrist over at
	// each end of the swing: on the move the blade goes partly along with the forearm, so the wrist stays near straight.
	const USkeletalMeshComponent* Body = Creature->GetMesh();
	const float Moving = FMath::SmoothStep(AedirCarryAimFadeFromSpeed, AedirCarryAimFadeFullSpeed, static_cast<float>(Creature->GetVelocity().Size2D()));
	// A pole is carried level and does not go along with the forearm.
	if (Body && Moving > 0.f && !IsPolearm(Item))
	{
		const TCHAR* Suffix = Hand == EAedirHand::Left ? TEXT("_l") : TEXT("_r");
		const FVector Forearm = (Body->GetBoneLocation(FName(*FString::Printf(TEXT("hand%s"), Suffix))) - Body->GetBoneLocation(FName(*FString::Printf(TEXT("lowerarm%s"), Suffix)))).GetSafeNormal();
		const FVector Along = (Carried + Forearm * (Moving * AedirCarryForearmShare)).GetSafeNormal();
		if (!Along.IsNearlyZero())
		{
			return Along;
		}
	}
	return Carried;
}

float UAedirCombatComponent::GetHalfSwordForwardDegrees(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	// The mouse towards the side of the carrying hand swings the point round to the enemy; the other way it goes back past the far shoulder. Mouse down also brings it forward.
	const float SwingDegrees = static_cast<float>(Arm.PoseAimDeg.X) * (Hand == EAedirHand::Right ? AedirHalfSwordSwingShare : -AedirHalfSwordSwingShare);
	const float DownDegrees = FMath::Max(-static_cast<float>(Arm.PoseAimDeg.Y), 0.f);
	return FMath::Clamp(AedirHalfSwordRestForward + SwingDegrees + DownDegrees * AedirHalfSwordForwardPerDownDegree, AedirHalfSwordMinForward, AedirHalfSwordMaxForward);
}

FVector UAedirCombatComponent::ComputeHalfSwordDir(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	// The blade lies across the chest towards the second hand and turns around the body like a door.
	const float SecondSide = Hand == EAedirHand::Right ? -1.f : 1.f;
	const float ForwardDegrees = GetHalfSwordForwardDegrees(Hand, Arm);
	const float ForwardRadians = FMath::DegreesToRadians(ForwardDegrees);
	const float RaisedShare = FMath::Clamp(static_cast<float>(Arm.PoseAimDeg.Y) / AedirHalfSwordRaisedPitch, 0.f, 1.f);
	const float RiseRadians = FMath::DegreesToRadians(AedirHalfSwordRestRise + FMath::Max(ForwardDegrees - AedirHalfSwordRestForward, 0.f) * AedirHalfSwordRisePerForward + FMath::Max(AedirHalfSwordRestForward - ForwardDegrees, 0.f) * AedirHalfSwordRisePerBack + RaisedShare * AedirHalfSwordRaisedRise);
	const FVector Level = Creature->GetActorRightVector() * (SecondSide * FMath::Cos(ForwardRadians)) + Creature->GetActorForwardVector() * FMath::Sin(ForwardRadians);
	return Level * FMath::Cos(RiseRadians) + FVector::UpVector * FMath::Sin(RiseRadians);
}

FVector UAedirCombatComponent::ComputeHalfSwordHand(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const
{
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float AimY = static_cast<float>(Arm.PoseAimDeg.Y);
	const float RaisedShare = FMath::Clamp(AimY / AedirHalfSwordRaisedPitch, 0.f, 1.f);
	const float Height = AedirHalfSwordCenterHeightCm + AimY * (AimY > 0.f ? AedirHalfSwordRaisePerDegree : AedirHalfSwordLowerPerDegree);
	// The middle of the weapon turns around the body with the blade, a little less far so the far arm still reaches it.
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float ArcRadians = FMath::DegreesToRadians((GetHalfSwordForwardDegrees(Hand, Arm) - AedirHalfSwordRestForward) * AedirHalfSwordArcShare);
	float Radius = FMath::Lerp(FMath::Lerp(AedirHalfSwordCenterForwardCm, AedirHalfSwordArcEndCm, FMath::Abs(FMath::Sin(ArcRadians))), AedirHalfSwordRaisedForwardCm, RaisedShare);
	if (bThrustMode)
	{
		Radius += (FMath::Clamp(Arm.Extension, 0.f, 1.f) - 0.5f) * AedirHalfSwordThrustCm;
	}
	const FTransform& Body = Creature->GetActorTransform();
	const FVector CenterLocal = FVector(Radius * FMath::Cos(ArcRadians), HandSide * Radius * FMath::Sin(ArcRadians), Height) * Scale;

	// The carrying hand is half the distance between the hands below the middle, the other one the same distance above it.
	const float HandsApartCm = GetSecondaryGripLocalCm(Item) - (Item->GripLocalCm + Item->HeldGripShiftCm);
	FVector HandLocal = CenterLocal - Body.InverseTransformVectorNoScale(ComputeWeaponTargetDir(Hand, Arm)) * (HandsApartCm * 0.5f * Scale);

	// It would come in against the ribs as the point goes forward, and hold the whole turn back, so it eases out to a distance from the body.
	const float FromBody = static_cast<float>(HandLocal.Size2D());
	const float Clear = AedirHalfSwordBodyClearCm * Scale;
	const float Soft = AedirHalfSwordBodyClearSoftCm * Scale;
	const float Kept = (FromBody + Clear + FMath::Sqrt(FMath::Square(FromBody - Clear) + Soft * Soft)) * 0.5f;
	const FVector2D Outward = FVector2D(HandLocal.X, HandLocal.Y).GetSafeNormal();
	HandLocal.X = Outward.IsNearlyZero() ? Kept : Outward.X * Kept;
	HandLocal.Y = Outward.Y * Kept;
	return Body.TransformPositionNoScale(HandLocal);
}

FVector UAedirCombatComponent::ComputeStanceHandLocal(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	const FVector2D Aim = Arm.PoseAimDeg;
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float FightShare = GetFightShare();

	// Held in both hands but not raised to fight, the weapon is carried low before the hips, the hands together there; it
	// comes up into the stance as the guard is raised.
	const float RestShare = bTwoHandGrip ? 1.f - FightShare : 0.f;
	const FVector Resting(AedirRestTwoHandForwardCm, HandSide * AedirRestTwoHandSideCm, AedirRestTwoHandHeightCm);

	if (bTwoHandGrip && IsPolearm(Item))
	{
		FVector PolearmHand;
		FVector PolearmDir;
		ComputePolearmPose(Hand, Arm, Item, PolearmHand, PolearmDir);
		return FMath::Lerp(PolearmHand, Resting + FVector(0.f, HandSide * AedirRestPoleSideCm, AedirRestPoleRaiseCm), RestShare);
	}

	if (UsesHandLedSwing(Hand))
	{
		return FMath::Lerp(ComputeHandLedHandLocal(Hand, Arm), Resting, RestShare);
	}

	// Where the hand is with the mouse in the middle.
	FVector Rest;
	if (bTwoHandGrip)
	{
		Rest = FMath::Lerp(AedirTwoHandRest, AedirTwoHandFight, FightShare);
	}
	else
	{
		const float RestPitch = FMath::DegreesToRadians(FMath::Lerp(AedirWeaponHandPitch, AedirWeaponFightHandPitch, FightShare));
		const float ChestHeight = AedirWeaponChestHeightCm - (Item && Item->bTwoHanded ? AedirOneHandedDroopCm : 0.f);
		Rest = FVector(AedirWeaponHandReachCm * FMath::Cos(RestPitch), HandSide * AedirWeaponChestSideCm, ChestHeight + AedirWeaponHandReachCm * FMath::Sin(RestPitch));
	}
	Rest.X += Arm.PunchBlend * AedirSwingThrowCm;
	Rest.Z += FMath::Min(static_cast<float>(Aim.Y), 0.f) * AedirWeaponLowerHandPerDegree - (Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopCm : 0.f);

	// One smooth curve through the poses of the swing, so the hand keeps moving as long as the arm does.
	const FVector SwingHand = CombatSwingSpline<FVector>(FMath::Clamp(static_cast<float>(Aim.X) / AedirWeaponYawLimit, -1.f, 1.f), [this, Hand, &Rest](float Travel) { return ComputeSwingHandKey(Hand, Rest, Travel); });

	const float RaiseShare = CombatRaiseShare(static_cast<float>(Aim.Y), AedirWeaponRaiseFullDegrees);
	const FVector Raised = FRotator(0.0, Aim.X * AedirWeaponRaisedYawShare, 0.0).RotateVector(FVector(AedirWeaponRaisedForwardCm, HandSide * (bTwoHandGrip ? AedirWeaponRaisedSideTwoHandCm : AedirWeaponRaisedSideCm), AedirWeaponRaisedHeightCm));
	return FMath::Lerp(SwingHand, Raised, RaiseShare);
}

FVector UAedirCombatComponent::ComputeSwingHandKey(EAedirHand Hand, const FVector& Rest, float Travel) const
{
	// The hand goes out around the body on a wide arc to the side, then back and up beside the shoulder.
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float SwingSign = FMath::Sign(Travel);
	const float SwingTravel = FMath::Abs(Travel);
	const float MidShare = CombatSmoothStep01(SwingTravel / AedirWeaponOpenStart);
	const float EndShare = CombatSmoothStep01((SwingTravel - AedirWeaponOpenStart) / (1.f - AedirWeaponOpenStart));
	const bool bAcrossBody = !bTwoHandGrip && SwingSign * HandSide < 0.f;
	const float MidRadius = bTwoHandGrip || bAcrossBody ? AedirSwingMidRadiusTwoHandCm : AedirSwingMidRadiusCm;
	const float Radius = FMath::Lerp(FMath::Lerp(static_cast<float>(Rest.X), MidRadius, MidShare), AedirSwingEndRadiusCm, EndShare);
	const float YawRadians = FMath::DegreesToRadians(FMath::Lerp(AedirSwingMidYaw * MidShare, AedirSwingEndYaw, EndShare));
	return FVector(
		Radius * FMath::Cos(YawRadians) + (bAcrossBody ? AedirSwingAcrossForwardCm * EndShare : 0.f),
		SwingSign * Radius * FMath::Sin(YawRadians) + Rest.Y * (1.f - MidShare),
		FMath::Lerp(FMath::Lerp(static_cast<float>(Rest.Z), AedirSwingMidHeightCm, MidShare), AedirSwingEndHeightCm, EndShare));
}

bool UAedirCombatComponent::IsPolearm(const AAedirItemActor* Item)
{
	return Item && Item->bPolearm;
}

bool UAedirCombatComponent::GetElbowHint(EAedirHand Hand, FVector& OutLocal, float& OutAlpha) const
{
	const EAedirHand AimHand = IsSecondaryGripHand(Hand) && IsGripHeld() ? GetGripHand() : Hand;
	const FAedirArmDynamics& Arm = GetArm(AimHand);
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;

	// A fist comes up along the ribs with the elbow low, out only for a hook.
	if (!GetHeldItem(Hand) && IsGuardActive(Hand) && !Arm.bScriptedControl)
	{
		const float Outward = FMath::Clamp(static_cast<float>(Arm.AimOffsetDeg.X) * Side / AedirArmOutwardYawLimit, 0.f, 1.f);
		OutLocal = FMath::Lerp(FVector(-0.2f, Side * 0.35f, -0.95f), FVector(-0.4f, Side * 0.8f, -0.45f), Outward).GetSafeNormal();
		OutAlpha = 1.f;
		return true;
	}

	if (!IsWeaponStance(AimHand) || Arm.bScriptedControl)
	{
		return false;
	}

	// On a long shaft both elbows stay low and close, the lower one drawn back past the waist.
	if (bTwoHandGrip && IsPolearm(GetHeldItem(AimHand)) && !bThrustMode)
	{
		const FVector Low = Hand == AimHand ? FVector(-0.75f, Side * 0.5f, -0.45f) : FVector(-0.1f, Side * 0.65f, -0.75f);
		OutLocal = FMath::Lerp(Low, FVector(0.25f, Side * 0.85f, -0.1f), CombatRaiseShare(static_cast<float>(Arm.PoseAimDeg.Y), AedirWeaponRaiseFullDegrees)).GetSafeNormal();
		OutAlpha = 1.f;
		return true;
	}

	const float WindShare = CombatSmoothStep01((FMath::Abs(static_cast<float>(Arm.PoseAimDeg.X)) / AedirWeaponYawLimit - AedirWeaponOpenStart) / (1.f - AedirWeaponOpenStart));
	// Wound up to the far side across the body, the elbow comes forward and down in front of the chest: pointing it out to its
	// own side there would wring the arm round at the shoulder.
	const bool bAcrossBody = !bTwoHandGrip && static_cast<float>(Arm.PoseAimDeg.X) * Side < 0.f;
	// Held before the body the elbows hang down by the ribs (stills 17, 20, 24, 29); they go out and up only as the hands are
	// raised over the shoulders (stills 54-57).
	const FVector WoundUp = bAcrossBody ? FVector(0.6f, Side * 0.15f, -0.6f) : FVector(0.1f, Side * 0.6f, -0.75f);
	FVector Hint = FMath::Lerp(FVector(-0.3f, Side * 0.45f, -0.85f), WoundUp, WindShare);
	Hint = FMath::Lerp(Hint, FVector(0.35f, Side * 0.8f, 0.f), CombatRaiseShare(static_cast<float>(Arm.PoseAimDeg.Y) - AedirElbowRaiseFromDegrees, AedirWeaponRaiseFullDegrees - AedirElbowRaiseFromDegrees));
	Hint = FMath::Lerp(Hint, FVector(-0.25f, Side * 0.6f, -0.75f), CombatSmoothStep01(HalfSwordAlpha));
	if (bThrustMode)
	{
		Hint = FVector(-0.85f, Side * 0.25f, -0.45f);
	}
	OutLocal = Hint.GetSafeNormal();
	OutAlpha = 1.f;
	return true;
}

float UAedirCombatComponent::GetWristFirmShare(EAedirHand Hand) const
{
	// In the half-sword the blade lies further along the forearm of the carrying hand as it comes round to the front.
	return IsWeaponStance(Hand) && !bThrustMode && !(bTwoHandGrip && IsPolearm(GetHeldItem(Hand))) ? GetFightShare() * (1.f - AedirHalfSwordWristLoose * GetHalfSwordShare()) : 0.f;
}

float UAedirCombatComponent::GetHeldOutShare(EAedirHand Hand) const
{
	const EAedirHand AimHand = IsSecondaryGripHand(Hand) && IsGripHeld() ? GetGripHand() : Hand;
	if (!GetHeldItem(AimHand) || !IsWeaponStance(AimHand))
	{
		return 0.f;
	}
	const FAedirArmDynamics& Arm = GetArm(AimHand);
	const float Out = FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend);
	return FMath::Clamp((Out - AedirKeyPoseLevelFrom) / (1.f - AedirKeyPoseLevelFrom), 0.f, 1.f);
}

float UAedirCombatComponent::GetArmLedShare(EAedirHand Hand) const
{
	// A blade swung in one hand, or by the carrying hand of two, is pointed by the arm. A thrust lays it along the forearm
	// and a long shaft is steered by both hands, so those keep their own wrists.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Item || !IsWeaponStance(Hand) || IsSecondaryGripHand(Hand) || (bThrustMode && !UsesHandLedSwing(Hand)) || (bTwoHandGrip && IsPolearm(Item)) || GetArm(Hand).bScriptedControl)
	{
		return 0.f;
	}
	// Where the mouse leads the hand, the hand is already where it should be and the wrist points the blade: turning the
	// arm about the blade as well would move the hand off its place and point the blade somewhere else.
	if (UsesHandLedSwing(Hand) && CVarAedirArmLedWithHand.GetValueOnGameThread() == 0)
	{
		return 0.f;
	}
	return GetFightShare() * (1.f - GetHalfSwordShare());
}

float UAedirCombatComponent::GetHangWristDegrees(EAedirHand Hand) const
{
	const FVector Carried = ComputeCarriedDir(Hand);
	return 90.f - FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(-Carried.Z), -1.f, 1.f)));
}

void UAedirCombatComponent::UpdatePoseAim(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime, float StrengthFactor, float WeaponWeight)
{
	const UAedirStatsComponent* Stats = Creature->Stats.Get();
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const float Load = WeaponWeight * (Item && Item->bTwoHanded && !bTwoHandGrip ? AedirOneHandedStrainMass : 1.f) * (bTwoHandGrip ? 0.5f : 1.f);
	const float Tired = 1.f - (Stats ? Stats->GetFatigue() : 0.f) * AedirFatigueSpeedLoss;
	const float Power = (StrengthFactor / AedirPoseStrengthReference) * Tired / (1.f + Load * AedirPoseSpeedMassShare);
	const float MaxSpeed = AedirPoseMaxSpeed * Power;
	const float MaxAccel = AedirPoseMaxAccel * Power;
	const float Frequency = AedirPoseFrequency / FMath::Sqrt(1.f + Load * AedirPoseSpeedMassShare);
	const float DampingRatio = FMath::Clamp(AedirPoseDampingLight - Load * AedirPoseDampingPerKg, AedirPoseDampingHeavy, AedirPoseDampingLight);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaTime / AedirPoseSubstep));
	const float StepSeconds = DeltaTime / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FVector2D Push = (Arm.AimOffsetDeg - Arm.PoseAimDeg) * (Frequency * Frequency) - Arm.PoseAimSpeed * (2.f * DampingRatio * Frequency);
		const double PushSize = Push.Size();
		if (PushSize > MaxAccel)
		{
			Push *= MaxAccel / PushSize;
		}
		Arm.PoseAimSpeed += Push * StepSeconds;
		const double Speed = Arm.PoseAimSpeed.Size();
		if (Speed > MaxSpeed)
		{
			Arm.PoseAimSpeed *= MaxSpeed / Speed;
		}
		Arm.PoseAimDeg += Arm.PoseAimSpeed * StepSeconds;
	}

	// Throwing a weight around tires the arm and builds it.
	const bool bSwinging = Item && !IsSecondaryGripHand(Hand) && (IsWeaponStance(Hand) || (IsObjectGrip() && Hand == GetGripHand()));
	const float Speed = static_cast<float>(Arm.PoseAimSpeed.Size());
	if (IsAuthoritative() && Creature->Stats && bSwinging && Speed > AedirSwingEffortMinSpeed)
	{
		const float Travel = Speed * DeltaTime;
		Creature->Stats->ConsumeStaminaEffort(FMath::Min((Travel - AedirSwingEffortMinSpeed * DeltaTime) * AedirSwingStaminaPerTravel * (1.f + Load * AedirSwingStaminaMassShare), Creature->Stats->GetStamina()), false);
		Creature->Stats->AddStrengthExperience(Travel * AedirSwingStrengthPerTravel * (1.f + Load * AedirSwingStrengthMassShare));
	}
}

FVector UAedirCombatComponent::ComputeObjectCenter(const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const
{
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Distance = Item->GetHoldRadiusCm() + FMath::Lerp(AedirObjectBodyGapCm, AedirObjectThrowGapCm, Arm.PunchBlend) * Scale;
	const FVector Direction = FRotator(FMath::Min(AedirObjectRestPitch + Arm.AimOffsetDeg.Y, AedirObjectMaxPitch), Creature->GetActorRotation().Yaw + Arm.AimOffsetDeg.X, 0.0).Vector();
	return Creature->GetActorTransform().TransformPositionNoScale(FVector(0.f, 0.f, AedirObjectChestHeightCm * Scale)) + Direction * Distance;
}

void UAedirCombatComponent::UpdateWeaponDir(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime, float StrengthFactor, float WeaponWeight)
{
	FVector Target = IsWeaponStance(Hand) ? ComputeWeaponTargetDir(Hand, Arm) : ComputeCarriedDir(Hand);
	// Where the blade is wanted is already outside the own body: wanted inside it, the blade would be pulled in by its
	// weight and pushed back out every frame, and shake between the two.
	Target = ClearDirOfOwnBody(Hand, Arm.HandWorld, Target);
	// Nor is it wanted in the ground: lowered there it would be stopped by the ground and drop again, frame after frame.
	if (const UCapsuleComponent* Capsule = Creature->GetCapsuleComponent(); Capsule && IsWeaponStance(Hand))
	{
		const double GroundZ = Creature->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight() + AedirBladeAboveGroundCm;
		const double Length = GetEffectiveWeaponLength(Hand) * Creature->GetActorScale3D().X;
		const double Drop = Arm.HandWorld.Z - GroundZ;
		if (Length > KINDA_SMALL_NUMBER && Arm.HandWorld.Z + Target.Z * Length < GroundZ)
		{
			const double MinZ = FMath::Clamp(-Drop / Length, -1.0, 1.0);
			const FVector Flat = FVector(Target.X, Target.Y, 0.0).GetSafeNormal();
			if (!Flat.IsNearlyZero())
			{
				Target = Flat * FMath::Sqrt(FMath::Max(1.0 - MinZ * MinZ, 0.0)) + FVector::UpVector * MinZ;
			}
		}
	}
	if (const UAedirInteractionComponent* Interaction = Creature->Interaction.Get())
	{
		// The wrist turns the item towards how it hangs on the belt, and up for the other hand to take it.
		FVector HolsterDir;
		float HolsterShare = 0.f;
		if (Interaction->GetHolsterAim(Hand, HolsterDir, HolsterShare))
		{
			Target = FMath::Lerp(Target, HolsterDir, HolsterShare).GetSafeNormal();
		}
		else if (Interaction->IsPassingHeld(Hand))
		{
			Target = FRotator(AedirWeaponFightPitch, Creature->GetActorRotation().Yaw, 0.0).Vector();
		}
	}

	// In a fight the arm points the blade and the wrist does not give: the weight of the weapon is already in how slowly
	// the arm comes round, so the blade goes where the arm takes it and nowhere else.
	// The mouse moving the hand, the blade's own weight carries it: it trails a quick move and swings on past the end of it.
	if (Arm.WeaponDir.IsNearlyZero() || (GetArmLedShare(Hand) > 0.f && !(IsWeaponStance(Hand) && UsesHandLedSwing(Hand))))
	{
		Arm.WeaponDir = Target;
		Arm.WeaponDirSpeed = FVector::ZeroVector;
		return;
	}

	// A heavy blade takes its time to come around, and far more so in one hand if it was made for two.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const float Strain = Item && Item->bTwoHanded && !bTwoHandGrip ? AedirOneHandedStrainMass : 1.f;
	// A mace or a hammer carries its weight in the head, far out from the hand: it comes round slower than a blade of the same
	// weight, and once going it swings on further past where the arm stops.
	const bool bHeadHeavy = Item && Item->GetDamageType() == EAedirDamageType::Blunt && !IsPolearm(Item) && Item->GetWeaponLength() > AedirHeadHeavyMinLengthCm;
	const float CarriedKg = WeaponWeight * Strain * (bTwoHandGrip ? 0.5f : 1.f) * (bHeadHeavy ? AedirHeadHeavyTurnMass : 1.f);
	const float Rate = AedirWeaponTurnRate * StrengthFactor / (1.f + CarriedKg * AedirWeaponTurnMassShare);
	const float Frequency = Rate * AedirWeaponTurnFrequencyShare;
	const float DampingRatio = FMath::Clamp(AedirWeaponTurnDampingLight - CarriedKg * AedirWeaponTurnDampingPerKg, AedirWeaponTurnDampingHeavy, AedirWeaponTurnDampingLight) * (bHeadHeavy ? AedirHeadHeavyDamping : 1.f);
	const float MaxTurn = FMath::DegreesToRadians(AedirWeaponMaxTurnDegrees) * StrengthFactor / AedirPoseStrengthReference / (1.f + WeaponWeight * AedirWeaponMaxTurnMassShare);

	// The arm brings the handle round and the weight of the head follows through the wrist: it falls behind on the way and swings on a little past the end.
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaTime / AedirWeaponTurnSubstep));
	const float StepSeconds = DeltaTime / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const FVector Pull = Target - Arm.WeaponDir;
		Arm.WeaponDirSpeed += (Pull * (Frequency * Frequency) - Arm.WeaponDirSpeed * (2.f * DampingRatio * Frequency)) * StepSeconds;
		Arm.WeaponDirSpeed = FVector::VectorPlaneProject(Arm.WeaponDirSpeed, Arm.WeaponDir).GetClampedToMaxSize(MaxTurn);
		const FVector Next = (Arm.WeaponDir + Arm.WeaponDirSpeed * StepSeconds).GetSafeNormal();
		if (!Next.IsNearlyZero())
		{
			Arm.WeaponDir = Next;
		}
	}
}

bool UAedirCombatComponent::IsLeadFootRight() const
{
	return bLeadFootRight;
}

void UAedirCombatComponent::UpdateLeadFoot()
{
	// The foot opposite the weapon leads: with the blade on the right the left foot is forward, with it on the left the
	// right one. Which side the blade is on is where the hands carrying it are; it changes over only once the hands are
	// clearly across, so the feet do not shuffle while the weapon passes the middle.
	const EAedirHand Carrying = GetGripHand();
	if (!GetHeldItem(Carrying))
	{
		const EAedirHand Other = Carrying == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
		if (!GetHeldItem(Other))
		{
			return;
		}
		bLeadFootRight = Other == EAedirHand::Left;
		return;
	}
	const FAedirArmDynamics& Arm = GetArm(Carrying);
	const FVector Local = Creature->GetActorTransform().InverseTransformPosition(Arm.HandWorld + Arm.WeaponDir * AedirLeadFootBladeReachCm);
	if (Local.Y > AedirLeadFootSwitchCm)
	{
		bLeadFootRight = false;
	}
	else if (Local.Y < -AedirLeadFootSwitchCm)
	{
		bLeadFootRight = true;
	}
}

float UAedirCombatComponent::ComputeImpactEnergy(float MassKg, float SpeedCmPerSec)
{
	const float SpeedMetersPerSecond = FMath::Max(SpeedCmPerSec, 0.f) / 100.f;
	return 0.5f * FMath::Max(MassKg, 0.f) * SpeedMetersPerSecond * SpeedMetersPerSecond;
}

EAedirBodyZone UAedirCombatComponent::ClassifyBodyZone(FName BoneName)
{
	static const TCHAR* const HeadTokens[] = { TEXT("head"), TEXT("neck") };
	static const TCHAR* const TorsoTokens[] = { TEXT("spine"), TEXT("pelvis"), TEXT("clavicle") };
	static const TCHAR* const ArmTokens[] = { TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
	static const TCHAR* const LegTokens[] = { TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball") };

	const FString Bone = BoneName.ToString();
	if (CombatBoneNameContainsAny(Bone, HeadTokens, UE_ARRAY_COUNT(HeadTokens)))
	{
		return EAedirBodyZone::Head;
	}

	if (CombatBoneNameContainsAny(Bone, TorsoTokens, UE_ARRAY_COUNT(TorsoTokens)))
	{
		return EAedirBodyZone::Torso;
	}

	if (CombatBoneNameContainsAny(Bone, ArmTokens, UE_ARRAY_COUNT(ArmTokens)))
	{
		return EAedirBodyZone::Arm;
	}

	if (CombatBoneNameContainsAny(Bone, LegTokens, UE_ARRAY_COUNT(LegTokens)))
	{
		return EAedirBodyZone::Leg;
	}

	return EAedirBodyZone::Torso;
}

float UAedirCombatComponent::GetZoneDamageMultiplier(EAedirBodyZone Zone)
{
	switch (Zone)
	{
	case EAedirBodyZone::Head:
		return 2.f;
	case EAedirBodyZone::Arm:
		return 0.6f;
	case EAedirBodyZone::Leg:
		return 0.7f;
	case EAedirBodyZone::Torso:
	default:
		return 1.f;
	}
}

float UAedirCombatComponent::ComputeDamageTypeFactor(EAedirDamageType Type, float AxialAlignment)
{
	switch (Type)
	{
	case EAedirDamageType::Slash:
		return 1.2f;
	case EAedirDamageType::Pierce:
		return AxialAlignment >= 0.7f ? 1.5f : 0.25f;
	case EAedirDamageType::Blunt:
	default:
		return 0.9f;
	}
}

bool UAedirCombatComponent::GetBoneLimb(FName BoneName, EAedirLimb& OutLimb)
{
	const EAedirBodyZone Zone = ClassifyBodyZone(BoneName);
	if (Zone != EAedirBodyZone::Arm && Zone != EAedirBodyZone::Leg)
	{
		return false;
	}

	const FString Bone = BoneName.ToString();
	const bool bLeft = Bone.EndsWith(TEXT("_l"), ESearchCase::IgnoreCase);
	const bool bRight = Bone.EndsWith(TEXT("_r"), ESearchCase::IgnoreCase);
	if (!bLeft && !bRight)
	{
		return false;
	}

	if (Zone == EAedirBodyZone::Arm)
	{
		OutLimb = bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm;
	}
	else
	{
		OutLimb = bLeft ? EAedirLimb::LeftLeg : EAedirLimb::RightLeg;
	}

	return true;
}

bool UAedirCombatComponent::IsAuthoritative() const
{
	return Creature && Creature->HasAuthority();
}

bool UAedirCombatComponent::IsAutonomousProxy() const
{
	return Creature && !Creature->HasAuthority() && Creature->IsLocallyControlled();
}

bool UAedirCombatComponent::IsSimulatedProxy() const
{
	return Creature && !Creature->HasAuthority() && !Creature->IsLocallyControlled();
}

int32 UAedirCombatComponent::HandIndex(EAedirHand Hand)
{
	return static_cast<int32>(Hand);
}

FAedirSwingState& UAedirCombatComponent::GetSwingState(EAedirHand Hand)
{
	return Hand == EAedirHand::Left ? LeftSwing : RightSwing;
}

const FAedirSwingState& UAedirCombatComponent::GetSwingState(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? LeftSwing : RightSwing;
}

FAedirArmDynamics& UAedirCombatComponent::GetArm(EAedirHand Hand)
{
	return Arms[HandIndex(Hand)];
}

const FAedirArmDynamics& UAedirCombatComponent::GetArm(EAedirHand Hand) const
{
	return Arms[HandIndex(Hand)];
}

FAedirArmNetState& UAedirCombatComponent::GetArmNet(EAedirHand Hand)
{
	return Hand == EAedirHand::Left ? LeftArmNet : RightArmNet;
}

const FAedirArmNetState& UAedirCombatComponent::GetArmNet(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? LeftArmNet : RightArmNet;
}

AAedirItemActor* UAedirCombatComponent::GetHeldItem(EAedirHand Hand) const
{
	return Creature && Creature->Interaction ? Creature->Interaction->GetHeldItem(Hand) : nullptr;
}

EAedirLimb UAedirCombatComponent::GetArmLimb(EAedirHand Hand) const
{
	return Hand == EAedirHand::Left ? EAedirLimb::LeftArm : EAedirLimb::RightArm;
}

void UAedirCombatComponent::SetGuardInternal(EAedirHand Hand, bool bActive)
{
	if (!Creature)
	{
		return;
	}

	bool& GuardFlag = Hand == EAedirHand::Left ? bLeftGuard : bRightGuard;
	const bool bWasActive = GuardFlag;
	GuardFlag = bActive;

	if (bActive && !bWasActive)
	{
		SetGuardRestAim(Hand);
	}

	// Keeps the movement fight stance flag in step on the server and on the owning client for prediction.
	Creature->SetWantsFightStance(IsInFightStance());
}

void UAedirCombatComponent::SetGuardRestAim(EAedirHand Hand)
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	if (HeldItem && HeldItem->ItemClass == EAedirItemClass::Object && IsObjectGrip())
	{
		return;
	}

	if (HeldItem && HeldItem->ItemClass == EAedirItemClass::Weapon)
	{
		// A second button on a weapon already up does not bring the blade back.
		const EAedirHand OtherHand = Hand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
		if (GetHeldItem(OtherHand) == HeldItem && IsGuardActive(OtherHand))
		{
			return;
		}

		// A weapon in both hands is aimed by the hand that carries it.
		FAedirArmDynamics& AimArm = GetArm(IsSecondaryGripHand(Hand) ? GetGripHand() : Hand);
		AimArm.AimOffsetDeg = FVector2D::ZeroVector;
		AimArm.Extension = 0.5f;
		return;
	}

	const float InwardYaw = Hand == EAedirHand::Left ? AedirArmGuardRestYaw : -AedirArmGuardRestYaw;
	GetArm(Hand).AimOffsetDeg = FVector2D(InwardYaw, AedirArmGuardRestPitch);
}

bool UAedirCombatComponent::CanStartSwing(EAedirHand Hand) const
{
	return IsGuardActive(Hand)
		&& !GetSwingState(Hand).bActive
		&& SwingCooldownRemaining[HandIndex(Hand)] <= 0.f
		&& !bShoving
		&& !IsSecondaryGripHand(Hand);
}

bool UAedirCombatComponent::IsArmActive(EAedirHand Hand) const
{
	// A weapon in both hands comes up with either button.
	const bool bGripGuard = bTwoHandGrip && Hand == GetGripHand() && IsInFightStance();
	return IsGuardActive(Hand) || bGripGuard || GetSwingState(Hand).bActive;
}

bool UAedirCombatComponent::IsArmPosed(EAedirHand Hand) const
{
	return IsArmActive(Hand) || bShoving || (IsGripHeld() && IsSecondaryGripHand(Hand)) || (bTwoHandGrip && Hand == GetGripHand() && GetHeldItem(Hand));
}

bool UAedirCombatComponent::IsSecondaryGripHand(EAedirHand Hand) const
{
	return bTwoHandGrip && Hand != GetGripHand();
}

float UAedirCombatComponent::ComputeWeightFactor(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float Weight = (HeldItem ? HeldItem->Weight : 0.f) * (HeldItem && HeldItem->bTwoHanded && !bTwoHandGrip ? AedirOneHandedStrainMass : 1.f);
	const float Strength = Creature && Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;

	return 1.f + Weight / FMath::Max(Strength, 1.f) * AedirWeightSlowdownPerRatio;
}

float UAedirCombatComponent::ComputeSwingDuration(EAedirHand Hand) const
{
	const float Athletism = Creature && Creature->Stats ? Creature->Stats->Athletism : AedirDefaultStat;
	const float AthletismAlpha = FMath::Clamp(Athletism / AedirCombatStatMax, 0.f, 1.f);

	return SwingDurationBase * ComputeWeightFactor(Hand) * FMath::Lerp(1.15f, 0.85f, AthletismAlpha);
}

float UAedirCombatComponent::GetEffectiveWeaponLength(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float Length = HeldItem ? HeldItem->GetWeaponLength() : 0.f;
	return Length > 0.f ? Length : AedirArmFallbackLength;
}

float UAedirCombatComponent::GetArmTotalMass(EAedirHand Hand) const
{
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	return (HeldItem ? HeldItem->Weight : 0.f) + ForearmMass;
}

bool UAedirCombatComponent::TryStartSwing(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	if (!IsAuthoritative() || !CanStartSwing(Hand))
	{
		return false;
	}

	const float Cost = SwingStaminaCost * ComputeWeightFactor(Hand);
	if (!Creature->Stats || Creature->Stats->GetStamina() < Cost)
	{
		return false;
	}

	// A two-handed grip splits the swing cost, each arm pays half.
	if (bTwoHandGrip)
	{
		Creature->Stats->ConsumeStamina(Cost * 0.5f);
		Creature->Stats->ConsumeStamina(Cost * 0.5f);
	}
	else
	{
		Creature->Stats->ConsumeStamina(Cost);
	}

	StartSwingInternal(Hand, Type, Direction);
	return true;
}

void UAedirCombatComponent::StartSwingInternal(EAedirHand Hand, EAedirSwingType Type, const FVector2D& Direction)
{
	FAedirSwingState& State = GetSwingState(Hand);

	State.bActive = true;
	State.Type = Type;
	State.Elapsed = 0.f;
	State.Duration = FMath::Max(ComputeSwingDuration(Hand), AedirMinSwingDuration);
	State.Direction = Direction;

	// The scripted trajectory drives the arm target; the arm dynamics then follow it with weapon inertia.
	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.bScriptedControl = true;
	Arm.ScriptedTargetWorld = GetSwingWorldTarget(Hand, Type, 0.f);
}

void UAedirCombatComponent::CancelSwing(EAedirHand Hand)
{
	FAedirSwingState& State = GetSwingState(Hand);
	if (!State.bActive)
	{
		return;
	}

	State.bActive = false;
	State.Elapsed = State.Duration;
	SwingCooldownRemaining[HandIndex(Hand)] = SwingCooldown;
	GetArm(Hand).bScriptedControl = bShoving;
}

void UAedirCombatComponent::UpdateSwings(float DeltaTime)
{
	if (!IsAuthoritative())
	{
		return;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirSwingState& State = GetSwingState(Hand);
		FAedirArmDynamics& Arm = Arms[Index];

		if (!State.bActive)
		{
			// A running shove owns the scripted control of both arms.
			Arm.bScriptedControl = bShoving;
			continue;
		}

		State.Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp(State.Elapsed / State.Duration, 0.f, 1.f);
		Arm.bScriptedControl = true;
		Arm.ScriptedTargetWorld = GetSwingWorldTarget(Hand, State.Type, Alpha);

		if (State.Elapsed >= State.Duration)
		{
			State.bActive = false;
			State.Elapsed = State.Duration;
			SwingCooldownRemaining[Index] = SwingCooldown;
			Arm.bScriptedControl = false;
		}
	}
}

FVector UAedirCombatComponent::GetSwingWorldTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	return Creature->GetActorTransform().TransformPositionNoScale(GetSwingLocalTarget(Hand, Type, Alpha) * Scale);
}

FVector UAedirCombatComponent::GetSwingLocalTarget(EAedirHand Hand, EAedirSwingType Type, float Alpha) const
{
	// Local space: X is forward, Y is the owner's right, Z is up. Scaled by the owner actor scale by the caller.
	const float Side = Hand == EAedirHand::Left ? -1.f : 1.f;
	const FVector Pivot(0.f, AedirShoulderLateral * Side, AedirShoulderHeight);
	const FVector Guard = Pivot + FVector(AedirGuardForward, -AedirGuardInward * Side, AedirGuardHeight);

	FVector ArcStart = Guard;
	FVector ArcEnd = Pivot + FVector(SwingReach, 0.f, -5.f);

	switch (Type)
	{
	case EAedirSwingType::Hook:
		ArcStart = FVector(20.f, AedirHookOutsideLateral * Side, AedirChestHeight);
		ArcEnd = FVector(60.f, -AedirHookAcrossLateral * Side, AedirChestHeight);
		break;
	case EAedirSwingType::Uppercut:
		ArcStart = FVector(30.f, AedirPunchLateral * Side, -25.f);
		ArcEnd = FVector(55.f, AedirPunchLateral * Side, 30.f);
		break;
	case EAedirSwingType::Overhand:
		ArcStart = FVector(0.f, AedirPunchLateral * Side, 60.f);
		ArcEnd = FVector(60.f, AedirPunchLateral * Side, -10.f);
		break;
	case EAedirSwingType::Jab:
	default:
		break;
	}

	// A quadratic bezier with its control point at the midpoint is a straight line, so jabs stay straight.
	const FVector Control = (ArcStart + ArcEnd) * 0.5f
		+ FVector(Type == EAedirSwingType::Jab ? 0.f : AedirArcBulge, 0.f, 0.f);

	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);

	if (Clamped < AedirSwingWindupEnd)
	{
		const float Eased = CombatSmoothStep01(Clamped / AedirSwingWindupEnd);
		return FMath::Lerp(Guard, ArcStart, Eased)
			+ FVector(-AedirWindupPullBack * FMath::Sin(Eased * UE_PI), 0.f, 0.f);
	}

	if (Clamped <= AedirSwingStrikeEnd)
	{
		const float Eased = CombatSmoothStep01((Clamped - AedirSwingWindupEnd) / (AedirSwingStrikeEnd - AedirSwingWindupEnd));
		return QuadraticBezier(ArcStart, Control, ArcEnd, Eased);
	}

	const float Eased = CombatSmoothStep01((Clamped - AedirSwingStrikeEnd) / (1.f - AedirSwingStrikeEnd));
	return FMath::Lerp(ArcEnd, Guard, Eased);
}

void UAedirCombatComponent::UpdateArms(float DeltaTime)
{
	const bool bProxy = IsSimulatedProxy();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		FAedirArmDynamics& Arm = Arms[Index];

		Arm.ShoulderWorld = ComputeArmShoulder(Hand);

		if (!Arm.bInitialized)
		{
			Arm.HandWorld = ComputeArmTarget(Hand, Arm);
			Arm.PrevHandWorld = Arm.HandWorld;
			Arm.PrevTipWorld = ComputeArmTip(Hand, Arm);
			Arm.bInitialized = true;
		}

		UpdateArmActivity(Hand, Arm, DeltaTime);

		if (bProxy)
		{
			ApplyRemoteArmState(Hand, Arm, DeltaTime);
			continue;
		}

		IntegrateArm(Hand, Arm, DeltaTime);

		if (IsAutonomousProxy())
		{
			const FVector ServerHand = GetArmNet(Hand).HandWorld;
			if (!ServerHand.IsNearlyZero() && FVector::Dist(ServerHand, Arm.HandWorld) > AedirArmReconcileDistance)
			{
				Arm.HandWorld = ServerHand;
				Arm.HandVelocity = GetArmNet(Hand).HandVelocity;
			}
		}
	}
}

FVector UAedirCombatComponent::ComputeArmShoulder(EAedirHand Hand) const
{
	const float Side = Hand == EAedirHand::Left ? -1.f : 1.f;
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const FVector Local(0.f, AedirShoulderLateral * Side * Scale, AedirShoulderHeight * Scale);
	return Creature->GetActorTransform().TransformPositionNoScale(Local);
}

FVector UAedirCombatComponent::ComputeArmTarget(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	if (Arm.bScriptedControl)
	{
		return Arm.ScriptedTargetWorld;
	}

	if (IsObjectGrip())
	{
		return Hand == GetGripHand() ? ComputeObjectCenter(Arm, GetHeldItem(Hand)) : Arm.HandWorld;
	}

	if (IsSecondaryGripHand(Hand))
	{
		return GetGripPointWorld();
	}

	if (IsWeaponStance(Hand))
	{
		const float WeaponScale = static_cast<float>(Creature->GetActorScale3D().X);
		const float HalfSwordShare = CombatSmoothStep01(HalfSwordAlpha);
		const FTransform& Body = Creature->GetActorTransform();
		FVector HandTarget;
		if (bThrustMode && !UsesHandLedSwing(Hand) && !(bTwoHandGrip && IsPolearm(GetHeldItem(Hand))))
		{
			const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
			const FVector Waist = Body.TransformPositionNoScale(FVector(AedirThrustWaistForwardCm, HandSide * (bTwoHandGrip ? AedirThrustWaistSideTwoHandCm : AedirThrustWaistSideCm), AedirThrustWaistUpCm) * WeaponScale);
			const FVector Chest = Body.TransformPositionNoScale(FVector(0.f, HandSide * (bTwoHandGrip ? AedirThrustSideTwoHandCm : AedirThrustSideCm), AedirThrustChestHeightCm) * WeaponScale);
			HandTarget = FMath::Lerp(Waist, Chest + ComputeWeaponTargetDir(Hand, Arm) * ((IsPolearm(GetHeldItem(Hand)) && bTwoHandGrip ? AedirPolearmThrustHandCm : AedirThrustHandMaxCm) * WeaponScale), FMath::Clamp(Arm.Extension, 0.f, 1.f));
		}
		else
		{
			HandTarget = Body.TransformPositionNoScale((ComputeStanceHandLocal(Hand, Arm) + Arm.ServoHandLocal) * WeaponScale);
		}
		return HalfSwordShare > 0.f ? FMath::Lerp(HandTarget, ComputeHalfSwordHand(Hand, Arm, GetHeldItem(Hand)), HalfSwordShare) : HandTarget;
	}

	// Centred fists converge slightly inwards and up to head height, so they meet the target instead of running parallel past it.
	const float OutwardSignForAim = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float CentreShare = 1.f - FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSignForAim / AedirArmOutwardYawLimit, 0.f, 1.f);
	const double Yaw = Creature->GetActorRotation().Yaw + Arm.AimOffsetDeg.X - OutwardSignForAim * AedirArmConvergeYawDegrees * CentreShare;
	// Height is relative to where the camera looks, so a punch aimed at the screen centre hits what the crosshair shows.
	const double CameraPitch = FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch);
	const FVector AimDirection = FRotator(CameraPitch + Arm.AimOffsetDeg.Y + AedirArmHeadHeightPitchDegrees * CentreShare, Yaw, 0.0).Vector();
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	// A quick throw reaches all the way out whatever the wheel set; the slower the mouse the more the wheel setting counts.
	const float Extension = FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend * AedirArmThrowReachShare);
	// Pulled outwards the elbow breaks and the hand travels an arc beside the body; centred the arm reaches straight ahead.
	const float OutwardSign = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float OutwardAlpha = FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSign / AedirArmOutwardYawLimit, 0.f, 1.f);
	const float ElbowBendScale = FMath::Lerp(1.f, 0.5f, OutwardAlpha);
	const float Reach = ElbowBendScale * FMath::Lerp(AedirArmReachMinScale, 1.f, Extension) * (SwingReach + GetEffectiveWeaponLength(Hand)) * Scale;

	const FVector AimTarget = Arm.ShoulderWorld + AimDirection * Reach;
	if (!Creature->IsLocallyControlled() || !Creature->IsPlayerControlled())
	{
		return AimTarget;
	}

	// An arm that is held out to the side stays there; only a centred arm falls back to the guard.
	// Only a clearly outward arm (past about a third of the arc) is held there by the side-hold.
	const float SideHold = FMath::Clamp((OutwardAlpha - AedirArmSideHoldStart) * AedirArmSideHoldGain, 0.f, 1.f);
	float PunchShare = FMath::Clamp(Arm.PunchBlend + SideHold, 0.f, 1.f);
	const EAedirHand OtherHand = Hand == EAedirHand::Right ? EAedirHand::Left : EAedirHand::Right;
	if (IsGuardActive(OtherHand))
	{
		const FAedirArmDynamics& Other = GetArm(OtherHand);
		const float OtherSign = OtherHand == EAedirHand::Right ? 1.f : -1.f;
		const float OtherOutward = FMath::Clamp(Other.AimOffsetDeg.X * OtherSign / AedirArmOutwardYawLimit, 0.f, 1.f);
		if (OtherOutward > OutwardAlpha + AedirArmDominanceMargin)
		{
			PunchShare *= AedirArmGuardKeepShare;
		}
	}

	// The guard fist sits in front of the chin, a little inside its own shoulder, as in Half Sword: wherever the head is, so
	// the crouch of the fight stance takes the fists down with it rather than leaving them up over the head.
	const float GuardSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	const USkeletalMeshComponent* Body = Creature->GetMesh();
	const float HeadCm = Body && Body->GetBoneIndex(TEXT("head")) != INDEX_NONE
		? static_cast<float>(Creature->GetActorTransform().InverseTransformPositionNoScale(Body->GetBoneLocation(TEXT("head"))).Z) / Scale
		: AedirGuardFaceHeightCm + AedirGuardBelowHeadCm;
	// The mouse still moves the guard up and down around the face.
	const float GuardHeightCm = HeadCm - AedirGuardBelowHeadCm + (Arm.AimOffsetDeg.Y - AedirArmGuardRestPitch) * AedirGuardHeightPerDegree;
	const FVector GuardLocal(AedirGuardForwardCm * Scale, GuardSide * AedirGuardInwardCm * Scale, GuardHeightCm * Scale);
	const FVector GuardTarget = Creature->GetActorTransform().TransformPositionNoScale(GuardLocal);
	return FMath::Lerp(GuardTarget, AimTarget, PunchShare);
}

FVector UAedirCombatComponent::ComputeArmTip(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	FVector Direction = (IsWeaponStance(Hand) && !Arm.WeaponDir.IsNearlyZero()) ? Arm.WeaponDir : (Arm.HandWorld - Arm.ShoulderWorld).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Creature ? Creature->GetActorForwardVector() : FVector::ForwardVector;
	}

	const float Scale = Creature ? static_cast<float>(Creature->GetActorScale3D().X) : 1.f;
	return Arm.HandWorld + Direction * (GetEffectiveWeaponLength(Hand) * Scale);
}

void UAedirCombatComponent::IntegrateArm(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
	const float StaminaFraction = Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f;
	const float ArmInjury = GetLimbInjury(GetArmLimb(Hand));
	// A two-handed grip shares the weapon load between both arms, so the stiffness sees only part of the weapon mass.
	const AAedirItemActor* HeldItem = GetHeldItem(Hand);
	const float WeaponWeight = HeldItem ? HeldItem->Weight : 0.f;
	const float OneHandedStrain = HeldItem && HeldItem->bTwoHanded && !bTwoHandGrip ? AedirOneHandedStrainMass : 1.f;
	const float StiffnessMass = WeaponWeight * OneHandedStrain * (bTwoHandGrip ? 1.f - GripWeaponMassShare : 1.f) + ForearmMass;
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);

	const float StrengthFactor = AedirArmStrengthBase + Strength / AedirCombatStatMax;
	const float StaminaFactor = FMath::Lerp(AedirArmStaminaFloor, 1.f, FMath::Clamp(StaminaFraction, 0.f, 1.f));
	const float InjuryFactor = 1.f - AedirArmInjuryStiffnessLoss * ArmInjury;
	const float DazeFactor = Creature->Balance ? Creature->Balance->GetDazeFactor() : 0.f;
	const float DazeFactorStiffness = 1.f - AedirArmDazeStiffnessLoss * DazeFactor;
	const float MassRatio = StiffnessMass / FMath::Max(Strength * AedirArmMassStrengthScale, 1.f);
	const float GripScale = (bTwoHandGrip && !IsSecondaryGripHand(Hand)) ? GripPrimaryStiffnessScale : 1.f;
	const float ShoveScale = bShoving ? AedirShoveStiffnessScale : 1.f;
	const float Stiffness = ArmStiffnessBase * StrengthFactor * StaminaFactor * InjuryFactor * DazeFactorStiffness * GripScale * ShoveScale / (1.f + MassRatio);
	const float Damping = 2.f * FMath::Sqrt(Stiffness) * AedirArmDampingRatio;
	const float MaxAcceleration = MaxArmAcceleration * StrengthFactor * StaminaFactor * InjuryFactor;
	const float MaxRadius = AedirArmReachHardLimit * (SwingReach + GetEffectiveWeaponLength(Hand)) * Scale;

	UpdatePoseAim(Hand, Arm, DeltaTime, StrengthFactor, WeaponWeight);

	const FVector PrevHand = Arm.HandWorld;
	const FVector PrevDir = Arm.WeaponDir;
	const FVector Target = ComputeArmTarget(Hand, Arm);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaTime / AedirArmSubstepSeconds));
	const float StepSeconds = DeltaTime / Steps;

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FVector Acceleration = Stiffness * (Target - Arm.HandWorld) - Damping * Arm.HandVelocity;
		const float AccelerationSize = static_cast<float>(Acceleration.Size());
		if (AccelerationSize > MaxAcceleration)
		{
			Acceleration *= MaxAcceleration / AccelerationSize;
		}

		Arm.HandVelocity += Acceleration * StepSeconds;
		Arm.HandWorld += Arm.HandVelocity * StepSeconds;

		const FVector FromShoulder = Arm.HandWorld - Arm.ShoulderWorld;
		const float Distance = static_cast<float>(FromShoulder.Size());
		if (Distance > MaxRadius && Distance > KINDA_SMALL_NUMBER)
		{
			const FVector Outward = FromShoulder / Distance;
			Arm.HandWorld = Arm.ShoulderWorld + Outward * MaxRadius;

			const float OutwardSpeed = static_cast<float>(FVector::DotProduct(Arm.HandVelocity, Outward));
			if (OutwardSpeed > 0.f)
			{
				Arm.HandVelocity -= Outward * OutwardSpeed;
			}
		}
	}

	UpdateWeaponDir(Hand, Arm, DeltaTime, StrengthFactor, WeaponWeight);
	UpdatePoseServo(Hand, Arm, DeltaTime);
	ResolveHeldCollision(Hand, Arm, PrevHand, PrevDir, DeltaTime);
	KeepOutOfOwnBody(Hand, Arm);
	UpdateLodged(Hand, Arm, DeltaTime);
}

void UAedirCombatComponent::UpdatePoseServo(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	// What is shown is what the pose asks, whichever way the arm came to it: the physical arm stops short of a pose by an amount
	// that depends on the way it came, so the difference between where the hand and the blade really are and where the pose
	// has them is fed back, and the arm is asked that much further. Only while the pose holds still: in the middle of a quick
	// movement the arm lags the pose on purpose, by its weight.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	const bool bServo = CVarAedirPoseServo.GetValueOnGameThread() != 0 && Item && Mesh && IsWeaponStance(Hand) && !IsSecondaryGripHand(Hand)
		&& !Arm.bScriptedControl && !Arm.LodgedIn.IsValid() && HalfSwordAlpha <= 0.f && GetFightShare() >= AedirServoFightShare;
	if (!bServo)
	{
		// Out of the stance it is forgotten, slowly, so a stance taken up again starts from about where it was.
		const float Fade = FMath::Clamp(DeltaTime * AedirServoForgetPerSecond, 0.f, 1.f);
		Arm.ServoHandLocal *= 1.f - Fade;
		Arm.ServoBladeLocal = FQuat::Slerp(Arm.ServoBladeLocal, FQuat::Identity, Fade).GetNormalized();
		return;
	}
	// Learned for one pose, it is no use for another: moved on to a pose well away from where it learned, it starts again.
	if (FVector2D::Distance(Arm.PoseAimDeg, Arm.ServoAimDeg) > AedirServoSamePoseDegrees)
	{
		Arm.ServoHandLocal = FVector::ZeroVector;
		Arm.ServoBladeLocal = FQuat::Identity;
		Arm.ServoAimDeg = Arm.PoseAimDeg;
	}
	const FName HandBone = Hand == EAedirHand::Right ? FName(TEXT("hand_r")) : FName(TEXT("hand_l"));
	// It learns only from an arm that has settled: one still moving is behind the pose for its weight, not off it.
	if (Arm.PoseAimSpeed.Size() > AedirServoStillAimSpeed || Mesh->GetPhysicsLinearVelocity(HandBone).Size() > AedirServoStillHandSpeed)
	{
		return;
	}
	const FTransform& Body = Creature->GetActorTransform();
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Gain = FMath::Clamp(DeltaTime * AedirServoGainPerSecond, 0.f, 1.f);

	const FVector ModelHand = ComputeStanceHandLocal(Hand, Arm);
	const FVector RealHand = Body.InverseTransformPositionNoScale(Mesh->GetBoneLocation(HandBone)) / Scale;
	Arm.ServoHandLocal = (Arm.ServoHandLocal + (ModelHand - RealHand) * Gain).GetClampedToMaxSize(AedirServoMaxHandCm);

	const FQuat Facing = FRotator(0.0, Creature->GetActorRotation().Yaw, 0.0).Quaternion();
	const FVector ModelBlade = Facing.UnrotateVector(ComputeWeaponModelDir(Hand, Arm)).GetSafeNormal();
	const FVector RealBlade = Facing.UnrotateVector(Item->GetActorForwardVector()).GetSafeNormal();
	if (!ModelBlade.IsNearlyZero() && !RealBlade.IsNearlyZero())
	{
		const FQuat Short = FQuat::FindBetweenNormals(RealBlade, ModelBlade);
		FQuat Next = (FQuat::Slerp(FQuat::Identity, Short, Gain) * Arm.ServoBladeLocal).GetNormalized();
		const float Angle = FMath::RadiansToDegrees(static_cast<float>(Next.GetAngle()));
		if (Angle > AedirServoMaxBladeDegrees)
		{
			Next = FQuat::Slerp(FQuat::Identity, Next, AedirServoMaxBladeDegrees / Angle).GetNormalized();
		}
		Arm.ServoBladeLocal = Next;
	}
}

void UAedirCombatComponent::BlockAgainstBody(FAedirArmDynamics& Arm, float Bounce)
{
	Arm.HandWorld = Arm.PrevHandWorld;
	const FVector Before = (Arm.PrevTipWorld - Arm.PrevHandWorld).GetSafeNormal();
	if (!Before.IsNearlyZero())
	{
		Arm.WeaponDir = Before;
	}
	Arm.HandVelocity *= -Bounce;
	Arm.WeaponDirSpeed *= -Bounce;
	Arm.PoseAimSpeed *= -Bounce;
}

void UAedirCombatComponent::KeepOutOfOwnBody(EAedirHand Hand, FAedirArmDynamics& Arm)
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const USkeletalMeshComponent* Body = Creature->GetMesh();
	if (!Item || !Body || Item->ItemClass != EAedirItemClass::Weapon || IsSecondaryGripHand(Hand) || Arm.WeaponDir.IsNearlyZero() || Arm.LodgedIn.IsValid())
	{
		return;
	}

	struct FBodyPart { const TCHAR* From; const TCHAR* To; float RadiusCm; };
	static const FBodyPart Parts[] =
	{
		{ TEXT("pelvis"), TEXT("spine_05"), AedirOwnTrunkRadiusCm },
		{ TEXT("neck_01"), TEXT("head"), AedirOwnHeadRadiusCm },
		{ TEXT("thigh_l"), TEXT("calf_l"), AedirOwnThighRadiusCm },
		{ TEXT("thigh_r"), TEXT("calf_r"), AedirOwnThighRadiusCm },
	};
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Length = GetEffectiveWeaponLength(Hand) * Scale;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const FBodyPart& Part : Parts)
		{
			const FVector BladeFrom = Arm.HandWorld + Arm.WeaponDir * (AedirOwnBladeGuardCm * Scale);
			const FVector BladeTo = Arm.HandWorld + Arm.WeaponDir * Length;
			FVector OnBlade;
			FVector OnBody;
			FMath::SegmentDistToSegmentSafe(BladeFrom, BladeTo, Body->GetBoneLocation(Part.From), Body->GetBoneLocation(Part.To), OnBlade, OnBody);
			const FVector Apart = OnBlade - OnBody;
			const double Gap = Apart.Size();
			const double Radius = Part.RadiusCm * Scale;
			if (Gap >= Radius)
			{
				continue;
			}
			// The blade turns about the hand until the part it touched lies outside the body.
			const FVector Out = Gap > KINDA_SMALL_NUMBER ? Apart / Gap : FVector::CrossProduct(Arm.WeaponDir, FVector::UpVector).GetSafeNormal();
			const double Reach = FMath::Max(FVector::Dist(OnBlade, Arm.HandWorld), 5.0);
			const FVector Tip = BladeTo + Out * ((Radius - Gap) * (Length / Reach));
			const FVector NewDir = (Tip - Arm.HandWorld).GetSafeNormal();
			if (!NewDir.IsNearlyZero())
			{
				Arm.WeaponDir = NewDir;
				const double Into = FVector::DotProduct(Arm.WeaponDirSpeed, -Out);
				if (Into > 0.0)
				{
					Arm.WeaponDirSpeed += Out * Into;
				}
			}
		}
	}
}

void UAedirCombatComponent::LodgeIn(FAedirArmDynamics& Arm, USkeletalMeshComponent* VictimBody, FName Bone, const FHitResult& Hit, const FVector& Way, float DepthCm)
{
	const FTransform BoneWorld = VictimBody->GetSocketTransform(Bone, RTS_World);
	const FVector Dir = Arm.WeaponDir.GetSafeNormal();
	const FVector In = Way.GetSafeNormal().IsNearlyZero() ? Dir : Way.GetSafeNormal();
	Arm.LodgedIn = VictimBody;
	Arm.LodgedBone = Bone;
	Arm.LodgedTipLocal = BoneWorld.InverseTransformPosition(FVector(Hit.ImpactPoint) + In * DepthCm);
	Arm.LodgedDirLocal = BoneWorld.InverseTransformVectorNoScale(Dir);
	Arm.LodgedSeconds = AedirLodgedHoldSeconds;
	Arm.LodgedDepthCm = DepthCm;
	UE_LOG(LogAedir, Log, TEXT("[Lodge] %s's blade is stuck %.0f cm deep in %s's %s"), *Creature->GetName(), DepthCm, *GetNameSafe(VictimBody->GetOwner()), *Bone.ToString());
}

FVector UAedirCombatComponent::ClearDirOfOwnBody(EAedirHand Hand, const FVector& HandWorld, const FVector& Dir) const
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	const USkeletalMeshComponent* Body = Creature ? Creature->GetMesh() : nullptr;
	if (!Item || !Body || Item->ItemClass != EAedirItemClass::Weapon || IsSecondaryGripHand(Hand) || Dir.IsNearlyZero())
	{
		return Dir;
	}
	struct FBodyPart { const TCHAR* From; const TCHAR* To; float RadiusCm; };
	static const FBodyPart Parts[] =
	{
		{ TEXT("pelvis"), TEXT("spine_05"), AedirOwnTrunkRadiusCm },
		{ TEXT("neck_01"), TEXT("head"), AedirOwnHeadRadiusCm },
		{ TEXT("thigh_l"), TEXT("calf_l"), AedirOwnThighRadiusCm },
		{ TEXT("thigh_r"), TEXT("calf_r"), AedirOwnThighRadiusCm },
	};
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Length = GetEffectiveWeaponLength(Hand) * Scale;
	FVector PartFrom[UE_ARRAY_COUNT(Parts)];
	FVector PartTo[UE_ARRAY_COUNT(Parts)];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parts); ++Index)
	{
		PartFrom[Index] = Body->GetBoneLocation(Parts[Index].From);
		PartTo[Index] = Body->GetBoneLocation(Parts[Index].To);
	}
	FVector Out = Dir.GetSafeNormal();
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Parts); ++Index)
		{
			const FBodyPart& Part = Parts[Index];
			const FVector BladeFrom = HandWorld + Out * (AedirOwnBladeGuardCm * Scale);
			const FVector BladeTo = HandWorld + Out * Length;
			FVector OnBlade;
			FVector OnBody;
			FMath::SegmentDistToSegmentSafe(BladeFrom, BladeTo, PartFrom[Index], PartTo[Index], OnBlade, OnBody);
			const FVector Apart = OnBlade - OnBody;
			const double Gap = Apart.Size();
			// A little clear of the body, so the blade held there does not graze it from frame to frame.
			const double Radius = (Part.RadiusCm + AedirOwnClearanceCm) * Scale;
			if (Gap >= Radius)
			{
				continue;
			}
			const FVector Away = Gap > KINDA_SMALL_NUMBER ? Apart / Gap : FVector::CrossProduct(Out, FVector::UpVector).GetSafeNormal();
			const double Reach = FMath::Max(FVector::Dist(OnBlade, HandWorld), 5.0);
			const FVector NewDir = (BladeTo + Away * ((Radius - Gap) * (Length / Reach)) - HandWorld).GetSafeNormal();
			if (!NewDir.IsNearlyZero())
			{
				Out = NewDir;
			}
		}
	}
	return Out;
}

void UAedirCombatComponent::UpdateLodged(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	USkeletalMeshComponent* Body = Arm.LodgedIn.Get();
	if (!Body)
	{
		Arm.LodgedSeconds = 0.f;
		return;
	}

	const FTransform BoneWorld = Body->GetSocketTransform(Arm.LodgedBone, RTS_World);
	const FVector Tip = BoneWorld.TransformPosition(Arm.LodgedTipLocal);
	const FVector Dir = BoneWorld.TransformVectorNoScale(Arm.LodgedDirLocal).GetSafeNormal();
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const FVector Held = Tip - Dir * (GetEffectiveWeaponLength(Hand) * Scale);
	AAedirCreature* Victim = Cast<AAedirCreature>(Body->GetOwner());

	// It stays in until it is pulled out, the harder the deeper it went; let go of, or held long past any struggle, it comes
	// away of itself.
	Arm.LodgedSeconds -= DeltaTime;
	const FVector Pull = ComputeArmTarget(Hand, Arm) - Held;
	const double PullCm = Pull.Size();
	const double TearCm = (AedirLodgedTearCm + Arm.LodgedDepthCm * AedirLodgedTearPerDepth) * Scale;
	const bool bTorn = PullCm > TearCm;
	if (Arm.LodgedSeconds <= 0.f || bTorn || Dir.IsNearlyZero() || !GetHeldItem(Hand))
	{
		if (Victim && Victim->Wounds && bTorn && IsAuthoritative())
		{
			Victim->Wounds->AddWound(Arm.LodgedBone, Tip, -Dir, EAedirDamageType::Slash, AedirLodgedTearJoules + Arm.LodgedDepthCm * AedirLodgedTearJoulesPerCm, 0.f, Dir * 10.f);
			UE_LOG(LogAedir, Log, TEXT("[Lodge] %s tore the blade out of %s"), *Creature->GetName(), *Victim->GetName());
		}
		Arm.LodgedIn.Reset();
		Arm.LodgedSeconds = 0.f;
		Arm.LodgedDepthCm = 0.f;
		if (AAedirItemActor* Item = GetHeldItem(Hand))
		{
			Item->SetLodged(false);
		}
		return;
	}

	// Pulling on the stuck blade drags the body it is in by that part and throws it off balance; it reels with the steel in it.
	if (Victim && IsAuthoritative() && PullCm > AedirLodgedPullFreeCm * Scale)
	{
		const FVector Way = Pull / PullCm;
		const float Share = static_cast<float>(FMath::Clamp((PullCm - AedirLodgedPullFreeCm * Scale) / TearCm, 0.0, 1.0));
		if (Body->IsAnySimulatingPhysics())
		{
			Body->AddImpulseAtLocation(Way * (AedirLodgedDragImpulsePerSecond * Share * DeltaTime), Tip, Arm.LodgedBone);
		}
		if (Victim->Balance)
		{
			Victim->Balance->ApplyBalanceImpact(Way, AedirLodgedBalancePerSecond * Share * DeltaTime);
		}
	}

	Arm.HandWorld = Held;
	Arm.HandVelocity = FVector::ZeroVector;
	Arm.WeaponDir = Dir;
	Arm.WeaponDirSpeed = FVector::ZeroVector;
	Arm.PoseAimSpeed = FVector2D::ZeroVector;
}

void UAedirCombatComponent::ApplyRemoteArmState(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const FAedirArmNetState& NetState = GetArmNet(Hand);
	const FVector ServerHand = NetState.HandWorld;
	if (ServerHand.IsNearlyZero())
	{
		return;
	}

	Arm.HandWorld = FMath::VInterpTo(Arm.HandWorld, ServerHand, DeltaTime, AedirArmProxyInterpSpeed);
	Arm.HandVelocity = NetState.HandVelocity;
}

void UAedirCombatComponent::PublishArmState(EAedirHand Hand, const FAedirArmDynamics& Arm)
{
	FAedirArmNetState& NetState = GetArmNet(Hand);
	NetState.HandWorld = Arm.HandWorld;
	NetState.HandVelocity = Arm.HandVelocity;
	NetState.Active = IsArmPosed(Hand) ? 1 : 0;
}

void UAedirCombatComponent::SendArmAimToServer(float DeltaTime)
{
	ArmAimSendTimer -= DeltaTime;
	if (ArmAimSendTimer > 0.f)
	{
		return;
	}

	ArmAimSendTimer = AedirArmNetSendInterval;

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!IsGuardActive(Hand))
		{
			continue;
		}

		const FAedirArmDynamics& Arm = Arms[Index];
		ServerUpdateArmAim(Hand, Arm.AimOffsetDeg, Arm.Extension);
	}
}

void UAedirCombatComponent::UpdateArmActivity(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	const bool bActive = IsSimulatedProxy() ? GetArmNet(Hand).Active != 0 : IsArmPosed(Hand);
	Arm.ActiveAlpha = FMath::FInterpConstantTo(Arm.ActiveAlpha, bActive ? 1.f : 0.f, DeltaTime, bActive ? 1.f / AedirArmRampInSeconds : 1.f / AedirArmRampOutSeconds);
}

void UAedirCombatComponent::UpdateClashes(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClashCandidateTimer -= DeltaTime;
	if (ClashCandidateTimer <= 0.f)
	{
		ClashCandidateTimer = AedirClashCacheInterval;
		RefreshClashCandidates();
	}

	const double Now = World->GetTimeSeconds();

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		if (!IsArmActive(Hand))
		{
			continue;
		}

		FAedirArmDynamics& Arm = Arms[Index];
		const FVector Start = Arm.HandWorld;
		const FVector End = ComputeArmTip(Hand, Arm);

		for (const TWeakObjectPtr<AAedirCreature>& Candidate : ClashCandidates)
		{
			AAedirCreature* Other = Candidate.Get();
			if (!Other || !Other->Combat || (Other->Stats && Other->Stats->IsDead()))
			{
				continue;
			}

			UAedirCombatComponent* OtherCombat = Other->Combat;
			for (int32 OtherIndex = 0; OtherIndex < 2; ++OtherIndex)
			{
				const EAedirHand OtherHand = static_cast<EAedirHand>(OtherIndex);
				if (!OtherCombat->IsArmActive(OtherHand))
				{
					continue;
				}

				const FAedirArmDynamics& OtherArm = OtherCombat->Arms[OtherIndex];

				FVector ClosestSelf = FVector::ZeroVector;
				FVector ClosestOther = FVector::ZeroVector;
				FMath::SegmentDistToSegment(Start, End, OtherArm.HandWorld, OtherCombat->ComputeArmTip(OtherHand, OtherArm), ClosestSelf, ClosestOther);

				const FVector Offset = ClosestSelf - ClosestOther;
				const float CombinedSpeed = static_cast<float>(Arm.HandVelocity.Size() + OtherArm.HandVelocity.Size());
				if (Offset.Size() >= ClashRadius || CombinedSpeed <= ClashMinCombinedSpeed)
				{
					continue;
				}

				ClashedThisTick.AddUnique(TWeakObjectPtr<AActor>(Other));

				if (IsClashCooldownElapsed(Other, Now))
				{
					FVector Normal = Offset.GetSafeNormal();
					if (Normal.IsNearlyZero())
					{
						Normal = FVector::UpVector;
					}

					ResolveClash(Hand, Arm, Other, OtherArm.HandVelocity, Normal, CombinedSpeed, Now);
				}
			}
		}
	}
}

void UAedirCombatComponent::RefreshClashCandidates()
{
	ClashCandidates.Reset();

	UWorld* World = GetWorld();
	if (!World || !Creature)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatClash), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Creature->GetActorLocation(), FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(ClashSearchRadius), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (Candidate && Candidate != Creature)
		{
			ClashCandidates.Add(Candidate);
		}
	}
}

bool UAedirCombatComponent::IsClashCooldownElapsed(const AAedirCreature* Other, double Now) const
{
	for (const FAedirTimedContact& Record : ClashRecords)
	{
		if (Record.Target.Get() == Other && Now - Record.Time < ClashCooldown)
		{
			return false;
		}
	}

	return true;
}

void UAedirCombatComponent::ResolveClash(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Other, const FVector& OtherVelocity, const FVector& Normal, float CombinedSpeed, double Now)
{
	const float MassEff = GetArmTotalMass(Hand);

	Arm.HandVelocity = Arm.HandVelocity * AedirClashSelfReflection + OtherVelocity * AedirClashOtherReflection;

	const float ReducedEnergy = ComputeImpactEnergy(MassEff, CombinedSpeed);
	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(FMath::Min(AedirClashEnergyShare * ReducedEnergy * AedirImpactStaminaPerEnergy, AedirImpactStaminaMax));
	}

	if (Creature->Balance)
	{
		Creature->Balance->ApplyImpulseImpact(Normal * (MassEff * CombinedSpeed * AedirClashBalanceScale));
	}

	ClashRecords.RemoveAll([this, Now](const FAedirTimedContact& Record)
	{
		return !Record.Target.IsValid() || Now - Record.Time >= ClashCooldown;
	});

	FAedirTimedContact* Record = ClashRecords.FindByPredicate([Other](const FAedirTimedContact& Existing)
	{
		return Existing.Target.Get() == Other;
	});

	if (Record)
	{
		Record->Time = Now;
	}
	else
	{
		FAedirTimedContact NewRecord;
		NewRecord.Target = Other;
		NewRecord.Time = Now;
		ClashRecords.Add(NewRecord);
	}

	OnClash.Broadcast(Other);
}

bool UAedirCombatComponent::GetStrikingSegment(EAedirHand Hand, FVector& OutFrom, FVector& OutTo, float& OutRadiusCm) const
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	if (Item)
	{
		const UStaticMeshComponent* ItemMesh = Item->Mesh.Get();
		const UStaticMesh* Shape = ItemMesh ? ItemMesh->GetStaticMesh() : nullptr;
		if (!Shape)
		{
			return false;
		}
		// The item's own body, end to end along its length, where the hand that holds it has it now.
		const FBox Box = Shape->GetBoundingBox();
		const FTransform Placed = ItemMesh->GetComponentTransform();
		const FVector Middle = Box.GetCenter();
		OutFrom = Placed.TransformPosition(FVector(Box.Min.X, Middle.Y, Middle.Z));
		OutTo = Placed.TransformPosition(FVector(Box.Max.X, Middle.Y, Middle.Z));
		const float Scale = static_cast<float>(Creature ? Creature->GetActorScale3D().X : 1.f);
		const float BladeHalfCm = static_cast<float>(FMath::Max(Item->BodyScale.Y, Item->BodyScale.Z) * 50.0);
		OutRadiusCm = (Item->BodyScale.Y <= AedirStrikeHaftBoxY ? AedirStrikeHaftRadiusCm : FMath::Clamp(BladeHalfCm + AedirStrikeSkinCm, AedirStrikeMinRadiusCm, AedirStrikeMaxRadiusCm)) * Scale;
		return true;
	}

	const USkeletalMeshComponent* Body = Creature ? Creature->GetMesh() : nullptr;
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	if (!Body || !Interaction)
	{
		return false;
	}
	// A bare hand strikes with the knuckles: from the wrist out over the back of the hand.
	const FName HandBone = Hand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
	const FName Knuckle = Hand == EAedirHand::Left ? FName(TEXT("middle_01_l")) : FName(TEXT("middle_01_r"));
	if (Body->GetBoneIndex(HandBone) == INDEX_NONE || Body->GetBoneIndex(Knuckle) == INDEX_NONE)
	{
		return false;
	}
	OutFrom = Body->GetBoneLocation(HandBone);
	OutTo = Body->GetBoneLocation(Knuckle);
	OutRadiusCm = FistRadius * static_cast<float>(Creature->GetActorScale3D().X);
	return true;
}

void UAedirCombatComponent::UpdateImpacts(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	UWorld* World = GetWorld();
	const int32 Index = HandIndex(Hand);
	FVector From;
	FVector To;
	float Radius = 0.f;
	const AActor* Item = GetHeldItem(Hand);
	if (!World || DeltaTime <= KINDA_SMALL_NUMBER || !GetStrikingSegment(Hand, From, To, Radius))
	{
		bStrikePrevValid[Index] = false;
		return;
	}

	// Only the path the real weapon took this frame strikes: it lands where it is seen to land, and nowhere short of it.
	const bool bHadPath = bStrikePrevValid[Index] && StrikePrevItem[Index].Get() == Item;
	const FVector PrevFrom = StrikePrevFrom[Index];
	const FVector PrevTo = StrikePrevTo[Index];
	StrikePrevFrom[Index] = From;
	StrikePrevTo[Index] = To;
	StrikePrevItem[Index] = Item;
	bStrikePrevValid[Index] = true;
	// A weapon still flying on after its button was let go strikes all the same: a blow is the steel arriving, not a key
	// held. A loose fist, or the second hand on a weapon the first one already strikes with, does not.
	const bool bLoose = !IsArmActive(Hand);
	if (!bHadPath || (bLoose && (!Item || IsSecondaryGripHand(Hand))))
	{
		return;
	}
	// Only a living, standing body strikes: a weapon in a dead, senseless or fallen hand only lies or tumbles about.
	const bool bHolderDown = (Creature->Stats && (Creature->Stats->IsDead() || Creature->Stats->IsDying() || Creature->Stats->IsKnockedOut()))
		|| (Creature->Balance && Creature->Balance->IsRagdoll() && !Creature->Balance->IsGettingUp());
	if (bHolderDown)
	{
		return;
	}

	const FVector FromVelocity = (From - PrevFrom) / DeltaTime;
	const FVector ToVelocity = (To - PrevTo) / DeltaTime;
	const double FastestEnd = FMath::Max(FromVelocity.Size(), ToVelocity.Size());
	// A fast swing is heard cutting the air, the faster the louder.
	if (FastestEnd > AedirSfxWhooshSpeed && World->GetTimeSeconds() - LastWhooshTime[Index] > AedirSfxWhooshSeconds)
	{
		LastWhooshTime[Index] = World->GetTimeSeconds();
		UAedirSfxSubsystem::Play(this, EAedirSfx::Whoosh, To, FMath::GetRangePct(AedirSfxWhooshSpeed, AedirSfxWhooshFullSpeed, static_cast<float>(FastestEnd)));
	}
	if (FastestEnd <= AedirArmMinSpeed || (bLoose && FastestEnd < AedirLooseStrikeMinSpeed))
	{
		return;
	}

	const float MassEff = GetArmTotalMass(Hand);
	const double Now = World->GetTimeSeconds();
	// The weapon's length is swept at close steps along its way, so nothing it passed through slips between two of them.
	const double Travel = FMath::Max(FVector::Dist(From, PrevFrom), FVector::Dist(To, PrevTo));
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Travel / FMath::Max(Radius * AedirStrikeStepRadii, 1.f)), 1, AedirStrikeMaxSteps);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const double T = static_cast<double>(Step) / Steps;
		ProcessImpactSweep(Hand, Arm, FMath::Lerp(PrevFrom, From, T), FMath::Lerp(PrevTo, To, T), FromVelocity, ToVelocity, Radius, MassEff, Now);
	}
}

FName UAedirCombatComponent::GetStruckPart(FName Bone)
{
	EAedirLimb Limb = EAedirLimb::LeftArm;
	if (GetBoneLimb(Bone, Limb))
	{
		return FName(TEXT("Limb"), static_cast<int32>(Limb) + 1);
	}
	return ClassifyBodyZone(Bone) == EAedirBodyZone::Head ? FName(TEXT("Head")) : FName(TEXT("Trunk"));
}

bool UAedirCombatComponent::IsImpactOnCooldown(EAedirHand Hand, const AActor* Victim, double Now, FName Part) const
{
	for (const FAedirTimedContact& Record : ImpactRecords[HandIndex(Hand)])
	{
		if (Record.Target.Get() == Victim && Record.Part == Part && Now - Record.Time < ImpactCooldown)
		{
			return true;
		}
	}

	return false;
}

void UAedirCombatComponent::RegisterImpact(EAedirHand Hand, AActor* Victim, double Now, FName Part)
{
	TArray<FAedirTimedContact>& Records = ImpactRecords[HandIndex(Hand)];
	Records.RemoveAll([this, Now](const FAedirTimedContact& Record)
	{
		return !Record.Target.IsValid() || Now - Record.Time >= ImpactCooldown;
	});

	FAedirTimedContact* Existing = Records.FindByPredicate([Victim, Part](const FAedirTimedContact& Record)
	{
		return Record.Target.Get() == Victim && Record.Part == Part;
	});

	if (Existing)
	{
		Existing->Time = Now;
		return;
	}

	FAedirTimedContact NewRecord;
	NewRecord.Target = Victim;
	NewRecord.Part = Part;
	NewRecord.Time = Now;
	Records.Add(NewRecord);
}

void UAedirCombatComponent::ProcessImpactSweep(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& From, const FVector& To, const FVector& FromVelocity, const FVector& ToVelocity, float RadiusCm, float MassEff, double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AAedirItemActor* HeldItem = GetHeldItem(Hand);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatImpact), false);
	Params.AddIgnoredActor(Creature);
	if (HeldItem)
	{
		Params.AddIgnoredActor(HeldItem);
	}

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(RadiusCm), Params);
	const FVector Along = To - From;
	const double AlongSquared = FMath::Max(Along.SizeSquared(), 1.0);
	const FVector BladeDir = Along.GetSafeNormal();

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Creature || HitActor == HeldItem)
		{
			continue;
		}
		// Each part of the weapon moves at its own speed: the far end of a long blade fastest, near the hand slowest.
		const double Share = FMath::Clamp(FVector::DotProduct(FVector(Hit.ImpactPoint) - From, Along) / AlongSquared, 0.0, 1.0);
		const FVector TipVelocity = FMath::Lerp(FromVelocity, ToVelocity, Share);
		const float AxialAlignment = static_cast<float>(FMath::Abs(FVector::DotProduct(TipVelocity.GetSafeNormal(), BladeDir)));

		if (AAedirCreature* Victim = Cast<AAedirCreature>(HitActor))
		{
			// Only the body itself is struck. The capsule the body walks in reaches well past its flesh: a blow that stopped on it
			// would stop in the air short of the body and hurt it without touching it.
			if (Hit.GetComponent() != Victim->GetMesh())
			{
				continue;
			}
			if (ClashedThisTick.Contains(TWeakObjectPtr<AActor>(Victim)))
			{
				continue;
			}

			// A body is solid: whatever does not cut through it stops against it, also between blows and when merely pushed.
			const int32 Index = HandIndex(Hand);
			const bool bPassing = PassVictim[Index].Get() == Victim && Now < PassUntil[Index];
			const float RelativeSpeed = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(TipVelocity - Victim->GetVelocity(), -Hit.ImpactNormal)));
			// A blade that has stayed against the body since it last struck presses or grinds; only one that comes off
			// and strikes again lands a new blow.
			// A cut that goes on from one part of the body into another lands on each: only the same part waits out the
			// cooldown, and only it counts as the blade still resting where it was.
			const FName Part = GetStruckPart(Hit.BoneName);
			// A fist or blade resting against the body that is driven in again from rest (it had all but stopped against it and
			// now comes in fast) lands a new blow too: a punch thrown from against a guard arm still hits.
			const float MinSpeed = HeldItem ? MinImpactSpeed : FMath::Min(MinImpactSpeed, AedirFistMinImpactSpeed);
			const bool bDrivenAgain = TouchSpeed[Index] < MinSpeed * AedirRestingIntoShare && RelativeSpeed >= MinSpeed;
			const bool bFreshContact = TouchVictim[Index].Get() != Victim || TouchPart[Index] != Part || Now - TouchTime[Index] > AedirFreshContactSeconds || bDrivenAgain;
			TouchVictim[Index] = Victim;
			TouchPart[Index] = Part;
			TouchTime[Index] = Now;
			TouchSpeed[Index] = RelativeSpeed;
			if (IsImpactOnCooldown(Hand, HitActor, Now, Part) || RelativeSpeed < MinSpeed || !bFreshContact)
			{
				UE_CLOG(CVarAedirHitLog.GetValueOnGameThread() != 0 && TipVelocity.Size() > MinImpactSpeed, LogAedir, Log, TEXT("[Hit] %s's %s touched %s on %s at %.0f cm/s (%.0f into it) and did not count: %s"),
					*Creature->GetName(), Hand == EAedirHand::Right ? TEXT("right") : TEXT("left"), *Victim->GetName(), *Part.ToString(), TipVelocity.Size(), RelativeSpeed,
					IsImpactOnCooldown(Hand, HitActor, Now, Part) ? TEXT("struck there just before") : !bFreshContact ? TEXT("still resting against it") : TEXT("too slow into it"));
				if (!bPassing && !Arm.LodgedIn.IsValid())
				{
					BlockAgainstBody(Arm, 0.f);
				}
				continue;
			}
			// Faster than any arm can move is a body that was just put somewhere else, not a blow.
			if (RelativeSpeed > AedirImpactMaxSpeed)
			{
				continue;
			}

			RegisterImpact(Hand, Victim, Now, Part);
			// What lands is the weapon at the speed of the part that struck, and the arm only at the speed of the hand behind
			// it: the forearm does not fly at the speed of a sword's point. Faster than any arm can really swing counts as that.
			const float HandSpeed = static_cast<float>(FromVelocity.Size());
			const float PointSpeed = FMath::Max(static_cast<float>(TipVelocity.Size()), 1.f);
			const float WeaponKg = HeldItem ? HeldItem->Weight : 0.f;
			const float MassAtPoint = FMath::Min(WeaponKg + (MassEff - WeaponKg) * FMath::Square(FMath::Min(HandSpeed / PointSpeed, 1.f)), MassEff);
			const float StruckSpeed = FMath::Min(RelativeSpeed, AedirImpactCountedMaxSpeed);
			ApplyCreatureImpact(Hand, Arm, Victim, Hit, TipVelocity, StruckSpeed, HeldItem ? MassAtPoint : MassEff, AxialAlignment);
			continue;
		}

		if (IsImpactOnCooldown(Hand, HitActor, Now))
		{
			continue;
		}

		UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (AAedirTrainingDummy* Dummy = Cast<AAedirTrainingDummy>(HitActor))
		{
			const float DummySpeed = static_cast<float>(TipVelocity.Size());
			if (DummySpeed >= MinImpactSpeed)
			{
				RegisterImpact(Hand, Dummy, Now);
				Dummy->ReceiveStrike(HitComponent, Hit.ImpactPoint, TipVelocity, Arm.WeaponDir, ComputeImpactEnergy(MassEff, DummySpeed), HeldItem && HeldItem->GetDamageType() == EAedirDamageType::Slash);
				Arm.HandVelocity *= AedirImpactRecoilScale;
			}
			continue;
		}

		if (!HitComponent || !HitComponent->IsSimulatingPhysics())
		{
			continue;
		}

		const float RelativeSpeed = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(TipVelocity - HitComponent->GetComponentVelocity(), -Hit.ImpactNormal)));
		if (RelativeSpeed < MinImpactSpeed)
		{
			continue;
		}

		RegisterImpact(Hand, HitActor, Now);
		ApplyPhysicsImpact(Arm, Hit, HitComponent, RelativeSpeed, MassEff);
	}
}

void UAedirCombatComponent::PushFriend(AAedirCreature* Victim, FName Bone, EAedirBodyZone Zone, const FVector& Location, const FVector& Motion, float Energy, float MassKg, float Speed) const
{
	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Bone = Bone;
		Impact.Zone = Zone;
		Impact.EnergyJoules = Energy * AedirImpactBodyEnergyShare;
		Impact.Direction = Motion.GetSafeNormal();
		Impact.Location = Location;
		Impact.MassKg = MassKg;
		Impact.RelativeSpeed = Speed;
		Victim->Balance->ApplyHitReaction(Impact);
	}
	UE_LOG(LogAedir, Log, TEXT("Impact: %s pushed %s, a friend, on %s: no harm"), *Creature->GetName(), *Victim->GetName(), *Bone.ToString());
}

void UAedirCombatComponent::ApplyCreatureImpact(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Victim, const FHitResult& Hit, const FVector& TipVelocity, float RelativeSpeed, float MassEff, float AxialAlignment)
{
	AAedirItemActor* HeldItem = GetHeldItem(Hand);
	// A blow that met the body's capsule still lands on whatever part of the body is closest to where it struck.
	const FName Bone = !Hit.BoneName.IsNone() || !Victim->GetMesh() ? Hit.BoneName : Victim->GetMesh()->FindClosestBone(Hit.ImpactPoint);
	const EAedirBodyZone Zone = ClassifyBodyZone(Bone);
	// A friend struck is only pushed: no harm, no wound, and no quarrel.
	if (Creature->IsFriendOf(Victim))
	{
		PushFriend(Victim, Bone, Zone, Hit.ImpactPoint, TipVelocity, ComputeImpactEnergy(MassEff, RelativeSpeed), MassEff, RelativeSpeed);
		return;
	}
	const EAedirDamageType DamageType = HeldItem ? HeldItem->GetDamageType() : EAedirDamageType::Blunt;
	const float Energy = ComputeImpactEnergy(MassEff, RelativeSpeed);
	// The armour where it landed, for the way it landed: an edge, a point driven in along the blade, or a blunt blow. Bare
	// places (the face under an open helmet, the neck, under the arms, the hands) take it all.
	const bool bPointFirst = HeldItem && DamageType != EAedirDamageType::Blunt && AxialAlignment >= AedirLodgeThrustAlignment;
	// A blade or an axe that meets the body with its flat strikes like a club: it bruises and can break bones, but cuts nothing.
	bool bFlat = false;
	if (HeldItem && DamageType != EAedirDamageType::Blunt && !bPointFirst)
	{
		const FVector FlatNormal = HeldItem->GetFlatNormalWorld();
		const FVector Across = FVector::VectorPlaneProject(TipVelocity, Arm.WeaponDir).GetSafeNormal();
		bFlat = !FlatNormal.IsNearlyZero() && !Across.IsNearlyZero() && FMath::Abs(FVector::DotProduct(Across, FlatNormal)) > AedirFlatStrikeShare;
	}
	const EAedirDamageType Landed = bFlat ? EAedirDamageType::Blunt : DamageType;
	const EAedirDamageType StruckAs = bPointFirst ? EAedirDamageType::Pierce : Landed;
	const FAedirArmorHit Armour = Victim->Interaction ? Victim->Interaction->GetArmorAt(Bone, Hit.ImpactPoint, StruckAs) : FAedirArmorHit();
	const float VictimZoneProtection = Armour.Protection;
	const float ArmorFactor = 1.f - VictimZoneProtection;
	UE_CLOG(Victim->Interaction && Victim->Interaction->HasWornArmor(), LogAedir, Log, TEXT("Armour: %s on %s took %.0f%% (%s)"),
		StruckAs == EAedirDamageType::Slash ? TEXT("edge") : StruckAs == EAedirDamageType::Pierce ? TEXT("point") : TEXT("blunt"), *Bone.ToString(), VictimZoneProtection * 100.f,
		!Armour.bCovered ? TEXT("bare") : Armour.Material == EAedirArmorMaterial::Plate ? TEXT("plate") : Armour.Material == EAedirArmorMaterial::Mail ? TEXT("mail") : TEXT("padding"));
	// Metal takes the blow and bends: the piece is battered for the next one, plate shows the dent where it landed, and a
	// helmet struck hard enough is knocked off the head.
	if (AAedirItemActor* Piece = Cast<AAedirItemActor>(Armour.PieceActor.Get()); Piece && Armour.bCovered && IsAuthoritative())
	{
		Piece->TakeArmorBlow(Energy);
		if (Armour.Material == EAedirArmorMaterial::Plate && Energy > AedirDentShowJoules)
		{
			MulticastArmourDent(Victim, Bone, Hit.ImpactPoint, Hit.ImpactNormal, Energy);
		}
		if (Piece->ArmorZone == EAedirBodyZone::Head && Victim->Interaction && FMath::FRand() < FMath::Clamp((Energy - AedirHelmetOffFromJoules) / AedirHelmetOffSpreadJoules, 0.f, AedirHelmetOffMaxChance))
		{
			Victim->Interaction->KnockOffWorn(Piece, TipVelocity.GetSafeNormal() * AedirHelmetOffSpeed + FVector(0.f, 0.f, AedirHelmetOffLift));
		}
	}
	// A blow to the head shakes the senses by what of it gets through: a helmet turns an edge, but the jolt of the blow
	// reaches the head under it all the same, half through plate.
	if (Zone == EAedirBodyZone::Head && Victim->Stats)
	{
		const float Jolt = Victim->Interaction ? Victim->Interaction->GetArmorAt(Bone, Hit.ImpactPoint, EAedirDamageType::Blunt).Protection : 0.f;
		const float Lost = FMath::Max(Energy * (1.f - Jolt) - AedirSensesFreeJoules, 0.f) * AedirSensesPerJoule;
		Victim->Stats->LoseConsciousness(Lost);
		UE_CLOG(Lost > 1.f, LogAedir, Log, TEXT("[Senses] %s took %.0f to the head, senses %.0f"), *Victim->GetName(), Lost, Victim->Stats->Consciousness);
	}
	// Only a real blow hurts: a touch, blades resting on each other or bodies rubbing shake the balance and do no harm; from
	// there the harm grows smoothly to the full of a hard blow.
	const float Harm = FMath::SmoothStep(AedirImpactNoHarmJoules, AedirImpactFullFlatJoules, Energy * ArmorFactor);
	const float FlatDamage = (HeldItem ? HeldItem->GetDamage() : FistDamage) * Harm;

	// One blow to the body or a limb leaves the victim alive however hard it was; only the head can be finished at once.
	// The weapon's own share has already been weighed by how hard the blow was; weighing it again would leave a solid blow
	// short of full strength next to nothing.
	const float RawDamage = Energy * ImpactDamageScale * ComputeDamageTypeFactor(Landed, AxialAlignment) * GetZoneDamageMultiplier(Zone) * ArmorFactor * Harm + FlatDamage;
	// How far an edge or a point gets depends on how fast it came: short of enough it glances off, between it bites and
	// sticks, past it it goes on through.
	float CutJoules = AedirCutTorsoJoules;
	switch (Zone)
	{
	case EAedirBodyZone::Head: CutJoules = AedirCutHeadJoules; break;
	case EAedirBodyZone::Arm: CutJoules = AedirCutArmJoules; break;
	case EAedirBodyZone::Leg: CutJoules = AedirCutLegJoules; break;
	default: break;
	}
	const bool bEdged = HeldItem && (Landed == EAedirDamageType::Slash || Landed == EAedirDamageType::Pierce);
	const float CutShare = Energy * ArmorFactor / CutJoules * FMath::FRandRange(1.f - AedirCutLuck, 1.f + AedirCutLuck);
	const bool bGlance = bEdged && CutShare < AedirCutGlanceShare;
	const float DamageShare = bGlance ? AedirGlanceDamageShare : 1.f;
	// No one blow takes more than a share of a body's health, the head a larger one: a fight is a few good blows, not one.
	const float MaxShare = Zone == EAedirBodyZone::Head ? AedirImpactMaxHealthShareHead : AedirImpactMaxHealthShare;
	const float Damage = DamageShare * (!Victim->Stats ? RawDamage : FMath::Min(RawDamage, Victim->Stats->GetMaxHealth() * MaxShare));
	const float AppliedDamage = Victim->ReceiveCreatureDamage(Damage);

	EAedirLimb Limb = EAedirLimb::LeftArm;
	if (Victim->Combat && GetBoneLimb(Bone, Limb))
	{
		Victim->Combat->ApplyLimbInjury(Limb, Damage * AedirImpactInjuryPerDamage);
		// A blow that gets through to the long bones of a limb hard enough breaks them; one on the hand or the collarbone
		// spends half of itself on the small bones there. A strong body's bones take more.
		// Only a blunt blow breaks: a mace, a fist, the back of an axe or a blade that struck flat or glanced. An edge that
		// bites cuts instead.
		const bool bCrushing = StruckAs == EAedirDamageType::Blunt || CutShare < AedirCutGlanceShare;
		if (bCrushing && Victim->Wounds && IsAuthoritative() && !(Victim->Stats && Victim->Stats->bInvulnerable))
		{
			const bool bLeg = Limb == EAedirLimb::LeftLeg || Limb == EAedirLimb::RightLeg;
			const FString BoneName = Bone.ToString();
			const float Reach = BoneName.StartsWith(TEXT("hand")) || BoneName.StartsWith(TEXT("clavicle")) || BoneName.StartsWith(TEXT("foot")) ? AedirBreakEndShare : 1.f;
			const float Through = Energy * Reach * (1.f - (Victim->Interaction ? Victim->Interaction->GetArmorAt(Bone, Hit.ImpactPoint, EAedirDamageType::Blunt).Protection : 0.f));
			const float Sturdy = FMath::Clamp((Victim->Stats ? Victim->Stats->Strength : 10.f) / 10.f, 0.8f, 1.3f);
			if (Through > (bLeg ? AedirBreakLegJoules : AedirBreakArmJoules) * Sturdy)
			{
				Victim->Wounds->BreakLimb(Limb);
				UAedirSfxSubsystem::Play(this, EAedirSfx::Bone, Hit.ImpactPoint, 1.f);
			}
		}
	}

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Bone = Bone;
		Impact.Zone = Zone;
		Impact.EnergyJoules = Energy * AedirImpactBodyEnergyShare;
		Impact.Direction = TipVelocity.GetSafeNormal();
		Impact.Location = Hit.ImpactPoint;
		Impact.MassKg = MassEff;
		Impact.RelativeSpeed = RelativeSpeed;
		Victim->Balance->ApplyHitReaction(Impact);
	}
	if (AAedirNpc* VictimNpc = Cast<AAedirNpc>(Victim))
	{
		VictimNpc->OnAttackedBy(Creature);
	}
	UE_LOG(LogAedir, Log, TEXT("Impact: %s hit %s on %s%s, %.0f cm/s, %.1f kg, %.0f J, %.1f damage, balance %.2f, down %d"), *Creature->GetName(), *Victim->GetName(), *Bone.ToString(), bFlat ? TEXT(" with the flat") : TEXT(""), RelativeSpeed, MassEff, Energy, AppliedDamage,
		Victim->Balance ? Victim->Balance->GetBalance() : 1.f, Victim->Balance && Victim->Balance->IsRagdoll() ? 1 : 0);

	bool bSevered = false;
	if (Victim->Wounds && Landed == EAedirDamageType::Slash && !bGlance)
	{
		const FVector CutNormal = FVector::CrossProduct(TipVelocity.GetSafeNormal(), Arm.WeaponDir).GetSafeNormal();
		// Only the edge chops: a blade drawn along its own length, or pushed in point first, opens a wound but takes nothing off.
		const float EdgeShare = FMath::Clamp(1.f - FMath::Abs(AxialAlignment), 0.f, 1.f);
		bSevered = Victim->Wounds->TrySever(Bone, Hit.ImpactPoint, CutNormal, TipVelocity, Energy * EdgeShare, VictimZoneProtection);
	}

	if (Victim->Wounds)
	{
		const FVector TipMotion = ComputeArmTip(Hand, Arm) - Arm.PrevTipWorld;
		Victim->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, bGlance ? EAedirDamageType::Blunt : Landed, Energy * DamageShare, VictimZoneProtection, TipMotion);
	}

	USkeletalMeshComponent* VictimBody = Victim->GetMesh();
	const TCHAR* Outcome = TEXT("stuck");
	if (!bEdged)
	{
		// A fist, a haft or a hammer head lands and stops on what it hit.
		BlockAgainstBody(Arm, AedirBluntBounce);
		Outcome = TEXT("blunt");
	}
	else if (bGlance)
	{
		Outcome = TEXT("glanced off");
		// Not enough behind it: the edge skips off and the arm is thrown back.
		BlockAgainstBody(Arm, AedirGlanceBounce);
	}
	else if (bPointFirst && !bSevered && VictimBody && !Bone.IsNone())
	{
		// A point driven in stays in: a hard thrust runs deep, up to half the blade, and has to be pulled back out.
		Outcome = CutShare >= 1.f ? TEXT("ran in deep") : TEXT("stuck");
		const float Depth = FMath::Lerp(AedirLodgeMinDepthCm, FMath::Max(GetEffectiveWeaponLength(Hand) * AedirLodgeMaxDepthShare, AedirLodgeMinDepthCm), FMath::Clamp(CutShare / AedirLodgeDeepShare, 0.f, 1.f));
		LodgeIn(Arm, VictimBody, Bone, Hit, Arm.WeaponDir, Depth);
	}
	else if (bSevered || CutShare >= 1.f || !VictimBody || Bone.IsNone())
	{
		// Clean through: it slows, but the swing carries on through the body.
		Outcome = TEXT("went through");
		Arm.HandVelocity *= AedirCutThroughKeep;
		Arm.PoseAimSpeed *= AedirCutThroughKeep;
		PassVictim[HandIndex(Hand)] = Victim;
		PassUntil[HandIndex(Hand)] = GetWorld()->GetTimeSeconds() + AedirPassThroughSeconds;
	}
	else if (AxialAlignment < AedirLodgeThrustAlignment && !(HeldItem && HeldItem->BodyScale.Y <= AedirLodgeHaftedBoxY && CutShare >= AedirLodgeChopShare))
	{
		// An edge that bit in without going through drags on through the cut, slowed by it, and is not held there: only a point
		// driven in or an axe chopped deep stays in what it struck.
		Outcome = TEXT("dragged through");
		Arm.HandVelocity *= AedirCutBiteKeep;
		Arm.PoseAimSpeed *= AedirCutBiteKeep;
		Arm.WeaponDirSpeed *= AedirCutBiteKeep;
		PassVictim[HandIndex(Hand)] = Victim;
		PassUntil[HandIndex(Hand)] = GetWorld()->GetTimeSeconds() + AedirPassThroughSeconds;
	}
	else
	{
		// An axe's bit chopped in and is held there by what it cut.
		LodgeIn(Arm, VictimBody, Bone, Hit, TipVelocity, FMath::Lerp(2.f, 9.f, FMath::Clamp(CutShare, 0.f, 1.f)));
	}
	if (HeldItem && Arm.LodgedIn.IsValid())
	{
		HeldItem->SetLodged(true);
	}
	UE_LOG(LogAedir, Log, TEXT("Cut: %s, share %.2f"), Outcome, CutShare);

	// What the blow sounds like: steel on plate, a point going in, an edge opening skin, or a dull blow on flesh; a hard one
	// is heard in the whole body as well.
	{
		const float Loud = FMath::Clamp(Energy / AedirSfxFullJoules, 0.f, 1.f);
		const FVector At = Hit.ImpactPoint;
		if (Armour.bCovered && Armour.Material != EAedirArmorMaterial::Soft)
		{
			UAedirSfxSubsystem::Play(this, EAedirSfx::Armour, At, Loud);
		}
		if (!bEdged)
		{
			UAedirSfxSubsystem::Play(this, HeldItem ? EAedirSfx::BodyImpact : EAedirSfx::Punch, At, Loud);
		}
		else if (bGlance)
		{
			UAedirSfxSubsystem::Play(this, EAedirSfx::Punch, At, Loud * 0.6f);
		}
		else if (bPointFirst)
		{
			UAedirSfxSubsystem::Play(this, EAedirSfx::Stab, At, Loud);
		}
		else
		{
			UAedirSfxSubsystem::Play(this, EAedirSfx::Cut, At, Loud);
		}
		if (Energy > AedirSfxHeavyJoules && bEdged)
		{
			UAedirSfxSubsystem::Play(this, EAedirSfx::BodyImpact, At + FVector(0.f, 0.f, 1.f), Loud * 0.6f);
		}
	}

	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(FMath::Min(Energy * AedirImpactStaminaPerEnergy, AedirImpactStaminaMax));
		if (HeldItem)
		{
			Creature->Stats->AddSkillExperience(EAedirSkill::SingleHandWeapon, AedirArmedSkillExperience);
		}
	}

	OnImpact.Broadcast(Victim, Bone, Energy, AppliedDamage);
	OnHitDealt.Broadcast(Victim, AppliedDamage);
}

void UAedirCombatComponent::ApplyPhysicsImpact(FAedirArmDynamics& Arm, const FHitResult& Hit, UPrimitiveComponent* HitComponent, float RelativeSpeed, float MassEff)
{
	const float Energy = ComputeImpactEnergy(MassEff, RelativeSpeed);
	// However hard the blow, a light prop gets knocked aside and does not fly away.
	const float PropImpulse = FMath::Min(MassEff * RelativeSpeed * AedirImpactPropImpulseScale, HitComponent->GetMass() * AedirPropMaxSpeedChange);
	HitComponent->AddImpulseAtLocation(-Hit.ImpactNormal * PropImpulse, Hit.ImpactPoint);

	Arm.HandVelocity *= AedirImpactRecoilScale;

	if (Creature->Stats)
	{
		Creature->Stats->ConsumeStamina(FMath::Min(Energy * AedirImpactStaminaPerEnergy, AedirImpactStaminaMax));
	}

	OnImpact.Broadcast(Hit.GetActor(), Hit.BoneName, Energy, 0.f);
}

void UAedirCombatComponent::ApplyLimbInjury(EAedirLimb Limb, float Amount)
{
	const int32 Index = static_cast<int32>(Limb);
	if (!IsAuthoritative() || !LimbInjury.IsValidIndex(Index))
	{
		return;
	}

	LimbInjury[Index] = FMath::Clamp(LimbInjury[Index] + Amount, 0.f, 1.f);
}

void UAedirCombatComponent::HealLimbInjuries(float DeltaTime)
{
	for (float& Injury : LimbInjury)
	{
		Injury = FMath::Max(0.f, Injury - AedirImpactLimbHealPerSecond * DeltaTime);
	}
}

void UAedirCombatComponent::UpdateAI(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AIRetargetTimer -= DeltaTime;
	if (AIRetargetTimer <= 0.f)
	{
		AIRetargetTimer = AedirAIRetargetInterval;
		UpdateAITarget(World->GetTimeSeconds());
	}

	if (AITarget && (!IsValid(AITarget) || (AITarget->Stats && AITarget->Stats->IsDead())))
	{
		AITarget = nullptr;
	}

	if (!AITarget)
	{
		CancelSwing(EAedirHand::Left);
		CancelSwing(EAedirHand::Right);

		if (IsGuardActive(EAedirHand::Left))
		{
			SetGuardInternal(EAedirHand::Left, false);
		}

		if (IsGuardActive(EAedirHand::Right))
		{
			SetGuardInternal(EAedirHand::Right, false);
		}

		bAIApproaching = false;
		AIAttackTimer = 0.f;
		bAIHadWeapon = false;
		return;
	}

	const FVector OwnerLocation = Creature->GetActorLocation();
	// One who is down lies where the hips are, not where it stood; it is closed on and struck at down there, bent over it.
	bAITargetDown = AITarget->Balance && (AITarget->Balance->IsRagdoll() || AITarget->Balance->IsGettingUp());
	const FVector TargetLocation = bAITargetDown && AITarget->GetMesh() ? AITarget->GetMesh()->GetBoneLocation(TEXT("pelvis")) : AITarget->GetActorLocation();
	const float Distance = static_cast<float>(FVector::Dist2D(OwnerLocation, TargetLocation));
	const bool bStoop = bAITargetDown && Distance < AedirAIStoopCm * static_cast<float>(Creature->GetActorScale3D().X);
	if (bStoop && !Creature->bIsCrouched && Creature->CanCrouch())
	{
		Creature->Crouch();
	}
	else if (!bStoop && Creature->bIsCrouched)
	{
		Creature->UnCrouch();
	}

	const float FacingYaw = static_cast<float>((TargetLocation - OwnerLocation).Rotation().Yaw);

	const EAedirHand WeaponHand = GetAIWeaponHand();
	const AAedirItemActor* Weapon = GetHeldItem(WeaponHand);
	const bool bArmed = Weapon && Weapon->ItemClass == EAedirItemClass::Weapon;
	bAIHadWeapon |= bArmed;
	const bool bLeftGone = Creature->Wounds && Creature->Wounds->HasUselessArm(true);
	const bool bRightGone = Creature->Wounds && Creature->Wounds->HasUselessArm(false);
	const bool bCanRearm = Creature->Interaction && Creature->Interaction->FindWaistWeaponSlot() != INDEX_NONE;
	if ((bLeftGone && bRightGone) || (bAIHadWeapon && !bArmed && !bCanRearm) || ((bLeftGone || bRightGone) && !bArmed && !bCanRearm))
	{
		// Disarmed or maimed: it turns and runs from the one it was fighting.
		CancelSwing(EAedirHand::Left);
		CancelSwing(EAedirHand::Right);
		SetGuardInternal(EAedirHand::Left, false);
		SetGuardInternal(EAedirHand::Right, false);
		Creature->SetWantsToRun(true);
		Creature->WalkTowards(OwnerLocation + (OwnerLocation - TargetLocation).GetSafeNormal2D() * AedirAIFleeCm, DeltaTime, AedirAITurnSpeed);
		return;
	}
	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	const float Reach = (AedirAIArmReachCm + (bArmed ? Weapon->GetWeaponLength() * AedirAIWeaponReachShare : 0.f)) * Scale;

	// Too far off for a fight yet: just get there, hands down.
	if (Distance > Reach + AedirAIStanceExtraCm * Scale)
	{
		CancelSwing(EAedirHand::Left);
		CancelSwing(EAedirHand::Right);
		SetGuardInternal(EAedirHand::Left, false);
		SetGuardInternal(EAedirHand::Right, false);
		AIStrikePhase = 0;
		AIAttackTimer = 0.f;
		Creature->SetWantsToRun(Distance > AedirAIRunBeyondCm);
		Creature->WalkTowards(TargetLocation, DeltaTime, AedirAITurnSpeed);
		return;
	}

	Creature->SetWantsToRun(false);
	// A weapon in the stance takes one button, the second would change the hold. Fists both come up.
	if (!IsGuardActive(WeaponHand))
	{
		SetGuardInternal(WeaponHand, true);
	}
	const EAedirHand OtherHand = WeaponHand == EAedirHand::Left ? EAedirHand::Right : EAedirHand::Left;
	if (IsGuardActive(OtherHand) == bArmed)
	{
		SetGuardInternal(OtherHand, !bArmed);
	}

	// The bodies never come closer than their own width and a little room, the weapon's length when there is one.
	const float BodiesApart = (Creature->GetCapsuleComponent()->GetScaledCapsuleRadius() + (AITarget->GetCapsuleComponent() ? AITarget->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f))
		+ (bArmed ? AedirAIArmedRoomCm : AedirAIUnarmedRoomCm) * Scale;
	const float KeepAt = FMath::Max(bArmed ? Reach * AedirAIBackOffShare : 0.f, BodiesApart);
	if (Distance > FMath::Max(Reach * AedirAICloseInShare, KeepAt + AedirAIKeepBandCm * Scale))
	{
		Creature->WalkTowards(TargetLocation, DeltaTime, AedirAITurnSpeed);
	}
	else
	{
		Creature->TurnBodyTo(FacingYaw, DeltaTime, AedirAITurnSpeed);
		if (Distance < KeepAt && !bAITargetDown)
		{
			Creature->AddMovementInput(Creature->GetActorForwardVector(), -AedirAIBackOffInput * FMath::Clamp((KeepAt - Distance) / (AedirAIKeepBandCm * Scale) + 0.4f, 0.f, 1.f));
		}
	}

	if (bArmed)
	{
		if (UpdateAIWeaponStrike(WeaponHand, Distance, Reach, DeltaTime))
		{
			return;
		}
	}
	else
	{
		UpdateAIFistStrike(Distance, Reach, DeltaTime);
	}
}

FVector UAedirCombatComponent::GetAITargetPoint(int32 Part) const
{
	const AAedirCreature* Target = AITarget.Get();
	const USkeletalMeshComponent* Body = Target ? Target->GetMesh() : nullptr;
	if (!Body)
	{
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}
	const TCHAR* Bone = Part == 0 ? TEXT("head") : Part == 1 ? TEXT("spine_03") : (FMath::RandBool() ? TEXT("thigh_l") : TEXT("thigh_r"));
	return Body->GetBoneLocation(FName(Bone));
}

FVector2D UAedirCombatComponent::ComputeFistAimAt(EAedirHand Hand, const FVector& PointWorld) const
{
	// The inverse of how a fist's target is made from its aim: the turn from the body and the rise from where the eyes look,
	// with the small pull to the middle the guard gives it.
	const float Out = Hand == EAedirHand::Right ? 1.f : -1.f;
	const FRotator Toward = (PointWorld - ComputeArmShoulder(Hand)).Rotation();
	const float Yaw = FRotator::NormalizeAxis(static_cast<float>(Toward.Yaw - Creature->GetActorRotation().Yaw));
	const float Centre = 1.f - FMath::Clamp(Yaw * Out / AedirArmOutwardYawLimit, 0.f, 1.f);
	const float CameraPitch = FRotator::NormalizeAxis(static_cast<float>(Creature->GetControlRotation().Pitch));
	const float AimX = FMath::Clamp((Yaw + Out * AedirArmConvergeYawDegrees * Centre) * Out, -AedirArmAcrossYawLimit - 25.f, AedirArmOutwardYawLimit) * Out;
	const float AimY = FMath::Clamp(static_cast<float>(Toward.Pitch) - CameraPitch - AedirArmHeadHeightPitchDegrees * Centre, AedirArmAimPitchMin, AedirFistAimPitchMax);
	return FVector2D(AimX, AimY);
}

void UAedirCombatComponent::UpdateAIFistStrike(float Distance, float Reach, float DeltaTime)
{
	// Bare fists go the way the player's do: both up in a guard, then one drawn back or out and thrown at the head or the body.
	FAedirArmDynamics& Arm = GetArm(AIFistHand);
	AIStrikeTimer += DeltaTime;
	switch (AIStrikePhase)
	{
	case 0:
	{
		for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
		{
			FAedirArmDynamics& Guard = GetArm(Hand);
			Guard.PunchBlend = 0.f;
			Guard.AimOffsetDeg = ComputeFistAimAt(Hand, GetAITargetPoint(0)) * FVector2D(0.5f, 1.f) + FVector2D(0.f, -10.f);
		}
		const bool bTired = Creature->Stats && Creature->Stats->GetStamina() < Creature->Stats->GetMaxStamina() * AedirAIRestStaminaShare;
		if (!bTired && Distance <= Reach + AedirAIContactRange * 0.25f && AIStrikeTimer >= AIAttackInterval)
		{
			// At grips a shove or a kick now and then.
			if (Distance <= AedirAIContactRange)
			{
				if (FMath::FRand() < AedirAIShoveChance)
				{
					StartShove();
				}
				else if (FMath::FRand() < AedirAIKickChance)
				{
					StartKick();
				}
			}
			AIFistHand = bAINextLeft ? EAedirHand::Left : EAedirHand::Right;
			bAINextLeft = !bAINextLeft;
			AIFistPart = FMath::FRand() < 0.55f ? 0 : 1;
			const float Out = AIFistHand == EAedirHand::Right ? 1.f : -1.f;
			const FVector2D Aim = ComputeFistAimAt(AIFistHand, GetAITargetPoint(AIFistPart));
			// A jab straight from the guard, a hook drawn out to the side, an uppercut dropped low first.
			const int32 Kind = Distance < AedirAICloseRange ? FMath::RandRange(1, 2) : FMath::RandRange(0, 2);
			AIFistWindup = Kind == 0 ? Aim : Kind == 1 ? FVector2D(Out * AedirArmOutwardYawLimit * 0.75f, Aim.Y) : FVector2D(Aim.X, AedirArmAimPitchMin * 0.6f);
			AIStrikeEnd = FVector2D(Kind == 0 ? 0.f : AedirAIFistWindupSeconds - AedirAIJabWindupSeconds, 0.f);
			AIStrikePhase = 1;
			AIStrikeTimer = 0.f;
		}
		break;
	}
	case 1:
		Arm.AimOffsetDeg = AIFistWindup;
		Arm.PunchBlend = 0.f;
		if (AIStrikeTimer >= AedirAIJabWindupSeconds + static_cast<float>(AIStrikeEnd.X))
		{
			AIStrikePhase = 2;
			AIStrikeTimer = 0.f;
		}
		break;
	case 2:
		// Thrown, it keeps going for the part it was meant for, wherever that has moved.
		Arm.AimOffsetDeg = ComputeFistAimAt(AIFistHand, GetAITargetPoint(AIFistPart));
		Arm.PunchBlend = 1.f;
		if (AIStrikeTimer >= AedirAIFistThrowSeconds)
		{
			Arm.PunchBlend = 0.f;
			AIStrikePhase = 3;
			AIStrikeTimer = 0.f;
		}
		break;
	default:
		if (AIStrikeTimer >= AedirAIFistRecoverSeconds)
		{
			AIStrikePhase = 0;
			AIStrikeTimer = 0.f;
		}
		break;
	}
}

EAedirHand UAedirCombatComponent::GetAIWeaponHand() const
{
	const AAedirItemActor* Left = GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* Right = GetHeldItem(EAedirHand::Right);
	if (Right && Right->ItemClass == EAedirItemClass::Weapon)
	{
		return EAedirHand::Right;
	}
	return Left && Left->ItemClass == EAedirItemClass::Weapon ? EAedirHand::Left : EAedirHand::Right;
}

bool UAedirCombatComponent::UpdateAIWeaponStrike(EAedirHand Hand, float Distance, float Reach, float DeltaTime)
{
	// A blow is the same arm the player moves: drawn back to one side or overhead, then thrown across to the other.
	FAedirArmDynamics& Arm = GetArm(Hand);
	AIStrikeTimer += DeltaTime;
	switch (AIStrikePhase)
	{
	case 0:
		Arm.PunchBlend = 0.f;
		// Out of breath it keeps its guard up and gives ground until it has the wind for another blow.
		if (Creature->Stats && Creature->Stats->GetStamina() < FMath::Max(SwingStaminaCost * ComputeWeightFactor(Hand), Creature->Stats->GetMaxStamina() * AedirAIRestStaminaShare))
		{
			Arm.AimOffsetDeg = FVector2D(0.f, 10.f);
			if (Distance < Reach)
			{
				Creature->AddMovementInput(Creature->GetActorForwardVector(), -0.5f);
			}
		}
		else if (Distance <= Reach && AIStrikeTimer >= AIAttackInterval)
		{
			// Blows come from every side and height: across at the body or low at the legs, down on a slant, rising from below,
			// straight down from overhead. One lying on the ground is struck down at from over it.
			const float Side = FMath::RandBool() ? 1.f : -1.f;
			const int32 Style = bAITargetDown ? -1 : FMath::RandRange(0, 5);
			switch (Style)
			{
			case -1:
				Arm.AimOffsetDeg = FVector2D(Side * 15.f, AedirWeaponRaiseFullDegrees);
				AIStrikeEnd = FVector2D(0.f, AedirAIGroundStrikePitch);
				break;
			case 0:
				Arm.AimOffsetDeg = FVector2D(Side * 85.f, 15.f);
				AIStrikeEnd = FVector2D(-Side * 70.f, 0.f);
				break;
			case 1:
				Arm.AimOffsetDeg = FVector2D(Side * 80.f, -15.f);
				AIStrikeEnd = FVector2D(-Side * 60.f, -30.f);
				break;
			case 2:
				Arm.AimOffsetDeg = FVector2D(Side * 70.f, 50.f);
				AIStrikeEnd = FVector2D(-Side * 50.f, -35.f);
				break;
			case 3:
				Arm.AimOffsetDeg = FVector2D(Side * 65.f, -30.f);
				AIStrikeEnd = FVector2D(-Side * 45.f, 45.f);
				break;
			default:
				Arm.AimOffsetDeg = FVector2D(Side * 10.f, AedirWeaponRaiseFullDegrees);
				AIStrikeEnd = FVector2D(-Side * 5.f, -25.f);
				break;
			}
			// The blow is laid on the part of the enemy it is meant for: the way it stands from here and how high that part is,
			// a low blow at the legs, one from overhead at the head, the rest at the body.
			if (Style >= 0)
			{
				const int32 Part = Style == 1 ? 2 : (Style == 2 || Style >= 4) ? 0 : 1;
				const FRotator Toward = (GetAITargetPoint(Part) - ComputeArmShoulder(Hand)).Rotation();
				const float Bearing = FMath::Clamp(FRotator::NormalizeAxis(static_cast<float>(Toward.Yaw - Creature->GetActorRotation().Yaw)), -40.f, 40.f);
				const float Rise = FRotator::NormalizeAxis(static_cast<float>(Toward.Pitch)) * AedirAIStrikeRiseShare;
				const auto Laid = [Bearing, Rise](const FVector2D& Aim)
				{
					return FVector2D(FMath::Clamp(static_cast<float>(Aim.X) + Bearing, -AedirWeaponYawLimit, AedirWeaponYawLimit),
						FMath::Clamp(static_cast<float>(Aim.Y) + Rise, AedirWeaponPitchMin, AedirWeaponRaiseFullDegrees));
				};
				Arm.AimOffsetDeg = Laid(Arm.AimOffsetDeg);
				AIStrikeEnd = Laid(AIStrikeEnd);
			}
			AIStrikePhase = 1;
			AIStrikeTimer = 0.f;
		}
		else if (Distance > Reach)
		{
			Arm.AimOffsetDeg = FVector2D(0.f, 10.f);
		}
		break;

	case 1:
		if (AIStrikeTimer >= AedirAIWindupSeconds)
		{
			Arm.AimOffsetDeg = AIStrikeEnd;
			Arm.PunchBlend = 1.f;
			AIStrikePhase = 2;
			AIStrikeTimer = 0.f;
		}
		break;

	case 2:
		if (AIStrikeTimer >= AedirAIStrikeSeconds)
		{
			Arm.AimOffsetDeg = FVector2D(0.f, 10.f);
			Arm.PunchBlend = 0.f;
			AIStrikePhase = 3;
			AIStrikeTimer = 0.f;
		}
		break;

	default:
		if (AIStrikeTimer >= AedirAIRecoverSeconds)
		{
			AIStrikePhase = 0;
			AIStrikeTimer = 0.f;
		}
		break;
	}
	return true;
}

void UAedirCombatComponent::SetAIGrudge(AAedirCreature* Attacker)
{
	if (Attacker && Attacker != Creature && !Creature->IsPlayerControlled())
	{
		AIGrudge = Attacker;
		AIGrudgeUntil = GetWorld()->GetTimeSeconds() + AedirAIGrudgeSeconds;
		AITarget = Attacker;
		AILastSeenTime = GetWorld()->GetTimeSeconds();
	}
}

void UAedirCombatComponent::UpdateAITarget(double Now)
{
	UWorld* World = GetWorld();
	UAedirGroupSubsystem* Groups = World ? World->GetSubsystem<UAedirGroupSubsystem>() : nullptr;
	if (!Groups)
	{
		return;
	}

	const FVector OwnerLocation = Creature->GetActorLocation();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatSight), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, OwnerLocation, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(AISightRange), Params);

	AAedirCreature* BestTarget = nullptr;
	double BestDistanceSquared = 0.0;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (!Candidate || (Candidate->Stats && Candidate->Stats->IsDead()))
		{
			continue;
		}

		const bool bGrudge = Candidate == AIGrudge && Now < AIGrudgeUntil;
		if ((Groups->GetRelation(Creature->GroupId, Candidate->GroupId) != EAedirRelation::Enemy && !bGrudge) || (!CanSeeTarget(Candidate) && !CanHearTarget(Candidate)))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(OwnerLocation, Candidate->GetActorLocation());
		if (!BestTarget || DistanceSquared < BestDistanceSquared)
		{
			BestTarget = Candidate;
			BestDistanceSquared = DistanceSquared;
		}
	}

	if (BestTarget)
	{
		AITarget = BestTarget;
		AILastSeenTime = Now;
		return;
	}

	const double Memory = AITarget == AIGrudge && Now < AIGrudgeUntil ? static_cast<double>(AedirAIGrudgeSeconds) : static_cast<double>(AedirAIMemorySeconds);
	if (AITarget && (!IsValid(AITarget) || (AITarget->Stats && AITarget->Stats->IsDead()) || Now - AILastSeenTime > Memory))
	{
		AITarget = nullptr;
	}
}

bool UAedirCombatComponent::CanSeeTarget(const AAedirCreature* Target) const
{
	UWorld* World = GetWorld();
	if (!World || !Creature || !Target)
	{
		return false;
	}

	const FVector EyeLocation = Creature->GetPawnViewLocation();
	const FVector TargetLocation = Target->GetPawnViewLocation();
	const FVector ToTarget = (TargetLocation - EyeLocation).GetSafeNormal();

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(AISightHalfAngleDeg));
	if (FVector::DotProduct(Creature->GetActorForwardVector(), ToTarget) < CosHalfAngle)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatVisibility), false);
	Params.AddIgnoredActor(Creature);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, EyeLocation, TargetLocation, ECC_Visibility, Params);
}

bool UAedirCombatComponent::CanHearTarget(const AAedirCreature* Target) const
{
	UWorld* World = GetWorld();
	if (!World || !Creature || !Target)
	{
		return false;
	}

	// A body makes noise as it moves: breathing and the creak of gear close by, footsteps further out, a run or a fight far
	// off. Crouching keeps it quiet, and a wall or a hill between muffles what gets through.
	const float Speed = static_cast<float>(Target->GetVelocity().Size2D());
	float Radius = AedirAIHearBaseCm + Speed * AedirAIHearPerSpeed;
	if (Target->bIsCrouched)
	{
		Radius *= AedirAIHearCrouchShare;
	}
	if (Target->Combat && (Target->Combat->IsInFightStance() || Target->Combat->IsKicking()))
	{
		Radius += AedirAIHearFightCm;
	}
	const FVector Ears = Creature->GetPawnViewLocation();
	const FVector Source = Target->GetActorLocation();
	const double DistanceSquared = FVector::DistSquared(Ears, Source);
	if (DistanceSquared > FMath::Square(Radius))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatHearing), false);
	Params.AddIgnoredActor(Creature);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	const bool bMuffled = World->LineTraceSingleByChannel(Hit, Ears, Source, ECC_Visibility, Params);
	return !bMuffled || DistanceSquared <= FMath::Square(Radius * AedirAIHearMuffledShare);
}

bool UAedirCombatComponent::IsTwoHandGrip() const
{
	return bTwoHandGrip;
}

EAedirHand UAedirCombatComponent::GetGripHand() const
{
	const bool bLeftHeld = GetHeldItem(EAedirHand::Left) != nullptr;
	const bool bRightHeld = GetHeldItem(EAedirHand::Right) != nullptr;
	if (bLeftHeld && bRightHeld)
	{
		// A two-handed item occupies both hands. Its own held hand is the primary one.
		const AAedirItemActor* SharedItem = GetHeldItem(EAedirHand::Left);
		return SharedItem ? SharedItem->HeldHand : EAedirHand::Left;
	}

	return bLeftHeld ? EAedirHand::Left : EAedirHand::Right;
}

bool UAedirCombatComponent::EvaluateTwoHandGrip() const
{
	const AAedirItemActor* LeftItem = GetHeldItem(EAedirHand::Left);
	const AAedirItemActor* RightItem = GetHeldItem(EAedirHand::Right);
	if (!LeftItem && !RightItem)
	{
		return false;
	}

	// A two-handed item is held by both hands as the same item, which does not count as holding a second item.
	if (LeftItem && RightItem && LeftItem != RightItem)
	{
		return false;
	}

	// A big object is a grip only with both hands on it; alone it hangs from one hand.
	if ((LeftItem ? LeftItem : RightItem)->ItemClass == EAedirItemClass::Object)
	{
		return LeftItem && RightItem;
	}

	// A player holds every weapon but a knife or a tool in both hands, while the other hand is empty and not let go of it on
	// purpose (right button held down). Others still bring the free hand on with the right button, or both guards.
	const AAedirItemActor* Single = LeftItem ? LeftItem : RightItem;
	if (Creature && Creature->IsPlayerControlled() && !(LeftItem && RightItem))
	{
		return !bOffHandFree && Single->ItemClass == EAedirItemClass::Weapon && Single->GetWeaponLength() >= AedirTwoHandMinLengthCm;
	}
	return (LeftItem && RightItem) || (bLeftGuard && RightItem && RightItem->ItemClass == EAedirItemClass::Weapon) || (bLeftGuard && bRightGuard);
}

void UAedirCombatComponent::UpdateTwoHandGrip()
{
	bTwoHandGrip = EvaluateTwoHandGrip();
	// Let go of with one hand, the weapon is no longer held by its blade either.
	if (!bTwoHandGrip)
	{
		bHalfSwordToggled = false;
	}
}

FVector UAedirCombatComponent::GetGripPointWorld() const
{
	// The second hand sits on the real weapon: from the real primary hand along the real blade, towards the tip.
	const EAedirHand PrimaryHand = GetGripHand();
	const AAedirItemActor* HeldItem = GetHeldItem(PrimaryHand);
	const USkeletalMeshComponent* BodyMesh = Creature ? Creature->GetMesh() : nullptr;
	const FAedirArmDynamics& PrimaryArm = GetArm(PrimaryHand);
	if (!HeldItem || !BodyMesh || !Creature->Interaction)
	{
		return PrimaryArm.HandWorld;
	}

	return HeldItem->GetActorLocation() + HeldItem->GetActorForwardVector() * GetSecondaryGripLocalCm(HeldItem);
}

float UAedirCombatComponent::ComputeShoveEnergy(float Strength, float StaminaFraction)
{
	const float StaminaScale = FMath::Lerp(AedirShoveMinStaminaScale, 1.f, FMath::Clamp(StaminaFraction, 0.f, 1.f));
	return (AedirShoveBaseJoules + FMath::Max(Strength, 0.f) * AedirShoveStrengthJoules) * StaminaScale;
}

FVector UAedirCombatComponent::EvaluateKickArc(const FVector& Start, const FVector& Hip, const FVector& Forward, float T01, float LiftCm)
{
	FVector Ahead = Forward.GetSafeNormal2D();
	if (Ahead.IsNearlyZero())
	{
		Ahead = FVector::ForwardVector;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	// The chamber: the knee is drawn up towards the belly, the foot hanging just in front of and below the hip.
	const FVector Chamber = Hip + Ahead * AedirKickChamberAheadCm + Right * AedirKickChamberRightCm + FVector(0.f, 0.f, AedirKickChamberBelowHipCm);
	const FVector Extension = Hip + Ahead * AedirKickExtendCm + FVector(0.f, 0.f, AedirKickExtendHeightCm - AedirKickGroundBelowHipCm + LiftCm);
	const float T = FMath::Clamp(T01, 0.f, 1.f);

	if (T < AedirKickChamberTime)
	{
		return FMath::Lerp(Start, Chamber, CombatSmoothStep01(T / AedirKickChamberTime));
	}

	if (T < AedirKickExtendTime)
	{
		return FMath::Lerp(Chamber, Extension, CombatSmoothStep01((T - AedirKickChamberTime) / (AedirKickExtendTime - AedirKickChamberTime)));
	}

	return FMath::Lerp(Extension, Start, CombatSmoothStep01((T - AedirKickExtendTime) / (1.f - AedirKickExtendTime)));
}

bool UAedirCombatComponent::IsKicking() const
{
	return KickNet.bActive;
}

bool UAedirCombatComponent::GetKickPose(bool& bOutLeftLeg, FVector& OutFootTargetWorld, float& OutAlpha) const
{
	bOutLeftLeg = KickNet.bLeftLeg;
	OutAlpha = FMath::Clamp(KickPoseAlpha, 0.f, 1.f);
	OutFootTargetWorld = FVector::ZeroVector;
	if (!Creature || OutAlpha <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float T01 = FMath::Clamp(KickNet.Elapsed / AedirKickDuration, 0.f, 1.f);
	OutFootTargetWorld = EvaluateKickArc(KickNet.StartWorld, GetPelvisWorld(), KickNet.Direction, T01, KickNet.LiftCm);
	return true;
}

bool UAedirCombatComponent::GetKickSole(FVector& OutDirectionWorld, float& OutAlpha) const
{
	if (!Creature || KickPoseAlpha <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// The foot turns toes up on the way out, so that the sole lands on the target.
	const float T01 = FMath::Clamp(KickNet.Elapsed / AedirKickDuration, 0.f, 1.f);
	const float Rise = CombatSmoothStep01((T01 - AedirKickSoleStartTime) / (AedirKickSoleFullTime - AedirKickSoleStartTime));
	const float Fall = 1.f - CombatSmoothStep01((T01 - AedirKickExtendTime) / (AedirKickSoleEndTime - AedirKickExtendTime));
	OutAlpha = FMath::Min(Rise, Fall) * FMath::Clamp(KickPoseAlpha, 0.f, 1.f);
	const FVector Ahead = KickNet.Direction.GetSafeNormal2D();
	OutDirectionWorld = (Ahead * AedirKickExtendCm + FVector(0.f, 0.f, AedirKickExtendHeightCm - AedirKickGroundBelowHipCm + KickNet.LiftCm)).GetSafeNormal();
	return OutAlpha > KINDA_SMALL_NUMBER;
}

bool UAedirCombatComponent::StartShove()
{
	if (!Creature)
	{
		return false;
	}

	if (IsAuthoritative())
	{
		return TryStartShove();
	}

	if (!Creature->IsLocallyControlled() || !CanStartShove())
	{
		return false;
	}

	BeginShove();
	ServerStartShove();
	return true;
}

bool UAedirCombatComponent::StartKick()
{
	if (!Creature)
	{
		return false;
	}

	if (IsAuthoritative())
	{
		return TryStartKick();
	}

	if (!Creature->IsLocallyControlled() || !CanStartKick())
	{
		return false;
	}

	BeginKick();
	ServerStartKick();
	return true;
}

bool UAedirCombatComponent::CanStartShove() const
{
	if (!Creature || !Creature->Stats || bShoving || ShoveCooldownRemaining > 0.f)
	{
		return false;
	}

	// A shove needs both arms free: a running swing on either hand blocks it.
	if (Creature->Stats->GetStamina() < ShoveStaminaCost || GetSwingState(EAedirHand::Left).bActive || GetSwingState(EAedirHand::Right).bActive)
	{
		return false;
	}

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		const AAedirItemActor* HeldItem = GetHeldItem(Hand);
		if ((HeldItem && HeldItem->Weight > AedirShoveHeavyItemKg) || GetLimbInjury(GetArmLimb(Hand)) > AedirShoveArmInjuryLimit)
		{
			return false;
		}
	}

	return true;
}

bool UAedirCombatComponent::TryStartShove()
{
	if (!IsAuthoritative() || !CanStartShove() || !Creature->Stats->ConsumeStamina(ShoveStaminaCost))
	{
		return false;
	}

	BeginShove();
	return true;
}

void UAedirCombatComponent::BeginShove()
{
	bShoving = true;
	ShoveElapsed = 0.f;
	bShoveStruck = false;
	ShoveCooldownRemaining = ShoveCooldown;

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		FAedirArmDynamics& Arm = GetArm(Hand);
		Arm.bScriptedControl = true;
		Arm.ScriptedTargetWorld = GetShoveWorldTarget(Hand, 0.f);
	}
}

void UAedirCombatComponent::EndShove()
{
	bShoving = false;
	ShoveElapsed = 0.f;

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (!GetSwingState(Hand).bActive)
		{
			GetArm(Hand).bScriptedControl = false;
		}
	}
}

void UAedirCombatComponent::UpdateShove(float DeltaTime)
{
	if (!bShoving || !Creature)
	{
		return;
	}

	ShoveElapsed += DeltaTime;

	if (!bShoveStruck && ShoveElapsed >= AedirShoveStrikeSeconds)
	{
		bShoveStruck = true;
		if (IsAuthoritative())
		{
			ResolveShoveStrike();
		}
	}

	if (ShoveElapsed >= AedirShoveDuration)
	{
		EndShove();
		return;
	}

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		GetArm(Hand).ScriptedTargetWorld = GetShoveWorldTarget(Hand, ShoveElapsed);
	}
}

FVector UAedirCombatComponent::GetShoveWorldTarget(EAedirHand Hand, float Elapsed) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	// Both hands thrust to the same point in front of the chest, then draw back to the guard.
	const FVector Guard = GetSwingLocalTarget(Hand, EAedirSwingType::Jab, 0.f);
	const FVector Thrust(AedirShoveReachCm, 0.f, AedirChestHeight);

	float Alpha = 0.f;
	if (Elapsed < AedirShoveStrikeSeconds)
	{
		Alpha = CombatSmoothStep01(Elapsed / AedirShoveStrikeSeconds);
	}
	else
	{
		Alpha = 1.f - CombatSmoothStep01((Elapsed - AedirShoveStrikeSeconds) / (AedirShoveDuration - AedirShoveStrikeSeconds));
	}

	const float Scale = static_cast<float>(Creature->GetActorScale3D().X);
	return Creature->GetActorTransform().TransformPositionNoScale(FMath::Lerp(Guard, Thrust, Alpha) * Scale);
}

void UAedirCombatComponent::ResolveShoveStrike()
{
	UWorld* World = GetWorld();
	if (!World || !Creature)
	{
		return;
	}

	const FVector OwnerLocation = Creature->GetActorLocation();
	const FVector Forward = Creature->GetActorForwardVector().GetSafeNormal2D();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatShove), false);
	Params.AddIgnoredActor(Creature);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, OwnerLocation, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(ShoveRange), Params);

	AAedirCreature* Victim = nullptr;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AAedirCreature* Candidate = Cast<AAedirCreature>(Overlap.GetActor());
		if (!Candidate || Candidate == Creature || (Candidate->Stats && Candidate->Stats->IsDead()))
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - OwnerLocation;
		if (ToCandidate.Size() > ShoveRange || FVector::DotProduct(Forward, ToCandidate.GetSafeNormal2D()) <= AedirShoveConeCos)
		{
			continue;
		}

		Victim = Candidate;
		break;
	}

	if (!Victim)
	{
		return;
	}

	const float Strength = Creature->Stats ? Creature->Stats->Strength : AedirDefaultStat;
	const float StaminaFraction = Creature->Stats ? Creature->Stats->GetStaminaFraction() : 1.f;

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Zone = EAedirBodyZone::Torso;
		Impact.EnergyJoules = ComputeShoveEnergy(Strength, StaminaFraction);
		Impact.Direction = Forward;
		Impact.Location = Victim->GetActorLocation();
		Victim->Balance->ApplyHitReaction(Impact);
	}

	// The shove pushes back on the attacker. A shove does not wound.
	if (Creature->Balance)
	{
		Creature->Balance->ApplyBalanceImpact(-Forward, AedirShoveRecoilBalance);
	}

	OnShove.Broadcast(Victim);
}

bool UAedirCombatComponent::CanStartKick() const
{
	if (!Creature || !Creature->Stats || KickNet.bActive || KickCooldownRemaining > 0.f)
	{
		return false;
	}

	if (Creature->Stats->GetStamina() < KickStaminaCost)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Creature->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround())
	{
		return false;
	}

	if (Creature->Balance && (Creature->Balance->IsRagdoll() || Creature->Balance->GetBalance() < AedirKickMinBalance))
	{
		return false;
	}

	return true;
}

bool UAedirCombatComponent::TryStartKick()
{
	if (!IsAuthoritative() || !CanStartKick() || !Creature->Stats->ConsumeStamina(KickStaminaCost))
	{
		return false;
	}

	BeginKick();

	// Standing on one leg costs balance.
	if (Creature->Balance)
	{
		Creature->Balance->ApplyBalanceImpact(-Creature->GetActorForwardVector(), AedirKickRecoilBalance);
	}

	return true;
}

void UAedirCombatComponent::BeginKick()
{
	const bool bLeftLeg = IsLeadFootRight();
	KickNet.bActive = true;
	KickNet.bLeftLeg = bLeftLeg;
	KickNet.Elapsed = 0.f;
	KickNet.StartWorld = GetFootBoneWorld(bLeftLeg);
	// The leg goes where the camera looks, as far as a leg can turn from the body.
	const FRotator View = Creature->Controller ? Creature->GetControlRotation() : Creature->GetActorRotation();
	const double YawFromBody = FMath::Clamp(FRotator::NormalizeAxis(View.Yaw - Creature->GetActorRotation().Yaw), -AedirKickMaxYawDegrees, AedirKickMaxYawDegrees);
	KickNet.Direction = FRotator(0.0, Creature->GetActorRotation().Yaw + YawFromBody, 0.0).Vector();
	KickNet.LiftCm = FMath::Clamp(AedirKickExtendCm * FMath::Sin(FMath::DegreesToRadians(static_cast<float>(FRotator::NormalizeAxis(View.Pitch)))), AedirKickMinLiftCm, AedirKickMaxLiftCm);
	KickPrevFootWorld = KickNet.StartWorld;
	bKickStruck = false;
	KickCooldownRemaining = KickCooldown;
}

void UAedirCombatComponent::UpdateKick(float DeltaTime)
{
	KickPoseAlpha = FMath::FInterpConstantTo(KickPoseAlpha, KickNet.bActive ? 1.f : 0.f, DeltaTime,
		KickNet.bActive ? 1.f / AedirKickPoseRiseSeconds : 1.f / AedirKickPoseFallSeconds);

	// A knockdown ends the kick at once.
	if (IsAuthoritative() && KickNet.bActive && Creature->Balance && Creature->Balance->IsRagdoll())
	{
		KickNet.bActive = false;
	}

	if (!KickNet.bActive || !Creature)
	{
		return;
	}

	KickNet.Elapsed = DevKickHoldShare >= 0.f ? DevKickHoldShare * AedirKickDuration : FMath::Min(KickNet.Elapsed + DeltaTime, AedirKickDuration);

	if (IsAuthoritative())
	{
		const FVector FootWorld = GetFootBoneWorld(KickNet.bLeftLeg);
		if (!bKickStruck && KickNet.Elapsed >= AedirKickStrikeStart && KickNet.Elapsed <= AedirKickStrikeEnd)
		{
			ResolveKickSweep(KickPrevFootWorld, FootWorld, DeltaTime);
		}

		KickPrevFootWorld = FootWorld;

		if (KickNet.Elapsed >= AedirKickDuration)
		{
			KickNet.bActive = false;
		}
	}
}

void UAedirCombatComponent::ResolveKickSweep(const FVector& From, const FVector& To, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !Creature || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCombatKick), false);
	Params.AddIgnoredActor(Creature);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(AedirKickHitRadius), Params);

	const FVector FootMotion = To - From;
	const float FootSpeed = FMath::Min(static_cast<float>(FootMotion.Size() / DeltaTime), AedirKickMaxFootSpeed);

	for (const FHitResult& Hit : Hits)
	{
		AAedirCreature* Victim = Cast<AAedirCreature>(Hit.GetActor());
		if (!Victim || Victim == Creature || Hit.GetComponent() != Victim->GetMesh())
		{
			continue;
		}

		ApplyKickImpact(Victim, Hit, FootMotion, FootSpeed);
		bKickStruck = true;
		return;
	}
}

void UAedirCombatComponent::ApplyKickImpact(AAedirCreature* Victim, const FHitResult& Hit, const FVector& FootMotion, float FootSpeed)
{
	const FName Bone = Hit.BoneName;
	EAedirBodyZone Zone = ClassifyBodyZone(Bone);
	if (Zone != EAedirBodyZone::Leg)
	{
		Zone = EAedirBodyZone::Torso;
	}

	const float Energy = ComputeImpactEnergy(AedirKickMassKg, FootSpeed) * AedirKickEnergyScale;
	if (Creature->IsFriendOf(Victim))
	{
		PushFriend(Victim, Bone, Zone, Hit.ImpactPoint, FootMotion, Energy, AedirKickMassKg, FootSpeed);
		return;
	}
	const float VictimZoneProtection = Victim->Interaction ? Victim->Interaction->GetArmorAt(Bone, Hit.ImpactPoint, EAedirDamageType::Blunt).Protection : 0.f;
	const float Damage = Energy * ImpactDamageScale * ComputeDamageTypeFactor(EAedirDamageType::Blunt, 0.f) * GetZoneDamageMultiplier(Zone) * (1.f - VictimZoneProtection);
	Victim->ReceiveCreatureDamage(Damage);

	EAedirLimb Limb = EAedirLimb::LeftArm;
	if (Victim->Combat && GetBoneLimb(Bone, Limb))
	{
		Victim->Combat->ApplyLimbInjury(Limb, Damage * AedirImpactInjuryPerDamage);
	}

	if (Victim->Balance)
	{
		FAedirImpactInfo Impact;
		Impact.Bone = Bone;
		Impact.Zone = Zone;
		Impact.EnergyJoules = Energy;
		Impact.Direction = FootMotion.GetSafeNormal();
		Impact.Location = Hit.ImpactPoint;
		Impact.MassKg = AedirKickMassKg;
		Impact.RelativeSpeed = FootSpeed;
		Victim->Balance->ApplyHitReaction(Impact);
	}

	if (Victim->Wounds)
	{
		Victim->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, EAedirDamageType::Blunt, Energy, VictimZoneProtection, FootMotion);
	}

	OnKick.Broadcast(Victim);
}

FVector UAedirCombatComponent::GetFootBoneWorld(bool bLeftLeg) const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	if (!Mesh)
	{
		return Creature->GetActorLocation();
	}

	return Mesh->GetBoneLocation(bLeftLeg ? AedirKickFootLeftBone : AedirKickFootRightBone);
}

FVector UAedirCombatComponent::GetPelvisWorld() const
{
	if (!Creature)
	{
		return FVector::ZeroVector;
	}

	const USkeletalMeshComponent* Mesh = Creature->GetMesh();
	return Mesh ? Mesh->GetBoneLocation(AedirKickPelvisBone) : Creature->GetActorLocation();
}

void UAedirCombatComponent::ServerStartShove_Implementation()
{
	TryStartShove();
}

void UAedirCombatComponent::ServerStartKick_Implementation()
{
	TryStartKick();
}

void UAedirCombatComponent::ServerSetGuard_Implementation(EAedirHand Hand, bool bActive)
{
	SetGuardInternal(Hand, bActive);
}

void UAedirCombatComponent::ServerSetHandModes_Implementation(bool bNewOffHandFree, bool bNewHalfSword)
{
	bOffHandFree = bNewOffHandFree;
	bHalfSwordToggled = bNewHalfSword;
}

void UAedirCombatComponent::SetOffHandFree(bool bFree)
{
	bOffHandFree = bFree;
	if (!IsAuthoritative())
	{
		ServerSetHandModes(bOffHandFree, bHalfSwordToggled);
	}
}

void UAedirCombatComponent::ToggleHalfSword()
{
	bHalfSwordToggled = !bHalfSwordToggled;
	if (!IsAuthoritative())
	{
		ServerSetHandModes(bOffHandFree, bHalfSwordToggled);
	}
}

void UAedirCombatComponent::ServerUpdateArmAim_Implementation(EAedirHand Hand, FVector2D Aim, float Extension)
{
	if (!IsGuardActive(Hand) || IsSecondaryGripHand(Hand))
	{
		return;
	}

	FAedirArmDynamics& Arm = GetArm(Hand);
	Arm.AimOffsetDeg = FVector2D(
		FMath::Clamp(Aim.X, -AedirArmAimYawLimit, AedirArmAimYawLimit),
		FMath::Clamp(Aim.Y, AedirArmAimPitchMin, AedirArmAimPitchMax));
	Arm.Extension = FMath::Clamp(Extension, 0.f, 1.f);
}

void UAedirCombatComponent::MulticastArmourDent_Implementation(AAedirCreature* Victim, FName Bone, FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, float EnergyJoules)
{
	USkeletalMeshComponent* Body = Victim ? Victim->GetMesh() : nullptr;
	static TWeakObjectPtr<UMaterialInterface> DentMaterial;
	if (!DentMaterial.IsValid())
	{
		DentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aedir/Materials/M_ArmourDent.M_ArmourDent"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Body || !DentMaterial.IsValid() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// The dent is pressed into whatever lies outermost at the place: the plate over the body.
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(DentMaterial.Get(), Victim);
	Material->SetScalarParameterValue(TEXT("Depth"), FMath::Clamp(EnergyJoules / 300.f, 0.4f, 1.2f));
	Material->SetScalarParameterValue(TEXT("Seed"), FMath::FRandRange(0.f, 10.f));
	const float Radius = FMath::Clamp(AedirDentRadiusMinCm + EnergyJoules / AedirDentJoulesPerCm, AedirDentRadiusMinCm, AedirDentRadiusMaxCm);
	FRotator Rotation = FRotationMatrix::MakeFromX(-FVector(Normal)).Rotator();
	Rotation.Roll = FMath::FRandRange(0.f, 360.f);
	if (UDecalComponent* Dent = UGameplayStatics::SpawnDecalAttached(Material, FVector(AedirDentDepthCm, Radius, Radius), Body, Bone, FVector(Point), Rotation, EAttachLocation::KeepWorldPosition, 0.f))
	{
		Dent->SetFadeScreenSize(0.f);
	}
}
