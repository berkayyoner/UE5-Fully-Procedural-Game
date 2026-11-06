#include "AedirBalanceComponent.h"

#include "Combat/AedirCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Aedir.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "TimerManager.h"

namespace
{
	constexpr float AedirLegInjuryRecoveryPenalty = 0.5f;
	constexpr float AedirLeanInterpSpeed = 8.f;
	constexpr float AedirSafeLandingSpeed = 900.f;
	constexpr float AedirUntrainedSafeLandingSpeed = 520.f;
	constexpr float AedirAthletismRecoveryScale = 100.f;
	constexpr float AedirAthletismDamageScale = 200.f;
	constexpr float AedirWalkableNormalZ = 0.5f;
	constexpr float AedirWallImpactMaxLoss = 0.8f;
	constexpr float AedirObjectImpactBalanceMultiplier = 10.f;
	constexpr float AedirObjectImpactDamagePerScore = 0.002f;
	constexpr float AedirWallImpactDamagePerSpeed = 0.01f;

	constexpr float HitReactionAthleticsCap = 100.f;
	constexpr float HitReactionAthleticsLossScale = 0.5f;
	constexpr float HitReactionStaminaLossScale = 0.5f;
	constexpr float HitReactionDefaultAthletism = 10.f;
	constexpr float HitReactionStaggerThreshold = 0.25f;
	constexpr float HitReactionStaggerMinSeconds = 0.4f;
	constexpr float HitReactionStaggerMaxSeconds = 1.f;
	constexpr float HitReactionStaggerSpeedPerJoule = 2.f;
	constexpr float HitReactionStaggerSpeedMax = 350.f;
	constexpr float HitReactionStaggerRecoveryScale = 0.7f;
	constexpr float HitReactionDazeEnergyRef = 60.f;
	constexpr float HitReactionDazeMaxSeconds = 2.5f;
	constexpr float HitReactionDazeRecoveryScale = 0.5f;
	constexpr float HitReactionKnockdownExtraSeconds = 0.8f;
	constexpr float HitReactionBoneImpulseScale = 0.35f;
	constexpr float HitReactionBoneImpulseMax = 25000.f;

	constexpr float ComBalanceSampleIntervalSeconds = 0.1f;
	constexpr float ComBalanceGroundToleranceCm = 12.f;
	constexpr float ComBalanceTraceUpCm = 20.f;
	constexpr float ComBalanceTraceDownCm = 40.f;
	constexpr float ComBalanceHeelOffsetCm = 6.f;
	constexpr float ComBalanceToeOffsetCm = 6.f;
	constexpr float ComBalanceFootHalfWidthCm = 4.5f;
	constexpr float ComBalanceFullLossMarginCm = 12.f;
	constexpr float ComBalanceMaxLossPerSecond = 0.9f;
	constexpr float ComBalanceRecoveryMarginCm = -8.f;
	constexpr float ComBalanceMovingSpeedRef = 300.f;
	constexpr float ComBalanceMovingAllowanceCm = 30.f;
	constexpr float ComBalanceReflexRangeCm = 220.f;
	constexpr float ComBalanceReflexFallSpeed = 350.f;
	constexpr float ComBalanceReflexRagdollSeconds = 0.4f;
	constexpr float ComBalanceReflexForwardCm = 25.f;
	constexpr float ComBalanceReflexInterpSpeed = 10.f;
	constexpr float ComBalanceReflexFallDamageReduction = 0.25f;

	// Cross product of (A - Origin) and (B - Origin). Positive when B is counter-clockwise from A around Origin.
	double ComBalanceCross(const FVector2D& Origin, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D OriginToA = A - Origin;
		const FVector2D OriginToB = B - Origin;
		return OriginToA.X * OriginToB.Y - OriginToA.Y * OriginToB.X;
	}

	double ComBalanceDistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Edge = B - A;
		const double LengthSquared = Edge.SizeSquared();
		if (LengthSquared <= UE_SMALL_NUMBER)
		{
			return (Point - A).Size();
		}

		const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSquared, 0.0, 1.0);
		return (Point - (A + Edge * T)).Size();
	}

	float HitReactionZoneEnergyRef(EAedirBodyZone Zone)
	{
		switch (Zone)
		{
		case EAedirBodyZone::Head: return 25.f;
		case EAedirBodyZone::Torso: return 60.f;
		case EAedirBodyZone::Arm: return 120.f;
		case EAedirBodyZone::Leg: return 40.f;
		default: checkNoEntry(); return 60.f;
		}
	}
}

UAedirBalanceComponent::UAedirBalanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UAedirBalanceComponent::BeginPlay()
{
	Super::BeginPlay();

	CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		return;
	}

	BodyMesh = CharacterOwner->GetMesh();
	BodyCapsule = CharacterOwner->GetCapsuleComponent();
	if (!BodyMesh || !BodyCapsule)
	{
		return;
	}

	BodyMesh->SetAllBodiesBelowSimulatePhysics(PelvisBoneName, true, true);
	BodyMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	BodyMesh->SetNotifyRigidBodyCollision(true);
	BodyMesh->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnMeshHit);

	BodyCapsule->SetNotifyRigidBodyCollision(true);
	BodyCapsule->OnComponentHit.AddDynamic(this, &UAedirBalanceComponent::OnCapsuleHit);

	PelvisRestHeight = static_cast<float>(BodyMesh->GetBoneLocation(PelvisBoneName, EBoneSpaces::ComponentSpace).Z);

	SetupMotors();

	if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}

	FTimerHandle DiagnosticsTimer;
	GetWorld()->GetTimerManager().SetTimer(DiagnosticsTimer, [this]()
	{
		if (!BodyMesh)
		{
			return;
		}
		int32 SimulatingBodies = 0;
		for (const FBodyInstance* Body : BodyMesh->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				++SimulatingBodies;
			}
		}
		UE_LOG(LogAedir, Log, TEXT("Balance diagnostics: physics asset %s, %d bodies, %d simulating, pelvis body %s."),
			BodyMesh->GetPhysicsAsset() ? *BodyMesh->GetPhysicsAsset()->GetName() : TEXT("none"), BodyMesh->Bodies.Num(), SimulatingBodies,
			BodyMesh->GetBodyInstance(PelvisBoneName) ? TEXT("found") : TEXT("missing"));
	}, 1.5f, false);
}

void UAedirBalanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CharacterOwner || !BodyMesh || !BodyCapsule)
	{
		return;
	}

	ComSampleTimer -= DeltaTime;
	if (ComSampleTimer <= 0.f)
	{
		ComSampleTimer = ComBalanceSampleIntervalSeconds;
		SampleCenterOfMass();
		SampleProtectiveReflex();
	}
	SmoothProtectiveReflex(DeltaTime);

	if (IsAuthoritative())
	{
		UpdateServerState(DeltaTime);
	}

	UpdatePelvisLean(DeltaTime);
	UpdateMotors(DeltaTime, false);
	UpdateLegBlend(DeltaTime);

	if (!bIsRagdoll)
	{
		DrivePelvis();
	}
}

void UAedirBalanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirBalanceComponent, Balance);
	DOREPLIFETIME(UAedirBalanceComponent, LeanDirection);
	DOREPLIFETIME(UAedirBalanceComponent, bIsRagdoll);
	DOREPLIFETIME(UAedirBalanceComponent, StaggerSeconds);
	DOREPLIFETIME(UAedirBalanceComponent, DazeSeconds);
}

void UAedirBalanceComponent::ApplyBalanceImpact(FVector WorldDirection, float Magnitude)
{
	if (!IsAuthoritative() || Magnitude <= 0.f)
	{
		return;
	}

	AddLeanDirection(WorldDirection, Magnitude);

	SetBalance(Balance - Magnitude);
}

void UAedirBalanceComponent::ApplyImpulseImpact(FVector WorldImpulse)
{
	ApplyBalanceImpact(WorldImpulse.GetSafeNormal(), static_cast<float>(WorldImpulse.Size()) * ImpactBalanceScale);
}

void UAedirBalanceComponent::HandleLanded(float LandingSpeed)
{
	if (!IsAuthoritative())
	{
		return;
	}

	// The protective reflex softens the landing. The alpha is read once and cleared for the next fall.
	const float ReflexAlpha = FMath::Clamp(LastProtectiveReflexAlpha, 0.f, 1.f);
	LastProtectiveReflexAlpha = 0.f;

	if (LandingSpeed > FallSpeedBalanceThreshold)
	{
		const float Loss = FMath::Clamp((LandingSpeed - FallSpeedBalanceThreshold) / FallSpeedBalanceThreshold, 0.f, 1.f);
		ApplyBalanceImpact(GetOwner()->GetActorForwardVector(), Loss);
	}

	if (UAedirStatsComponent* StatsComponent = FindStats())
	{
		// A trained body absorbs more of a fall: the safe speed rises with athletism, an untrained one crumples at about 1.4 m.
		const float Athletism = StatsComponent->Athletism;
		const float SafeSpeed = FMath::Lerp(AedirUntrainedSafeLandingSpeed, AedirSafeLandingSpeed, FMath::Clamp(Athletism / AedirAthletismRecoveryScale, 0.f, 1.f));
		if (LandingSpeed > SafeSpeed)
		{
			const float ReflexScale = 1.f - ComBalanceReflexFallDamageReduction * ReflexAlpha;
			StatsComponent->ApplyDamage((LandingSpeed - SafeSpeed) * FallDamagePerSpeed * (1.f - Athletism / AedirAthletismDamageScale) * ReflexScale);
			// Past the safe speed the legs give way and the body slams into the ground.
			const float CrumpleLoss = FMath::Clamp((LandingSpeed - SafeSpeed) / (SafeSpeed * 0.3f), 0.5f, 1.f);
			ApplyBalanceImpact(GetOwner()->GetActorForwardVector(), CrumpleLoss);
		}
	}
}

