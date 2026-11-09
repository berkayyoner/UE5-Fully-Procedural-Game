#include "Items/AedirCatalogItem.h"
#include "Game/AedirClasses.h"

#include "AnimationRuntime.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Engine/SkeletalMesh.h"
#include "Graphics/AedirScannedMaterial.h"
#include "Creature/AedirStatsComponent.h"

#include "Combat/AedirCombatComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemCatalog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CatalogPolearmMinLengthCm = 140.f;
	const FLinearColor CatalogItemSteel(0.55f, 0.57f, 0.6f);
	const FLinearColor CatalogItemWood(0.36f, 0.22f, 0.12f);
	const FLinearColor CatalogItemStrap(0.12f, 0.08f, 0.05f);

	// Engine basic shapes are 100 cm across at unit scale.
	constexpr float CatalogItemShapeCm = 100.f;

	// Crossguard, grip and pommel sizes of the swords, in cm.
	constexpr float CatalogItemPommelSize = 3.f;
	constexpr float CatalogItemGuardThickness = 1.4f;

	// Armour laid on the ground is this share as thick as it is deep when worn.
	constexpr float CatalogLaidThickShare = 0.6f;

	EAedirCatalogShape CatalogItemRootShape(FName Id)
	{
		const FString IdText = Id.ToString();
		if (IdText == TEXT("Bracers") || IdText == TEXT("IronGreaves") || IdText == TEXT("SmallPotionPoison"))
		{
			return EAedirCatalogShape::Cylinder;
		}

		if (IdText == TEXT("LeatherCap") || IdText == TEXT("IronHelm") || IdText == TEXT("Boulder"))
		{
			return EAedirCatalogShape::Sphere;
		}

		return EAedirCatalogShape::Cube;
	}
}

AAedirCatalogItem::AAedirCatalogItem()
{
	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	BaseMaterial = MaterialFinder.Object;
}

AAedirCatalogItem* AAedirCatalogItem::SpawnCatalogItem(UWorld* World, FName Id, const FTransform& Transform)
{
	if (!World || !UAedirItemCatalog::Find(Id))
	{
		return nullptr;
	}

	AAedirCatalogItem* Item = World->SpawnActorDeferred<AAedirCatalogItem>(AedirClasses::Find<AAedirCatalogItem>(TEXT("BP_AedirItem")), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Item)
	{
		return nullptr;
	}

	Item->CatalogId = Id;
	Item->FinishSpawning(Transform);
	return Item;
}

void AAedirCatalogItem::BeginPlay()
{
	Super::BeginPlay();

	ApplyDefinition();
}

void AAedirCatalogItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAedirCatalogItem, CatalogId);
}

void AAedirCatalogItem::Use_Implementation(AAedirCreature* User)
{
	if (ItemClass == EAedirItemClass::Food)
	{
		const FAedirItemDef* Def = UAedirItemCatalog::Find(CatalogId);
		if (!HasAuthority() || !Def || !User || !User->Stats)
		{
			return;
		}

		User->Stats->AddFood(Def->FoodValue, Def->DrinkValue);
		OnUsed(User);
		if (++UsesSpent >= Def->Uses)
		{
			Destroy();
		}
		return;
	}

	if (ItemClass != EAedirItemClass::Armor)
	{
		Super::Use_Implementation(User);
		return;
	}

	if (!HasAuthority() || !User || !User->Combat || !User->Interaction || bWorn)
	{
		return;
	}

	if (User->Interaction->WearItem(this, HeldHand))
	{
		const float CurrentProtection = User->Combat->GetArmorProtection(ArmorZone);
		User->Combat->SetArmorProtection(ArmorZone, FMath::Max(CurrentProtection, ArmorProtection));
		OnUsed(User);
	}
}

