#include "Fists/AedirFistArms.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

UAedirFistArms::UAedirFistArms()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UAedirFistArms::SetHeld(EAedirHand Hand, bool bInHeld, float ViewPitchDegrees)
{
	const int32 Slot = Index(Hand);
	// The first button down starts the mouse from the middle across, and up or down where the view looks: looking down, the
	// first blow goes down.
	if (bInHeld && !bHeld[0] && !bHeld[1])
	{
		const float Range = ViewPitchDegrees > 0.f ? ArcUpDegrees : ArcDownDegrees;
		Aim = FVector2D(0.0, FMath::Clamp(ViewPitchDegrees / FMath::Max(Range, 1.f), -1.f, 1.f));
		PendingMouse = FVector2D::ZeroVector;
		Punch = 0.f;
	}
	bHeld[Slot] = bInHeld;
}

void UAedirFistArms::AddMouse(const FVector2D& Delta)
{
	if (IsSteering())
	{
		PendingMouse += Delta;
	}
}

bool UAedirFistArms::GetArm(EAedirHand Hand, FVector& OutFistWorld, FVector& OutElbowWorld, float& OutAlpha) const
{
	const int32 Slot = Index(Hand);
	OutFistWorld = FistWorld[Slot];
	OutElbowWorld = ElbowWorld[Slot];
	OutAlpha = Alpha[Slot];
	return OutAlpha > 0.f;
}