void UAedirBalanceComponent::ApplyHitReaction(const FAedirImpactInfo& Impact)
{
	if (!IsAuthoritative() || !CharacterOwner)
	{
		return;
	}

	const UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : HitReactionDefaultAthletism;
	const float StaminaFraction = StatsComponent ? StatsComponent->GetStaminaFraction() : 1.f;

	const float Loss = ComputeBalanceLoss(Impact.Zone, Impact.EnergyJoules, Athletism, StaminaFraction);
	ApplyBalanceImpact(Impact.Direction, Loss);

	if (Impact.Bone != NAME_None && BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		const float ImpulseSize = FMath::Min(Impact.MassKg * Impact.RelativeSpeed * HitReactionBoneImpulseScale, HitReactionBoneImpulseMax);
		MulticastBoneImpulse(Impact.Bone, Impact.Direction * ImpulseSize, Impact.Location);
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bKnockedDown = Impact.EnergyJoules >= GetKnockdownEnergy(Impact.Zone) || Balance < FallBalanceThreshold;
	bool bStaggered = false;

	if (bKnockedDown)
	{
		if (!bIsRagdoll)
		{
			EnterRagdoll(MinRagdollSeconds + HitReactionKnockdownExtraSeconds);
		}
		else
		{
			const float ElapsedRagdoll = static_cast<float>(Now - RagdollStartTime);
			CurrentRagdollMinSeconds = FMath::Max(CurrentRagdollMinSeconds, ElapsedRagdoll + HitReactionKnockdownExtraSeconds);
		}
	}
	else if (!bIsRagdoll && ShouldStagger(Loss))
	{
		bStaggered = true;
		StaggerSeconds = FMath::Lerp(HitReactionStaggerMinSeconds, HitReactionStaggerMaxSeconds, Loss);

		const FVector PlanarDirection = FVector(Impact.Direction.X, Impact.Direction.Y, 0.f).GetSafeNormal();
		if (!PlanarDirection.IsNearlyZero())
		{
			CharacterOwner->LaunchCharacter(PlanarDirection * ComputeStaggerSpeed(Impact.EnergyJoules), true, false);
		}
	}

	DazeSeconds = FMath::Max(DazeSeconds, ComputeDazeSeconds(Impact.Zone, Impact.EnergyJoules));

	OnHitReaction.Broadcast(Impact.Zone, Impact.EnergyJoules, bStaggered, bKnockedDown);
}

float UAedirBalanceComponent::ComputeBalanceLoss(EAedirBodyZone Zone, float EnergyJoules, float Athletism, float StaminaFraction)
{
	const float EnergyShare = FMath::Clamp(EnergyJoules / HitReactionZoneEnergyRef(Zone), 0.f, 1.f);
	const float AthleticsFactor = 1.f - HitReactionAthleticsLossScale * FMath::Clamp(Athletism / HitReactionAthleticsCap, 0.f, 1.f);
	const float StaminaFactor = 1.f + HitReactionStaminaLossScale * (1.f - FMath::Clamp(StaminaFraction, 0.f, 1.f));

	return FMath::Clamp(EnergyShare * AthleticsFactor * StaminaFactor, 0.f, 1.f);
}

bool UAedirBalanceComponent::ShouldStagger(float BalanceLoss)
{
	return BalanceLoss >= HitReactionStaggerThreshold;
}

float UAedirBalanceComponent::GetKnockdownEnergy(EAedirBodyZone Zone)
{
	switch (Zone)
	{
	case EAedirBodyZone::Head: return 90.f;
	case EAedirBodyZone::Torso: return 220.f;
	case EAedirBodyZone::Arm: return 400.f;
	case EAedirBodyZone::Leg: return 70.f;
	default: checkNoEntry(); return 220.f;
	}
}

float UAedirBalanceComponent::ComputeDazeSeconds(EAedirBodyZone Zone, float EnergyJoules)
{
	if (Zone != EAedirBodyZone::Head)
	{
		return 0.f;
	}

	return FMath::Clamp(EnergyJoules / HitReactionDazeEnergyRef, 0.f, 1.f) * HitReactionDazeMaxSeconds;
}

float UAedirBalanceComponent::ComputeStaggerSpeed(float EnergyJoules)
{
	return FMath::Clamp(EnergyJoules * HitReactionStaggerSpeedPerJoule, 0.f, HitReactionStaggerSpeedMax);
}

float UAedirBalanceComponent::GetBalance() const
{
	return Balance;
}

bool UAedirBalanceComponent::IsRagdoll() const
{
	return bIsRagdoll;
}

bool UAedirBalanceComponent::IsStaggering() const
{
	return StaggerSeconds > 0.f;
}

bool UAedirBalanceComponent::IsDazed() const
{
	return DazeSeconds > 0.f;
}

float UAedirBalanceComponent::GetDazeSeconds() const
{
	return DazeSeconds;
}

float UAedirBalanceComponent::GetDazeFactor() const
{
	return FMath::Clamp(DazeSeconds / HitReactionDazeMaxSeconds, 0.f, 1.f);
}

FRotator UAedirBalanceComponent::GetPelvisLean() const
{
	return CurrentLean;
}

FVector UAedirBalanceComponent::GetCenterOfMassWorld() const
{
	return CenterOfMassWorld;
}

float UAedirBalanceComponent::GetSupportMargin() const
{
	return SupportMargin;
}

bool UAedirBalanceComponent::IsCenterOfMassOutsideSupport() const
{
	return bSupportGrounded && SupportMargin < 0.f;
}

bool UAedirBalanceComponent::GetRecoveryStepRequest(FVector& OutDirectionWorld, float& OutUrgency) const
{
	if (bIsRagdoll || !bSupportGrounded || SupportMargin >= ComBalanceRecoveryMarginCm || ComOffsetDirection.IsNearlyZero())
	{
		return false;
	}

	OutDirectionWorld = ComOffsetDirection;
	OutUrgency = FMath::Clamp(-SupportMargin / ComBalanceFullLossMarginCm, 0.f, 1.f);
	return true;
}

bool UAedirBalanceComponent::GetProtectiveReflex(float& OutAlpha, FVector& OutGroundTargetWorld) const
{
	const bool bActive = ProtectiveAlpha > KINDA_SMALL_NUMBER;
	OutAlpha = bActive ? ProtectiveAlpha : 0.f;
	OutGroundTargetWorld = bActive ? ProtectiveGroundWorld : FVector::ZeroVector;
	return bActive;
}

void UAedirBalanceComponent::ComputeConvexHull2D(const TArray<FVector2D>& Points, TArray<FVector2D>& OutHull)
{
	OutHull.Reset();

	TArray<FVector2D> Sorted = Points;
	Sorted.Sort([](const FVector2D& A, const FVector2D& B)
	{
		return A.X < B.X || (A.X == B.X && A.Y < B.Y);
	});

	int32 UniqueCount = 0;
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		if (UniqueCount == 0 || !Sorted[Index].Equals(Sorted[UniqueCount - 1]))
		{
			Sorted[UniqueCount++] = Sorted[Index];
		}
	}
	Sorted.SetNum(UniqueCount);

	if (Sorted.Num() < 3)
	{
		OutHull = Sorted;
		return;
	}

	// Andrew's monotone chain: lower hull left to right, then upper hull right to left.
	TArray<FVector2D> Chain;
	Chain.Reserve(Sorted.Num() * 2);
	for (const FVector2D& Point : Sorted)
	{
		while (Chain.Num() >= 2 && ComBalanceCross(Chain[Chain.Num() - 2], Chain.Last(), Point) <= 0.0)
		{
			Chain.Pop();
		}
		Chain.Add(Point);
	}

	const int32 LowerCount = Chain.Num() + 1;
	for (int32 Index = Sorted.Num() - 2; Index >= 0; --Index)
	{
		const FVector2D& Point = Sorted[Index];
		while (Chain.Num() >= LowerCount && ComBalanceCross(Chain[Chain.Num() - 2], Chain.Last(), Point) <= 0.0)
		{
			Chain.Pop();
		}
		Chain.Add(Point);
	}

	// The last point repeats the first one.
	Chain.Pop();
	OutHull = Chain;
}