void AAedirCatalogItem::ApplyWornLook()
{
	const FAedirItemDef* Def = UAedirItemCatalog::Find(CatalogId);
	USkeletalMeshComponent* Body = Holder ? Holder->GetMesh() : nullptr;
	if (!Def || Def->WornMeshPath.IsEmpty() || !Body || WornMesh)
	{
		return;
	}
	USkeletalMesh* Model = LoadObject<USkeletalMesh>(nullptr, *Def->WornMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Model)
	{
		UE_LOG(LogTemp, Warning, TEXT("Item %s: worn model %s is missing."), *CatalogId.ToString(), *Def->WornMeshPath);
		return;
	}

	// The piece bends with the body bone by bone, and what stood for it on the bone is put away.
	WornMesh = NewObject<USkeletalMeshComponent>(this);
	WornMesh->SetSkeletalMeshAsset(Model);
	WornMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WornMesh->SetupAttachment(Body);
	WornMesh->RegisterComponent();
	WornMesh->SetLeaderPoseComponent(Body);
	// Each section wears the scanned material with the textures named after it.
	const TArray<FSkeletalMaterial>& Slots = Model->GetMaterials();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (UMaterialInterface* Material = AedirScannedMaterial::Make(WornMesh, Slots[Slot].MaterialSlotName.ToString()))
		{
			WornMesh->SetMaterial(Slot, Material);
		}
	}
	Mesh->SetVisibility(false, false);
	for (const TObjectPtr<UStaticMeshComponent>& Detail : DetailMeshes)
	{
		if (Detail)
		{
			Detail->SetVisibility(false);
		}
	}
	for (const TObjectPtr<UPoseableMeshComponent>& Piece : LaidPieces)
	{
		if (Piece)
		{
			Piece->SetVisibility(false);
		}
	}
}

void AAedirCatalogItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WornMesh)
	{
		WornMesh->DestroyComponent();
		WornMesh = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AAedirCatalogItem::OnRep_CatalogId()
{
	ApplyDefinition();
}

void AAedirCatalogItem::ApplyDefinition()
{
	const FAedirItemDef* Def = UAedirItemCatalog::Find(CatalogId);
	if (!Def || !Mesh || AppliedCatalogId == CatalogId)
	{
		return;
	}

	AppliedCatalogId = CatalogId;

	ItemName = Def->DisplayName;
	ItemClass = Def->ItemClass;
	Weight = Def->WeightKg;
	bTwoHanded = Def->bTwoHanded;
	bDrinkable = Def->DrinkValue > 0.f || Def->ItemClass == EAedirItemClass::Poison;
	// The reach of a weapon counts from the hand to the tip.
	WeaponLength = Def->WeaponLengthCm > 0.f ? Def->WeaponLengthCm * 0.5f - Def->GripCm : 0.f;
	bPolearm = Def->WeaponLengthCm >= CatalogPolearmMinLengthCm;
	GripLocalCm = Def->GripCm;
	GripSpanCm = Def->GripSpanCm;
	bHangsDiagonal = Def->bHangsDiagonal;
	bUseHandGrip = true;
	DamageType = Def->DamageType;
	Damage = Def->FlatDamage;
	ArmorZone = Def->ArmorZone;
	ArmorProtection = Def->ArmorProtection;
	ArmorAttachBone = Def->ArmorAttachBone;
	WornTransform = FTransform(Def->WornRotation, Def->WornOffsetCm);
	BodyScale = Def->BodyScale;

	Mesh->SetMassOverrideInKg(NAME_None, Weight, true);

	BuildVisual(*Def);
}

void AAedirCatalogItem::BuildSheath(const FAedirItemDef& Def)
{
	UStaticMesh* Model = Def.ScabbardMeshPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Def.ScabbardMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Model)
	{
		return;
	}
	// The scabbard runs along the blade from the guard to past the point, its mouth at the guard. Its model stands on its
	// tip with the mouth up, as the blade's own model does.
	const FBox Box = Model->GetBoundingBox();
	const FVector Size = Box.GetSize();
	const float Length = FMath::Max(Def.ScabbardLengthCm, 1.f);
	const float Scale = Length / FMath::Max(static_cast<float>(Size.Z), 1.f);
	// Turned with the blade, so its flat lies along the blade's flat.
	const FQuat Fix = Def.ModelFixRotation.Quaternion();
	const FQuat Turn = Fix * FRotator(90.f, 0.f, 0.f).Quaternion();
	const float Blade = Def.WeaponLengthCm;
	Sheath = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Sheath->SetupAttachment(Mesh);
	Sheath->SetAbsolute(false, false, true);
	Sheath->SetStaticMesh(Model);
	Sheath->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sheath->SetWorldScale3D(FVector(Scale));
	Sheath->SetRelativeRotation(Turn);
	const FVector Middle(-Blade * 0.5f + Length * 0.5f - 2.f, 0.f, 0.f);
	Sheath->SetRelativeLocation((Turn.RotateVector(-Box.GetCenter() * Scale) + Middle + Def.ModelShiftCm) / BodyScale);
	Sheath->RegisterComponent();
	Sheath->SetVisibility(bHolstered);
	DetailMeshes.Add(Sheath);
}

