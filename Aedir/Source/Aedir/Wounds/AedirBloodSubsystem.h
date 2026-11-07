#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "Wounds/AedirWoundTypes.h"
#include "AedirBloodSubsystem.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UStaticMeshComponent;
struct FHitResult;

// Small falling blood droplet. Splats on the first hit with the world and destroys itself.
UCLASS()
class AAedirBloodDrop : public AActor
{
	GENERATED_BODY()

public:
	AAedirBloodDrop();

	void Launch(const FVector& Velocity, UMaterialInterface* Material);

private:
	UFUNCTION()
	void OnDropHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, Category="Aedir|Blood")
	TObjectPtr<UStaticMeshComponent> Mesh;

	bool bSplatted = false;
};

UCLASS()
class AEDIR_API UAedirBloodSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxDrops = 64;
	static constexpr int32 MaxSplats = 700;
	// Three game days at Skyrim's pace, where a day passes in 72 real minutes.
	static constexpr float SplatLifetime = 3.f * 72.f * 60.f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void SpawnDrop(const FVector& Location, const FVector& Velocity);
	void SpawnSpray(const FVector& Location, const FVector& Direction, float Severity, EAedirWoundType Type);
	void SpawnSplat(const FVector& Location, const FVector& Normal, float Scale);
	bool IsBloodAt(const FVector& Location, float RadiusCm);

private:
	UMaterialInterface* GetDropMaterial();
	void PruneDrops();
	void PruneSplats();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DropMaterial;

	TArray<TWeakObjectPtr<AAedirBloodDrop>> Drops;
	TArray<TWeakObjectPtr<UDecalComponent>> Splats;
	FRandomStream Random;
};