float UAedirBalanceComponent::ComputeSupportMargin(const TArray<FVector2D>& Hull, const FVector2D& Point)
{
	const int32 Count = Hull.Num();
	if (Count == 0)
	{
		return -TNumericLimits<float>::Max();
	}

	if (Count == 1)
	{
		return -static_cast<float>((Point - Hull[0]).Size());
	}

	if (Count == 2)
	{
		return -static_cast<float>(ComBalanceDistanceToSegment(Point, Hull[0], Hull[1]));
	}

	// The hull is counter-clockwise, so a point is inside when it lies left of every edge.
	bool bInside = true;
	double MinEdgeDistance = TNumericLimits<double>::Max();
	double NearestBoundary = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& A = Hull[Index];
		const FVector2D& B = Hull[(Index + 1) % Count];
		const double EdgeLength = (B - A).Size();
		if (EdgeLength <= UE_SMALL_NUMBER)
		{
			continue;
		}

		const double Side = ComBalanceCross(A, B, Point) / EdgeLength;
		if (Side < 0.0)
		{
			bInside = false;
		}
		MinEdgeDistance = FMath::Min(MinEdgeDistance, Side);
		NearestBoundary = FMath::Min(NearestBoundary, ComBalanceDistanceToSegment(Point, A, B));
	}

	return bInside ? static_cast<float>(MinEdgeDistance) : -static_cast<float>(NearestBoundary);
}

float UAedirBalanceComponent::ComputeComBalanceLoss(float MarginCm, float DeltaSeconds)
{
	if (MarginCm >= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(-MarginCm / ComBalanceFullLossMarginCm, 0.f, 1.f) * ComBalanceMaxLossPerSecond * DeltaSeconds;
}

void UAedirBalanceComponent::AddLeanDirection(const FVector& WorldDirection, float Magnitude)
{
	const FVector LocalDirection = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(WorldDirection);
	const FVector2D PlanarDirection = FVector2D(LocalDirection.X, LocalDirection.Y).GetSafeNormal();
	LeanDirection = (LeanDirection + PlanarDirection * Magnitude).GetClampedToMaxSize(1.f);
}

void UAedirBalanceComponent::SampleCenterOfMass()
{
	bSupportGrounded = false;
	SupportMargin = 0.f;
	ComOffsetDirection = FVector::ZeroVector;

	if (bIsRagdoll || !BodyMesh || !CharacterOwner)
	{
		return;
	}

	// Mass-weighted mean over every body, the pelvis included.
	FVector WeightedSum = FVector::ZeroVector;
	double TotalMass = 0.0;
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		const float Mass = Body ? Body->GetBodyMass() : 0.f;
		if (Mass <= 0.f)
		{
			continue;
		}

		WeightedSum += Body->GetCOMPosition() * Mass;
		TotalMass += Mass;
	}

	if (TotalMass <= UE_SMALL_NUMBER)
	{
		return;
	}
	CenterOfMassWorld = WeightedSum / TotalMass;

	TArray<FVector2D> SupportPoints;
	AddFootSupportPoints(FootLeftBone, BallLeftBone, SupportPoints);
	AddFootSupportPoints(FootRightBone, BallRightBone, SupportPoints);
	if (SupportPoints.Num() == 0)
	{
		return;
	}

	TArray<FVector2D> Hull;
	ComputeConvexHull2D(SupportPoints, Hull);

	const FVector2D ComXY(CenterOfMassWorld.X, CenterOfMassWorld.Y);
	SupportMargin = ComputeSupportMargin(Hull, ComXY);
	// Walking is a controlled fall: the swing foot leaves the polygon every step, so moving bodies get a margin allowance.
	const float MoveSpeed = CharacterOwner->GetVelocity().Size2D();
	SupportMargin += FMath::Clamp(MoveSpeed / ComBalanceMovingSpeedRef, 0.f, 1.f) * ComBalanceMovingAllowanceCm;
	// Any ordinary locomotion cannot topple a balanced body, the recovery step is for pushes and stumbles only.
	if (MoveSpeed > 20.f && StaggerSeconds <= 0.f)
	{
		SupportMargin = FMath::Max(SupportMargin, 0.f);
	}
	bSupportGrounded = true;

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : Hull)
	{
		Centroid += Vertex;
	}
	Centroid /= static_cast<double>(FMath::Max(Hull.Num(), 1));

	const FVector2D Offset = (ComXY - Centroid).GetSafeNormal();
	ComOffsetDirection = FVector(Offset.X, Offset.Y, 0.0);
}