void AAedirCatalogItem::BuildVisual(const FAedirItemDef& Def)
{
	ClearDetails();
	Sheath = nullptr;

	if (Def.MeshPath.IsEmpty() && !Def.WornMeshPath.IsEmpty() && BuildLaidArmour(Def))
	{
		return;
	}

	if (!Def.MeshPath.IsEmpty())
	{
		if (UStaticMesh* Scanned = LoadObject<UStaticMesh>(nullptr, *Def.MeshPath))
		{
			// The item's length runs along X and its middle on the root, so grips and belts need not know what the model looks like.
			const FBox Box = Scanned->GetBoundingBox();
			const FVector Size = Box.GetSize();
			FRotator Turn = FRotator::ZeroRotator;
			double Longest = Size.X;
			if (Size.Y > Longest) { Longest = Size.Y; Turn = FRotator(0.f, -90.f, 0.f); }
			// A bucket or a pot keeps standing the way it was made.
			const bool bSquat = Def.bStandsUpright;
			if (Size.Z > Longest && !bSquat) { Longest = Size.Z; Turn = FRotator(Def.DrinkValue > 0.f || Def.bModelTopForward ? -90.f : 90.f, 0.f, 0.f); } // A jug has its top towards the front of the item, where it is drunk from.
			Longest = FMath::Max(Longest, Size.Z * (bSquat ? 1.0 : 0.0));
			if (Def.bModelAsIs)
			{
				Turn = FRotator::ZeroRotator;
				Longest = FMath::Max3(Size.X, Size.Y, Size.Z);
			}
			if (Def.bModelReversed)
			{
				Turn = (FRotator(0.f, 180.f, 0.f).Quaternion() * Turn.Quaternion()).Rotator();
			}
			const double Wanted = Def.ModelLengthCm > 0.f ? Def.ModelLengthCm : Def.WeaponLengthCm > 0.f ? Def.WeaponLengthCm : FMath::Max3(Def.BodyScale.X, Def.BodyScale.Y, Def.BodyScale.Z) * 100.0;
			const float ModelScale = static_cast<float>(Wanted / FMath::Max(Longest, 1.0));
			Mesh->SetStaticMesh(GetShapeMesh(EAedirCatalogShape::Cube));
			Mesh->SetMaterial(0, nullptr);
			Mesh->SetRelativeScale3D(BodyScale);
			Mesh->SetVisibility(false, false);
			if (UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient))
			{
				Model->SetupAttachment(Mesh);
				Model->SetAbsolute(false, false, true);
				Model->SetStaticMesh(Scanned);
				Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				// The length is the weapon's own; across it the model may be slimmed.
				FVector Scale3D(ModelScale * Def.ModelThickShare);
				const int32 LongAxis = Longest == Size.X ? 0 : Longest == Size.Y ? 1 : 2;
				Scale3D[LongAxis] = ModelScale;
				Model->SetWorldScale3D(Scale3D);
				// Laid along the item, then turned and set off as the model's own measure asks, so the handle is where the palm is.
				const FQuat Fix = Def.ModelFixRotation.Quaternion();
				const FQuat Placed = Fix * Turn.Quaternion();
				Model->SetRelativeRotation(Placed);
				Model->SetRelativeLocation((Fix.RotateVector(Turn.RotateVector(-Box.GetCenter() * ModelScale) + FVector(Def.ModelCenterCm, 0.f, 0.f)) + Def.ModelShiftCm) / BodyScale);
				Model->RegisterComponent();
				AedirScannedMaterial::Apply(Model, AedirScannedMaterial::FolderOfPath(Def.MeshPath));
				DetailMeshes.Add(Model);
				for (const FString& PartPath : Def.ExtraMeshPaths)
				{
					UStaticMesh* PartMesh = LoadObject<UStaticMesh>(nullptr, *PartPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
					UStaticMeshComponent* Part = PartMesh ? NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient) : nullptr;
					if (!Part)
					{
						continue;
					}
					Part->SetupAttachment(Mesh);
					Part->SetAbsolute(false, false, true);
					Part->SetStaticMesh(PartMesh);
					Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Part->SetWorldScale3D(Scale3D);
					Part->SetRelativeRotation(Placed);
					Part->SetRelativeLocation(Model->GetRelativeLocation());
					Part->RegisterComponent();
					DetailMeshes.Add(Part);
				}
				BuildSheath(Def);
				return;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("Item %s: model %s is missing, building its shape instead."), *Def.Id.ToString(), *Def.MeshPath);
	}

	Mesh->SetVisibility(true, false);
	Mesh->SetStaticMesh(GetShapeMesh(CatalogItemRootShape(Def.Id)));
	Mesh->SetMaterial(0, CreateColorMaterial(Def.Color));
	Mesh->SetRelativeScale3D(BodyScale);

	BuildDetails(Def.Id);
}

