#include "AedirItemActor.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Items/AedirInteractionComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAedirWeldHeld(TEXT("aedir.WeldHeld"), 1, TEXT("Make a held item part of the hand's physical body."));
	constexpr int32 AedirHighlightStencilValue = 250;
}

namespace
{
	constexpr float ItemSettleSeconds = 0.25f;
	constexpr float HangSwingStiffness = 45.f;
	constexpr float HangSwingDamping = 5.f;
	constexpr float HangSwingGain = 0.05f;
	constexpr float HangSwingMaxRadians = 1.1f;
	// What hangs on the belt runs into things: it swings away from what stands firm and shoves what is loose.
	constexpr float HangSweepRadiusCm = 3.f;
	constexpr float HangBlockRadiansPerSecond = 5.f;
	constexpr float HangContactHoldSeconds = 0.3f;
	constexpr float HangBlockMassKg = 40.f;
	constexpr float HangEndOnShare = 0.2f;
	constexpr float HangShoveShare = 0.3f;
	constexpr float HangShoveMinSpeed = 30.f;
	constexpr float HangShoveMaxSpeed = 400.f;
	constexpr float HangMaxAcceleration = 1800.f;
	constexpr double ReleaseGraceSeconds = 1.5;
	// An item let go inside the capsule is pushed out of it gently, not shot away.
	constexpr float ItemMaxDepenetrationSpeed = 80.f;
	constexpr float ItemLinearDamping = 0.6f;
	constexpr float ItemAngularDamping = 2.f;
}

AAedirItemActor::AAedirItemActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	// Loose things are walked round or pushed, never stood on: the body could not follow the capsule up onto them.
	Mesh->CanCharacterStepUpOn = ECB_No;
	Mesh->SetSimulatePhysics(true);
	Mesh->SetUseCCD(true);
	Mesh->SetLinearDamping(ItemLinearDamping);
	Mesh->SetAngularDamping(ItemAngularDamping);
}

void AAedirItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyItemMass();
}

void AAedirItemActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyItemMass();
}

void AAedirItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirItemActor, Holder);
	DOREPLIFETIME(AAedirItemActor, HeldHand);
	DOREPLIFETIME(AAedirItemActor, HeldGripShiftCm);
	DOREPLIFETIME(AAedirItemActor, bHolstered);
	DOREPLIFETIME(AAedirItemActor, bWorn);
	DOREPLIFETIME(AAedirItemActor, bStowed);
}

void AAedirItemActor::Use_Implementation(AAedirCreature* User)
{
	if (!HasAuthority() || !User || !User->Stats)
	{
		return;
	}

	if (ItemClass == EAedirItemClass::Poison)
	{
		User->Stats->ApplyDamage(PoisonDamage);
	}

	OnUsed(User);
}

void AAedirItemActor::AttachToCreatureHand(AAedirCreature* Creature, EAedirHand Hand, float GripShiftCm)
{
	if (!HasAuthority() || !Creature || !Creature->Interaction)
	{
		return;
	}

	HeldGripShiftCm = GripShiftCm;
	const FName BoneName = Hand == EAedirHand::Left ? Creature->Interaction->LeftHandBone : Creature->Interaction->RightHandBone;
	AttachToBone(Creature, BoneName, bUseHandGrip ? ComputeHandGrip(Creature, Hand) : HeldTransform);

	Holder = Creature;
	HeldHand = Hand;
	bHolstered = false;
	ApplyHeldCollision(Creature);
}

void AAedirItemActor::SettleIntoCreatureHand(AAedirCreature* Creature, EAedirHand Hand)
{
	const FTransform Before = GetActorTransform();
	AttachToCreatureHand(Creature, Hand);
	if (Holder != Creature || !GetRootComponent())
	{
		return;
	}

	// Seen from the hand, where it was and where the grip wants it; it goes from one to the other, never through a jump.
	SettleTo = GetRootComponent()->GetRelativeTransform();
	// It slides into the grip first and becomes part of the hand once it is there.
	UnweldFromHand();
	SetActorTransform(Before);
	SettleFrom = GetRootComponent()->GetRelativeTransform();
	SettleAlpha = 0.f;
	SetActorTickEnabled(true);
}

void AAedirItemActor::AttachToHandInPlace(AAedirCreature* Creature, EAedirHand Hand)
{
	if (!HasAuthority() || !Creature || !Creature->Interaction)
	{
		return;
	}

	SetFreePhysics(false);
	const FName BoneName = Hand == EAedirHand::Left ? Creature->Interaction->LeftHandBone : Creature->Interaction->RightHandBone;
	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::KeepWorldTransform, BoneName);

	Holder = Creature;
	HeldHand = Hand;
	bHolstered = false;
}