void UAedirBalanceComponent::AddFootSupportPoints(FName FootBone, FName BallBone, TArray<FVector2D>& OutPoints) const
{
	if (BodyMesh->GetBoneIndex(FootBone) == INDEX_NONE || BodyMesh->GetBoneIndex(BallBone) == INDEX_NONE)
	{
		return;
	}

	const FVector Foot = BodyMesh->GetBoneLocation(FootBone, EBoneSpaces::WorldSpace);
	const FVector Ball = BodyMesh->GetBoneLocation(BallBone, EBoneSpaces::WorldSpace);

	FVector GroundPoint;
	if (!TraceGround(Foot + FVector::UpVector * ComBalanceTraceUpCm, Foot - FVector::UpVector * ComBalanceTraceDownCm, GroundPoint))
	{
		return;
	}

	if (FMath::Abs(Foot.Z - GroundPoint.Z) > ComBalanceGroundToleranceCm)
	{
		return;
	}

	// Forward runs from the heel to the ball, flattened onto the ground plane.
	FVector Forward = FVector(Ball.X - Foot.X, Ball.Y - Foot.Y, 0.0).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = GetOwner()->GetActorForwardVector().GetSafeNormal2D();
	}

	const FVector2D Forward2D(Forward.X, Forward.Y);
	const FVector2D Side2D(-Forward.Y, Forward.X);
	const FVector2D FootXY(Foot.X, Foot.Y);
	const FVector2D BallXY(Ball.X, Ball.Y);
	const FVector2D Heel = FootXY - Forward2D * ComBalanceHeelOffsetCm;
	const FVector2D Toe = BallXY + Forward2D * ComBalanceToeOffsetCm;
	const FVector2D HalfWidth = Side2D * ComBalanceFootHalfWidthCm;

	OutPoints.Add(Heel + HalfWidth);
	OutPoints.Add(Heel - HalfWidth);
	OutPoints.Add(Toe + HalfWidth);
	OutPoints.Add(Toe - HalfWidth);
}

void UAedirBalanceComponent::ApplyCenterOfMassLoss(float DeltaTime)
{
	if (!bSupportGrounded || bIsRagdoll)
	{
		return;
	}

	const float Loss = ComputeComBalanceLoss(SupportMargin, DeltaTime);
	if (Loss <= 0.f)
	{
		return;
	}

	// The body falls towards the side the centre of mass has left the support polygon on.
	AddLeanDirection(ComOffsetDirection, Loss);
	SetBalance(Balance - Loss);
}

void UAedirBalanceComponent::SampleProtectiveReflex()
{
	TargetProtectiveAlpha = 0.f;

	if (!CharacterOwner || !BodyMesh)
	{
		return;
	}

	const UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	const FVector Velocity = GetFallVelocity();
	const bool bFastFall = -Velocity.Z > ComBalanceReflexFallSpeed;
	const bool bFallingNow = Movement && Movement->MovementMode == MOVE_Falling && bFastFall;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bRagdollImpact = bIsRagdoll && bFastFall && (Now - RagdollStartTime) < ComBalanceReflexRagdollSeconds;
	if (!bFallingNow && !bRagdollImpact)
	{
		return;
	}

	const FVector Pelvis = BodyMesh->GetBoneLocation(PelvisBoneName, EBoneSpaces::WorldSpace);
	FVector GroundPoint;
	if (!TraceGround(Pelvis, Pelvis - FVector::UpVector * ComBalanceReflexRangeCm, GroundPoint))
	{
		return;
	}

	FVector Heading = Velocity.GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}

	const float Distance = static_cast<float>(Pelvis.Z - GroundPoint.Z);
	TargetProtectiveAlpha = FMath::Clamp(1.f - Distance / ComBalanceReflexRangeCm, 0.f, 1.f);
	ProtectiveGroundWorld = GroundPoint + Heading * ComBalanceReflexForwardCm;
}

void UAedirBalanceComponent::SmoothProtectiveReflex(float DeltaTime)
{
	ProtectiveAlpha = FMath::FInterpTo(ProtectiveAlpha, TargetProtectiveAlpha, DeltaTime, ComBalanceReflexInterpSpeed);
	if (TargetProtectiveAlpha > KINDA_SMALL_NUMBER)
	{
		LastProtectiveReflexAlpha = ProtectiveAlpha;
	}
}

FVector UAedirBalanceComponent::GetFallVelocity()
{
	if (bIsRagdoll && BodyMesh)
	{
		if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
		{
			return Pelvis->GetUnrealWorldVelocity();
		}
	}

	const UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	return Movement ? Movement->Velocity : FVector::ZeroVector;
}

