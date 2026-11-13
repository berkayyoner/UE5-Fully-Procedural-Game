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
	float CenterDeadZone = 0.05f;

	// Between these shares of the mouse's travel across, the elbow of the arm drawn back to its own side rises from down by
	// the ribs to up behind the fist.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowLiftFrom = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowLiftTo = 0.25f;

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
	float GuardGapCm = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float GuardHeightCm = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float ElbowSpreadDegrees = 18.f;

	// Out at the side of the arc the fist is this far from the middle of the shoulders.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float SideRadiusCm = 40.f;

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

	// The mouse all the way up or down raises or lowers the guard this far, and bends the waist back or forward this much.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float RaiseCm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float LowerCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistBackDegrees = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float WaistForwardDegrees = 25.f;

	// How long an arm takes to come up into the guard and to drop again, in seconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Fists")
	float RaiseSeconds = 0.15f;

	// The button for an arm went down or up. An arm with something in its hand is left to the rest of the game.
	void SetHeld(EAedirHand Hand, bool bHeld);
	bool IsHeld(EAedirHand Hand) const { return bHeld[Index(Hand)]; }
	bool IsSteering() const { return bHeld[0] || bHeld[1]; }

	// The mouse moved while a button is held (mouse axis units, up positive).
	void AddMouse(const FVector2D& Delta);

	// Where the pose puts each arm this frame: the fist and the elbow in the world, and how far the arm is in the guard (0..1).
	bool GetArm(EAedirHand Hand, FVector& OutFistWorld, FVector& OutElbowWorld, float& OutAlpha) const;

	// Torso pitch the guard asks for, FRotator convention (forward negative).
	float GetWaistPitch() const { return WaistPitch; }

	// For tests and measuring: where the mouse stands across and up (-1..1) and how far out the punch is (0..1).
	FVector2D GetAim() const { return Aim; }
	float GetPunch() const { return Punch; }
	// The furthest the punch went out since this was last asked, and where the mouse stood then.
	float TakePeakPunch(FVector2D& OutAimThen) { const float Peak = PeakPunch; OutAimThen = PeakAim; PeakPunch = 0.f; return Peak; }
	// For pictures: holds the mouse and the punch where they are put (bFrozen), until let go.
	void DevSetPose(const FVector2D& InAim, float InPunch, bool bFrozen) { Aim = InAim; Punch = InPunch; bDevFrozen = bFrozen; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	static int32 Index(EAedirHand Hand) { return Hand == EAedirHand::Left ? 0 : 1; }

	bool bHeld[2] = { false, false };
	float Alpha[2] = { 0.f, 0.f };
	FVector FistWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector ElbowWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector2D Aim = FVector2D::ZeroVector;
	FVector2D PendingMouse = FVector2D::ZeroVector;
	float Punch = 0.f;
	float WaistPitch = 0.f;
	float PeakPunch = 0.f;
	FVector2D PeakAim = FVector2D::ZeroVector;
	bool bDevFrozen = false;
};
