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
	float PunchSlowSpeed = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchFastSpeed = 5.f;

	// How quickly the arc reaches out for a punch and comes back in after it, in shares a second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchOutRate = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float PunchBackRate = 3.f;

	// The mouse all the way up or down takes the whole guard round an arc about the chest this many degrees up or down, the
	// fists keeping their shape and distance, and bends the waist back or forward this much.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcUpDegrees = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcDownDegrees = 50.f;

	// Drawn round to the side the fist leaves the height of the middle behind and goes the other way: from a guard raised it
	// comes down across, from one lowered up. At the side it is this share of the middle's angle the other way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ArcSideReverse = 0.25f;

	// The waist turns with the fists across: this many degrees with the mouse all the way to one side.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistTurnDegrees = 30.f;

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
	float PunchStaminaCost = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistBackDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistForwardDegrees = 25.f;

	// How long an arm takes to come up into the guard and to drop again, in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float RaiseSeconds = 0.15f;

	// A blade in the hand: held up still, its point looks ahead at the other; drawn round to its own side the wrist turns
	// out the opposite way a fist's does.
	// A blade is held up lower and further out than a fist, before the chest rather than the face: this much lower and this
	// far before the middle of the shoulders, in the middle of the arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeGuardDropCm = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeGuardForwardCm = 36.f;

	// The mouse all the way up raises a blade this far round over the head and back, for a blow straight down, the arm
	// reaching this much further up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeArcUpDegrees = 115.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeUpReachCm = 15.f;

	// Drawn back to its own side, a blade arm's elbow is pointed this far below level, which leaves the upper arm about half
	// way down (45 degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeElbowLiftDegrees = -52.f;

	// Brought across to the other side, a blade's wrist turns in this far.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float BladeCrossTurnDegrees = 30.f;

	// Held in both hands, the second hand closes on the grip this far below the first, towards the pommel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists|Blade")
	float TwoHandGapCm = 9.f;

	// A knife or a one-handed sword: what these hands swing like a fist, one in a hand.
	static bool IsOneHandBlade(const class AAedirItemActor* Item);

	// The button for an arm went down or up. An arm holding something other than a one-handed blade is left to the rest of
	// the game.
	// The first button down starts the fists where the view looks: up or down by its pitch (degrees, up positive).
	void SetHeld(EAedirHand Hand, bool bHeld, float ViewPitchDegrees = 0.f);
	bool IsHeld(EAedirHand Hand) const { return bHeld[Index(Hand)]; }
	bool IsSteering() const { return bHeld[0] || bHeld[1]; }

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

	// Where the pose puts each arm this frame: the fist and the elbow in the world, and how far the arm is in the guard (0..1).
	bool GetArm(EAedirHand Hand, FVector& OutFistWorld, FVector& OutElbowWorld, float& OutAlpha) const;

	// Torso pitch the guard asks for, FRotator convention (forward negative).
	float GetWaistPitch() const { return WaistPitch; }
	// Torso turn the fists ask for, FRotator convention.
	float GetWaistYaw() const { return WaistYaw; }
	// How far an arm's fist is turned over about its forearm from the guard's, degrees.
	float GetFistTurn(EAedirHand Hand) const { return FistTurn[Index(Hand)]; }

	// For tests and measuring: where the mouse stands across and up (-1..1) and how far out the punch is (0..1).
	FVector2D GetAim() const { return Aim; }
	float GetPunch() const { return Punch; }
	// The furthest the punch went out since this was last asked, and where the mouse stood then.
	float TakePeakPunch(FVector2D& OutAimThen) { const float Peak = PeakPunch; OutAimThen = PeakAim; PeakPunch = 0.f; return Peak; }
	// For pictures: holds the mouse and the punch where they are put (bFrozen), until let go.
	void DevSetPose(const FVector2D& InAim, float InPunch, bool bFrozen) { Aim = InAim; Punch = InPunch; bDevFrozen = bFrozen; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// The fists are steered where the player is; the stamina they cost is taken where the stats are kept.
	UFUNCTION(Server, Unreliable)
	void ServerSpendStamina(float Amount);
	void SpendStamina(float Amount);

	static int32 Index(EAedirHand Hand) { return Hand == EAedirHand::Left ? 0 : 1; }

	bool bHeld[2] = { false, false };
	float Alpha[2] = { 0.f, 0.f };
	FVector FistWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector ElbowWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector2D Aim = FVector2D::ZeroVector;
	FVector2D PendingMouse = FVector2D::ZeroVector;
	float Punch = 0.f;
	// The arm the punch belongs to: the one that was drawn back on the side the mouse last came in from.
	int32 PunchSlot = 1;
	float WaistPitch = 0.f;
	float WaistYaw = 0.f;
	float FistTurn[2] = { 0.f, 0.f };
	bool bArmed[2] = { false, false };
	float BladeAhead[2] = { 0.f, 0.f };
	bool bTwoHand = false;
	bool bOffHandOut = false;
	float GripAlpha = 0.f;
	float PeakPunch = 0.f;
	FVector2D PeakAim = FVector2D::ZeroVector;
	bool bDevFrozen = false;
};
