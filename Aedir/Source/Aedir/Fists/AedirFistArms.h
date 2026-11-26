#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/AedirItemTypes.h"
#include "AedirFistArms.generated.h"

// Bare-handed fighting, built again from the start one step at a time. Only an arm whose button is held works: the left
// button raises the right arm, the right button the left. Held, the fists come together before the chin with the elbows a
// little apart (the guard). The mouse across takes the fists round the body on an arc about the chest, back to the side
// of the arm it is drawn towards; up and down it raises and lowers the guard, the waist bending with it. Drawn fast in
// towards the middle the arc reaches out from the body, a punch; slowed or stopped, it comes back in to the guard.
// A knife or a one-handed sword in a hand is swung the same way, on the same arc: see the blade settings below.
UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirFistArms : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirFistArms();

	// The whole arc the fists go round, from one side of the body to the other, in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcDegrees = 180.f;

	// Share of the mouse's travel either side of the middle that leaves the guard where it is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float CenterDeadZone = 0.15f;

	// Between these shares of the mouse's travel across, the elbow of the arm drawn back to its own side rises from down by
	// the ribs to out behind the fist.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowLiftFrom = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowLiftTo = 0.9f;

	// At the most the elbow is lifted to out at its side, this many degrees over level (below it when negative).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowLiftDegrees = -10.f;

	// While a punch is out, its arm's elbow is lifted this share of the way at the least, as if drawn to the side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchElbowLift = 0.8f;

	// Share of the mouse that still turns the view while a fist's button is down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float CameraShare = 0.01f;

	// How far the mouse goes from the middle to the end of the arc, and to the top or bottom (mouse axis units, 0.07 a count).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float MouseFieldAcross = 131.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float MouseFieldUpDown = 122.f;

	// Knocked off something fixed, the arm's own way round the arc is turned back by this share of its speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="1"))
	float KnockBackAimShare = 0.5f;

	// And the fist is thrown back off it by this much for each cm/s it came in at, at most so far, coming back over this long.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0"))
	float KnockOffCmPerSpeed = 0.035f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0"))
	float KnockOffMaxCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0.02"))
	float KnockOffSeconds = 0.3f;

	// The guard: how far before the middle of the shoulders the fists are, how far apart, how high over the shoulders, and the
	// angle between the two elbows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float GuardForwardCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float GuardGapCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float GuardHeightCm = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowSpreadDegrees = 18.f;

	// Out at the side of the arc the fist is this far from the middle of the shoulders.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SideRadiusCm = 38.f;

	// A punch: how much further out the middle of the arc goes at most, and the mouse speeds in towards the middle (shares of
	// its travel a second) between which it goes from none of that to all of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchReachCm = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchSlowSpeed = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchFastSpeed = 7.f;

	// How quickly the arc reaches out for a punch and comes back in after it, in shares a second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchOutRate = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchBackRate = 2.2f;

	// The mouse up, a fist's elbow is lifted by this much of the way to straight up (the fist itself stays where it was), so a
	// rising punch comes in round from the side rather than up from below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="3"))
	float RisingElbowRaise = 0.6f;

	// The mouse down, a fist's far points at the sides come this share nearer the body and FallingSideBackCm further back, and
	// its elbow is lifted by FallingElbowRaise, so a falling punch comes straight down rather than across from up high.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="0.9"))
	float FallingSideInShare = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float FallingSideBackCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="3"))
	float FallingElbowRaise = 0.35f;

	// A punch thrown stays out while the arm is still coming in to the middle and this long after, before it comes back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0"))
	float PunchHoldSeconds = 0.12f;

	// The mouse all the way up or down takes the whole guard round an arc about the chest this many degrees up or down, the
	// fists keeping their shape and distance, and bends the waist back or forward this much.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcUpDegrees = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcDownDegrees = 80.f;

	// The mouse all the way down, a fist reaches this much further out, to strike one lying on the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float FistDownReachCm = 15.f;

	// The mouse all the way down, the hips sink this far, the knees bending, so a blade reaches one lying down. Bare-handed
	// the mouse does not take the body down: to strike one on the ground the fighter crouches (Ctrl).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float DownHipDropCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeDownHipDropCm = 15.f;

	// Drawn round to the side the fist leaves the height of the middle behind and goes the other way: from a guard raised it
	// comes down across, from one lowered up. At the side it is this share of the middle's angle the other way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcSideReverse = 0.25f;

	// The waist turns with the fists across: this many degrees with the mouse all the way to one side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistTurnDegrees = 45.f;

	// A punch turns the waist on round the other way, the punching shoulder going forward: this many degrees at a full punch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchWaistTurnDegrees = 30.f;

	// A punch takes the chest forward after the fist, this many degrees at a full punch, so the blow reaches on with the
	// body behind it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchLeanDegrees = 14.f;

	// A fist goes out no further from the shoulder than this share of the arm's own length.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0.5", ClampMax="1"))
	float FistMaxReachShare = 0.99f;

	// The waist goes round after where it is wanted as a weight on a spring of this many cycles a second, and never turns
	// faster than this many degrees a second: a trunk is heavier than an arm, and the fists on it must not be flung with it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistTurnHz = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistTurnMaxDegreesPerSecond = 720.f;

	// The foot on the other side from where the mouse is drawn steps forward; it changes over once the mouse has stayed this
	// share of its way out to the other side for LeadFootSwitchHoldSeconds, so the feet do not shuffle with every punch drawn
	// back and thrown.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="1"))
	float LeadFootSwitchAcross = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float LeadFootSwitchHoldSeconds = 0.5f;

	// Drawn round to its own side the fist turns over about the forearm this far, palm going down, as for a hook.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SideFistTurnDegrees = 90.f;

	// Drawn round to the side, the fist alone (not the elbow) goes this much higher with the mouse all the way up and this
	// much lower with it all the way down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SideFistRiseCm = 10.f;

	// One arm up alone reaches past the middle to the other side only this share of the way it would go there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SingleArmAcrossShare = 0.5f;

	// Brought across that far, its elbow turns out from the shoulder this share of the way to straight out at its side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float CrossElbowOut = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SideFistDropCm = 10.f;

	// A whole punch thrown out costs this much stamina; without enough left, it only goes out as far as what is left pays for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchStaminaCost = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistBackDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistForwardDegrees = 38.f;

	// How long an arm takes to come up into the guard and to drop again, in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float RaiseSeconds = 0.15f;

	// Getting up, finding its feet and RisingCalmSeconds after, a raised fist and its elbow go about the chest no faster than
	// this: the frame of the body turns and the pose changes there at once, and the arm would be flung after it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="1", Units="cm/s"))
	float RisingFistSpeedCm = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", Units="s"))
	float RisingCalmSeconds = 0.6f;

	// The mouse is eased in a little, over about this long (seconds): a jolt of the hand is spread over a few frames.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float MouseSmoothSeconds = 0.03f;

	// The arms follow the mouse as a weight on a spring: bare fists quickly (this many times a second), a blade more slowly,
	// the heavier the slower, and with a little give past where the mouse stopped (damping below 1). A blade of
	// BladeAimRefKg follows at BladeAimHz; one twice as heavy at about 0.7 of it, kept within BladeAimMin/MaxShare of it.
	// Held in both hands, it follows TwoHandAimShare times faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float FistAimHz = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float FistAimDamping = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAimHz = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAimDamping = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAimRefKg = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAimMinShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAimMaxShare = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandAimShare = 1.25f;

	// Out of breath, below TiredFrom of the stamina, the arms swing and punch slower, down to TiredSpeedShare of their speed
	// with none left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float TiredFrom = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float TiredSpeedShare = 0.7f;

	// A blade in the hand: held up still, its point looks ahead at the other; drawn round to its own side the wrist turns
	// out the opposite way a fist's does.
	// A blade is held up lower and further out than a fist, before the chest rather than the face: this much lower and this
	// far before the middle of the shoulders, in the middle of the arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeGuardDropCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeGuardForwardCm = 36.f;

	// The mouse all the way up raises a blade this far round over the head and back, for a blow straight down, the fist this
	// much further out than in the guard, so the arm stays a little bent over the head and is never thrown straight up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeArcUpDegrees = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeUpReachCm = 5.f;

	// Looking up, a blade's swing reaches this much further out as it is thrown (cm at BladeLookUpFullDegrees up and more),
	// so a blow at someone above or taller still gets there. The guard itself stays where it is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLookUpPunchCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLookUpFullDegrees = 35.f;

	// Drawn back to its own side, a blade arm's elbow is pointed this far below level, which leaves the upper arm about half
	// way down (45 degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeElbowLiftDegrees = -52.f;

	// The mouse all the way down takes a blade this far round down (a fist goes ArcDownDegrees), the arm reaching this much
	// further out and the waist bending this far forward (a fist's bends WaistForwardDegrees), to strike low.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeArcDownDegrees = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeDownReachCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeWaistForwardDegrees = 42.f;

	// Drawn to its own side, a blade's wrist turns in two steps. Up to this share of the way from the middle it turns in, as
	// far as BladeSideInDegrees, the wrist still bending the point round ahead, so it keeps looking at the other; from
	// there to the end it turns back out again,
	// on to BladeSideOutDegrees the other way, the blade opening out to the side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSideTurnSplit = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSideInDegrees = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSideOutDegrees = 40.f;

	// Past the middle of the way to its own side the wrist leaves its turn in quickly at first (1 evenly, 2 at twice the
	// speed at the start), so the point swings out and away from the head before the arm comes up beside it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSideOutEase = 3.f;

	// The wrist bends up (towards the thumb, the point going back over the forearm) or down (towards the little finger,
	// the point going on along the forearm), in degrees, up positive, without turning the hand about the forearm: up
	// this far drawn all the way round to either side (from the middle of the way there on), up this far raised all the
	// way and down this far lowered all the way, so it reaches the ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSideWristUpDegrees = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeRaiseWristUpDegrees = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLowerWristDownDegrees = 30.f;

	// Drawn across from the middle to a side the wrist bends a blade down, most halfway (this far) and none at either end,
	// so the blade is carried out clear of the head as it goes by it; less of it the more the blade is raised.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSweepWristDownDegrees = 24.f;

	// Brought across to the other side a blade's wrist turns out this far by BladeSideTurnSplit of the way, the point kept
	// on the other, then back in to BladeCrossTurnDegrees at the end, the blade reaching from the other side round back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeCrossInDegrees = -90.f;

	// A blade held in one hand alone goes this share of the way across past the middle (a fist SingleArmAcrossShare).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeAloneAcrossShare = 1.f;

	// Drawn over to the other side, a blade goes round only this share of the arc a fist would, so it does not reach as far.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeCrossArcShare = 0.6f;

	// Over at the other side a blade's hand comes in this close to the chest (a fist SideRadiusCm), so the elbow stays bent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeCrossRadiusCm = 26.f;

	// Drawn round to the side a blade keeps this share of the middle's angle up or down, the same way (a fist turns the
	// other way, ArcSideReverse), so the whole sweep is tipped up or down with the mouse and strikes high or low.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeArcSideKeep = 0.5f;

	// Held up still, the point looks ahead and this much up (share of up to ahead), the wrist bending the blade on from
	// upright at most this far, as far as a wrist bends.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeGuardUpShare = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeWristBendDegrees = 45.f;

	// Drawn across this fast (shares of the mouse's travel a second) the blade starts to cut in one turn, edge on along the
	// way it goes, instead of first turning in and then out; from the second speed on it is all one turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeOneTurnSpeedMin = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeOneTurnSpeedMax = 5.f;

	// How quickly a fast cut goes back to the slow two-turn way once the mouse slows, a share a second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeOneTurnFadeRate = 2.5f;

	// A blade swung out costs this much stamina where a fist's punch costs PunchStaminaCost, and every quick move of it
	// (faster than PunchSlowSpeed) this much more for each whole width of the mouse's field it goes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeSwingStaminaCost = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeFastMoveStaminaPerField = 5.f;

	// Brought all the way across to the other side, a blade's wrist ends turned in this far (it turns in BladeSideInDegrees
	// up to BladeSideTurnSplit of the way, as on its own side).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeCrossTurnDegrees = 30.f;

	// Held in both hands, the second hand closes on the grip this far below the first, towards the pommel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandGapCm = 8.5f;

	// On a short handle the second hand comes no further down than this far past the end of the pommel, closing over it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandPastPommelCm = 5.5f;

	// The second arm's elbow points at least this share out to its own side (0 straight down or back, 1 straight out), so
	// it never bends in through the chest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandElbowOutShare = 0.f;

	// Brought across past its own shoulder, the second elbow stays this far before the shoulder, out of the chest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandElbowFrontCm = 18.f;

	// Drawn round to the right in both hands, the second elbow rises this much more at the end, in cm, so its forearm comes
	// to the grip along it and the wrist is not bent over.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandElbowRiseCm = 14.f;

	// Carried low in both hands, the second elbow stays at least this far below its shoulder, in cm, however the view turns.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestElbowBelowShoulderCm = 29.f;

	// The furthest the second hand turns at the wrist off the line its forearm gives it to close on the grip, in degrees (at
	// 32 it lay 28 degrees across the grip at rest, crooked to the carrying hand; at 60, 7):
	// past it the hand would turn into the blade.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandWristMaxDegrees = 60.f;

	// Held in both hands and drawn round to its own side, the blade comes in this close to the chest (in one hand
	// SideRadiusCm), so the second arm still reaches it bent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandSideRadiusCm = 28.f;

	// Held in both hands and not raised, the blade is carried as a two-handed sword is: the right hand this far ahead of the
	// middle of the body, to the side (right positive) and up from it, before the hips, the blade aslant across, its point
	// turned this far out to the right and this far up (down negative). Raised, it goes as in one hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestForwardCm = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestSideCm = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestHeightCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestYawDegrees = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestPitchDegrees = 10.f;

	// Held in both hands and thrust out fast, the elbows lift only this share of what one arm's punch lifts them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandPunchElbowLiftShare = 0.15f;

	// ...and the blade is thrust out this much lower at full reach, in cm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandPunchDropCm = 20.f;

	// ...and goes out only this share as far, the elbows kept a little bent at their sides.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandPunchReachShare = 0.7f;

	// Carried low in both hands, the blade and the arms turn with the chest this share of the way the view turns it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandRestFollowTwist = 1.f;

	// A blade's whole arc is tipped up or down with the view, this share of its pitch and no further than these, so a blow
	// drawn across goes round the way the view looks and the arm drawn back goes back from there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLookTiltShare = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLookTiltDownMaxDegrees = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeLookTiltUpMaxDegrees = 40.f;

	// How far below the first hand the second closes on this blade's grip: a hand's width, but no further than just past
	// the end of its pommel.
	float GetGripGapCm(const class AAedirItemActor* Blade) const;

	// A knife or a one-handed sword: what these hands swing like a fist, one in a hand.
	static bool IsOneHandBlade(const class AAedirItemActor* Item);

	// The button for an arm went down or up. An arm holding something other than a one-handed blade is left to the rest of
	// the game.
	// The first button down starts the fists where the view looks: up or down by its pitch (degrees, up positive).
	void SetHeld(EAedirHand Hand, bool bHeld, float ViewPitchDegrees = 0.f);
	// Whether an arm is up: its button held, and that arm not flinching from a hurt just now.
	bool IsHeld(EAedirHand Hand) const { return HeldNow(Index(Hand)); }
	// Whether its button is held, flinching or not.
	bool IsButtonHeld(EAedirHand Hand) const { return bHeld[Index(Hand)]; }
	// Whether the mouse steers the fists: while any button is held, even with that arm down for a moment, so the view does not
	// turn with it.
	bool IsSteering() const { return bHeld[0] || bHeld[1]; }

	// Hurt just now: that arm comes down out of the guard for this long, whatever its button says, and goes back up after if
	// it is still held; the other arm goes on. A longer one already going is kept.
	// Out of the fight for exactly this long from now (0 back in it at once).
	// A fist stopped by something it struck: the punch is over and the arm comes back in.
	// Off something that does not give it is thrown back (the way and how far, cm) before it comes on to its guard again.
	void KnockBack(EAedirHand Hand, const FVector& Off = FVector::ZeroVector);
	void Flinch(EAedirHand Hand, float Seconds) { FlinchLeft[Index(Hand)] = FMath::Max(Seconds, 0.f); }
	// Someone's hand holds this arm at that place, by that share of the struggle (0 to 1): the fist gets only so far from it
	// and is thrown out the slower. It lasts as long as it is told again every moment.
	void SetHeldBy(EAedirHand Hand, const FVector& HoldWorld, float Share)
	{
		HeldAt[Index(Hand)] = HoldWorld;
		HeldShare[Index(Hand)] = FMath::Clamp(Share, 0.f, 1.f);
		HeldLeft[Index(Hand)] = 0.15f;
	}

	// Held fully, the fist reaches no further than this from the holding hand (cm), and is thrown out this share as fast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float HeldSlackCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists", meta=(ClampMin="0", ClampMax="1"))
	float HeldPunchSpeedShare = 0.25f;
	bool IsFlinching(EAedirHand Hand) const { return FlinchLeft[Index(Hand)] > 0.f; }

	// The mouse moved while a button is held (mouse axis units, up positive).
	void AddMouse(const FVector2D& Delta);

	// A one-handed blade in the right hand with the left hand empty: the right button clicked takes it in both hands or
	// back in one, held down it lets the left hand go and raises it on its own.
	bool CanTakeBothHands() const;
	bool IsTwoHand() const { return bTwoHand; }
	void SetTwoHand(bool bInTwoHand);
	void SetOffHandOut(bool bOut) { bOffHandOut = bOut; }
	// How far the left hand is on the grip of the blade in the right hand (0..1).
	float GetGripAlpha() const { return GripAlpha; }
	// Whether an arm holds a blade swung as a fist, and how far its point is to be brought round ahead (0..1).
	bool IsArmed(EAedirHand Hand) const { return bArmed[Index(Hand)]; }
	float GetBladeAhead(EAedirHand Hand) const { return BladeAhead[Index(Hand)]; }
	// How far the wrist brings a blade's point round to look ahead (0..1), and how far it bends it up (degrees, down negative).
	float GetBladeAim(EAedirHand Hand) const { return BladeAim[Index(Hand)]; }
	float GetBladeWristUp(EAedirHand Hand) const { return BladeWristUp[Index(Hand)]; }
	// Which way (world) a blade's knuckle row is to lean, turned up or down round the chest with the arm.
	FVector GetBladeLean(EAedirHand Hand) const { return BladeLean[Index(Hand)]; }
	// Which way (world) the wrist brings a blade's point round to while it looks ahead.
	FVector GetBladeAimWay(EAedirHand Hand) const { return BladeAimWay[Index(Hand)]; }

	// Where the pose puts each arm this frame: the fist and the elbow in the world, and how far the arm is in the guard (0..1).
	bool GetArm(EAedirHand Hand, FVector& OutFistWorld, FVector& OutElbowWorld, float& OutAlpha) const;

	// Torso pitch the guard asks for, FRotator convention (forward negative).
	float GetWaistPitch() const { return WaistPitch; }
	// How far the hips sink, the knees bending, to reach down low.
	float GetHipDrop() const { return HipDrop; }
	// Torso turn the fists ask for, FRotator convention.
	float GetWaistYaw() const { return WaistYaw; }
	// How far an arm's fist is turned over about its forearm from the guard's, degrees.
	float GetFistTurn(EAedirHand Hand) const { return FistTurn[Index(Hand)]; }

	// For tests and measuring: where the arms stand across and up (-1..1) and how far out the punch is (0..1).
	FVector2D GetAim() const { return Aim; }
	// Where the mouse itself stands; the arms follow it with their weight.
	FVector2D GetMouseAim() const { return MouseAim; }
	// How far a blade now cuts in one turn rather than two (0..1).
	float GetOneTurn() const { return OneTurn; }
	// How far an arm is up in the guard, apart from the second hand holding on.
	float GetRaised(EAedirHand Hand) const { return Alpha[Index(Hand)]; }
	float GetPunch() const { return Punch; }
	// The arm the punch now goes out with (0 left, 1 right).
	int32 GetPunchSlot() const { return PunchSlot; }
	// The furthest the punch went out since this was last asked, and where the mouse stood then.
	float TakePeakPunch(FVector2D& OutAimThen) { const float Peak = PeakPunch; OutAimThen = PeakAim; PeakPunch = 0.f; return Peak; }
	// How straight an arm got since this was last asked: shoulder to wrist over the length of the arm (1 = straight).
	float TakePeakStraight(EAedirHand Hand) { const float Peak = PeakStraight[Index(Hand)]; PeakStraight[Index(Hand)] = 0.f; return Peak; }
	// How far ahead of the chest the wrist got since this was last asked, in cm along the body's facing.
	float TakePeakAhead(EAedirHand Hand) { const float Peak = PeakAhead[Index(Hand)]; PeakAhead[Index(Hand)] = 0.f; return Peak; }
	// For pictures: holds the mouse and the punch where they are put (bFrozen), until let go.
	void DevSetPose(const FVector2D& InAim, float InPunch, bool bFrozen) { Aim = InAim; MouseAim = InAim; AimSpeed = FVector2D::ZeroVector; Punch = InPunch; bDevFrozen = bFrozen; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// The fists are steered where the player is; the stamina they cost is taken where the stats are kept.
	UFUNCTION(Server, Unreliable)
	void ServerSpendStamina(float Amount);
	void SpendStamina(float Amount);

	static int32 Index(EAedirHand Hand) { return Hand == EAedirHand::Left ? 0 : 1; }

	bool bHeld[2] = { false, false };
	float FlinchLeft[2] = { 0.f, 0.f };
	FVector HeldAt[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float HeldShare[2] = { 0.f, 0.f };
	float HeldLeft[2] = { 0.f, 0.f };
	// Getting up, an arm the body leans on does not come up into the fight; the one kept off the ground does.
	bool bOnGround[2] = { false, false };
	// A broken (or lost) arm does not come up at all.
	bool bArmUseless[2] = { false, false };
	bool HeldNow(int32 Slot) const { return bHeld[Slot] && FlinchLeft[Slot] <= 0.f && !bOnGround[Slot] && !bArmUseless[Slot]; }
	float Alpha[2] = { 0.f, 0.f };
	float CalmLeft = 0.f;
	FVector LastFistFromChest[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector LastElbowFromChest[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bLastShown[2] = { false, false };
	// How far each arm is the fists' to pose: up, or carried in both hands.
	float ShownAlpha[2] = { 0.f, 0.f };
	FVector FistWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector KnockOff[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float KnockOffLeft[2] = { 0.f, 0.f };
	FVector ElbowWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector2D Aim = FVector2D::ZeroVector;
	// Where the mouse has put the arms to go, and how fast they are on their way there.
	FVector2D MouseAim = FVector2D::ZeroVector;
	FVector2D AimSpeed = FVector2D::ZeroVector;
	FVector2D PendingMouse = FVector2D::ZeroVector;
	// Mouse movement not yet let through the easing.
	FVector2D MouseCarry = FVector2D::ZeroVector;
	float Punch = 0.f;
	// How far the blade cuts in one turn now, from how fast it was last drawn across.
	float OneTurn = 0.f;
	// The arm the punch belongs to: the one that was drawn back on the side the mouse last came in from.
	int32 PunchSlot = 1;
	float WaistPitch = 0.f;
	float WaistYaw = 0.f;
	float WaistYawRate = 0.f;
	float FistTurn[2] = { 0.f, 0.f };
	bool bArmed[2] = { false, false };
	float BladeAhead[2] = { 0.f, 0.f };
	float BladeAim[2] = { 0.f, 0.f };
	float BladeWristUp[2] = { 0.f, 0.f };
	FVector BladeLean[2] = { FVector::UpVector, FVector::UpVector };
	FVector BladeAimWay[2] = { FVector::ForwardVector, FVector::ForwardVector };
	bool bTwoHand = false;
	bool bOffHandOut = false;
	float GripAlpha = 0.f;
	float PeakPunch = 0.f;
	float PeakStraight[2] = { 0.f, 0.f };
	float PeakAhead[2] = { 0.f, 0.f };
	float PunchHoldLeft = 0.f;
	FVector2D PeakAim = FVector2D::ZeroVector;
	bool bDevFrozen = false;
	float HipDrop = 0.f;
};
