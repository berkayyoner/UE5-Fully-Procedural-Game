#include "Items/AedirCatalogItem.h"
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

	AAedirCatalogItem* Item = World->SpawnActorDeferred<AAedirCatalogItem>(AAedirCatalogItem::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
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
	BodyScale = Def->BodyScale;

	Mesh->SetMassOverrideInKg(NAME_None, Weight, true);

	BuildVisual(*Def);
}

void AAedirCatalogItem::BuildVisual(const FAedirItemDef& Def)
{
	ClearDetails();

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
			if (Size.Z > Longest) { Longest = Size.Z; Turn = FRotator(90.f, 0.f, 0.f); }
			const double Wanted = Def.WeaponLengthCm > 0.f ? Def.WeaponLengthCm : FMath::Max3(Def.BodyScale.X, Def.BodyScale.Y, Def.BodyScale.Z) * 100.0;
			const float ModelScale = static_cast<float>(Wanted / FMath::Max(Longest, 1.0));
			Mesh->SetStaticMesh(GetShapeMesh(EAedirCatalogShape::Cube));
			Mesh->SetMaterial(0, nullptr);
			Mesh->SetRelativeScale3D(BodyScale);
			Mesh->SetVisibility(false, false);
			if (UStaticMeshComponent* Model = NewObject<UStaticMeshComponent>(this))
			{
				Model->SetupAttachment(Mesh);
				Model->SetAbsolute(false, false, true);
				Model->SetStaticMesh(Scanned);
				Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Model->SetWorldScale3D(FVector(ModelScale));
				Model->SetRelativeRotation(Turn);
				Model->SetRelativeLocation(Turn.RotateVector(-Box.GetCenter() * ModelScale) / BodyScale);
				Model->RegisterComponent();
				AedirScannedMaterial::Apply(Model, AedirScannedMaterial::FolderOfPath(Def.MeshPath));
				DetailMeshes.Add(Model);
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
	UStaticMeshComponent* Detail = NewObject<UStaticMeshComponent>(this);
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
