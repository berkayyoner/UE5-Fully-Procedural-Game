#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirFoliageSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;

// Grass and flowers of the world: they lean away from whoever walks through, move with the wind of the weather, and stay flattened for a while where they were stepped on.
UCLASS()
class AEDIR_API UAedirFoliageSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

	// Covers a stretch of ground, skipping what stands on it. Around is the middle, on the ground.
	void PlantMeadow(const FVector& Around, float HalfSizeCm, float DensityPerSquareMeter, int32 RandomSeed);

	// Stands trees in a ring around a place, with a trunk that can be walked into. They sway on the wind of the weather.
	void PlantTrees(const FVector& Around, float InnerCm, float OuterCm, int32 Count, int32 RandomSeed);

	int32 GetTreeCount() const { return TreeCount; }
	int32 GetCrushedCount() const { return Crushed.Num(); }
	int32 GetInstanceCount() const;

	// Whether grass grows within the radius of the point, for what a step on it sounds like.
	bool IsGrassAt(const FVector& Point, float RadiusCm) const;

private:
	struct FCrushed
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Mesh;
		int32 Instance = 0;
		float StandingZ = 1.f;
		float Share = 0.f;
		float HoldSeconds = 0.f;
	};

	void UpdateBending();
	void UpdateWind(float DeltaTime);
	void EnsureMeadowActor();
	void CrushUnderFeet();
	void Recover(float DeltaTime);
	void SetInstanceFlatness(FCrushed& Entry);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Patches;

	UPROPERTY(Transient)
	TObjectPtr<AActor> MeadowActor;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UInstancedSkinnedMeshComponent>> TreeMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UObject> TreeWindData;

	int32 TreeCount = 0;
	TArray<FCrushed> Crushed;
	TSet<TPair<UInstancedStaticMeshComponent*, int32>> CrushedKeys;
	float CrushTimer = 0.f;
	float WindNow = 1.f;
	bool bBendSet = false;
};