bool UAedirBalanceComponent::TraceGround(const FVector& Start, const FVector& End, FVector& OutHitPoint) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FHitResult Hit;
	const FCollisionQueryParams Params(FName(TEXT("AedirBalanceGround")), false, GetOwner());
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return false;
	}

	OutHitPoint = Hit.ImpactPoint;
	return true;
}

bool UAedirBalanceComponent::IsAuthoritative() const
{
	return GetOwner() != nullptr && GetOwner()->HasAuthority();
}

UAedirStatsComponent* UAedirBalanceComponent::FindStats() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UAedirStatsComponent>() : nullptr;
}

void UAedirBalanceComponent::SetBalance(float NewBalance)
{
	const bool bWasBalanced = Balance >= FallBalanceThreshold;
	Balance = FMath::Clamp(NewBalance, 0.f, 1.f);

	if (bWasBalanced && Balance < FallBalanceThreshold)
	{
		OnBalanceLost.Broadcast();
	}
}

void UAedirBalanceComponent::DecayLeanDirection(float Amount)
{
	const double Length = LeanDirection.Size();
	LeanDirection = Length <= Amount ? FVector2D::ZeroVector : LeanDirection * ((Length - Amount) / Length);
}

void UAedirBalanceComponent::UpdateFallState()
{
	const UCharacterMovementComponent* Movement = CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const bool bFalling = Movement->IsFalling();
	if (bFalling)
	{
		LastFallSpeed = static_cast<float>(FMath::Abs(Movement->Velocity.Z));
	}
	else if (bWasFalling && Movement->IsMovingOnGround())
	{
		HandleLanded(LastFallSpeed);
	}

	bWasFalling = bFalling;
}

void UAedirBalanceComponent::UpdateServerState(float DeltaTime)
{
	UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : 0.f;
	const bool bDead = StatsComponent && StatsComponent->IsDead();

	const AAedirCreature* Creature = Cast<AAedirCreature>(GetOwner());
	const float MaxLegInjury = Creature && Creature->Combat
		? FMath::Max(Creature->Combat->GetLimbInjury(EAedirLimb::LeftLeg), Creature->Combat->GetLimbInjury(EAedirLimb::RightLeg))
		: 0.f;

	const float DazeRecoveryScale = IsDazed() ? HitReactionDazeRecoveryScale : 1.f;
	const float StaggerRecoveryScale = IsStaggering() ? HitReactionStaggerRecoveryScale : 1.f;
	const float Recovery = RecoveryPerSecond * (1.f + Athletism / AedirAthletismRecoveryScale) * (1.f - AedirLegInjuryRecoveryPenalty * MaxLegInjury) * DazeRecoveryScale * StaggerRecoveryScale * DeltaTime;
	SetBalance(Balance + Recovery);
	DecayLeanDirection(Recovery);
	ApplyCenterOfMassLoss(DeltaTime);

	StaggerSeconds = FMath::Max(0.f, StaggerSeconds - DeltaTime);
	DazeSeconds = FMath::Max(0.f, DazeSeconds - DeltaTime);

	UpdateFallState();

	if (Balance < FallBalanceThreshold)
	{
		TimeBelowThreshold += DeltaTime;
	}
	else
	{
		TimeBelowThreshold = 0.f;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (!bIsRagdoll && (bDead || TimeBelowThreshold >= FallDelaySeconds))
	{
		EnterRagdoll(MinRagdollSeconds);
	}
	else if (bIsRagdoll && !bDead && Balance >= GetUpBalanceThreshold && Now - RagdollStartTime >= CurrentRagdollMinSeconds)
	{
		bIsRagdoll = false;
		SetBalance(GetUpBalanceThreshold);
		SetRagdollPresentation(false);
	}
}

void UAedirBalanceComponent::EnterRagdoll(float MinimumSeconds)
{
	const UWorld* World = GetWorld();
	bIsRagdoll = true;
	RagdollStartTime = World ? World->GetTimeSeconds() : 0.0;
	CurrentRagdollMinSeconds = MinimumSeconds;
	SetRagdollPresentation(true);
}

void UAedirBalanceComponent::MulticastBoneImpulse_Implementation(FName Bone, FVector Impulse, FVector Location)
{
	if (BodyMesh && BodyMesh->IsAnySimulatingPhysics())
	{
		BodyMesh->AddImpulseAtLocation(Impulse, Location, Bone);
	}
}

void UAedirBalanceComponent::UpdatePelvisLean(float DeltaTime)
{
	const float Disturbance = FMath::Clamp(1.f - Balance, 0.f, 1.f);

	// Positive pitch raises the nose, moving the top of the pelvis toward -X; positive roll moves it toward +Y.
	// The lean therefore points opposite to the disturbance on both axes.
	const double Pitch = LeanDirection.X * Disturbance * MaxLeanDegrees;
	const double Roll = -LeanDirection.Y * Disturbance * MaxLeanDegrees;

	CurrentLean = FMath::RInterpTo(CurrentLean, FRotator(Pitch, 0.0, Roll), DeltaTime, AedirLeanInterpSpeed);
}

void UAedirBalanceComponent::DrivePelvis()
{
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);
	if (!Pelvis)
	{
		return;
	}

	const float MeshScale = static_cast<float>(BodyMesh->GetComponentScale().Z);
	const FVector Location = BodyCapsule->GetComponentLocation()
		+ FVector(0.f, 0.f, -BodyCapsule->GetScaledCapsuleHalfHeight() + PelvisRestHeight * MeshScale);
	const FQuat Rotation = CharacterOwner->GetActorQuat() * CurrentLean.Quaternion();

	Pelvis->SetBodyTransform(FTransform(Rotation, Location), ETeleportType::None);
}

