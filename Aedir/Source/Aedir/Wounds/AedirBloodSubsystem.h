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
class UProceduralMeshComponent;
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
	// Where it left the wound, for how far it flew.
	FVector LaunchedFrom = FVector::ZeroVector;
};

// One drop of a burst, about the burst's own place.
struct FAedirBurstDrop
{
	FVector At = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Radius = 0.3f;
	float Life = 0.3f;
	float Age = 0.f;
};

// The little burst off the skin the moment a wound opens: drops drawn for a fraction of a second, that mark nothing.
UCLASS()
class AAedirBloodBurst : public AActor
{
	GENERATED_BODY()

public:
	AAedirBloodBurst();

	void Throw(TArray<FAedirBurstDrop>&& Thrown, UMaterialInterface* Material);
	virtual void Tick(float DeltaSeconds) override;

private:
	void Build(bool bFirst);

	UPROPERTY(VisibleAnywhere, Category="Aedir|Blood")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	TArray<FAedirBurstDrop> Drops;
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

	// The drops of a spray leave the wound this share as fast as a cut's or a stab's full speed: at the full speed they flew
	// twice as far as blood does. Measured from the chest: half the speed lands them half as far (half of them within 130 cm
	// instead of 239, nine in ten within 186 instead of 398).
	UPROPERTY(EditAnywhere, Category="Aedir|Blood", meta=(ClampMin="0.05", ClampMax="1"))
	float SpraySpeedShare = 0.5f;

	// The burst off the skin as a wound opens: how many drops, for the slightest and the worst, how fast they leave (cm/s),
	// how far to the sides (degrees), how thick (cm) and how long they are seen (s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	int32 BurstDropsMin = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	int32 BurstDropsMax = 34;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstSpeedMin = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstSpeedMax = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0", ClampMax="90"))
	float BurstConeDegrees = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstDropMinCm = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstDropMaxCm = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstMinSeconds = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Blood|Burst", meta=(ClampMin="0"))
	float BurstMaxSeconds = 0.3f;

	static constexpr int32 MaxSplats = 700;
	int32 GetSplatCount() const { return Splats.Num(); }
	// Three game days at Skyrim's pace, where a day passes in 72 real minutes.
	static constexpr float SplatLifetime = 3.f * 72.f * 60.f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	void SpawnDrop(const FVector& Location, const FVector& Velocity);
	// Outward: off the skin there.
	void SpawnSpray(const FVector& Location, const FVector& Direction, float Severity, EAedirWoundType Type, const FVector& Outward);
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
