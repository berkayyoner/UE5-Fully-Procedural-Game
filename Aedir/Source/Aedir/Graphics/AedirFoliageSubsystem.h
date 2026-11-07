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

	int32 GetCrushedCount() const { return Crushed.Num(); }
	int32 GetInstanceCount() const;

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
	void CrushUnderFeet();
	void Recover(float DeltaTime);
	void SetInstanceFlatness(FCrushed& Entry);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Patches;

	UPROPERTY(Transient)
	TObjectPtr<AActor> MeadowActor;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;

	TArray<FCrushed> Crushed;
	TSet<TPair<UInstancedStaticMeshComponent*, int32>> CrushedKeys;
	float CrushTimer = 0.f;
	float WindNow = 1.f;
	bool bBendSet = false;
};
