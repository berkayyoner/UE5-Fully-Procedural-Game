#include "Combat/AedirCombatComponent.h"

#include "Aedir.h"
#include "AnimationRuntime.h"
#include "Audio/AedirSfxSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirGestureComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AedirItemActor.h"
#include "Npc/AedirTrainingDummy.h"
#include "Physics/AedirBalanceComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	const FName AedirHeadbuttHeadBone(TEXT("head"));
	const FName AedirHeadbuttNeckBone(TEXT("neck_01"));
}

float UAedirCombatComponent::EvaluateHeadbutt(float Elapsed, float WindSeconds, float ThrowSeconds, float RecoverSeconds)
{
	// Drawn back over the wind-up (to -1), thrown forward (to 1), then back to standing.
	const float Wind = FMath::Max(WindSeconds, 0.01f);
	const float Throw = FMath::Max(ThrowSeconds, 0.01f);
	const float Recover = FMath::Max(RecoverSeconds, 0.01f);
	if (Elapsed <= 0.f)
	{
		return 0.f;
	}
	if (Elapsed < Wind)
	{
		return -FMath::SmoothStep(0.f, 1.f, Elapsed / Wind);
	}
	if (Elapsed < Wind + Throw)
	{
		// Fast from the start: the head is thrown, not eased.
		const float T = (Elapsed - Wind) / Throw;
		return FMath::Lerp(-1.f, 1.f, 1.f - FMath::Square(1.f - T));
	}
	if (Elapsed < Wind + Throw + Recover)
	{
		return 1.f - FMath::SmoothStep(0.f, 1.f, (Elapsed - Wind - Throw) / Recover);
	}
	return 0.f;
}

bool UAedirCombatComponent::CanStartHeadbutt() const
{
	if (!Creature || !Creature->Stats || HeadbuttElapsed >= 0.f || HeadbuttCooldownRemaining > 0.f || KickNet.bActive)
	{
		return false;
	}
	if (Creature->Stats->GetStamina() < HeadbuttStaminaCost || Creature->Stats->IsKnockedOut() || Creature->Stats->IsDead())
	{
		return false;
	}
	if (Creature->Gesture && Creature->Gesture->AreHandsUp())
	{
		return false;
	}
	return !(Creature->Balance && (Creature->Balance->IsRagdoll() || Creature->Balance->IsGettingUp() || Creature->Balance->GetBalance() < HeadbuttLeastBalance));
}

bool UAedirCombatComponent::StartHeadbutt()
{
	if (!Creature || !CanStartHeadbutt())
	{
		return false;
	}
	if (IsAuthoritative())
	{
		if (!Creature->Stats->ConsumeStamina(HeadbuttStaminaCost))
		{
			return false;
		}
		BeginHeadbutt();
		return true;
	}
	if (!Creature->IsLocallyControlled())
	{
		return false;
	}
	BeginHeadbutt();
	ServerStartHeadbutt();
	return true;
}

void UAedirCombatComponent::ServerStartHeadbutt_Implementation()
{
	StartHeadbutt();
}

void UAedirCombatComponent::BeginHeadbutt()
{
	HeadbuttElapsed = 0.f;
	bHeadbuttStruck = false;
	bHeadbuttThrown = false;
	HeadbuttCooldownRemaining = HeadbuttCooldown;
	HeadbuttPrevForehead = GetForeheadWorld();
	++HeadbuttCount;
	UE_LOG(LogAedir, Log, TEXT("[Headbutt] %s draws its head back"), *Creature->GetName());
}

void UAedirCombatComponent::OnRep_HeadbuttCount()
{
	// Seen from elsewhere: the head goes the same way, the server works out what it strikes.
	if (Creature && !Creature->IsLocallyControlled())
	{
		HeadbuttElapsed = 0.f;
		bHeadbuttThrown = false;
		bHeadbuttStruck = true;
	}
}

FVector UAedirCombatComponent::GetForeheadWorld(float ExtraUpCm) const
{
	const USkeletalMeshComponent* Mesh = Creature ? Creature->GetMesh() : nullptr;
	if (!Mesh || Mesh->GetBoneIndex(AedirHeadbuttHeadBone) == INDEX_NONE)
	{
		return Creature ? Creature->GetActorLocation() : FVector::ZeroVector;
	}
	// The face looks along the mesh's +Y in its rest pose: carried onto the head as it is now.
	const USkeletalMesh* Model = Mesh->GetSkeletalMeshAsset();
	const int32 Head = Model ? Model->GetRefSkeleton().FindBoneIndex(AedirHeadbuttHeadBone) : INDEX_NONE;
	const FQuat Rest = Head != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Model->GetRefSkeleton(), Head).GetRotation() : FQuat::Identity;
	const FQuat Turned = Mesh->GetBoneQuaternion(AedirHeadbuttHeadBone) * Rest.Inverse();
	const FVector FaceWorld = Turned.RotateVector(FVector(0.f, 1.f, 0.f));
	const FVector UpWorld = Turned.RotateVector(FVector(0.f, 0.f, 1.f));
	const float Scale = static_cast<float>(Mesh->GetComponentScale().Z);
	return Mesh->GetBoneLocation(AedirHeadbuttHeadBone) + (FaceWorld.GetSafeNormal() * HeadbuttForeheadAheadCm + UpWorld.GetSafeNormal() * (HeadbuttForeheadUpCm + ExtraUpCm)) * Scale;
}