void AAedirItemActor::AttachCarried(AAedirCreature* Creature)
{
	if (HasAuthority() && Creature)
	{
		AttachToComponent(Creature->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	}
}

bool AAedirItemActor::WasJustReleasedBy(const AActor* Creature) const
{
	const UWorld* World = GetWorld();
	return World && LastHolder.Get() == Creature && World->GetTimeSeconds() - LastReleaseTime < ReleaseGraceSeconds;
}

FTransform AAedirItemActor::ComputeHandGrip(const AAedirCreature* Creature, EAedirHand Hand) const
{
	const USkeletalMesh* BodyAsset = Creature->GetMesh() ? Creature->GetMesh()->GetSkeletalMeshAsset() : nullptr;
	if (!BodyAsset)
	{
		return HeldTransform;
	}

	// The axes of a closed hand come from the skeleton: a handle runs across the knuckles, its blade out on the thumb side.
	const TCHAR* Suffix = Hand == EAedirHand::Left ? TEXT("_l") : TEXT("_r");
	const auto RefLocation = [&](const TCHAR* Bone)
	{
		return BodyAsset->GetComposedRefPoseMatrix(FName(*FString::Printf(TEXT("%s%s"), Bone, Suffix))).GetOrigin();
	};

	const FMatrix HandMatrix = BodyAsset->GetComposedRefPoseMatrix(FName(*FString::Printf(TEXT("hand%s"), Suffix)));
	const FQuat HandRotation = HandMatrix.ToQuat();
	const FVector HandLocation = HandMatrix.GetOrigin();
	const FVector KnuckleAxis = (RefLocation(TEXT("index_01")) - RefLocation(TEXT("pinky_01"))).GetSafeNormal();
	const FVector FingerAxis = (RefLocation(TEXT("middle_01")) - HandLocation).GetSafeNormal();
	// The handle lies in the hollow of the palm: part way to the knuckles and off the back of the hand, on the side the thumb is on.
	FVector PalmNormal = FVector::CrossProduct(KnuckleAxis, FingerAxis).GetSafeNormal();
	if (FVector::DotProduct(PalmNormal, RefLocation(TEXT("thumb_01")) - HandLocation) < 0.0)
	{
		PalmNormal = -PalmNormal;
	}
	const FVector Palm = HandLocation + (RefLocation(TEXT("middle_01")) - HandLocation) * HandGripPalmShare + PalmNormal * HandGripPalmDepthCm;

	const FQuat ItemRotation = FRotationMatrix::MakeFromXY(KnuckleAxis, FingerAxis).ToQuat();
	const FQuat RelativeRotation = HandRotation.Inverse() * ItemRotation;
	const FVector RelativePalm = HandRotation.Inverse().RotateVector(Palm - HandLocation);
	return FTransform(RelativeRotation, RelativePalm - RelativeRotation.RotateVector(FVector(GripLocalCm + HeldGripShiftCm, 0.f, 0.f)));
}

void AAedirItemActor::AttachToWaist(AAedirCreature* Creature, FName BoneName, const FVector& SlotOnBone, const FQuat& RotationOnBone, float PivotCm)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	HeldGripShiftCm = 0.f;
	AttachToBone(Creature, BoneName, FTransform(RotationOnBone, SlotOnBone - RotationOnBone.RotateVector(FVector(PivotCm, 0.f, 0.f))));

	Holder = Creature;
	bHolstered = true;

	HangSlot = SlotOnBone;
	HangRotation = RotationOnBone;
	HangPivotCm = PivotCm;
	HangSwing = FVector::ZeroVector;
	HangSwingSpeed = FVector::ZeroVector;
	HangContactHold = 0.f;
	bHangTracking = false;
	SetActorTickEnabled(true);
}

void AAedirItemActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const USceneComponent* Parent = GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr;
	if (SettleAlpha < 1.f && Parent && !bHolstered)
	{
		SettleAlpha = FMath::Min(SettleAlpha + DeltaSeconds / ItemSettleSeconds, 1.f);
		const float Share = FMath::SmoothStep(0.f, 1.f, SettleAlpha);
		FTransform Now;
		Now.Blend(SettleFrom, SettleTo, Share);
		SetActorRelativeTransform(Now);
		if (SettleAlpha >= 1.f && !bHolstered)
		{
			WeldToHand();
		}
		return;
	}
	SettleAlpha = 1.f;
	if (!bHolstered || !Parent || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		SetActorTickEnabled(false);
		return;
	}

	// A hanging item is a small pendulum: the acceleration of the bone it hangs from throws it around its pivot.
	const FTransform BoneWorld = Parent->GetSocketTransform(GetRootComponent()->GetAttachSocketName());
	const FVector BoneLocation = BoneWorld.GetLocation();
	FVector Acceleration = FVector::ZeroVector;
	if (bHangTracking)
	{
		const FVector Velocity = (BoneLocation - HangPreviousBone) / DeltaSeconds;
		Acceleration = ((Velocity - HangPreviousVelocity) / DeltaSeconds).GetClampedToMaxSize(HangMaxAcceleration);
		HangPreviousVelocity = Velocity;
	}
	HangPreviousBone = BoneLocation;
	bHangTracking = true;

	// The lever runs from the pivot to the middle of the item.
	const FVector Lever = HangRotation.RotateVector(FVector(-HangPivotCm >= 0.f ? 1.f : -1.f, 0.f, 0.f));
	const FVector Inertial = BoneWorld.GetRotation().Inverse().RotateVector(-Acceleration);
	const FVector Torque = FVector::CrossProduct(Lever, Inertial) * HangSwingGain;
	// Pushed aside it stays there a moment, or it would rattle against what it touches.
	HangContactHold = FMath::Max(HangContactHold - DeltaSeconds, 0.f);
	HangSwingSpeed = HangContactHold > 0.f ? FVector::ZeroVector : HangSwingSpeed + (Torque - HangSwing * HangSwingStiffness - HangSwingSpeed * HangSwingDamping) * DeltaSeconds;
	HangSwing = (HangSwing + HangSwingSpeed * DeltaSeconds).GetClampedToMaxSize(HangSwingMaxRadians);
	UpdateHangContact(BoneWorld, DeltaSeconds);

	const float Angle = static_cast<float>(HangSwing.Size());
	const FQuat Swing = Angle > KINDA_SMALL_NUMBER ? FQuat(HangSwing / Angle, Angle) : FQuat::Identity;
	const FQuat Rotation = Swing * HangRotation;
	SetActorRelativeTransform(FTransform(Rotation, HangSlot - Rotation.RotateVector(FVector(HangPivotCm, 0.f, 0.f)), BodyScale));
}

void AAedirItemActor::UpdateHangContact(const FTransform& BoneWorld, float DeltaSeconds)
{
	UWorld* World = GetWorld();
	// The length of a weapon runs from its grip to its point, and the mesh is centred on the actor.
	const float HalfLength = GetWeaponLength() > 0.f ? FMath::Max(GripLocalCm + GetWeaponLength(), -GripLocalCm) : (Mesh ? static_cast<float>(Mesh->Bounds.SphereRadius) : 0.f);
	if (!World || HalfLength <= 0.f)
	{
		return;
	}

	// Along its whole length, as it hangs this moment.
	const FVector Pivot = BoneWorld.TransformPosition(HangSlot);
	const FVector Axis = GetActorForwardVector();
	const FVector Middle = GetActorLocation();
	const FVector EndA = Middle + Axis * HalfLength;
	const FVector EndB = Middle - Axis * HalfLength;

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirHangingItem), false);
	Params.AddIgnoredActor(this);
	if (AActor* HolderActor = GetAttachParentActor())
	{
		Params.AddIgnoredActor(HolderActor);
		TArray<AActor*> Attached;
		HolderActor->GetAttachedActors(Attached);
		Params.AddIgnoredActors(Attached);
	}

	FHitResult Hit;
	if (!World->SweepSingleByObjectType(Hit, EndA, EndB, FQuat::Identity, Objects, FCollisionShape::MakeSphere(HangSweepRadiusCm), Params))
	{
		return;
	}

	UPrimitiveComponent* Other = Hit.GetComponent();
	if (Other && Other->IsSimulatingPhysics() && Other->GetMass() <= HangBlockMassKg)
	{
		const FVector Motion = (HangPreviousVelocity).GetClampedToMaxSize(HangShoveMaxSpeed);
		if (HasAuthority() && Motion.SizeSquared() > FMath::Square(HangShoveMinSpeed))
		{
			Other->AddImpulseAtLocation(Motion * (Other->GetMass() * HangShoveShare * DeltaSeconds * 10.f), Hit.ImpactPoint);
		}
		return;
	}

	// The part that touches swings out of the way, around the pivot and away from what it touches. Pushed along its own length, as on the floor, it goes back behind the body.
	const FVector FarDirection = FVector::DotProduct(Hit.Location - Pivot, Axis) >= 0.0 ? Axis : -Axis;
	FVector Away = FVector::VectorPlaneProject(Hit.ImpactNormal, FarDirection);
	if (Away.SizeSquared() < FMath::Square(HangEndOnShare))
	{
		const AActor* Wearer = GetAttachParentActor();
		Away = FVector::VectorPlaneProject(Wearer ? -Wearer->GetActorForwardVector() : -GetActorRightVector(), FarDirection);
	}
	const FVector Turn = FVector::CrossProduct(FarDirection, Away.GetSafeNormal());
	HangSwing = (HangSwing + BoneWorld.GetRotation().Inverse().RotateVector(Turn) * (HangBlockRadiansPerSecond * DeltaSeconds)).GetClampedToMaxSize(HangSwingMaxRadians);
	HangSwingSpeed = FVector::ZeroVector;
	HangContactHold = HangContactHoldSeconds;
}

