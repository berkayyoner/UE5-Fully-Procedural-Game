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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Armor is worn on the server. Every other item uses the base behaviour.
	virtual void Use_Implementation(AAedirCreature* User) override;

private:
	UFUNCTION()
	void OnRep_CatalogId();

	void ApplyDefinition();
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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> DetailMeshes;

	FName AppliedCatalogId = NAME_None;
};