float UAedirCombatComponent::GetHeadbuttShare() const
{
	return HeadbuttElapsed >= 0.f ? EvaluateHeadbutt(HeadbuttElapsed, HeadbuttWindSeconds, HeadbuttThrowSeconds, HeadbuttRecoverSeconds) : 0.f;
}

void UAedirCombatComponent::UpdateHeadbutt(float DeltaTime)
{
	HeadbuttCooldownRemaining = FMath::Max(HeadbuttCooldownRemaining - DeltaTime, 0.f);
	if (HeadbuttElapsed < 0.f || !Creature)
	{
		return;
	}
	// Felled, the head goes down with the body.
	if (Creature->Balance && Creature->Balance->IsRagdoll())
	{
		HeadbuttElapsed = -1.f;
		return;
	}
	const float Before = HeadbuttElapsed;
	HeadbuttElapsed += DeltaTime;
	const float ThrowAt = HeadbuttWindSeconds;
	const FVector Forehead = GetForeheadWorld();
	// Thrown: the body steps into it and the head is driven forward by the neck and the back.
	if (!bHeadbuttThrown && HeadbuttElapsed >= ThrowAt)
	{
		bHeadbuttThrown = true;
		if (UCharacterMovementComponent* Movement = Creature->GetCharacterMovement(); Movement && Movement->IsMovingOnGround() && (IsAuthoritative() || Creature->IsLocallyControlled()))
		{
			Movement->Velocity += Creature->GetActorForwardVector() * HeadbuttLungeSpeed;
		}
		if (USkeletalMeshComponent* Mesh = Creature->GetMesh())
		{
			for (const TPair<FName, float>& Part : { TPair<FName, float>(AedirHeadbuttHeadBone, 1.f), TPair<FName, float>(AedirHeadbuttNeckBone, 0.6f), TPair<FName, float>(FName(TEXT("spine_05")), 0.3f) })
			{
				if (FBodyInstance* Body = Mesh->GetBodyInstance(Part.Key); Body && Body->IsInstanceSimulatingPhysics())
				{
					Body->SetLinearVelocity(Creature->GetActorForwardVector() * (HeadbuttHeadSpeed * Part.Value), true);
				}
			}
		}
	}
	// What the forehead meets from the throw until a little into the way back.
	const bool bStriking = Before >= ThrowAt * 0.8f && HeadbuttElapsed <= ThrowAt + HeadbuttThrowSeconds + HeadbuttStrikeAfterSeconds;
	if (bStriking && !bHeadbuttStruck && IsAuthoritative() && DeltaTime > KINDA_SMALL_NUMBER)
	{
		ResolveHeadbuttSweep(HeadbuttPrevForehead, Forehead, DeltaTime);
	}
	HeadbuttPrevForehead = Forehead;
	if (HeadbuttElapsed >= HeadbuttWindSeconds + HeadbuttThrowSeconds + HeadbuttRecoverSeconds)
	{
		HeadbuttElapsed = -1.f;
	}
}