FVector AAedirItemActor::GetGripWorld() const
{
	return GetActorLocation() + GetActorForwardVector() * GripLocalCm;
}

void AAedirItemActor::AttachToBackpack(AAedirCreature* Creature)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	SetFreePhysics(false);
	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	Holder = Creature;
	bHolstered = false;
	bStowed = true;
	ApplyStowed(true);
}

void AAedirItemActor::Detach(const FVector& ThrowVelocity)
{
	if (!HasAuthority())
	{
		return;
	}

	bStowed = false;
	ApplyStowed(false);
	HeldGripShiftCm = 0.f;

	// Let go in the middle of a swing, it flies on as the hand was carrying it: the speed of the hand's body at the item and
	// the hand's spin go with it, on top of any toss.
	FVector CarriedVelocity = ThrowVelocity;
	FVector CarriedSpin = FVector::ZeroVector;
	{
		const USkeletalMeshComponent* HandMesh = Cast<USkeletalMeshComponent>(GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr);
		const FName HandBone = GetRootComponent() ? GetRootComponent()->GetAttachSocketName() : NAME_None;
		const FBodyInstance* HandBody = HandMesh ? HandMesh->GetBodyInstance(HandMesh->GetSocketBoneName(HandBone)) : nullptr;
		if (HandBody && HandBody->IsInstanceSimulatingPhysics())
		{
			const FVector Center = Mesh ? FVector(Mesh->Bounds.Origin) : GetActorLocation();
			const FVector HolderVelocity = Holder ? Holder->GetVelocity() : FVector::ZeroVector;
			CarriedVelocity = HandBody->GetUnrealWorldVelocityAtPoint(Center) + (ThrowVelocity - HolderVelocity);
			CarriedSpin = HandBody->GetUnrealWorldAngularVelocityInRadians();
		}
	}

	SetActorTickEnabled(false);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (Holder)
	{
		LastHolder = Holder;
		LastReleaseTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}
	Holder = nullptr;
	bHolstered = false;

	SetFreePhysics(true);
	if (Mesh)
	{
		Mesh->SetPhysicsLinearVelocity(CarriedVelocity);
		Mesh->SetPhysicsAngularVelocityInRadians(CarriedSpin);
	}
}

void AAedirItemActor::SetHighlighted(bool bNewHighlighted)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->SetRenderCustomDepth(bNewHighlighted);
	Mesh->SetCustomDepthStencilValue(AedirHighlightStencilValue);

	if (HighlightOverlayMaterial)
	{
		Mesh->SetOverlayMaterial(bNewHighlighted ? HighlightOverlayMaterial.Get() : nullptr);
	}
}

float AAedirItemActor::GetHoldRadiusCm() const
{
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return 0.f;
	}

	// From the mesh itself: the box around a turned item in the world is larger than the item.
	const FVector Extent = Mesh->GetStaticMesh()->GetBounds().BoxExtent * Mesh->GetComponentScale().GetAbs();
	return static_cast<float>(FMath::Max(Extent.X, Extent.Y));
}

FVector AAedirItemActor::GetBoundsCenterWorld() const
{
	if (!Mesh)
	{
		return GetActorLocation();
	}

	return Mesh->GetComponentTransform().TransformPosition(GetStaticBoundsCenter());
}

bool AAedirItemActor::IsHeld() const
{
	return Holder != nullptr;
}

bool AAedirItemActor::IsUsable() const
{
	return ItemClass == EAedirItemClass::Poison || ItemClass == EAedirItemClass::Book || ItemClass == EAedirItemClass::Armor;
}

float AAedirItemActor::GetDamage() const
{
	return Damage;
}

float AAedirItemActor::GetWeaponLength() const
{
	return WeaponLength;
}

EAedirDamageType AAedirItemActor::GetDamageType() const
{
	return DamageType;
}

