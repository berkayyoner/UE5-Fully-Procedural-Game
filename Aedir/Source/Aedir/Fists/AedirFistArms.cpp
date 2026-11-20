#include "Fists/AedirFistArms.h"
#include "Physics/AedirBalanceComponent.h"

#include "Animation/AedirGaitComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Character.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	const AAedirItemActor* AedirFistsHeldItem(const AActor* Owner, int32 Slot)
	{
		const AAedirCreature* Creature = Cast<AAedirCreature>(Owner);
		const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
		return Interaction ? Interaction->GetHeldItem(Slot == 0 ? EAedirHand::Left : EAedirHand::Right) : nullptr;
	}
}

bool UAedirFistArms::IsOneHandBlade(const AAedirItemActor* Item)
{
	return Item && Item->ItemClass == EAedirItemClass::Weapon && !Item->bTwoHanded && !Item->bPolearm && !Item->IsShield()
		&& (Item->DamageType == EAedirDamageType::Slash || Item->DamageType == EAedirDamageType::Pierce || Item->DamageType == EAedirDamageType::Blunt);
}

float UAedirFistArms::GetGripGapCm(const AAedirItemActor* Blade) const
{
	return Blade ? FMath::Min(TwoHandGapCm, Blade->GetPommelBelowGripCm() + TwoHandPastPommelCm) : TwoHandGapCm;
}

bool UAedirFistArms::CanTakeBothHands() const
{
	const AAedirItemActor* Right = AedirFistsHeldItem(GetOwner(), 1);
	return IsOneHandBlade(Right) && !AedirFistsHeldItem(GetOwner(), 0);
}

void UAedirFistArms::SetTwoHand(bool bInTwoHand)
{
	bTwoHand = bInTwoHand && CanTakeBothHands();
	bOffHandOut = false;
}

UAedirFistArms::UAedirFistArms()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UAedirFistArms::SpendStamina(float Amount)
{
	if (Amount <= 0.f || !GetOwner())
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		if (UAedirStatsComponent* Stats = GetOwner()->FindComponentByClass<UAedirStatsComponent>())
		{
			Stats->ConsumeStamina(FMath::Min(Amount, Stats->GetStamina()));
		}
	}
	else
	{
		ServerSpendStamina(Amount);
	}
}

void UAedirFistArms::ServerSpendStamina_Implementation(float Amount)
{
	SpendStamina(FMath::Min(Amount, FMath::Max(PunchStaminaCost, BladeSwingStaminaCost)));
}

