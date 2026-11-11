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
	// A weapon whose body is a box this thin across is a haft. Held by the blade, the hand is this share of the blade's length
	// from its middle towards the point, and the pommel lies this share of the grip beyond the hand's place on it.
	constexpr double ItemHaftedBoxY = 0.035;
	constexpr float ItemMurderHoldShare = 0.12f;
	TAutoConsoleVariable<int32> CVarAedirEdgeOn(TEXT("aedir.EdgeOn"), 1, TEXT("A blade is held with its edge to the enemy (1) or as its model lies (0)."));
	// Across a blade, thinner one way than this share of the other: it has a flat that can strike.
	constexpr double AedirItemFlatRatio = 0.5;
	TAutoConsoleVariable<int32> CVarAedirHeldPassesOwnBody(TEXT("aedir.HeldPassesOwnBody"), 1, TEXT("A held weapon passes through its holder's own body (1) or stops against it (0): stopping, a blade caught on the own head or hip turned over the wrong way round and stuck there."));
	// A piece worn on the arm that is this thin for its width is a shield.
	constexpr float ItemShieldThinShare = 0.3f;
	constexpr float ItemPommelBeyondGripShare = 0.6f;
	// Where armour leaves a body bare, in cm from the bone the blow landed on: the face in front of the head below the brow,
	// under the arm where it meets the trunk, and high on the sides under the arms.
	constexpr float ArmorFaceForwardCm = 4.f;
	constexpr float ArmorBrowHeightCm = 14.f;
	constexpr float ArmorArmpitDropCm = 3.f;
	constexpr float ArmorArmpitInsideCm = 2.f;
	constexpr float ArmorSideGapCm = 13.f;
	constexpr float ArmorMaxTake = 0.97f;
	// Metal shrugs off blows below this; past it, this many joules all told bend a piece through, and bent through it takes
	// this much less of a blow.
	constexpr float ArmorDentFromJoules = 60.f;
	constexpr float ArmorWearJoules = 2500.f;
	constexpr float ArmorWearLoss = 0.4f;
	// Welding a held item onto the hand's body leaves its size in the welded shape's transform, which breaks the physics'
	// queries and contacts on it; the hand carries its weight by force and its blade stops by the held sweep instead.
	TAutoConsoleVariable<int32> CVarAedirWeldHeld(TEXT("aedir.WeldHeld"), 0, TEXT("Make a held item part of the hand's physical body."));
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
	bLodged = false;
	// Drawn from the belt, the blade comes out of its scabbard, which stays where it hung.
	if (bHolstered)
	{
		LeaveSheathOnBelt();
	}
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

bool AAedirItemActor::IsShield() const
{
	return ItemClass == EAedirItemClass::Armor && ArmorZone == EAedirBodyZone::Arm && BodyScale.Z < ItemShieldThinShare * FMath::Min(BodyScale.X, BodyScale.Y);
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

	// A shield is gripped by the bar behind its boss: the fist goes into the boss and the face stands across the way the
	// knuckles point, its round side out.
	const FQuat ItemRotation = IsShield() ? FRotationMatrix::MakeFromXZ(KnuckleAxis, FingerAxis).ToQuat() : FRotationMatrix::MakeFromXY(KnuckleAxis, FingerAxis).ToQuat();
	// A blade is held edge on: its edge goes the way the fingers point, out to the enemy, and its flat faces the palm. A model
	// whose thin side lies the other way round is turned a quarter about its length.
	(void)GetFlatNormalWorld();
	const bool bEdgeOn = !IsShield() && ItemClass == EAedirItemClass::Weapon && bHasFlat && CVarAedirEdgeOn.GetValueOnGameThread() != 0;
	const FQuat EdgeRoll = bEdgeOn && FMath::Abs(FlatAxisLocal.Y) > 0.5 ? FQuat(FVector::ForwardVector, UE_HALF_PI) : FQuat::Identity;
	// A reversed blade lies end for end in the hand, turned about the palm.
	const FQuat Turn = (GripTurn == EGripTurn::Reversed ? FQuat(FVector::UpVector, UE_PI) : FQuat::Identity) * EdgeRoll;
	const FQuat RelativeRotation = HandRotation.Inverse() * ItemRotation * Turn;
	const FVector RelativePalm = HandRotation.Inverse().RotateVector(Palm - HandLocation);
	return FTransform(RelativeRotation, RelativePalm - RelativeRotation.RotateVector(FVector(GripLocalCm + HeldGripShiftCm, 0.f, 0.f)));
}