bool AAedirItemActor::IsPickable(const AAedirItemActor* Item)
{
	return Item && !Item->IsHeld() && !Item->bWorn;
}

void AAedirItemActor::OnRep_Holder()
{
	SetFreePhysics(Holder == nullptr);
	if (Holder && IsHeld())
	{
		ApplyHeldCollision(Holder);
	}
}

void AAedirItemActor::OnRep_Stowed()
{
	ApplyStowed(bStowed);
}

void AAedirItemActor::ApplyStowed(bool bNewStowed)
{
	SetActorHiddenInGame(bNewStowed);
	if (Mesh && bNewStowed)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AAedirItemActor::AttachToBone(AAedirCreature* Creature, FName BoneName, const FTransform& HoldOffset)
{
	if (bStowed)
	{
		bStowed = false;
		ApplyStowed(false);
	}

	SetActorTickEnabled(false);
	SetFreePhysics(false);

	AttachToComponent(Creature->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, BoneName);
	SetActorRelativeTransform(ComputeHoldTransform(HoldOffset));
}

void AAedirItemActor::AttachToWornBone(AAedirCreature* Creature)
{
	if (!HasAuthority() || !Creature || ArmorAttachBone.IsNone())
	{
		return;
	}

	AttachToBone(Creature, ArmorAttachBone, FTransform::Identity);

	Holder = Creature;
	bHolstered = false;
	bWorn = true;
	ApplyWornLook();
}

void AAedirItemActor::TakeOff()
{
	if (!HasAuthority() || !bWorn)
	{
		return;
	}
	bWorn = false;
	ClearWornLook();
}

void AAedirItemActor::SetFreePhysics(bool bFree)
{
	if (!Mesh)
	{
		return;
	}

	UnweldFromHand();
	Mesh->SetCollisionEnabled(bFree ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetSimulatePhysics(bFree);
	if (bHeldCollision)
	{
		bHeldCollision = false;
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, FreePawnResponse);
		Mesh->BodyInstance.SetMaskFilter(0);
	}
}

void AAedirItemActor::ApplyHeldCollision(const AAedirCreature* Creature)
{
	if (!Mesh || !Creature || bWorn || bHolstered || bStowed)
	{
		return;
	}
	// In the hand it is solid but moved by the hand, not by its own weight: it shoves loose things and the limbs of others
	// out of its way. It passes the body that holds it, and the standing bodies' capsules, which only the blow itself reaches.
	if (!bHeldCollision)
	{
		FreePawnResponse = Mesh->GetCollisionResponseToChannel(ECC_Pawn);
	}
	bHeldCollision = true;
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->BodyInstance.SetMaskFilter(Creature->GetBodyMaskBit());
	if (SettleAlpha >= 1.f)
	{
		WeldToHand();
	}
}

void AAedirItemActor::WeldToHand()
{
	// Held, the item becomes part of the hand's own body: its weight is in the arm, it stops against a wall or a shield, and
	// it shoves whatever it meets as hard as the arm behind it.
	USceneComponent* Parent = Mesh ? Mesh->GetAttachParent() : nullptr;
	if (!bHeldCollision || !Parent || Mesh->IsWelded() || CVarAedirWeldHeld.GetValueOnGameThread() == 0)
	{
		return;
	}
	Mesh->WeldTo(Parent, Mesh->GetAttachSocketName());
}

void AAedirItemActor::UnweldFromHand()
{
	if (Mesh && Mesh->IsWelded())
	{
		Mesh->UnWeldFromParent();
	}
}

void AAedirItemActor::ApplyItemMass()
{
	if (Mesh)
	{
		Mesh->SetMassOverrideInKg(NAME_None, Weight, true);
		Mesh->BodyInstance.SetOverrideMaxDepenetrationVelocity(true);
		Mesh->BodyInstance.SetMaxDepenetrationVelocity(ItemMaxDepenetrationSpeed);
	}
}

FTransform AAedirItemActor::ComputeHoldTransform(const FTransform& HoldOffset) const
{
	// The root transform maps mesh-local space to the parent, so the bounds center lands on the hold offset translation.
	const FVector CenterOffset = GetStaticBoundsCenter();
	return FTransform(HoldOffset.GetRotation(), HoldOffset.TransformPosition(-CenterOffset), HoldOffset.GetScale3D() * BodyScale);
}

FVector AAedirItemActor::GetStaticBoundsCenter() const
{
	if (!Mesh || !Mesh->GetStaticMesh())
	{
		return FVector::ZeroVector;
	}

	return Mesh->GetStaticMesh()->GetBounds().Origin;
}