void UAedirCombatComponent::ResolveHeadbuttSweep(const FVector& From, const FVector& To, float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVector Motion = To - From;
	const FVector HeadVelocity = Motion / DeltaTime;
	const float Speed = FMath::Min(static_cast<float>(HeadVelocity.Size()), HeadbuttMaxSpeed);
	const FVector Along = (Motion.IsNearlyZero() ? Creature->GetActorForwardVector() : Motion).GetSafeNormal();
	FCollisionObjectQueryParams Types;
	Types.AddObjectTypesToQuery(ECC_Pawn);
	Types.AddObjectTypesToQuery(ECC_PhysicsBody);
	Types.AddObjectTypesToQuery(ECC_WorldStatic);
	Types.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirHeadbutt), false, Creature);
	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (AAedirItemActor* Held = GetHeldItem(Hand))
		{
			Params.AddIgnoredActor(Held);
		}
	}
	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, From, To + Along * HeadbuttReachCm, FQuat::Identity, Types, FCollisionShape::MakeSphere(HeadbuttRadiusCm), Params);
	for (const FHitResult& Hit : Hits)
	{
		AActor* Thing = Hit.GetActor();
		UPrimitiveComponent* Part = Hit.GetComponent();
		if (!Thing || !Part || Thing == Creature || Cast<AAedirCreature>(Thing->GetAttachParentActor()) == Creature)
		{
			continue;
		}
		if (AAedirCreature* Victim = Cast<AAedirCreature>(Thing))
		{
			// Only the body itself: the capsule it walks in reaches well past its flesh.
			if (Part != Victim->GetMesh())
			{
				continue;
			}
			bHeadbuttStruck = true;
			const float Came = FMath::Min(FMath::Max(static_cast<float>(FVector::DotProduct(HeadVelocity - Victim->GetVelocity(), Along)), Speed * 0.5f), HeadbuttMaxSpeed);
			const float Senses = Victim->Stats ? Victim->Stats->Consciousness : 0.f;
			ApplyBodyPartBlow(Victim, Hit, Motion, Came, Speed, HeadbuttMassKg, HeadbuttPushShare, EAedirSfx::PunchBody, HeadbuttSoundTake, TEXT("Headbutt"));
			// Head on head, the striker's own is shaken too, less.
			if (ClassifyBodyZone(Hit.BoneName) == EAedirBodyZone::Head && Victim->Stats && Creature->Stats)
			{
				Creature->Stats->LoseConsciousness(FMath::Max(Senses - Victim->Stats->Consciousness, 0.f) * HeadbuttOwnSensesShare);
			}
			return;
		}
		if (AAedirTrainingDummy* Dummy = Cast<AAedirTrainingDummy>(Thing))
		{
			bHeadbuttStruck = true;
			Dummy->ReceiveStrike(Part, Hit.ImpactPoint, HeadVelocity, Along, ComputeImpactEnergy(HeadbuttMassKg, Speed), false);
			UAedirSfxSubsystem::Play(this, EAedirSfx::PunchBody, Hit.ImpactPoint, 1.f, HeadbuttSoundTake);
			return;
		}
		if (Part->IsSimulatingPhysics())
		{
			// A loose thing is knocked along.
			if (Speed < HeadbuttFixedMinSpeed * 0.5f)
			{
				continue;
			}
			bHeadbuttStruck = true;
			const float Push = FMath::Min(HeadbuttMassKg * GetStrengthBlowScale() * Speed * 0.6f, Part->GetMass() * 220.f);
			Part->AddImpulseAtLocation(Along * Push, Hit.ImpactPoint);
			UAedirSfxSubsystem::Play(this, EAedirSfx::PunchBody, Hit.ImpactPoint, FMath::Clamp(Speed / 600.f, 0.4f, 1.f), HeadbuttSoundTake);
			UE_LOG(LogAedir, Log, TEXT("[Headbutt] %s knocks %s along at %.0f cm/s"), *Creature->GetName(), *Thing->GetName(), Speed);
			return;
		}
		// Something fixed: the head is knocked, hurt and shaken by it.
		const FVector Into = Hit.bStartPenetrating || Hit.ImpactNormal.IsNearlyZero() ? Along : -FVector(Hit.ImpactNormal);
		const float Came = Speed * FMath::Max(static_cast<float>(FVector::DotProduct(Along, Into)), 0.f);
		if (Part->GetCollisionResponseToChannel(ECC_PhysicsBody) != ECR_Block || Cast<AAedirItemActor>(Thing) || Came < HeadbuttFixedMinSpeed || FMath::Abs(Into.Z) > 0.8f)
		{
			continue;
		}
		bHeadbuttStruck = true;
		FHitResult Knock = Hit;
		Knock.ImpactNormal = -Into;
		Knock.ImpactPoint = Hit.bStartPenetrating ? To : FVector(Hit.ImpactPoint);
		if (Creature->Balance)
		{
			// Marked on the forehead's skin, which met the thing, not where the sweep ahead of it touched.
			Creature->Balance->KnockHead(Came, Knock, HeadbuttSoundTake, GetForeheadWorld(HeadbuttMarkUpCm));
			Creature->Balance->ApplyBalanceImpact(-Into, HeadbuttFixedRecoilBalance);
		}
		if (Creature->Stats)
		{
			Creature->Stats->LoseConsciousness(FMath::Max(Came - HeadbuttFixedMinSpeed, 0.f) * HeadbuttFixedSensesPerSpeed);
		}
		UE_LOG(LogAedir, Log, TEXT("[Headbutt] %s's head strikes %s at %.0f cm/s"), *Creature->GetName(), *Thing->GetName(), Came);
		return;
	}
}
