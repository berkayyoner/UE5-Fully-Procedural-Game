#include "AedirBalanceComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float AedirLeanInterpSpeed = 8.f;
	constexpr float AedirSafeLandingSpeed = 900.f;
	constexpr float AedirAthletismRecoveryScale = 100.f;
	constexpr float AedirAthletismDamageScale = 200.f;
	constexpr float AedirWalkableNormalZ = 0.5f;
	constexpr float AedirWallImpactMaxLoss = 0.8f;
	constexpr float AedirObjectImpactBalanceMultiplier = 10.f;
	constexpr float AedirObjectImpactDamagePerScore = 0.002f;
	constexpr float AedirWallImpactDamagePerSpeed = 0.01f;
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

	if (FBodyInstance* Pelvis = BodyMesh->GetBodyInstance(PelvisBoneName))
	{
		Pelvis->SetInstanceSimulatePhysics(false);
	}
}

void UAedirBalanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CharacterOwner || !BodyMesh || !BodyCapsule)
	{
		return;
	}

	if (IsAuthoritative())
	{
		UpdateServerState(DeltaTime);
	}

	UpdatePelvisLean(DeltaTime);

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
}

void UAedirBalanceComponent::ApplyBalanceImpact(FVector WorldDirection, float Magnitude)
{
	if (!IsAuthoritative() || Magnitude <= 0.f)
	{
		return;
	}

	const FVector LocalDirection = GetOwner()->GetActorTransform().InverseTransformVectorNoScale(WorldDirection);
	const FVector2D PlanarDirection = FVector2D(LocalDirection.X, LocalDirection.Y).GetSafeNormal();
	LeanDirection = (LeanDirection + PlanarDirection * Magnitude).GetClampedToMaxSize(1.f);

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

	if (LandingSpeed > FallSpeedBalanceThreshold)
	{
		const float Loss = FMath::Clamp((LandingSpeed - FallSpeedBalanceThreshold) / FallSpeedBalanceThreshold, 0.f, 1.f);
		ApplyBalanceImpact(GetOwner()->GetActorForwardVector(), Loss);
	}

	if (LandingSpeed > AedirSafeLandingSpeed)
	{
		if (UAedirStatsComponent* StatsComponent = FindStats())
		{
			const float Athletism = StatsComponent->Athletism;
			StatsComponent->ApplyDamage((LandingSpeed - AedirSafeLandingSpeed) * FallDamagePerSpeed * (1.f - Athletism / AedirAthletismDamageScale));
		}
	}
}

float UAedirBalanceComponent::GetBalance() const
{
	return Balance;
}

bool UAedirBalanceComponent::IsRagdoll() const
{
	return bIsRagdoll;
}

FRotator UAedirBalanceComponent::GetPelvisLean() const
{
	return CurrentLean;
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

	const float Recovery = RecoveryPerSecond * (1.f + Athletism / AedirAthletismRecoveryScale) * DeltaTime;
	SetBalance(Balance + Recovery);
	DecayLeanDirection(Recovery);

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
		bIsRagdoll = true;
		RagdollStartTime = Now;
		SetRagdollPresentation(true);
	}
	else if (bIsRagdoll && !bDead && Balance >= GetUpBalanceThreshold && Now - RagdollStartTime >= MinRagdollSeconds)
	{
		bIsRagdoll = false;
		SetBalance(GetUpBalanceThreshold);
		SetRagdollPresentation(false);
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
	if (!IsAuthoritative() || OtherActor == Owner)
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