void UAedirBalanceComponent::SetRagdollPresentation(bool bRagdoll)
{
	ApplyRagdollPhysics(bRagdoll);

	if (bRagdoll)
	{
		OnFell.Broadcast();
	}
	else
	{
		OnGotUp.Broadcast();
	}
}

void UAedirBalanceComponent::ApplyRagdollPhysics(bool bRagdoll)
{
	if (!CharacterOwner || !BodyMesh || !BodyCapsule)
	{
		return;
	}

	UpdateMotors(0.f, true);

	UCharacterMovementComponent* Movement = CharacterOwner->GetCharacterMovement();
	FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName);

	if (bRagdoll)
	{
		if (Pelvis)
		{
			Pelvis->SetInstanceSimulatePhysics(true);
		}

		if (Movement)
		{
			Movement->DisableMovement();
		}

		BodyCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	if (Pelvis)
	{
		const FVector PelvisLocation = Pelvis->GetUnrealWorldTransform().GetLocation();
		Pelvis->SetInstanceSimulatePhysics(false);

		if (IsAuthoritative())
		{
			const float MeshScale = static_cast<float>(BodyMesh->GetComponentScale().Z);
			const FVector CapsuleLocation = PelvisLocation
				+ FVector(0.f, 0.f, BodyCapsule->GetScaledCapsuleHalfHeight() - PelvisRestHeight * MeshScale);
			CharacterOwner->SetActorLocation(CapsuleLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (Movement)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	BodyCapsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void UAedirBalanceComponent::ProcessHit(AActor* OtherActor, UPrimitiveComponent* OtherComponent, const FHitResult& Hit)
{
	AActor* Owner = GetOwner();
	// Creatures hurt each other through combat; walking into or over a body, a ragdoll included, is no impact.
	if (!IsAuthoritative() || OtherActor == Owner || Cast<AAedirCreature>(OtherActor))
	{
		return;
	}

	UAedirStatsComponent* StatsComponent = FindStats();
	const float Athletism = StatsComponent ? StatsComponent->Athletism : 0.f;
	const FVector OwnerVelocity = Owner->GetVelocity();

	if (OtherComponent && OtherComponent->IsSimulatingPhysics())
	{
		const FVector RelativeVelocity = OtherComponent->GetComponentVelocity() - OwnerVelocity;
		const float ImpactScore = OtherComponent->GetMass() * static_cast<float>(RelativeVelocity.Size());

		if (ImpactScore > ObjectImpactMassSpeedThreshold)
		{
			const FVector Direction = (Owner->GetActorLocation() - OtherComponent->GetComponentLocation()).GetSafeNormal();
			ApplyBalanceImpact(Direction, FMath::Clamp(ImpactScore * ImpactBalanceScale * AedirObjectImpactBalanceMultiplier, 0.f, 1.f));

			if (StatsComponent)
			{
				StatsComponent->ApplyDamage(ImpactScore * AedirObjectImpactDamagePerScore);
			}
		}
		return;
	}

	// Upward-facing surfaces are ground contact, which is handled by the landing logic instead.
	if (Hit.ImpactNormal.Z > AedirWalkableNormalZ)
	{
		return;
	}

	const float ApproachSpeed = static_cast<float>(FVector::DotProduct(OwnerVelocity, -Hit.ImpactNormal));
	if (ApproachSpeed <= WallImpactSpeedThreshold)
	{
		return;
	}

	const float Excess = ApproachSpeed - WallImpactSpeedThreshold;
	ApplyBalanceImpact(OwnerVelocity.GetSafeNormal(), FMath::Clamp(Excess / WallImpactSpeedThreshold, 0.f, AedirWallImpactMaxLoss));

	if (StatsComponent)
	{
		StatsComponent->ApplyDamage(Excess * AedirWallImpactDamagePerSpeed * (1.f - Athletism / AedirAthletismDamageScale));
	}
}

void UAedirBalanceComponent::OnRep_Ragdoll()
{
	SetRagdollPresentation(bIsRagdoll);
}

void UAedirBalanceComponent::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessHit(OtherActor, OtherComponent, Hit);
}

void UAedirBalanceComponent::OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	ProcessHit(OtherActor, OtherComponent, Hit);
}

void UAedirBalanceComponent::SetupMotors()
{
	if (!BodyMesh || !CharacterOwner)
	{
		return;
	}

	for (TArray<FName>& Group : MotorGroupBones)
	{
		Group.Reset();
	}

	// Group 0 = spine and head, 1 = left arm, 2 = right arm, 3 = left leg, 4 = right leg.
	for (const FBodyInstance* Body : BodyMesh->Bodies)
	{
		if (!Body || !Body->BodySetup.IsValid())
		{
			continue;
		}

		const FName BoneName = Body->BodySetup->BoneName;
		if (BoneName == PelvisBoneName || BoneName == NAME_None)
		{
			continue;
		}

		const EAedirBodyZone Zone = UAedirCombatComponent::ClassifyBodyZone(BoneName);
		EAedirLimb Limb = EAedirLimb::LeftArm;
		if (UAedirCombatComponent::GetBoneLimb(BoneName, Limb))
		{
			switch (Limb)
			{
			case EAedirLimb::LeftArm:
				MotorGroupBones[1].Add(BoneName);
				break;
			case EAedirLimb::RightArm:
				MotorGroupBones[2].Add(BoneName);
				break;
			case EAedirLimb::LeftLeg:
				MotorGroupBones[3].Add(BoneName);
				break;
			case EAedirLimb::RightLeg:
				MotorGroupBones[4].Add(BoneName);
				break;
			}
		}
		else if (Zone == EAedirBodyZone::Head || Zone == EAedirBodyZone::Torso)
		{
			MotorGroupBones[0].Add(BoneName);
		}
	}

	MotorComponent = NewObject<UPhysicalAnimationComponent>(CharacterOwner, TEXT("AedirPhysicalMotors"));
	if (!MotorComponent)
	{
		return;
	}

	MotorComponent->RegisterComponent();
	MotorComponent->SetSkeletalMeshComponent(BodyMesh);
	UpdateMotors(0.f, true);
}

void UAedirBalanceComponent::UpdateMotors(float DeltaTime, bool bForce)
{
	if (!MotorComponent)
	{
		return;
	}

	MotorUpdateTimer -= DeltaTime;
	if (!bForce && MotorUpdateTimer > 0.f)
	{
		return;
	}
	MotorUpdateTimer = 0.2f;

	const UAedirStatsComponent* Stats = FindStats();
	const float StaminaFraction = Stats ? FMath::Clamp(Stats->GetStaminaFraction(), 0.f, 1.f) : 1.f;
	const UAedirCombatComponent* Combat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;

	float Common = FMath::Lerp(0.35f, 1.f, StaminaFraction) * (1.f - 0.6f * GetDazeFactor());
	if (StaggerSeconds > 0.f)
	{
		Common *= 0.7f;
	}
	if (bIsRagdoll || (Stats && Stats->IsDead()))
	{
		Common = 0.04f;
	}

	float Strength[5];
	Strength[0] = Common;
	Strength[1] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::LeftArm) : 0.f));
	Strength[2] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::RightArm) : 0.f));
	Strength[3] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::LeftLeg) : 0.f));
	Strength[4] = Common * (1.f - 0.75f * (Combat ? Combat->GetLimbInjury(EAedirLimb::RightLeg) : 0.f));

	for (int32 GroupIndex = 0; GroupIndex < 5; ++GroupIndex)
	{
		if (bForce || FMath::Abs(Strength[GroupIndex] - AppliedMotorStrength[GroupIndex]) > 0.04f)
		{
			ApplyMotorGroup(GroupIndex, Strength[GroupIndex]);
		}
	}
}