void UAedirFistArms::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || DeltaTime <= 0.f)
	{
		return;
	}

	// Where the mouse stands, as shares of its travel, and how fast it came in towards the middle across.
	const bool bSteering = IsSteering();
	float Inward = 0.f;
	if (bSteering && !bDevFrozen)
	{
		const float Before = static_cast<float>(Aim.X);
		Aim.X = FMath::Clamp(Aim.X + PendingMouse.X / FMath::Max(MouseFieldAcross, 1.f), -1.0, 1.0);
		Aim.Y = FMath::Clamp(Aim.Y + PendingMouse.Y / FMath::Max(MouseFieldUpDown, 1.f), -1.0, 1.0);
		const float After = static_cast<float>(Aim.X);
		// Through the middle, only the way in to it counts as coming in.
		const float InwardShare = FMath::Sign(Before) == FMath::Sign(After) ? FMath::Abs(Before) - FMath::Abs(After) : FMath::Abs(Before);
		Inward = FMath::Max(InwardShare, 0.f) / DeltaTime;
		if (InwardShare > 0.f)
		{
			PunchSlot = Before > 0.f ? 1 : 0;
		}
	}
	PendingMouse = FVector2D::ZeroVector;

	// Fast in towards the middle the arc reaches out; slow, still or let go, it comes back in to the guard.
	const float PunchWanted = bSteering ? FMath::Clamp((Inward - PunchSlowSpeed) / FMath::Max(PunchFastSpeed - PunchSlowSpeed, KINDA_SMALL_NUMBER), 0.f, 1.f) : 0.f;
	if (!bDevFrozen)
	{
		Punch = FMath::FInterpConstantTo(Punch, PunchWanted, DeltaTime, PunchWanted > Punch ? PunchOutRate : PunchBackRate);
	}
	if (Punch > PeakPunch)
	{
		PeakPunch = Punch;
		PeakAim = Aim;
	}

	// Across the dead zone in the middle the guard stays; past it the fists go round the arc.
	const float AcrossAbs = FMath::Abs(static_cast<float>(Aim.X));
	const float Across = FMath::Sign(static_cast<float>(Aim.X)) * FMath::Max(AcrossAbs - CenterDeadZone, 0.f) / FMath::Max(1.f - CenterDeadZone, KINDA_SMALL_NUMBER);
	const float Theta = FMath::DegreesToRadians(Across * ArcDegrees * 0.5f);
	const float UpDown = static_cast<float>(Aim.Y);

	// The frame of the body, turned with it but always upright.
	const FRotator Yaw(0.f, Owner->GetActorRotation().Yaw, 0.f);
	const FVector Forward = Yaw.RotateVector(FVector::ForwardVector);
	const FVector Right = Yaw.RotateVector(FVector::RightVector);
	const FVector Up = FVector::UpVector;
	const USkeletalMeshComponent* Mesh = Owner->GetMesh();
	const FVector ShoulderL = Mesh ? Mesh->GetBoneLocation(TEXT("upperarm_l")) : Owner->GetActorLocation() - Right * 18.f + Up * 45.f;
	const FVector ShoulderR = Mesh ? Mesh->GetBoneLocation(TEXT("upperarm_r")) : Owner->GetActorLocation() + Right * 18.f + Up * 45.f;
	const FVector Chest = (ShoulderL + ShoulderR) * 0.5;

	// Up and down the guard turns round the chest as a whole, so its shape holds.
	const float Phi = FMath::DegreesToRadians(UpDown > 0.f ? UpDown * ArcUpDegrees : UpDown * ArcDownDegrees);

	// The elbow goes from down by the ribs, the two a little apart, to up behind the fist as the arm is drawn round.
	const float Lift = FMath::SmoothStep(ElbowLiftFrom, ElbowLiftTo, AcrossAbs);
	const float HalfSpread = FMath::DegreesToRadians(ElbowSpreadDegrees * 0.5f);
	const float AimSide = FMath::Sign(static_cast<float>(Aim.X));
	const bool bBoth = bHeld[0] && bHeld[1];
	float Raised = 0.f;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const float Side = Slot == 1 ? 1.f : -1.f;
		Alpha[Slot] = FMath::FInterpConstantTo(Alpha[Slot], bHeld[Slot] ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(RaiseSeconds, KINDA_SMALL_NUMBER));
		Raised = FMath::Max(Raised, Alpha[Slot]);
		// With both arms up, only the arm on the side the mouse is drawn to goes round the arc and only the arm that came in
		// punches; the other keeps its guard. One arm alone goes all the way round.
		const bool bFollows = !bBoth || AimSide == Side;
		const float ArmTheta = bFollows ? Theta : 0.f;
		const float ArmPunch = (!bBoth || Slot == PunchSlot) ? Punch : 0.f;
		// The arc: in at the sides, out at the middle as far as the punch carries it.
		const float SinT = FMath::Sin(ArmTheta);
		const float CosT = FMath::Cos(ArmTheta);
		const float Radius = FMath::Lerp(GuardForwardCm, SideRadiusCm, FMath::Abs(SinT)) + ArmPunch * PunchReachCm * CosT * CosT;
		const FVector Out = Forward * CosT + Right * SinT;
		const FVector Along = -Forward * SinT + Right * CosT;
		// Round towards the side the height of the middle falls away and turns the other way, so a raised fist comes down
		// across and a lowered one up.
		const float ArmPhi = Phi * (1.f - (1.f + ArcSideReverse) * FMath::Abs(SinT));
		const float SinP = FMath::Sin(ArmPhi);
		const float CosP = FMath::Cos(ArmPhi);
		// Tips a direction of the level guard up round the chest, about the axis across this arm's way out.
		const auto Tip = [&](const FVector& V)
		{
			const double A = FVector::DotProduct(V, Out);
			const double B = FVector::DotProduct(V, Up);
			return V - Out * A - Up * B + Out * (A * CosP - B * SinP) + Up * (A * SinP + B * CosP);
		};
		// The knuckles lead out from the chest the way the arc goes there: up for a blow upwards, down for one downwards,
		// out to the side drawn round, never curled in.
		KnuckleDirection[Slot] = Tip(Out).GetSafeNormal();
		FistWorld[Slot] = Chest + Tip(Out * Radius + Along * (Side * GuardGapCm * 0.5f) + Up * GuardHeightCm);
		const FVector GuardPole = -Forward * 0.25f + Right * (Side * FMath::Sin(HalfSpread)) - Up * FMath::Cos(HalfSpread);
		const float LiftAngle = FMath::DegreesToRadians(ElbowLiftDegrees);
		// Lifted, the elbow goes back and out behind the fist, about level.
		const FVector LiftPole = (-Forward + Right * Side).GetSafeNormal() * FMath::Cos(LiftAngle) + Up * FMath::Sin(LiftAngle);
		// Only the arm drawn back to its own side lifts its elbow; the one brought across keeps it down.
		const float OwnLift = FMath::Max(AimSide == Side ? Lift : 0.f, ArmPunch * PunchElbowLift);
		const FVector Pole = Tip(FMath::Lerp(GuardPole.GetSafeNormal(), LiftPole.GetSafeNormal(), OwnLift).GetSafeNormal());
		const FVector Shoulder = Slot == 1 ? ShoulderR : ShoulderL;
		// Out at its own side the wrist turns back out against the forearm's roll.
		WristTurn[Slot] = (bFollows && AimSide == Side ? FMath::Abs(SinT) : 0.f) * WristTurnOutDegrees * Side;
		ElbowWorld[Slot] = (Shoulder + FistWorld[Slot]) * 0.5 + Pole * 40.f;
	}
	WaistPitch = (UpDown > 0.f ? UpDown * WaistBackDegrees : UpDown * WaistForwardDegrees) * Raised;
	WaistYaw = Across * WaistTurnDegrees * Raised;
}
