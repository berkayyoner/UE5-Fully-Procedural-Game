#pragma once

#include "CoreMinimal.h"
#include "Items/AedirItemActor.h"
#include "AedirCatalogItem.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FAedirItemDef;

enum class EAedirCatalogShape : uint8
{
	Cube,
	Cylinder,
	Sphere
};

// Item actor configured from UAedirItemCatalog. The visuals are engine primitives tinted with the definition colour.
UCLASS(Blueprintable)
class AEDIR_API AAedirCatalogItem : public AAedirItemActor
{
	GENERATED_BODY()

public:
	AAedirCatalogItem();

	UPROPERTY(ReplicatedUsing=OnRep_CatalogId, EditAnywhere, BlueprintReadOnly, Category="Aedir|Item")
	FName CatalogId;

	// Server only. Spawns the item with the given catalog id. Returns null for an unknown id.
	static AAedirCatalogItem* SpawnCatalogItem(UWorld* World, FName Id, const FTransform& Transform);

	virtual void BeginPlay() override;

	// In the editor the item shows as it will in the game, so it can be placed by eye.
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Armor is worn on the server. Every other item uses the base behaviour.
	virtual void Use_Implementation(AAedirCreature* User) override;

private:
	UFUNCTION()
	void OnRep_CatalogId();

	void ApplyDefinition();
	int32 UsesSpent = 0;
	void BuildVisual(const FAedirItemDef& Def);
	void BuildDetails(FName Id);
	void AddDetail(EAedirCatalogShape Shape, const FVector& CenterCm, const FVector& SizeCm, const FRotator& Rotation, const FLinearColor& Color);
	void ClearDetails();
	UStaticMesh* GetShapeMesh(EAedirCatalogShape Shape) const;
	UMaterialInstanceDynamic* CreateColorMaterial(const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	// A blade's scabbard, built along it from the catalog.
	void BuildSheath(const FAedirItemDef& Def);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> DetailMeshes;

	UPROPERTY(Transient)
	TObjectPtr<class USkeletalMeshComponent> WornMesh;

	// A piece of armour lying on the ground: its worn model laid out on its back, a pair side by side, instead of a box.
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UPoseableMeshComponent>> LaidPieces;

	bool BuildLaidArmour(const FAedirItemDef& Def);

	virtual void ApplyWornLook() override;
	virtual void ClearWornLook() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	FName AppliedCatalogId = NAME_None;
};