void UAedirBalanceComponent::UpdateLegBlend(float DeltaTime)
{
	if (!BodyMesh)
	{
		return;
	}

	// Balanced legs follow the IK pose exactly, so simulated feet cannot fight the ground and jitter. Staggering, falling or hurt legs go physical.
	const UAedirCombatComponent* Combat = CharacterOwner ? CharacterOwner->FindComponentByClass<UAedirCombatComponent>() : nullptr;
	const bool bLegHurt = Combat && (Combat->GetLimbInjury(EAedirLimb::LeftLeg) > 0.3f || Combat->GetLimbInjury(EAedirLimb::RightLeg) > 0.3f);
	const bool bPhysical = bIsRagdoll || StaggerSeconds > 0.f || DazeSeconds > 0.f || Balance < 0.6f || bLegHurt;
	LegPhysicsBlend = FMath::FInterpTo(LegPhysicsBlend, bPhysical ? 1.f : 0.f, DeltaTime, 8.f);

	// The spine and head follow the pose too; a simulated torso lags behind a quickly turning pelvis and swings the wrong way.
	for (const int32 GroupIndex : { 0, 3, 4 })
	{
		for (const FName& BoneName : MotorGroupBones[GroupIndex])
		{
			if (FBodyInstance* Body = BodyMesh->GetBodyInstance(BoneName))
			{
				Body->PhysicsBlendWeight = LegPhysicsBlend;
			}
		}
	}
}

void UAedirBalanceComponent::ApplyMotorGroup(int32 GroupIndex, float Strength)
{
	if (!MotorComponent || GroupIndex < 0 || GroupIndex >= 5)
	{
		return;
	}

	// Base spring strengths per group: legs carry the body weight and need the stiffest motors.
	static const float MotorBaseOrientation[5] = { 3000.f, 1800.f, 1800.f, 6000.f, 6000.f };
	static const float MotorBaseAngularVelocity[5] = { 300.f, 180.f, 180.f, 600.f, 600.f };

	AppliedMotorStrength[GroupIndex] = Strength;

	FPhysicalAnimationData Data;
	Data.bIsLocalSimulation = true;
	Data.OrientationStrength = MotorBaseOrientation[GroupIndex] * Strength;
	Data.AngularVelocityStrength = MotorBaseAngularVelocity[GroupIndex] * Strength;
	Data.PositionStrength = 0.f;
	Data.VelocityStrength = 0.f;
	Data.MaxLinearForce = 0.f;
	Data.MaxAngularForce = 0.f;

	for (const FName& BoneName : MotorGroupBones[GroupIndex])
	{
		MotorComponent->ApplyPhysicalAnimationSettings(BoneName, Data);
	}
}