void AAedirItemActor::LeaveSheathOnBelt()
{
	USceneComponent* Hip = GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr;
	UE_LOG(LogTemp, Verbose, TEXT("[Sheath] %s drawn: sheath %d visible %d on %s"), *GetName(), Sheath ? 1 : 0, Sheath && Sheath->IsVisible() ? 1 : 0, *GetNameSafe(Hip));
	if (!Sheath || !Sheath->IsVisible() || !Hip || BeltSheath)
	{
		return;
	}
	BeltSheath = NewObject<UStaticMeshComponent>(Hip->GetOwner(), NAME_None, RF_Transient);
	BeltSheath->SetStaticMesh(Sheath->GetStaticMesh());
	for (int32 Slot = 0; Slot < Sheath->GetNumMaterials(); ++Slot)
	{
		BeltSheath->SetMaterial(Slot, Sheath->GetMaterial(Slot));
	}
	BeltSheath->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeltSheath->SetWorldTransform(Sheath->GetComponentTransform());
	BeltSheath->SetupAttachment(Hip, GetRootComponent()->GetAttachSocketName());
	BeltSheath->RegisterComponent();
	BeltSheath->AttachToComponent(Hip, FAttachmentTransformRules::KeepWorldTransform, GetRootComponent()->GetAttachSocketName());
	Sheath->SetVisibility(false);
}

void AAedirItemActor::TakeSheathBack()
{
	if (BeltSheath)
	{
		BeltSheath->DestroyComponent();
		BeltSheath = nullptr;
	}
	if (Sheath)
	{
		Sheath->SetVisibility(true);
	}
}

void AAedirItemActor::AttachToWaist(AAedirCreature* Creature, FName BoneName, const FVector& SlotOnBone, const FQuat& RotationOnBone, float PivotCm)
{
	if (!HasAuthority() || !Creature)
	{
		return;
	}

	HeldGripShiftCm = 0.f;
	AttachToBone(Creature, BoneName, FTransform(RotationOnBone, SlotOnBone - RotationOnBone.RotateVector(FVector(PivotCm, 0.f, 0.f))));
	// Put away, the blade goes back into its scabbard.
	TakeSheathBack();

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

FAedirArmorHit AAedirItemActor::GetArmorAt(FName Bone, const FVector& Point, EAedirDamageType Type, const AActor* Wearer) const
{
	FAedirArmorHit Hit;
	const AAedirCreature* Body = Cast<AAedirCreature>(Wearer);
	const USkeletalMeshComponent* Mesh3 = Body ? Body->GetMesh() : nullptr;
	if (!Mesh3 || !ArmorBones.Contains(Bone))
	{
		return Hit;
	}
	const float Scale = static_cast<float>(Body->GetActorScale3D().Z);
	const FVector Forward = Body->GetActorForwardVector();
	const FVector Up = Body->GetActorUpVector();
	const FVector Rel = Point - Mesh3->GetBoneLocation(Bone);
	const FString Name = Bone.ToString();

	// An open helmet leaves the face bare: in front of the head and below the brow.
	if (bOpenFace && Name == TEXT("head") && FVector::DotProduct(Rel, Forward) > ArmorFaceForwardCm * Scale && FVector::DotProduct(Rel, Up) < ArmorBrowHeightCm * Scale)
	{
		return Hit;
	}
	// Plate over the shoulder and down the arm is open underneath, where the arm meets the trunk; a breastplate is open at the
	// sides high under the arms.
	const bool bLeft = Name.EndsWith(TEXT("_l"));
	const FVector Outward = Body->GetActorRightVector() * (bLeft ? -1.f : 1.f);
	if ((Name.StartsWith(TEXT("upperarm")) || Name.StartsWith(TEXT("clavicle"))) && FVector::DotProduct(Rel, Up) < -ArmorArmpitDropCm * Scale && FVector::DotProduct(Rel, Outward) < ArmorArmpitInsideCm * Scale)
	{
		return Hit;
	}
	if (Name == TEXT("spine_05") || Name == TEXT("spine_04"))
	{
		const FVector Side = FVector::VectorPlaneProject(Rel, Forward);
		if (FMath::Abs(FVector::DotProduct(Side, Body->GetActorRightVector())) > ArmorSideGapCm * Scale && FVector::DotProduct(Rel, Up) < 0.f && Name == TEXT("spine_05"))
		{
			return Hit;
		}
	}

	// What each material takes of an edge, a point and a blunt blow, before its make and thickness.
	static const float Takes[3][3] = {
		// Slash, Pierce, Blunt
		{ 0.45f, 0.30f, 0.35f }, // Soft
		{ 0.85f, 0.45f, 0.25f }, // Mail
		{ 0.95f, 0.90f, 0.50f }, // Plate
	};
	static const float Usual[3] = { 0.3f, 0.5f, 0.6f };
	const int32 MaterialIndex = static_cast<int32>(ArmorMaterial);
	const int32 TypeIndex = Type == EAedirDamageType::Slash ? 0 : Type == EAedirDamageType::Pierce ? 1 : 2;
	// The make counts for less than the material: thin plate is still plate.
	const float Make = FMath::Clamp(ArmorProtection / Usual[MaterialIndex], 0.85f, 1.1f);
	Hit.Protection = FMath::Min(Takes[MaterialIndex][TypeIndex] * Make, ArmorMaxTake) * (1.f - ArmorWearLoss * ArmorWear);
	Hit.bCovered = true;
	Hit.Material = ArmorMaterial;
	Hit.Piece = GetFName();
	Hit.PieceActor = const_cast<AAedirItemActor*>(this);
	return Hit;
}

void AAedirItemActor::TakeArmorBlow(float EnergyJoules)
{
	// Padding does not bend; metal does, a little for every blow past what it shrugs off.
	if (ArmorMaterial == EAedirArmorMaterial::Soft || EnergyJoules <= ArmorDentFromJoules)
	{
		return;
	}
	ArmorWear = FMath::Min(ArmorWear + (EnergyJoules - ArmorDentFromJoules) / ArmorWearJoules, 1.f);
}

FVector AAedirItemActor::GetGripWorld() const
{
	return GetActorLocation() + GetActorForwardVector() * GripLocalCm;
}

void AAedirItemActor::SetPassesOwnBody(bool bPasses)
{
	if (bPassesOwnBody == bPasses)
	{
		return;
	}
	bPassesOwnBody = bPasses;
	// Bodies are physics bodies; for the short while it passes them it passes everyone's, which a weapon coming out of a belt
	// does not notice.
	if (Mesh && bHeldCollision)
	{
		Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, bPassesOwnBody || bLodged || CVarAedirHeldPassesOwnBody.GetValueOnGameThread() != 0 ? ECR_Ignore : ECR_Block);
	}
}

