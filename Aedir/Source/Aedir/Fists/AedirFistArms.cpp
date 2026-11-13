#include "Fists/AedirFistArms.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

UAedirFistArms::UAedirFistArms()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UAedirFistArms::SetHeld(EAedirHand Hand, bool bInHeld)
{
	const int32 Slot = Index(Hand);
	// The first button down starts the mouse from the middle: the guard comes up straight before the chin.
	if (bInHeld && !bHeld[0] && !bHeld[1])
	{
		Aim = FVector2D::ZeroVector;
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

	// The arc: in at the sides, out at the middle as far as the punch carries it.
	const float SinT = FMath::Sin(Theta);
	const float CosT = FMath::Cos(Theta);
	const float Radius = FMath::Lerp(GuardForwardCm, SideRadiusCm, FMath::Abs(SinT)) + Punch * PunchReachCm * CosT * CosT;
	const FVector Out = Forward * CosT + Right * SinT;
	const FVector Along = -Forward * SinT + Right * CosT;
	const float Height = GuardHeightCm + (UpDown > 0.f ? UpDown * RaiseCm : UpDown * LowerCm);

	// The elbow goes from down by the ribs, the two a little apart, to up behind the fist as the arm is drawn round.
	const float Lift = FMath::SmoothStep(ElbowLiftFrom, ElbowLiftTo, AcrossAbs);
	const float HalfSpread = FMath::DegreesToRadians(ElbowSpreadDegrees * 0.5f);
	float Raised = 0.f;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const float Side = Slot == 1 ? 1.f : -1.f;
		Alpha[Slot] = FMath::FInterpConstantTo(Alpha[Slot], bHeld[Slot] ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(RaiseSeconds, KINDA_SMALL_NUMBER));
		Raised = FMath::Max(Raised, Alpha[Slot]);
		FistWorld[Slot] = Chest + Out * Radius + Along * (Side * GuardGapCm * 0.5f) + Up * Height;
		const FVector GuardPole = -Forward * 0.25f + Right * (Side * FMath::Sin(HalfSpread)) - Up * FMath::Cos(HalfSpread);
		const FVector LiftPole = -Forward * 0.3f + Right * (Side * 0.45f) + Up * 0.85f;
		// Only the arm drawn back to its own side lifts its elbow; the one brought across keeps it down.
		const float OwnLift = FMath::Sign(static_cast<float>(Aim.X)) == Side ? Lift : 0.f;
		const FVector Pole = FMath::Lerp(GuardPole.GetSafeNormal(), LiftPole.GetSafeNormal(), OwnLift).GetSafeNormal();
		const FVector Shoulder = Slot == 1 ? ShoulderR : ShoulderL;
		ElbowWorld[Slot] = (Shoulder + FistWorld[Slot]) * 0.5 + Pole * 40.f;
	}
	WaistPitch = (UpDown > 0.f ? UpDown * WaistBackDegrees : UpDown * WaistForwardDegrees) * Raised;
}