void UAedirFistArms::SetHeld(EAedirHand Hand, bool bInHeld, float ViewPitchDegrees)
{
	const int32 Slot = Index(Hand);
	// The first button down starts the mouse from the middle across, and up or down where the view looks: looking down, the
	// first blow goes down.
	if (bInHeld && !bHeld[0] && !bHeld[1])
	{
		const float Range = ViewPitchDegrees > 0.f ? ArcUpDegrees : ArcDownDegrees;
		// A blade's arc is tipped with the view itself, so its mouse starts in the middle.
		const bool bBladeArc = bArmed[0] || bArmed[1];
		Aim = FVector2D(0.0, bBladeArc ? 0.f : FMath::Clamp(ViewPitchDegrees / FMath::Max(Range, 1.f), -1.f, 1.f));
		MouseAim = Aim;
		AimSpeed = FVector2D::ZeroVector;
		PendingMouse = FVector2D::ZeroVector;
		MouseCarry = FVector2D::ZeroVector;
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
	OutAlpha = ShownAlpha[Slot];
	return OutAlpha > 0.f;
}

void UAedirFistArms::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	{
		const UAedirBalanceComponent* OwnBalance = GetOwner() ? GetOwner()->FindComponentByClass<UAedirBalanceComponent>() : nullptr;
		bBodyDown = OwnBalance && (OwnBalance->IsRagdoll() || OwnBalance->IsGettingUp());
		const AAedirCreature* OwnCreature = Cast<AAedirCreature>(GetOwner());
		const UAedirWoundComponent* OwnWounds = OwnCreature ? OwnCreature->Wounds.Get() : nullptr;
		bArmUseless[0] = OwnWounds && OwnWounds->HasUselessArm(true);
		bArmUseless[1] = OwnWounds && OwnWounds->HasUselessArm(false);
	}
	FlinchLeft[0] = FMath::Max(FlinchLeft[0] - DeltaTime, 0.f);
	FlinchLeft[1] = FMath::Max(FlinchLeft[1] - DeltaTime, 0.f);
	for (int32 HeldSlot = 0; HeldSlot < 2; ++HeldSlot)
	{
		HeldLeft[HeldSlot] = FMath::Max(HeldLeft[HeldSlot] - DeltaTime, 0.f);
		if (HeldLeft[HeldSlot] <= 0.f)
		{
			HeldShare[HeldSlot] = 0.f;
		}
	}
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner || DeltaTime <= 0.f)
	{
		return;
	}

	// Which hands hold a blade swung like a fist; both hands on one only while the left is still empty.
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const AAedirItemActor* Item = AedirFistsHeldItem(Owner, Slot);
		bArmed[Slot] = IsOneHandBlade(Item) && AedirFistsHeldItem(Owner, 1 - Slot) != Item;
	}
	if (bTwoHand && !CanTakeBothHands())
	{
		bTwoHand = false;
		bOffHandOut = false;
	}
	// A blade has no still middle: it answers the mouse from the first move.
	const float DeadZone = bArmed[0] || bArmed[1] ? 0.f : CenterDeadZone;

	// Where the mouse stands, as shares of its travel, and how fast it came in towards the middle across. The mouse is
	// eased in over a moment, and the arms follow where it stands as a weight does: fists quickly, a blade more slowly.
	const bool bSteering = IsSteering();
	float Inward = 0.f;
	MouseCarry += PendingMouse;
	const FVector2D MouseNow = MouseSmoothSeconds > KINDA_SMALL_NUMBER ? MouseCarry * (1.f - FMath::Exp(-DeltaTime / MouseSmoothSeconds)) : MouseCarry;
	MouseCarry -= MouseNow;
	if (bSteering && !bDevFrozen)
	{
		const float Before = static_cast<float>(MouseAim.X);
		// One arm alone goes across past the middle only this share of the arc on the other side.
		const double OtherSide = DeadZone + SingleArmAcrossShare * (1.f - DeadZone);
		// A blade alone goes further, so its wrist can turn back in and lay it round behind.
		const double BladeOtherSide = DeadZone + BladeAloneAcrossShare * (1.f - DeadZone);
		const double AcrossMin = HeldNow(1) && !HeldNow(0) ? -(bArmed[1] ? BladeOtherSide : OtherSide) : -1.0;
		const double AcrossMax = HeldNow(0) && !HeldNow(1) ? (bArmed[0] ? BladeOtherSide : OtherSide) : 1.0;
		MouseAim.X = FMath::Clamp(MouseAim.X + MouseNow.X / FMath::Max(MouseFieldAcross, 1.f), AcrossMin, AcrossMax);
		const FVector2D AimBefore(Before, MouseAim.Y);
		MouseAim.Y = FMath::Clamp(MouseAim.Y + MouseNow.Y / FMath::Max(MouseFieldUpDown, 1.f), -1.0, 1.0);
		const float After = static_cast<float>(MouseAim.X);
		// A blade moved quickly, any way, takes breath.
		const float Travel = static_cast<float>(FVector2D::Distance(AimBefore, MouseAim));
		if (((HeldNow(0) && bArmed[0]) || (HeldNow(1) && bArmed[1])) && Travel / DeltaTime > PunchSlowSpeed)
		{
			SpendStamina(Travel * 0.5f * BladeFastMoveStaminaPerField);
		}
		// Through the middle, only the way in to it counts as coming in.
		const float InwardShare = FMath::Sign(Before) == FMath::Sign(After) ? FMath::Abs(Before) - FMath::Abs(After) : FMath::Abs(Before);
		// A blade drawn fast either way cuts in one turn; it keeps to that while the stroke lasts and lets go of it slowly.
		const float AcrossSpeed = FMath::Abs(After - Before) / DeltaTime;
		const float Fast = FMath::Clamp((AcrossSpeed - BladeOneTurnSpeedMin) / FMath::Max(BladeOneTurnSpeedMax - BladeOneTurnSpeedMin, KINDA_SMALL_NUMBER), 0.f, 1.f);
		OneTurn = FMath::Max(Fast, FMath::FInterpConstantTo(OneTurn, 0.f, DeltaTime, BladeOneTurnFadeRate));
		Inward = FMath::Max(InwardShare, 0.f) / DeltaTime;
		if (InwardShare > 0.f)
		{
			PunchSlot = Before > 0.f ? 1 : 0;
		}

		// The arms go after the mouse as a weight on a spring, in small steps so a long frame does not throw them off.
		const AAedirItemActor* Blade = AedirFistsHeldItem(Owner, bArmed[1] ? 1 : 0);
		const bool bBladeHeld = (HeldNow(0) && bArmed[0]) || (HeldNow(1) && bArmed[1]);
		float Hz = FistAimHz;
		float Damping = FistAimDamping;
		if (bBladeHeld && Blade)
		{
			const float Heavier = FMath::Sqrt(BladeAimRefKg / FMath::Max(Blade->Weight, 0.1f));
			Hz = BladeAimHz * FMath::Clamp(Heavier, BladeAimMinShare, BladeAimMaxShare) * (bTwoHand ? TwoHandAimShare : 1.f);
			Damping = BladeAimDamping;
		}
		// Out of breath, slower.
		const UAedirStatsComponent* Breath = Owner->FindComponentByClass<UAedirStatsComponent>();
		const float Tired = Breath && TiredFrom > KINDA_SMALL_NUMBER ? FMath::Clamp((TiredFrom - Breath->GetStaminaFraction()) / TiredFrom, 0.f, 1.f) : 0.f;
		Hz *= FMath::Lerp(1.f, TiredSpeedShare, Tired);
		const float W = UE_TWO_PI * FMath::Max(Hz, 0.5f);
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime * W / 0.25f), 1, 32);
		const float Step = DeltaTime / Steps;
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			AimSpeed += ((MouseAim - Aim) * (W * W) - AimSpeed * (2.f * Damping * W)) * Step;
			Aim += AimSpeed * Step;
		}
		Aim.X = FMath::Clamp(Aim.X, -1.0, 1.0);
		Aim.Y = FMath::Clamp(Aim.Y, -1.0, 1.0);
	}
	PendingMouse = FVector2D::ZeroVector;
	if (!bSteering)
	{
		OneTurn = FMath::FInterpConstantTo(OneTurn, 0.f, DeltaTime, BladeOneTurnFadeRate);
		MouseCarry = FVector2D::ZeroVector;
		MouseAim = Aim;
		AimSpeed = FVector2D::ZeroVector;
	}

	// Fast in towards the middle the arc reaches out; slow, still or let go, it comes back in to the guard.
	float PunchWanted = bSteering ? FMath::Clamp((Inward - PunchSlowSpeed) / FMath::Max(PunchFastSpeed - PunchSlowSpeed, KINDA_SMALL_NUMBER), 0.f, 1.f) : 0.f;
	// Thrown, the punch stays out while the arm, behind the mouse on its spring, is still coming round in to the middle, and
	// PunchHoldSeconds after: it lands straight out in front instead of spending itself at the side on the way in.
	const bool bArmComingIn = bSteering && Punch > 0.f && FMath::Abs(static_cast<float>(Aim.X)) > CenterDeadZone && FMath::Sign(AimSpeed.X) == -FMath::Sign(Aim.X);
	PunchHoldLeft = bArmComingIn ? PunchHoldSeconds : FMath::Max(PunchHoldLeft - DeltaTime, 0.f);
	if (bSteering && PunchHoldLeft > 0.f)
	{
		PunchWanted = FMath::Max(PunchWanted, Punch);
	}
	if (!bDevFrozen)
	{
		const float Before = Punch;
		const UAedirStatsComponent* Breath = Owner->FindComponentByClass<UAedirStatsComponent>();
		const float Tired = Breath && TiredFrom > KINDA_SMALL_NUMBER ? FMath::Clamp((TiredFrom - Breath->GetStaminaFraction()) / TiredFrom, 0.f, 1.f) : 0.f;
		// An arm someone holds goes out the slower, the harder it is held.
		const float HeldSlow = FMath::Lerp(1.f, HeldPunchSpeedShare, HeldShare[PunchSlot]);
		float Next = FMath::FInterpConstantTo(Punch, PunchWanted, DeltaTime, (PunchWanted > Punch ? PunchOutRate * HeldSlow : PunchBackRate) * FMath::Lerp(1.f, TiredSpeedShare, Tired));
		// Going out costs breath: as far as what is left pays for, and no further. A blade costs more than a fist.
		const float Cost = bArmed[PunchSlot] && HeldNow(PunchSlot) ? BladeSwingStaminaCost : PunchStaminaCost;
		if (Next > Before && Cost > 0.f)
		{
			const UAedirStatsComponent* Stats = Owner->FindComponentByClass<UAedirStatsComponent>();
			if (Stats)
			{
				Next = FMath::Min(Next, Before + Stats->GetStamina() / Cost);
			}
			SpendStamina((Next - Before) * Cost);
		}
		Punch = Next;
	}
	if (Punch > PeakPunch)
	{
		PeakPunch = Punch;
		PeakAim = Aim;
	}

	// Across the dead zone in the middle the guard stays; past it the fists go round the arc.
	const float AcrossAbs = FMath::Abs(static_cast<float>(Aim.X));
	const float Across = FMath::Sign(static_cast<float>(Aim.X)) * FMath::Max(AcrossAbs - DeadZone, 0.f) / FMath::Max(1.f - DeadZone, KINDA_SMALL_NUMBER);
	const float Theta = FMath::DegreesToRadians(Across * ArcDegrees * 0.5f);
	const float UpDown = static_cast<float>(Aim.Y);

	// The frame of the body, turned with it but always upright.
	const FRotator Yaw(0.f, Owner->GetActorRotation().Yaw, 0.f);
	const FVector Forward = Yaw.RotateVector(FVector::ForwardVector);
	const FVector Right = Yaw.RotateVector(FVector::RightVector);
	const FVector Up = FVector::UpVector;
	const USkeletalMeshComponent* Mesh = Owner->GetMesh();
	if (Mesh)
	{
		for (int32 Arm = 0; Arm < 2; ++Arm)
		{
			const FVector ShoulderAt = Mesh->GetBoneLocation(Arm == 1 ? TEXT("upperarm_r") : TEXT("upperarm_l"));
			const FVector ElbowAt = Mesh->GetBoneLocation(Arm == 1 ? TEXT("lowerarm_r") : TEXT("lowerarm_l"));
			const FVector WristAt = Mesh->GetBoneLocation(Arm == 1 ? TEXT("hand_r") : TEXT("hand_l"));
			const double Length = FVector::Dist(ShoulderAt, ElbowAt) + FVector::Dist(ElbowAt, WristAt);
			PeakStraight[Arm] = FMath::Max(PeakStraight[Arm], Length > KINDA_SMALL_NUMBER ? static_cast<float>(FVector::Dist(ShoulderAt, WristAt) / Length) : 0.f);
			const FVector ChestAt = (Mesh->GetBoneLocation(TEXT("upperarm_l")) + Mesh->GetBoneLocation(TEXT("upperarm_r"))) * 0.5;
			PeakAhead[Arm] = FMath::Max(PeakAhead[Arm], static_cast<float>(FVector::DotProduct(WristAt - ChestAt, Owner->GetActorForwardVector())));
		}
	}
	const FVector ShoulderL = Mesh ? Mesh->GetBoneLocation(TEXT("upperarm_l")) : Owner->GetActorLocation() - Right * 18.f + Up * 45.f;
	const FVector ShoulderR = Mesh ? Mesh->GetBoneLocation(TEXT("upperarm_r")) : Owner->GetActorLocation() + Right * 18.f + Up * 45.f;
	const FVector Chest = (ShoulderL + ShoulderR) * 0.5;
	// Every reach and height of the guard and the arcs is for a body of the usual size: a bigger, stronger one reaches as much
	// further as it is bigger, a smaller one less.
	const UAedirStatsComponent* GrowthStats = Owner->FindComponentByClass<UAedirStatsComponent>();
	const float BodyScale = GrowthStats ? GrowthStats->GetStrengthGrowth() : 1.f;

	// Up and down the guard turns round the chest as a whole, so its shape holds.
	const float Phi = FMath::DegreesToRadians(UpDown > 0.f ? UpDown * ArcUpDegrees : UpDown * ArcDownDegrees);

	// The elbow goes from down by the ribs, the two a little apart, to up behind the fist as the arm is drawn round.
	const float Lift = FMath::SmoothStep(ElbowLiftFrom, ElbowLiftTo, AcrossAbs);
	const float HalfSpread = FMath::DegreesToRadians(ElbowSpreadDegrees * 0.5f);
	const float AimSide = FMath::Sign(static_cast<float>(Aim.X));
	const bool bBoth = HeldNow(0) && HeldNow(1);
	// A blade's arc tipped with the view, about the axis across the body through the chest.
	const float ViewPitch = static_cast<float>(FRotator::NormalizeAxis(Owner->GetControlRotation().Pitch));
	const float LookTilt = FMath::DegreesToRadians(FMath::Clamp(ViewPitch * BladeLookTiltShare, -BladeLookTiltDownMaxDegrees, BladeLookTiltUpMaxDegrees));
	float Raised = 0.f;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const float Side = Slot == 1 ? 1.f : -1.f;
		Alpha[Slot] = FMath::FInterpConstantTo(Alpha[Slot], HeldNow(Slot) ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(RaiseSeconds, KINDA_SMALL_NUMBER));
		// In both hands the left one stays on the grip, whether the blade is up or not, until it is let go.
		if (Slot == 0)
		{
			GripAlpha = FMath::FInterpConstantTo(GripAlpha, bTwoHand && !bOffHandOut && FlinchLeft[0] <= 0.f ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(RaiseSeconds, KINDA_SMALL_NUMBER));
		}
		Raised = FMath::Max(Raised, Alpha[Slot]);
		// With both arms up, only the arm on the side the mouse is drawn to goes round the arc and only the arm that came in
		// punches; the other keeps its guard. One arm alone goes all the way round.
		const bool bFollows = !bBoth || AimSide == Side;
		const bool bBlade = bArmed[Slot];
		// Over to the other side a blade goes round a shorter way.
		const float ArmTheta = bFollows ? Theta * (bBlade && Theta * Side < 0.f ? BladeCrossArcShare : 1.f) : 0.f;
		const float ArmPunch = (!bBoth || Slot == PunchSlot) ? Punch : 0.f;
		const float Raise = FMath::Max(UpDown, 0.f);
		// The arc: in at the sides, out at the middle as far as the punch carries it.
		const float SinT = FMath::Sin(ArmTheta);
		const float CosT = FMath::Cos(ArmTheta);
		// A blade is held further out, and reaches up further over the head.
		// In both hands, drawn to its own side, it stays nearer the chest, where the second arm reaches it.
		const float FistLower = bBlade ? 0.f : FMath::Max(-UpDown, 0.f);
		const float FistRaise = bBlade ? 0.f : Raise;
		// Lowered, a fist's far points at the sides come in nearer the body (FallingSideInShare), so it comes down from there.
		const float OwnSideRadius = FMath::Lerp(SideRadiusCm, TwoHandSideRadiusCm, Slot == 1 && bBlade ? GripAlpha : 0.f) * (1.f - FistLower * FallingSideInShare);
		const float Radius = FMath::Lerp(bBlade ? BladeGuardForwardCm : GuardForwardCm, bBlade && Theta * Side < 0.f ? BladeCrossRadiusCm : OwnSideRadius, FMath::Abs(SinT)) + ArmPunch * PunchReachCm * (bBlade && Slot == 1 ? FMath::Lerp(1.f, TwoHandPunchReachShare, GripAlpha) : 1.f) * CosT * CosT + (bBlade ? Raise * BladeUpReachCm + ArmPunch * BladeLookUpPunchCm * FMath::Clamp(ViewPitch / FMath::Max(BladeLookUpFullDegrees, 1.f), 0.f, 1.f) + FMath::Max(-UpDown, 0.f) * BladeDownReachCm : FMath::Max(-UpDown, 0.f) * FistDownReachCm);
		const FVector Out = Forward * CosT + Right * SinT;
		const FVector Along = -Forward * SinT + Right * CosT;
		// Round towards the side the height of the middle falls away and turns the other way, so a raised fist comes down
		// across and a lowered one up.
		// A blade goes on up round over the head and back.
		// and further down, to strike low.
		const float ArmUp = bBlade ? FMath::DegreesToRadians(UpDown > 0.f ? UpDown * BladeArcUpDegrees : UpDown * BladeArcDownDegrees) : Phi;
		// A blade keeps some of it the same way at the side, so the sweep is tipped up or down with the mouse.
		const float ArmPhi = ArmUp * (bBlade ? FMath::Lerp(1.f, BladeArcSideKeep, FMath::Abs(SinT)) : 1.f - (1.f + ArcSideReverse) * FMath::Abs(SinT));
		const float SinP = FMath::Sin(ArmPhi);
		const float CosP = FMath::Cos(ArmPhi);
		// Tips a direction of the level guard up round the chest, about the axis across this arm's way out.
		const auto Tip = [&](const FVector& V)
		{
			const double A = FVector::DotProduct(V, Out);
			const double B = FVector::DotProduct(V, Up);
			return V - Out * A - Up * B + Out * (A * CosP - B * SinP) + Up * (A * SinP + B * CosP);
		};
		// A blade held up in the middle is lower, before the chest; raised or drawn round it comes up to the fist's height.
		const float Height = GuardHeightCm - (bBlade ? BladeGuardDropCm * (1.f - FMath::Abs(SinT)) * (1.f - Raise) : 0.f);
		// Lowered, round at the sides the fist is drawn further back too (FallingSideBackCm), so it is brought down from above
		// and behind rather than swept across from up high.
		const FVector FistFromChest = Tip(Out * Radius + Along * (Side * GuardGapCm * 0.5f) + Up * Height) - Forward * (FMath::Abs(SinT) * FistLower * FallingSideBackCm);
		const FVector GuardPole = -Forward * 0.25f + Right * (Side * FMath::Sin(HalfSpread)) - Up * FMath::Cos(HalfSpread);
		const float LiftAngle = FMath::DegreesToRadians(bBlade ? BladeElbowLiftDegrees : ElbowLiftDegrees);
		// Lifted, the elbow goes back and out behind the fist, about level.
		const FVector LiftPole = (-Forward + Right * Side).GetSafeNormal() * FMath::Cos(LiftAngle) + Up * FMath::Sin(LiftAngle);
		// Only the arm drawn back to its own side lifts its elbow; the one brought across keeps it down.
		// Held in both hands the blade is thrust out by both arms together, the elbows staying down.
		const float PunchLift = PunchElbowLift * (bBlade ? FMath::Lerp(1.f, TwoHandPunchElbowLiftShare, GripAlpha) : 1.f);
		const float OwnLift = FMath::Max(AimSide == Side ? Lift : 0.f, ArmPunch * PunchLift);
		FVector PoleLevel = FMath::Lerp(GuardPole.GetSafeNormal(), LiftPole.GetSafeNormal(), OwnLift).GetSafeNormal();
		// Raised the elbow is held higher (RisingElbowRaise), so a fist going up comes in round from the side and not from
		// below; lowered it is held a little higher too (FallingElbowRaise), so the fist comes straight down. The fist itself
		// stays where it was.
		PoleLevel = (PoleLevel + Up * (FistRaise * RisingElbowRaise + FistLower * FallingElbowRaise)).GetSafeNormal();
		// One arm alone brought across past the middle turns its elbow out from the shoulder, so the arm goes round the body
		// and not into it.
		if (!bBoth && AimSide != 0.f && AimSide != Side)
		{
			const FVector CrossPole = (Right * Side + Up * 0.25f).GetSafeNormal();
			PoleLevel = FMath::Lerp(PoleLevel, CrossPole, CrossElbowOut * FMath::Clamp(FMath::Abs(SinT) / FMath::Max(FMath::Sin(FMath::DegreesToRadians(SingleArmAcrossShare * ArcDegrees * 0.5f)), 0.01f), 0.f, 1.f)).GetSafeNormal();
		}
		const FVector Shoulder = Slot == 1 ? ShoulderR : ShoulderL;
		const float SinL = FMath::Sin(bBlade ? LookTilt : 0.f);
		const float CosL = FMath::Cos(bBlade ? LookTilt : 0.f);
		const auto Look = [&](const FVector& V)
		{
			const double A = FVector::DotProduct(V, Forward);
			const double B = FVector::DotProduct(V, Up);
			return V - Forward * A - Up * B + Forward * (A * CosL - B * SinL) + Up * (A * SinL + B * CosL);
		};
		FistWorld[Slot] = Chest + Look(FistFromChest) * BodyScale;
		// Thrust out in both hands, the blade goes out and a little down, so the arms reach on below the shoulders and the
		// elbows are not drawn up with them.
		if (bBlade)
		{
			FistWorld[Slot] -= Up * (ArmPunch * TwoHandPunchDropCm * BodyScale * (Slot == 1 ? GripAlpha : 0.f));
		}
		const FVector Pole = Look(Tip(PoleLevel));
		// Out at its own side the fist turns over, palm down; brought across or in the guard it stays upright. A blade's wrist
		// turns out the other way there, and the nearer the middle, the more its point is brought round ahead.
		FistTurn[Slot] = (bFollows && AimSide == Side ? FMath::Abs(SinT) : 0.f) * SideFistTurnDegrees * Side;
		// A blade drawn either way: first in, the point kept on the other, then on its own side back out past where it
		// started, across on to a little in.
		const float Split = FMath::Clamp(BladeSideTurnSplit, 0.01f, 0.99f);
		const bool bOwnSide = AimSide == Side;
		const float Share = bFollows && AimSide != 0.f ? FMath::Abs(Across) : 0.f;
		if (bBlade)
		{
			// Past the middle of the way to its own side the turn in is left quickly, so the point swings out from the head.
			const float Past = (Share - Split) / (1.f - Split);
			const float OutShare = bOwnSide ? 1.f - FMath::Pow(1.f - FMath::Clamp(Past, 0.f, 1.f), FMath::Max(BladeSideOutEase, 1.f)) : Past;
			const float TwoTurns = Share <= Split
				? FMath::Lerp(0.f, bOwnSide ? BladeSideInDegrees : BladeCrossInDegrees, Share / Split)
				: FMath::Lerp(bOwnSide ? BladeSideInDegrees : BladeCrossInDegrees, bOwnSide ? -BladeSideOutDegrees : BladeCrossTurnDegrees, OutShare);
			// Fast, the wrist goes the one way all along, the edge leading.
			const float OneWay = FMath::Lerp(0.f, bOwnSide ? -BladeSideOutDegrees : BladeCrossTurnDegrees, Share);
			FistTurn[Slot] = FMath::Lerp(TwoTurns, OneWay, OneTurn) * Side;
		}
		// Either way it keeps looking ahead while the wrist turns in, and lets go of that from there on. Raised or lowered the
		// hand is not turned about the forearm: only the wrist bends, so a blow comes straight down. Cut in one turn, it lets
		// go of looking ahead all along the way.
		const float Ahead = FMath::Lerp(Share <= Split ? 1.f : 1.f - (Share - Split) / (1.f - Split), 1.f - Share, OneTurn);
		BladeAhead[Slot] = bBlade ? Ahead : 0.f;
		// The way the knuckles (and so the blade) lean, tipped up or down round the chest with the arm.
		BladeLean[Slot] = Look(Tip(FMath::Lerp((Up - Forward).GetSafeNormal(), Up, Ahead).GetSafeNormal())).GetSafeNormal();
		// Raised or lowered, the point is no longer bent round ahead and up by the wrist.
		const float Lower = FMath::Max(-UpDown, 0.f);
		BladeAim[Slot] = bBlade ? Ahead * (1.f - Raise) * (1.f - Lower) : 0.f;
		// Up at either side from the middle of the way there on, up raised, down lowered.
		// Brought across to the other side it stays straight.
		const float SideShare = bFollows && bOwnSide ? FMath::Clamp((FMath::Abs(Across) - Split) / (1.f - Split), 0.f, 1.f) : 0.f;
		// Drawn round and raised at once, the larger of the two, not both on top of each other.
		// Raised, the side's bend gives way to the raise's, so over the head the point is not thrown back.
		BladeWristUp[Slot] = bBlade ? FMath::Max(SideShare * (1.f - Raise) * BladeSideWristUpDegrees, Raise * BladeRaiseWristUpDegrees) - Lower * BladeLowerWristDownDegrees : 0.f;
		// Going by the head on the way across, the blade is bent down out of the way and comes up again at the side.
		if (bBlade)
		{
			BladeWristUp[Slot] -= FMath::Sin(UE_PI * FMath::Clamp(FMath::Abs(static_cast<float>(Aim.X)), 0.f, 1.f)) * (1.f - Raise) * BladeSweepWristDownDegrees;
		}
		BladeAimWay[Slot] = Look((Forward + Up * BladeGuardUpShare).GetSafeNormal());
		ElbowWorld[Slot] = (Shoulder + FistWorld[Slot]) * 0.5 + Pole * (40.f * BodyScale);
		// Round at the side the fist alone goes on up or down with the mouse; the elbow stays where it was.
		FistWorld[Slot] += Up * (FMath::Abs(SinT) * (UpDown > 0.f ? UpDown * SideFistRiseCm : UpDown * SideFistDropCm) * BodyScale);
		// A fist is never sent further than its own arm reaches: a shorter body's punch goes out to where its arm, straight,
		// can put the fist, and the arm gets there, instead of straining after a place it cannot reach.
		if (!bBlade && Mesh)
		{
			const FVector ElbowBone = Mesh->GetBoneLocation(Slot == 1 ? TEXT("lowerarm_r") : TEXT("lowerarm_l"));
			const FVector HandBone = Mesh->GetBoneLocation(Slot == 1 ? TEXT("hand_r") : TEXT("hand_l"));
			const float ArmLength = static_cast<float>(FVector::Dist(Shoulder, ElbowBone) + FVector::Dist(ElbowBone, HandBone)) * FistMaxReachShare;
			const FVector FromShoulder = FistWorld[Slot] - Shoulder;
			if (ArmLength > KINDA_SMALL_NUMBER && FromShoulder.Size() > ArmLength)
			{
				FistWorld[Slot] = Shoulder + FromShoulder.GetSafeNormal() * ArmLength;
			}
		}
		ShownAlpha[Slot] = Alpha[Slot];
		// In both hands and not raised, the blade is carried low before the hips, aslant across with its point out to the
		// right and down, the elbow hanging out at its side; raised, it goes as in one hand.
		const float Rest = Slot == 1 && bBlade ? GripAlpha * (1.f - Alpha[Slot]) : 0.f;
		if (Rest > 0.f)
		{
			// Carried low it turns with the chest as the view turns it, arms and blade together, so turning to look does not
			// lift the second elbow off the side.
			const UAedirGaitComponent* Gait = Owner->FindComponentByClass<UAedirGaitComponent>();
			const FRotator ChestYaw(0.f, Owner->GetActorRotation().Yaw + (Gait ? Gait->GetGaitState().SpineTwistYaw * TwoHandRestFollowTwist : 0.f), 0.f);
			const FVector RestForward = ChestYaw.RotateVector(FVector::ForwardVector);
			const FVector RestRight = ChestYaw.RotateVector(FVector::RightVector);
			const FVector Lay = FRotator(TwoHandRestPitchDegrees, TwoHandRestYawDegrees, 0.f).RotateVector(FVector::ForwardVector);
			const FVector Way = RestForward * Lay.X + RestRight * Lay.Y + Up * Lay.Z;
			const FVector RestPlace = Owner->GetActorLocation() + (RestForward * TwoHandRestForwardCm + RestRight * TwoHandRestSideCm + Up * TwoHandRestHeightCm) * BodyScale;
			const FVector RestPole = (-Up * 0.8f + RestRight * 0.6f - RestForward * 0.2f).GetSafeNormal();
			FistWorld[Slot] = FMath::Lerp(FistWorld[Slot], RestPlace, Rest);
			ElbowWorld[Slot] = FMath::Lerp(ElbowWorld[Slot], (Shoulder + RestPlace) * 0.5 + RestPole * (40.f * BodyScale), Rest);
			BladeAimWay[Slot] = FMath::Lerp(BladeAimWay[Slot], Way, Rest).GetSafeNormal();
			BladeLean[Slot] = FMath::Lerp(BladeLean[Slot], Way, Rest).GetSafeNormal();
			BladeAim[Slot] = FMath::Lerp(BladeAim[Slot], 1.f, Rest);
			BladeAhead[Slot] = FMath::Lerp(BladeAhead[Slot], 1.f, Rest);
			BladeWristUp[Slot] = FMath::Lerp(BladeWristUp[Slot], 0.f, Rest);
			FistTurn[Slot] = FMath::Lerp(FistTurn[Slot], 0.f, Rest);
			ShownAlpha[Slot] = 1.f - (1.f - Alpha[Slot]) * (1.f - GripAlpha);
		}
		// Held, the fist gets only as far from the holding hand as the struggle lets it.
		if (HeldShare[Slot] > 0.f)
		{
			const FVector Kept = HeldAt[Slot] + (FistWorld[Slot] - HeldAt[Slot]).GetClampedToMaxSize(HeldSlackCm * BodyScale);
			const FVector Moved = FMath::Lerp(FistWorld[Slot], Kept, HeldShare[Slot]) - FistWorld[Slot];
			FistWorld[Slot] += Moved;
			ElbowWorld[Slot] += Moved * 0.5;
		}
	}
	const bool bBladeUp = (HeldNow(0) && bArmed[0]) || (HeldNow(1) && bArmed[1]);
	WaistPitch = (UpDown > 0.f ? UpDown * WaistBackDegrees : UpDown * (bBladeUp ? BladeWaistForwardDegrees : WaistForwardDegrees)) * Raised;
	// A bare fist thrown leans the chest in after it (forward is negative here).
	if (!bBladeUp)
	{
		WaistPitch -= Punch * PunchLeanDegrees * Raised;
	}
	// The punching arm's shoulder goes forward with the blow: the right one turns the waist left, the left one right.
	const float PunchTurn = Punch * PunchWaistTurnDegrees * (bBoth ? (PunchSlot == 1 ? -1.f : 1.f) : (HeldNow(1) ? -1.f : 1.f));
	const float WaistWanted = (Across * WaistTurnDegrees + PunchTurn) * Raised;
	// The waist follows on its own heavier spring, never faster than a trunk turns.
	const float WaistW = UE_TWO_PI * FMath::Max(WaistTurnHz, 0.5f);
	const int32 WaistSteps = FMath::Clamp(FMath::CeilToInt(DeltaTime * WaistW / 0.25f), 1, 32);
	const float WaistStep = DeltaTime / WaistSteps;
	for (int32 Index = 0; Index < WaistSteps; ++Index)
	{
		WaistYawRate += ((WaistWanted - WaistYaw) * WaistW * WaistW - WaistYawRate * 2.f * WaistW) * WaistStep;
		WaistYawRate = FMath::Clamp(WaistYawRate, -WaistTurnMaxDegreesPerSecond, WaistTurnMaxDegreesPerSecond);
		WaistYaw += WaistYawRate * WaistStep;
	}
	HipDrop = FMath::Max(-UpDown, 0.f) * (bBladeUp ? BladeDownHipDropCm : DownHipDropCm) * Raised;
}