void AAedirItemActor::SetLodged(bool bNewLodged)
{
	if (bLodged == bNewLodged)
	{
		return;
	}
	bLodged = bNewLodged;
	// Stuck in a body it lies inside that body's flesh, which it must not shove out of its way.
	if (Mesh && bHeldCollision)
	{
		Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, bPassesOwnBody || bLodged || CVarAedirHeldPassesOwnBody.GetValueOnGameThread() != 0 ? ECR_Ignore : ECR_Block);
	}
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

	if (bHolstered)
	{
		// Let go of from the belt, the blade falls in its scabbard.
		TakeSheathBack();
	}
	else if (Sheath && !BeltSheath)
	{
		Sheath->SetVisibility(false);
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

FVector AAedirItemActor::GetFlatNormalWorld() const
{
	// Across the blade the item is thin one way (the flat) and wide the other (from edge to edge, or from the edge of an axe
	// to its back): the thin way is read off the model's own shape, once.
	if (!bFlatAxisKnown)
	{
		bFlatAxisKnown = true;
		FBox Local(ForceInit);
		// In the actor's own frame without its scale: the root's scale is the size of its collision box, not of the model.
		const FTransform ToItem = FTransform(GetActorQuat(), GetActorLocation()).Inverse();
		TInlineComponentArray<UStaticMeshComponent*> Parts(this);
		for (const UStaticMeshComponent* Part : Parts)
		{
			if (!Part || !Part->GetStaticMesh() || Part == Sheath || Part == BeltSheath)
			{
				continue;
			}
			const FBox Box = Part->GetStaticMesh()->GetBoundingBox();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector P((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
				Local += ToItem.TransformPosition(Part->GetComponentTransform().TransformPosition(P));
			}
		}
		const FVector Size = Local.IsValid ? Local.GetSize() : FVector(1.0, 1.0, 0.1);
		const int32 Long = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : Size.Y >= Size.Z ? 1 : 2;
		const int32 A = (Long + 1) % 3;
		const int32 B = (Long + 2) % 3;
		FlatAxisLocal = FVector::ZeroVector;
		FlatAxisLocal[Size[A] <= Size[B] ? A : B] = 1.0;
		// A round or square haft has no flat: it strikes the same whichever way it meets.
		bHasFlat = FMath::Min(Size[A], Size[B]) < FMath::Max(Size[A], Size[B]) * AedirItemFlatRatio;
	}
	return bHasFlat ? GetActorQuat().RotateVector(FlatAxisLocal) : FVector::ZeroVector;
}

EAedirDamageType AAedirItemActor::GetDamageType() const
{
	// The back of an axe and the hilt of a sword held by its blade strike blunt.
	return GripTurn == EGripTurn::Back || GripTurn == EGripTurn::Murder ? EAedirDamageType::Blunt : DamageType;
}

bool AAedirItemActor::IsHafted() const
{
	return ItemClass == EAedirItemClass::Weapon && BodyScale.Y <= ItemHaftedBoxY;
}

AAedirItemActor::EGripTurn AAedirItemActor::TurnInHand(AAedirCreature* Creature, bool bHalfSword)
{
	if (!Creature || Holder != Creature || ItemClass != EAedirItemClass::Weapon || !GetRootComponent())
	{
		return GripTurn;
	}
	const EGripTurn Was = GripTurn;
	ResetGripTurn();
	if (Was == EGripTurn::Normal)
	{
		if (IsHafted())
		{
			// A hafted weapon (its body a thin round box) turns over: its head has a back to strike with and its haft is the
			// same all round, so the hand holds it as before.
			GripTurn = EGripTurn::Back;
			TurnLookOnHaft();
		}
		else if (bHalfSword)
		{
			// Held by the blade in both hands, a sword strikes with its hilt: the hand moves up the blade, and the reach is from
			// there back to the pommel.
			GripTurn = EGripTurn::Murder;
			MadeGripLocalCm = GripLocalCm;
			MadeWeaponLength = WeaponLength;
			const float BladeCm = static_cast<float>(BodyScale.X) * 100.f;
			const float HoldCm = BladeCm * ItemMurderHoldShare;
			const float PommelCm = GripLocalCm - GripSpanCm * ItemPommelBeyondGripShare;
			GripLocalCm = HoldCm;
			WeaponLength = HoldCm - PommelCm;
			// Its point now lies back towards the body that holds it, which it passes rather than shoves.
			SetPassesOwnBody(true);
		}
		else if (!bTwoHanded)
		{
			GripTurn = EGripTurn::Reversed;
		}
	}
	SettleAlpha = 1.f;
	SetActorRelativeTransform(ComputeHandGrip(Creature, HeldHand));
	return GripTurn;
}

void AAedirItemActor::ResetGripTurn()
{
	if (GripTurn == EGripTurn::Back)
	{
		TurnLookOnHaft();
	}
	else if (GripTurn == EGripTurn::Murder)
	{
		GripLocalCm = MadeGripLocalCm;
		WeaponLength = MadeWeaponLength;
		GripTurn = EGripTurn::Normal;
		SetPassesOwnBody(false);
	}
	GripTurn = EGripTurn::Normal;
}

void AAedirItemActor::TurnLookOnHaft()
{
	const FQuat Half(FVector::ForwardVector, UE_PI);
	if (Mesh)
	{
		for (USceneComponent* Part : Mesh->GetAttachChildren())
		{
			if (Part)
			{
				Part->SetRelativeLocationAndRotation(Half.RotateVector(Part->GetRelativeLocation()), Half * Part->GetRelativeRotation().Quaternion());
			}
		}
	}
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

	AttachToBone(Creature, ArmorAttachBone, WornTransform);

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
	// Let go of, it is taken up again as it was made to be held.
	if (bFree)
	{
		ResetGripTurn();
	}
	Mesh->SetCollisionEnabled(bFree ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Mesh->SetSimulatePhysics(bFree);
	if (bHeldCollision)
	{
		bHeldCollision = false;
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, FreePawnResponse);
		Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Mesh->BodyInstance.SetMaskFilter(0);
	}
	bPassesOwnBody = false;
}

void AAedirItemActor::ApplyHeldCollision(const AAedirCreature* Creature)
{
	if (!Mesh || !Creature || bWorn || bHolstered || bStowed)
	{
		return;
	}
	// In the hand it is solid against the world and against every body, also the holder's own trunk and limbs; it passes only
	// the holder's own hands, which close on it (the mask bit it shares with them), and the standing bodies' capsules.
	if (!bHeldCollision)
	{
		FreePawnResponse = Mesh->GetCollisionResponseToChannel(ECC_Pawn);
	}
	bHeldCollision = true;
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->BodyInstance.SetMaskFilter(Creature->GetHeldMaskBit());
	Mesh->SetCollisionResponseToChannel(ECC_PhysicsBody, bPassesOwnBody || bLodged || CVarAedirHeldPassesOwnBody.GetValueOnGameThread() != 0 ? ECR_Ignore : ECR_Block);
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
