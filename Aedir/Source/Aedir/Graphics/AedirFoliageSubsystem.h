#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirFoliageSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialParameterCollection;
class UStaticMesh;

// Grass and flowers of the world: they lean away from whoever walks through, move with the wind of the weather, and stay flattened for a while where they were stepped on.
UCLASS()
class AEDIR_API UAedirFoliageSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

	// A meadow and trees planted in the editor and saved in the level are taken up as if planted now.
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Whether the first solid thing over a point is the ground, not a rock, a prop or a roof.
	static bool IsBareGround(const UWorld& InWorld, const FVector& Point);

	// The actor the grass and trees are planted on wears this tag; in the editor it is saved with the level.
	static const FName MeadowTag;

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
		// How far the steps on it have pressed it down; it bends there over a moment.
		float Pressed = 0.f;
		float HoldSeconds = 0.f;
	};

	void UpdateBending();
	void UpdateWind(float DeltaTime);
	void EnsureMeadowActor();
	// Keeps a component made at run time out of the level file, and one made in the editor in it.
	void AddToMeadow(USceneComponent* Component);
	void EnsureTreeWindData();
	void CrushUnderFeet();
	void ApplyGrassSettings();
	int32 AppliedGrassShadows = -1;
	int32 AppliedGrassDistance = -1;
	int32 AppliedGrassPatches = -1;
	void Recover(float DeltaTime);
	void SetInstanceFlatness(FCrushed& Entry);
	// The scanned Megascans grass clumps, when they are in the project.
	static TArray<UStaticMesh*> LoadScannedGrass();
	// Grows the scanned grass where the saved meadow has blades of an older grass, as tall as those were, fewer since a clump covers more ground.
	void ReplantWithScannedGrass(AActor* Meadow);

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
