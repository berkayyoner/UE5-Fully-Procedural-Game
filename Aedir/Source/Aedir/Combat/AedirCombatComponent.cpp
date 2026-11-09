#include "Combat/AedirCombatComponent.h"

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
	constexpr float AedirArmPunchRiseSpeed = 30.f;
	constexpr float AedirArmPunchFallSpeed = 8.f;
	// Guard: both fists meet in front of the face, at nose height measured from the capsule centre.
	constexpr float AedirGuardForwardCm = 32.f;
	constexpr float AedirGuardInwardCm = 1.5f;
	constexpr float AedirGuardFaceHeightCm = 62.f;
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
	constexpr float AedirWeaponMouseShare = 1.8f;
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
	// Raising the mouse takes the hands just over the head with the elbows bent, the blade straight back behind them along the middle of the body.
	constexpr float AedirWeaponRaiseFullDegrees = 80.f;
	constexpr float AedirWeaponRaisedForwardCm = 20.f;
	constexpr float AedirWeaponRaisedSideCm = 8.f;
	constexpr float AedirWeaponRaisedSideTwoHandCm = 4.f;
	constexpr float AedirWeaponRaisedHeightCm = 88.f;
	constexpr float AedirWeaponRaisedBack = 0.96f;
	constexpr float AedirWeaponRaisedDown = 0.28f;
	constexpr float AedirWeaponRaisedYawShare = 0.3f;
	// A weapon carried in the loose arm hangs from the hand. A long one leans forward so its point stays off the ground.
	constexpr float AedirCarryHangPitch = 30.f;
	constexpr float AedirCarryHandHeightCm = 72.f;
	// A two-handed weapon in one hand: the arm has to bear all of it, far from the hand.
	constexpr float AedirOneHandedStrainMass = 3.4f;
	constexpr float AedirOneHandedDroopCm = 8.f;
	constexpr float AedirOneHandedDroopPitch = 30.f;
	// Held items stop at what they hit. Only what is light enough gives way.
	constexpr float AedirBladeGuardCm = 10.f;
	constexpr float AedirBladeRadiusCm = 2.5f;
	constexpr float AedirBladeBlockMassKg = 40.f;
	constexpr float AedirBladePushMaxSpeed = 500.f;
	// A clash of weapons: the fastest a blade's tip counts, and how much of the push the struck hand takes.
	constexpr float AedirClashMaxSpeed = 1500.f;
	constexpr float AedirClashHandShare = 0.6f;

	// The mouse moves the hand: around its own shoulder (two hands on one grip around the middle of the chest), the mouse's
	// side to side turning it around the body and its up and down raising it from low to over the head.
	static TAutoConsoleVariable<int32> CVarAedirHandLedSwing(TEXT("aedir.HandLedSwing"), 1, TEXT("The mouse moves the hand and the blade follows it by its weight (1), or swings go through set poses (0)."));
	constexpr float AedirHandLedYawShare = 1.1f;
	constexpr float AedirHandLedInwardYaw = 12.f;
	constexpr float AedirHandLedRestPitch = 18.f;
	constexpr float AedirHandLedPitchMin = -75.f;
	constexpr float AedirHandLedPitchMax = 78.f;
	constexpr float AedirHandLedNearCm = 32.f;
	constexpr float AedirHandLedFarCm = 50.f;
	constexpr float AedirHandLedBehindShorterCm = 10.f;
	// How far round a hand can be held: out to its own side, and across in front of the body to the other.
	constexpr float AedirHandLedOutwardYaw = 100.f;
	constexpr float AedirHandLedAcrossYaw = 55.f;
	constexpr float AedirHandLedTwoHandSideCm = 6.f;
	// The blade stands out of the fist this far above the line of the arm; weight and tiredness let it sink.
	constexpr float AedirHandLedBladeRaise = 50.f;
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
	constexpr float AedirThrustMouseExtension = 0.012f;
	constexpr float AedirThrustSteerShare = 0.25f;
	constexpr float AedirThrustHandMaxCm = 56.f;
	// A drawn-back thrust waits with the hand beside the waist.
	constexpr float AedirThrustWaistForwardCm = -4.f;
	constexpr float AedirThrustWaistSideCm = 32.f;
	constexpr float AedirThrustWaistSideTwoHandCm = 22.f;
	constexpr float AedirThrustWaistUpCm = 2.f;
	// It goes out a shoulder's width from the middle with the point converging on the middle ahead, the elbow pointing back.
	constexpr float AedirThrustSideCm = 24.f;
	constexpr float AedirThrustChestHeightCm = 30.f;
	constexpr float AedirThrustInwardYaw = 8.f;
	constexpr float AedirArmGuardRestYaw = 12.f;
	constexpr float AedirArmGuardRestPitch = 15.f;
	constexpr float AedirArmExtensionStep = 0.08f;
	constexpr int32 AedirArmorZoneCount = 4;
	constexpr int32 AedirLimbCount = 4;
	constexpr float AedirArmorMaxProtection = 0.9f;

	// A graze does little of what a weapon can do; the full damage takes a blow with this much energy behind it.
	constexpr float AedirImpactFullFlatJoules = 120.f;
	constexpr float AedirImpactMaxHealthShare = 0.6f;
	constexpr float AedirAIRestStaminaShare = 0.15f;
	constexpr float AedirAIFleeCm = 1500.f;
	constexpr float AedirImpactMaxSpeed = 5000.f;
	// Room the own body takes up, which a held blade keeps out of.
	constexpr float AedirOwnTrunkRadiusCm = 17.f;
	constexpr float AedirOwnHeadRadiusCm = 13.f;
	constexpr float AedirOwnThighRadiusCm = 9.f;
	constexpr float AedirOwnBladeGuardCm = 12.f;
	// How a blow that does not get through comes off the body, and how long a clean cut may carry on through.
	constexpr float AedirBluntBounce = 0.2f;
	constexpr double AedirPassThroughSeconds = 0.35;
	// How fast a caught blade has to have been going to tear itself out of the hand.
	constexpr float AedirWrenchSpeedBase = 2600.f;
	constexpr float AedirWrenchSpeedPerStrength = 12.f;
	constexpr float AedirWrenchTwoHandShare = 1.6f;
	// An edge needs this much energy, before armour, to go right through each part of the body. A blow well short of it
	// glances off, one in between bites in and sticks, and anything past it carries on.
	constexpr float AedirCutHeadJoules = 320.f;
	constexpr float AedirCutTorsoJoules = 340.f;
	constexpr float AedirCutArmJoules = 120.f;
	constexpr float AedirCutLegJoules = 190.f;
	constexpr float AedirCutGlanceShare = 0.3f;
	constexpr float AedirCutLuck = 0.2f;
	constexpr float AedirGlanceBounce = 0.35f;
	constexpr float AedirGlanceDamageShare = 0.4f;
	constexpr float AedirCutThroughKeep = 0.7f;
	constexpr float AedirLodgedMinSeconds = 0.12f;
	constexpr float AedirLodgedMaxSeconds = 0.45f;
	constexpr float AedirLodgedTearCm = 14.f;
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
	constexpr float AedirBreakArmJoules = 380.f;
	constexpr float AedirBreakLegJoules = 520.f;
	constexpr float AedirBreakEndShare = 0.5f;
	constexpr float AedirDentShowJoules = 70.f;
	constexpr float AedirHelmetOffFromJoules = 180.f;
	constexpr float AedirHelmetOffSpreadJoules = 500.f;
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
	constexpr float AedirImpactNoHarmJoules = 25.f;
	// Most of a blow goes into the wound. This share of it is what pushes the body off its feet.
	constexpr float AedirImpactBodyEnergyShare = 0.35f;
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
	constexpr float AedirKickHitRadius = 14.f;
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
			Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X + MouseDeltaThisFrame.X * AimSensitivity, -AedirWeaponYawLimit, AedirWeaponYawLimit);
			return;
		}

		Arm.AimOffsetDeg.X = FMath::Clamp(Arm.AimOffsetDeg.X + MouseDeltaThisFrame.X * AimSensitivity * AedirWeaponMouseShare, -AedirWeaponYawLimit, AedirWeaponYawLimit);
		Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity * AedirWeaponMouseShare, AedirWeaponPitchMin, AedirWeaponRaiseFullDegrees);
		return;
	}

	// A quick mouse movement throws the arm out along the aim; a slow one pulls it back to the guard in front of the face.
	const float RawSpeed = static_cast<float>(MouseDeltaThisFrame.Size()) / DeltaTime;
	// Attack is instant so a flick counts at once; the speed then decays smoothly.
	Arm.MouseSpeed = FMath::Max(RawSpeed, FMath::FInterpTo(Arm.MouseSpeed, RawSpeed, DeltaTime, AedirArmMouseSpeedSmoothing));
	const float PunchTarget = FMath::Clamp((Arm.MouseSpeed - AedirArmGuardSpeedLow) / (AedirArmGuardSpeedHigh - AedirArmGuardSpeedLow), 0.f, 1.f);
	Arm.PunchBlend = FMath::FInterpTo(Arm.PunchBlend, PunchTarget, DeltaTime, PunchTarget > Arm.PunchBlend ? AedirArmPunchRiseSpeed : AedirArmPunchFallSpeed);

	// Mouse Y input is positive when the mouse moves up, so moving the mouse up raises the arm.
	// Outwards the arm sweeps a 90 degree arc, across the body it only reaches a little past the centre line.
	const float OutwardSign = Hand == EAedirHand::Right ? 1.f : -1.f;
	const float OutwardYaw = FMath::Clamp(Arm.AimOffsetDeg.X * OutwardSign + MouseDeltaThisFrame.X * OutwardSign * AimSensitivity, -AedirArmAcrossYawLimit, AedirArmOutwardYawLimit);
	Arm.AimOffsetDeg.X = OutwardYaw * OutwardSign;
	Arm.AimOffsetDeg.Y = FMath::Clamp(Arm.AimOffsetDeg.Y + MouseDeltaThisFrame.Y * AimSensitivity, AedirArmAimPitchMin, AedirFistAimPitchMax);
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

	return GuardAlpha[HandIndex(Hand)];
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
	return HeldItem && HeldItem->ItemClass == EAedirItemClass::Weapon && (IsArmActive(Hand) || (IsGripHeld() && Hand == GetGripHand()));
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
	return GetWorld() && GetWorld()->SweepSingleByObjectType(OutHit, HandWorld + Direction * AedirBladeGuardCm, HandWorld + Direction * LengthCm, FQuat::Identity, GetHeldBlockObjects(), FCollisionShape::MakeSphere(AedirBladeRadiusCm), Params);
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
		const EAedirHand OtherHand = Hit.BoneName.ToString().EndsWith(TEXT("_l")) ? EAedirHand::Left : EAedirHand::Right;
		const AAedirItemActor* OtherItem = OtherCombat ? OtherCombat->GetHeldItem(OtherHand) : nullptr;
		const bool bOnWeapon = OtherItem && (Hit.GetActor() == OtherItem || Hit.BoneName.ToString().StartsWith(TEXT("hand")));
		if (bOnWeapon)
		{
			const FVector TipMotion = ((Arm.HandWorld + Arm.WeaponDir * Length) - (PrevHand + PrevDir * Length)) / DeltaTime;
			const float MyMass = Item->Weight + ForearmMass;
			const float TheirMass = OtherItem->Weight + ForearmMass;
			const FVector Push = TipMotion.GetClampedToMaxSize(AedirClashMaxSpeed) * (MyMass / (MyMass + TheirMass));
			OtherCombat->ReceiveClash(OtherHand, Push);
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
	return Bend * GetFightShare();
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
	const bool bHalfSwordWanted = bTwoHandGrip && bLeftGuard && CanHalfSword(Item) && IsGripHeld() && bBothOnItem;
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

float UAedirCombatComponent::GetSecondaryGripLocalCm(const AAedirItemActor* Item) const
{
	// The upper hand slides from the handle up onto the blade; the carrying hand stays on the handle.
	const float BladeCm = Item->GripLocalCm + FMath::Clamp(Item->GetWeaponLength() * AedirHalfSwordHandsApartShare, AedirHalfSwordHandsApartMinCm, AedirHalfSwordHandsApartMaxCm);
	return FMath::Lerp(Item->GripLocalCm + Item->GripSpanCm, BladeCm, CombatSmoothStep01(HalfSwordAlpha));
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
	const double ActorYaw = Creature->GetActorRotation().Yaw;
	const FVector2D Aim = Arm.PoseAimDeg;
	const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	if (bThrustMode)
	{
		const double CameraPitch = FRotator::NormalizeAxis(Creature->GetControlRotation().Pitch);
		return FRotator(CameraPitch, ActorYaw - HandSide * AedirThrustInwardYaw + Aim.X * AedirThrustSteerShare, 0.0).Vector();
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
	return HalfSwordAlpha > 0.f ? FMath::Lerp(StanceDir, ComputeHalfSwordDir(Hand, Arm), CombatSmoothStep01(HalfSwordAlpha)).GetSafeNormal() : StanceDir;
}

bool UAedirCombatComponent::UsesHandLedSwing(EAedirHand Hand) const
{
	const AAedirItemActor* Item = GetHeldItem(Hand);
	return CVarAedirHandLedSwing.GetValueOnGameThread() != 0 && !bThrustMode && !(bTwoHandGrip && IsPolearm(Item)) && !GetArm(Hand).bScriptedControl;
}

FVector UAedirCombatComponent::ComputeHandLedPivotLocal(EAedirHand Hand) const
{
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	return FVector(0.f, Side * (bTwoHandGrip ? AedirHandLedTwoHandSideCm : AedirShoulderLateral), AedirShoulderHeight);
}

FVector UAedirCombatComponent::ComputeHandLedHandLocal(EAedirHand Hand, const FAedirArmDynamics& Arm) const
{
	const float Side = Hand == EAedirHand::Right ? 1.f : -1.f;
	const FVector2D Aim = Arm.PoseAimDeg;
	// Round to the far side the arm runs out of shoulder before the mouse runs out: the hand stops there, elbow down, rather
	// than the elbow turning over to reach further.
	const float RawYaw = static_cast<float>(Aim.X) * AedirHandLedYawShare - Side * AedirHandLedInwardYaw;
	const float Yaw = Side > 0.f ? FMath::Clamp(RawYaw, -AedirHandLedAcrossYaw, AedirHandLedOutwardYaw) : FMath::Clamp(RawYaw, -AedirHandLedOutwardYaw, AedirHandLedAcrossYaw);
	const float Pitch = FMath::Clamp(AedirHandLedRestPitch + static_cast<float>(Aim.Y), AedirHandLedPitchMin, AedirHandLedPitchMax);
	// The wheel sets how far out the hand is held, a quick throw takes it all the way, and round behind the body the arm
	// cannot reach as far.
	const float Out = FMath::Lerp(FMath::Clamp(Arm.Extension, 0.f, 1.f), 1.f, Arm.PunchBlend);
	const float Behind = FMath::Clamp((FMath::Abs(Yaw) - 70.f) / 40.f, 0.f, 1.f);
	const float Reach = FMath::Lerp(AedirHandLedNearCm, AedirHandLedFarCm, Out) - Behind * AedirHandLedBehindShorterCm;
	FVector Hand3 = ComputeHandLedPivotLocal(Hand) + FRotator(Pitch, Yaw, 0.f).Vector() * Reach;
	Hand3.Z -= Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopCm : 0.f;
	return Hand3;
}

FVector UAedirCombatComponent::ComputeHandLedBladeDir(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item) const
{
	// The blade comes out of the fist at an angle above the line of the arm: up from a hand held low or to the side, back
	// over the head from a hand raised overhead. Swinging the hand round, its weight trails and swings on through the wrist.
	const FVector ArmLine = (ComputeHandLedHandLocal(Hand, Arm) - ComputeHandLedPivotLocal(Hand)).GetSafeNormal();
	FVector Up = FVector::VectorPlaneProject(FVector::UpVector, ArmLine).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = -FVector::ForwardVector;
	}
	const float Droop = (Item && Item->bTwoHanded && !bTwoHandGrip ? AedirOneHandedDroopPitch : 0.f) + (Creature->Stats ? Creature->Stats->GetFatigue() * AedirFatigueDroopPitch : 0.f)
		+ (Item ? Item->Weight * (bTwoHandGrip ? 0.5f : 1.f) * AedirWeaponWeightDroopPerKg : 0.f);
	const float Raise = FMath::DegreesToRadians(FMath::Max(AedirHandLedBladeRaise - Droop, 0.f));
	const FVector Local = ArmLine * FMath::Cos(Raise) + Up * FMath::Sin(Raise);
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

void UAedirCombatComponent::ComputePolearmPose(EAedirHand Hand, const FAedirArmDynamics& Arm, const AAedirItemActor* Item, FVector& OutHandLocal, FVector& OutDirLocal) const
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
	const float HangPitch = Length > AedirCarryHandHeightCm ? FMath::RadiansToDegrees(FMath::Asin(AedirCarryHandHeightCm / Length)) : AedirCarryHangPitch;
	return FRotator(-FMath::Min(HangPitch, AedirCarryHangPitch), Creature->GetActorRotation().Yaw, 0.0).Vector();
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

	if (bTwoHandGrip && IsPolearm(Item))
	{
		FVector PolearmHand;
		FVector PolearmDir;
		ComputePolearmPose(Hand, Arm, Item, PolearmHand, PolearmDir);
		return PolearmHand;
	}

	if (UsesHandLedSwing(Hand))
	{
		return ComputeHandLedHandLocal(Hand, Arm);
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
	const FVector WoundUp = bAcrossBody ? FVector(0.6f, Side * 0.15f, -0.6f) : FVector(0.2f, Side * 0.85f, -0.5f);
	FVector Hint = FMath::Lerp(FVector(-0.35f, Side * 0.75f, -0.55f), WoundUp, WindShare);
	Hint = FMath::Lerp(Hint, FVector(0.45f, Side * 0.85f, 0.1f), CombatRaiseShare(static_cast<float>(Arm.PoseAimDeg.Y), AedirWeaponRaiseFullDegrees));
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

float UAedirCombatComponent::GetArmLedShare(EAedirHand Hand) const
{
	// A blade swung in one hand, or by the carrying hand of two, is pointed by the arm. A thrust lays it along the forearm
	// and a long shaft is steered by both hands, so those keep their own wrists.
	const AAedirItemActor* Item = GetHeldItem(Hand);
	if (!Item || !IsWeaponStance(Hand) || IsSecondaryGripHand(Hand) || bThrustMode || (bTwoHandGrip && IsPolearm(Item)) || GetArm(Hand).bScriptedControl)
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
	const float CarriedKg = WeaponWeight * Strain * (bTwoHandGrip ? 0.5f : 1.f);
	const float Rate = AedirWeaponTurnRate * StrengthFactor / (1.f + CarriedKg * AedirWeaponTurnMassShare);
	const float Frequency = Rate * AedirWeaponTurnFrequencyShare;
	const float DampingRatio = FMath::Clamp(AedirWeaponTurnDampingLight - CarriedKg * AedirWeaponTurnDampingPerKg, AedirWeaponTurnDampingHeavy, AedirWeaponTurnDampingLight);
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
	return IsArmActive(Hand) || bShoving || (IsGripHeld() && (IsSecondaryGripHand(Hand) || Hand == GetGripHand()));
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
		if (bThrustMode)
		{
			const float HandSide = Hand == EAedirHand::Right ? 1.f : -1.f;
			const FVector Waist = Body.TransformPositionNoScale(FVector(AedirThrustWaistForwardCm, HandSide * (bTwoHandGrip ? AedirThrustWaistSideTwoHandCm : AedirThrustWaistSideCm), AedirThrustWaistUpCm) * WeaponScale);
			const FVector Chest = Body.TransformPositionNoScale(FVector(0.f, HandSide * AedirThrustSideCm, AedirThrustChestHeightCm) * WeaponScale);
			HandTarget = FMath::Lerp(Waist, Chest + ComputeWeaponTargetDir(Hand, Arm) * ((IsPolearm(GetHeldItem(Hand)) && bTwoHandGrip ? AedirPolearmThrustHandCm : AedirThrustHandMaxCm) * WeaponScale), FMath::Clamp(Arm.Extension, 0.f, 1.f));
		}
		else
		{
			HandTarget = Body.TransformPositionNoScale(ComputeStanceHandLocal(Hand, Arm) * WeaponScale);
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

	// The guard fist sits in front of the face, a little inside its own shoulder.
	const float GuardSide = Hand == EAedirHand::Right ? 1.f : -1.f;
	// The mouse still moves the guard up and down around the face.
	const float GuardHeightCm = AedirGuardFaceHeightCm + (Arm.AimOffsetDeg.Y - AedirArmGuardRestPitch) * AedirGuardHeightPerDegree;
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
	ResolveHeldCollision(Hand, Arm, PrevHand, PrevDir, DeltaTime);
	KeepOutOfOwnBody(Hand, Arm);
	UpdateLodged(Hand, Arm, DeltaTime);
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

	// A hard pull tears it out early; otherwise it works free on its own after a moment.
	Arm.LodgedSeconds -= DeltaTime;
	const bool bTorn = FVector::Dist(ComputeArmTarget(Hand, Arm), Held) > AedirLodgedTearCm * Scale;
	if (Arm.LodgedSeconds <= 0.f || bTorn || Dir.IsNearlyZero())
	{
		if (AAedirCreature* Victim = Cast<AAedirCreature>(Body->GetOwner()); Victim && Victim->Wounds && bTorn && IsAuthoritative())
		{
			Victim->Wounds->AddWound(Arm.LodgedBone, Tip, -Dir, EAedirDamageType::Slash, AedirCutArmJoules * 0.25f, 0.f, Dir * 10.f);
		}
		Arm.LodgedIn.Reset();
		Arm.LodgedSeconds = 0.f;
		return;
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

void UAedirCombatComponent::UpdateImpacts(EAedirHand Hand, FAedirArmDynamics& Arm, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || DeltaTime <= KINDA_SMALL_NUMBER || !IsArmActive(Hand))
	{
		return;
	}

	const FVector Tip = ComputeArmTip(Hand, Arm);
	const FVector TipVelocity = (Tip - Arm.PrevTipWorld) / DeltaTime;
	const float Speed = static_cast<float>(FMath::Max(Arm.HandVelocity.Size(), TipVelocity.Size()));
	if (Speed <= AedirArmMinSpeed)
	{
		return;
	}

	const FVector WeaponDirection = (Arm.HandWorld - Arm.ShoulderWorld).GetSafeNormal();
	const float AxialAlignment = static_cast<float>(FMath::Abs(FVector::DotProduct(TipVelocity.GetSafeNormal(), WeaponDirection)));
	const float MassEff = GetArmTotalMass(Hand);
	const double Now = World->GetTimeSeconds();

	ProcessImpactSweep(Hand, Arm, Arm.PrevHandWorld, Arm.HandWorld, TipVelocity, MassEff, AxialAlignment, Now);
	ProcessImpactSweep(Hand, Arm, Arm.PrevTipWorld, Tip, TipVelocity, MassEff, AxialAlignment, Now);
	ProcessImpactSweep(Hand, Arm, (Arm.PrevHandWorld + Arm.PrevTipWorld) * 0.5, (Arm.HandWorld + Tip) * 0.5, TipVelocity, MassEff, AxialAlignment, Now);
}

bool UAedirCombatComponent::IsImpactOnCooldown(EAedirHand Hand, const AActor* Victim, double Now) const
{
	for (const FAedirTimedContact& Record : ImpactRecords[HandIndex(Hand)])
	{
		if (Record.Target.Get() == Victim && Now - Record.Time < ImpactCooldown)
		{
			return true;
		}
	}

	return false;
}

void UAedirCombatComponent::RegisterImpact(EAedirHand Hand, AActor* Victim, double Now)
{
	TArray<FAedirTimedContact>& Records = ImpactRecords[HandIndex(Hand)];
	Records.RemoveAll([this, Now](const FAedirTimedContact& Record)
	{
		return !Record.Target.IsValid() || Now - Record.Time >= ImpactCooldown;
	});

	FAedirTimedContact* Existing = Records.FindByPredicate([Victim](const FAedirTimedContact& Record)
	{
		return Record.Target.Get() == Victim;
	});

	if (Existing)
	{
		Existing->Time = Now;
		return;
	}

	FAedirTimedContact NewRecord;
	NewRecord.Target = Victim;
	NewRecord.Time = Now;
	Records.Add(NewRecord);
}

void UAedirCombatComponent::ProcessImpactSweep(EAedirHand Hand, FAedirArmDynamics& Arm, const FVector& From, const FVector& To, const FVector& TipVelocity, float MassEff, float AxialAlignment, double Now)
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
	World->SweepMultiByObjectType(Hits, From, To, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(HitRadius), Params);

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Creature || HitActor == HeldItem)
		{
			continue;
		}

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
			const bool bFreshContact = TouchVictim[Index].Get() != Victim || Now - TouchTime[Index] > AedirFreshContactSeconds;
			TouchVictim[Index] = Victim;
			TouchTime[Index] = Now;
			if (IsImpactOnCooldown(Hand, HitActor, Now) || RelativeSpeed < MinImpactSpeed || !bFreshContact)
			{
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

			RegisterImpact(Hand, Victim, Now);
			ApplyCreatureImpact(Hand, Arm, Victim, Hit, TipVelocity, RelativeSpeed, MassEff, AxialAlignment);
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

void UAedirCombatComponent::ApplyCreatureImpact(EAedirHand Hand, FAedirArmDynamics& Arm, AAedirCreature* Victim, const FHitResult& Hit, const FVector& TipVelocity, float RelativeSpeed, float MassEff, float AxialAlignment)
{
	AAedirItemActor* HeldItem = GetHeldItem(Hand);
	// A blow that met the body's capsule still lands on whatever part of the body is closest to where it struck.
	const FName Bone = !Hit.BoneName.IsNone() || !Victim->GetMesh() ? Hit.BoneName : Victim->GetMesh()->FindClosestBone(Hit.ImpactPoint);
	const EAedirBodyZone Zone = ClassifyBodyZone(Bone);
	const EAedirDamageType DamageType = HeldItem ? HeldItem->GetDamageType() : EAedirDamageType::Blunt;
	const float Energy = ComputeImpactEnergy(MassEff, RelativeSpeed);
	// The armour where it landed, for the way it landed: an edge, a point driven in along the blade, or a blunt blow. Bare
	// places (the face under an open helmet, the neck, under the arms, the hands) take it all.
	const bool bPointFirst = HeldItem && DamageType != EAedirDamageType::Blunt && AxialAlignment >= AedirLodgeThrustAlignment;
	const EAedirDamageType StruckAs = bPointFirst ? EAedirDamageType::Pierce : DamageType;
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
	const float RawDamage = (Energy * ImpactDamageScale * ComputeDamageTypeFactor(DamageType, AxialAlignment) * GetZoneDamageMultiplier(Zone) * ArmorFactor + FlatDamage) * Harm;
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
	const bool bEdged = HeldItem && (DamageType == EAedirDamageType::Slash || DamageType == EAedirDamageType::Pierce);
	const float CutShare = Energy * ArmorFactor / CutJoules * FMath::FRandRange(1.f - AedirCutLuck, 1.f + AedirCutLuck);
	const bool bGlance = bEdged && CutShare < AedirCutGlanceShare;
	const float DamageShare = bGlance ? AedirGlanceDamageShare : 1.f;
	const float Damage = DamageShare * (Zone == EAedirBodyZone::Head || !Victim->Stats ? RawDamage : FMath::Min(RawDamage, Victim->Stats->GetMaxHealth() * AedirImpactMaxHealthShare));
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
	UE_LOG(LogAedir, Log, TEXT("Impact: %s hit %s on %s, %.0f cm/s, %.1f kg, %.0f J, %.1f damage, balance %.2f, down %d"), *Creature->GetName(), *Victim->GetName(), *Bone.ToString(), RelativeSpeed, MassEff, Energy, AppliedDamage,
		Victim->Balance ? Victim->Balance->GetBalance() : 1.f, Victim->Balance && Victim->Balance->IsRagdoll() ? 1 : 0);

	bool bSevered = false;
	if (Victim->Wounds && DamageType == EAedirDamageType::Slash && !bGlance)
	{
		const FVector CutNormal = FVector::CrossProduct(TipVelocity.GetSafeNormal(), Arm.WeaponDir).GetSafeNormal();
		// Only the edge chops: a blade drawn along its own length, or pushed in point first, opens a wound but takes nothing off.
		const float EdgeShare = FMath::Clamp(1.f - FMath::Abs(AxialAlignment), 0.f, 1.f);
		bSevered = Victim->Wounds->TrySever(Bone, Hit.ImpactPoint, CutNormal, TipVelocity, Energy * EdgeShare, VictimZoneProtection);
	}

	if (Victim->Wounds)
	{
		const FVector TipMotion = ComputeArmTip(Hand, Arm) - Arm.PrevTipWorld;
		Victim->Wounds->AddWound(Bone, Hit.ImpactPoint, Hit.ImpactNormal, bGlance ? EAedirDamageType::Blunt : DamageType, Energy * DamageShare, VictimZoneProtection, TipMotion);
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
		// It bit in and is held there by what it cut.
		const FTransform BoneWorld = VictimBody->GetSocketTransform(Bone, RTS_World);
		const FVector Dir = Arm.WeaponDir.GetSafeNormal();
		const float Depth = FMath::Lerp(2.f, 9.f, FMath::Clamp(CutShare, 0.f, 1.f));
		Arm.LodgedIn = VictimBody;
		Arm.LodgedBone = Bone;
		Arm.LodgedTipLocal = BoneWorld.InverseTransformPosition(FVector(Hit.ImpactPoint) + TipVelocity.GetSafeNormal() * Depth);
		Arm.LodgedDirLocal = BoneWorld.InverseTransformVectorNoScale(Dir);
		Arm.LodgedSeconds = FMath::Lerp(AedirLodgedMaxSeconds, AedirLodgedMinSeconds, FMath::Clamp(CutShare, 0.f, 1.f));
	}
	UE_LOG(LogAedir, Log, TEXT("Cut: %s, share %.2f"), Outcome, CutShare);

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

	// The right mouse button brings the free left hand onto a weapon in the right hand. Otherwise an empty hand joins only while both guards are held.
	return (LeftItem && RightItem) || (bLeftGuard && RightItem && RightItem->ItemClass == EAedirItemClass::Weapon) || (bLeftGuard && bRightGuard);
}

void UAedirCombatComponent::UpdateTwoHandGrip()
{
	bTwoHandGrip = EvaluateTwoHandGrip();
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
