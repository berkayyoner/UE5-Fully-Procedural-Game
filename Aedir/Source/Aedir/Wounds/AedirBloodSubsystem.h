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
	// The most drops in the air at once; the oldest goes when a new one needs room.
	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	int32 MaxDrops = 1000;

	// How many drops a cut throws out, back the way the blow came: a number between these two, both counted in.
	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	int32 CutDropsMin = 120;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	int32 CutDropsMax = 180;

	// The same for a stab.
	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	int32 PunctureDropsMin = 20;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	int32 PunctureDropsMax = 34;

	// Blood dripping on blood pools: a drop that lands within PoolRadiusCm of drops already lying there joins them. On a
	// slope the pool runs downhill: each drop lands further down, by PoolRunCmPerDrop for every drop under it on a slope of
	// PoolRunSlopeDegrees (more on a steeper one, less on a gentler one), at most PoolRunMaxCm, drawn out along the way it
	// runs. On level ground it spreads instead, each drop under it making it PoolSpreadPerDrop larger, at most
	// PoolSpreadMax times.
	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolRadiusCm = 5.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolRunCmPerDrop = 1.5f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolRunSlopeDegrees = 10.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolRunMaxCm = 45.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolLevelDegrees = 3.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolSpreadPerDrop = 0.08f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float PoolSpreadMax = 2.2f;

	// The burst of blood where a blow lands plays as it is for BurstFullSeconds, then ages BurstFadeSpeed times as fast so
	// it is gone quickly, and is taken away after BurstMaxSeconds whatever is left of it.
	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float BurstFullSeconds = 0.3f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float BurstFadeSpeed = 6.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Blood")
	float BurstMaxSeconds = 2.f;

	static constexpr int32 MaxSplats = 700;
	int32 GetSplatCount() const { return Splats.Num(); }
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