void AAedirCatalogItem::BuildDetails(FName Id)
{
	const FString IdText = Id.ToString();

	if (IdText == TEXT("Dagger"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(-15.f, 0.f, 0.f), FVector(CatalogItemGuardThickness, 9.f, 1.6f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cylinder, FVector(-21.f, 0.f, 0.f), FVector(9.f, 2.2f, 2.2f), FRotator(90.f, 0.f, 0.f), CatalogItemWood);
		AddDetail(EAedirCatalogShape::Sphere, FVector(-26.3f, 0.f, 0.f), FVector(CatalogItemPommelSize, CatalogItemPommelSize, CatalogItemPommelSize), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("ShortSword"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(-35.f, 0.f, 0.f), FVector(CatalogItemGuardThickness, 12.f, 1.8f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cylinder, FVector(-42.f, 0.f, 0.f), FVector(11.f, 2.4f, 2.4f), FRotator(90.f, 0.f, 0.f), CatalogItemWood);
		AddDetail(EAedirCatalogShape::Sphere, FVector(-48.6f, 0.f, 0.f), FVector(CatalogItemPommelSize, CatalogItemPommelSize, CatalogItemPommelSize), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Longsword"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(-59.f, 0.f, 0.f), FVector(CatalogItemGuardThickness, 20.f, 2.f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cylinder, FVector(-73.f, 0.f, 0.f), FVector(27.f, 2.7f, 2.7f), FRotator(90.f, 0.f, 0.f), CatalogItemWood);
		AddDetail(EAedirCatalogShape::Sphere, FVector(-88.1f, 0.f, 0.f), FVector(3.4f, 3.4f, 3.4f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Sword"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(-52.5f, 0.f, 0.f), FVector(CatalogItemGuardThickness, 18.f, 2.f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cylinder, FVector(-62.f, 0.f, 0.f), FVector(17.f, 2.6f, 2.6f), FRotator(90.f, 0.f, 0.f), CatalogItemWood);
		AddDetail(EAedirCatalogShape::Sphere, FVector(-72.6f, 0.f, 0.f), FVector(CatalogItemPommelSize, CatalogItemPommelSize, CatalogItemPommelSize), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("BattleAxe"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(30.f, 6.f, 0.f), FVector(2.6f, 12.f, 11.f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(-40.f, 0.f, 0.f), FVector(3.f, 3.f, 3.f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Mace"))
	{
		AddDetail(EAedirCatalogShape::Sphere, FVector(29.f, 0.f, 0.f), FVector(9.5f, 9.5f, 9.5f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Warhammer"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(38.5f, 0.f, 0.f), FVector(7.f, 6.f, 13.f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(34.f, 0.f, 0.f), FVector(4.f, 3.f, 6.f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Club"))
	{
		AddDetail(EAedirCatalogShape::Sphere, FVector(29.f, 0.f, 0.f), FVector(10.f, 10.f, 10.f), FRotator::ZeroRotator, CatalogItemWood);
	}
	else if (IdText == TEXT("Spear"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(84.f, 0.f, 0.f), FVector(4.f, 1.8f, 1.8f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(93.f, 0.f, 0.f), FVector(6.f, 1.6f, 1.6f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Quarterstaff"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(81.f, 0.f, 0.f), FVector(8.f, 4.4f, 4.4f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(-81.f, 0.f, 0.f), FVector(8.f, 4.4f, 4.4f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("Halberd"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(70.f, 11.f, 0.f), FVector(22.f, 20.f, 1.4f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(70.f, -8.f, 0.f), FVector(5.f, 14.f, 1.6f), FRotator::ZeroRotator, CatalogItemSteel);
		AddDetail(EAedirCatalogShape::Cube, FVector(91.f, 0.f, 0.f), FVector(20.f, 3.f, 1.4f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("IronHelm"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(13.f, 0.f, -2.f), FVector(2.f, 1.2f, 9.f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("LeatherArmor"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(0.f, 0.f, -14.f), FVector(23.f, 37.f, 3.f), FRotator::ZeroRotator, CatalogItemStrap);
	}
	else if (IdText == TEXT("IronCuirass"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(12.f, 0.f, 4.f), FVector(2.f, 20.f, 8.f), FRotator::ZeroRotator, CatalogItemSteel);
	}
	else if (IdText == TEXT("SmallPotionPoison"))
	{
		AddDetail(EAedirCatalogShape::Cube, FVector(0.f, 0.f, 7.5f), FVector(3.f, 3.f, 2.4f), FRotator::ZeroRotator, CatalogItemWood);
	}
}

void AAedirCatalogItem::AddDetail(EAedirCatalogShape Shape, const FVector& CenterCm, const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Color)
{
	UStaticMeshComponent* Detail = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	Detail->SetStaticMesh(GetShapeMesh(Shape));
	Detail->SetupAttachment(Mesh);
	Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// The root carries BodyScale, so child transforms are divided by it to land in centimetres in the world.
	Detail->SetRelativeLocation(CenterCm / BodyScale);
	Detail->SetRelativeRotation(Rotation);
	// Scale combines per axis regardless of rotation, so the size is first expressed along the detail's own axes and only then divided by the parent's scale.
	Detail->SetRelativeScale3D(Rotation.UnrotateVector(SizeCm).GetAbs() / BodyScale / CatalogItemShapeCm);
	Detail->SetMaterial(0, CreateColorMaterial(Color));

	Detail->RegisterComponent();
	AddInstanceComponent(Detail);
	DetailMeshes.Add(Detail);
}

void AAedirCatalogItem::ClearDetails()
{
	for (const TObjectPtr<UStaticMeshComponent>& Detail : DetailMeshes)
	{
		if (Detail)
		{
			Detail->DestroyComponent();
		}
	}

	DetailMeshes.Reset();
	for (const TObjectPtr<UPoseableMeshComponent>& Piece : LaidPieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	LaidPieces.Reset();
}

bool AAedirCatalogItem::BuildLaidArmour(const FAedirItemDef& Def)
{
	USkeletalMesh* Model = LoadObject<USkeletalMesh>(nullptr, *Def.WornMeshPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Model)
	{
		return false;
	}
	const FReferenceSkeleton& Skeleton = Model->GetRefSkeleton();

	// The whole model is laid down once: a pair worn on both arms or legs lies as the pair it is, not as two copies of it
	// (a scanned pair is skinned to both sides at once, so hiding one side does not take it away).
	TArray<FName> Pieces = { Def.ArmorAttachBone };

	// The body faces +Y in the model and stands along +Z: laid on its back, its front looks up and its head points along
	// the item's length.
	const FRotator OnItsBack = FMatrix(FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f), FVector(1.f, 0.f, 0.f), FVector::ZeroVector).Rotator();
	const FVector Size = Def.BodyScale * 100.f;
	// A pair lies as wide as the two sides stand apart.
	float PairSpan = 0.f;
	if (Def.ArmorAttachBone.ToString().EndsWith(TEXT("_l")) || Def.ArmorAttachBone.ToString().EndsWith(TEXT("_r")))
	{
		const int32 SideIndex = Skeleton.FindBoneIndex(Def.ArmorAttachBone);
		PairSpan = SideIndex == INDEX_NONE ? 0.f : 2.f * FMath::Abs(static_cast<float>(FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, SideIndex).GetLocation().X));
	}
	BodyScale = FVector(Size.Z, Size.Y * Pieces.Num() + PairSpan, Size.X * CatalogLaidThickShare) / 100.f;
	Mesh->SetStaticMesh(GetShapeMesh(EAedirCatalogShape::Cube));
	Mesh->SetMaterial(0, nullptr);
	Mesh->SetRelativeScale3D(BodyScale);
	Mesh->SetVisibility(false, false);

	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const int32 BoneIndex = Skeleton.FindBoneIndex(Pieces[Index]);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}
		// The middle of the piece: halfway down the limb to the next joint, or the bone itself on the trunk.
		FVector Middle = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, BoneIndex).GetLocation();
		const FString Name = Pieces[Index].ToString();
		const TCHAR* const Joints[][2] = { { TEXT("lowerarm"), TEXT("hand") }, { TEXT("upperarm"), TEXT("lowerarm") }, { TEXT("thigh"), TEXT("calf") }, { TEXT("calf"), TEXT("foot") } };
		for (const auto& Joint : Joints)
		{
			if (Name.StartsWith(Joint[0]))
			{
				const int32 Next = Skeleton.FindBoneIndex(FName(Name.Replace(Joint[0], Joint[1])));
				if (Next != INDEX_NONE)
				{
					Middle = (Middle + FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Next).GetLocation()) * 0.5;
				}
			}
		}
		// A pair lies with its middle between the two sides.
		if (Name.EndsWith(TEXT("_l")) || Name.EndsWith(TEXT("_r")))
		{
			Middle.X = 0.0;
		}

		UPoseableMeshComponent* Piece = NewObject<UPoseableMeshComponent>(this, NAME_None, RF_Transient);
		Piece->SetSkinnedAssetAndUpdate(Model);
		Piece->SetupAttachment(Mesh);
		Piece->SetAbsolute(false, false, true);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(true);
		const float Side = Pieces.Num() > 1 ? (Index == 0 ? -0.5f : 0.5f) * Size.Y : 0.f;
		Piece->SetRelativeRotation(OnItsBack);
		Piece->SetRelativeLocation((FVector(0.f, Side, 0.f) - OnItsBack.RotateVector(Middle)) / BodyScale);
		Piece->RegisterComponent();
		for (int32 Other = 0; Other < Pieces.Num(); ++Other)
		{
			if (Other != Index)
			{
				Piece->HideBoneByName(Pieces[Other], PBO_None);
			}
		}
		const TArray<FSkeletalMaterial>& Slots = Model->GetMaterials();
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			if (UMaterialInterface* Material = AedirScannedMaterial::Make(Piece, Slots[Slot].MaterialSlotName.ToString()))
			{
				Piece->SetMaterial(Slot, Material);
			}
		}
		LaidPieces.Add(Piece);
	}
	return !LaidPieces.IsEmpty();
}

void AAedirCatalogItem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		AppliedCatalogId = NAME_None;
		ApplyDefinition();
	}
}

void AAedirCatalogItem::ClearWornLook()
{
	if (WornMesh)
	{
		WornMesh->DestroyComponent();
		WornMesh = nullptr;
	}
	// Built again as it lies on the ground or sits in a hand.
	AppliedCatalogId = NAME_None;
	ApplyDefinition();
}

UStaticMesh* AAedirCatalogItem::GetShapeMesh(EAedirCatalogShape Shape) const
{
	switch (Shape)
	{
	case EAedirCatalogShape::Cylinder:
		return CylinderMesh;
	case EAedirCatalogShape::Sphere:
		return SphereMesh;
	case EAedirCatalogShape::Cube:
	default:
		return CubeMesh;
	}
}

UMaterialInstanceDynamic* AAedirCatalogItem::CreateColorMaterial(const FLinearColor& Color)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}

	return Material;
}
