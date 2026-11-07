#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirPrecipitationSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

// Rain and snow around whoever is looking, as many drops as the weather asks for, leaning with the wind. Nothing falls under a roof.
UCLASS()
class AEDIR_API UAedirPrecipitationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	int32 GetActiveRain() const { return Rain.Active; }
	int32 GetActiveSnow() const { return Snow.Active; }

private:
	struct FFall
	{
		TArray<FVector> Positions;
		TArray<FVector> Velocities;
		TArray<FTransform> Transforms;
		int32 Active = 0;
		float Fade = 0.f;
	};

	void EnsureMeshes();
	void Spawn(FFall& Fall, int32 Index, const FVector& Center, bool bAnyHeight, const FVector& Wind, float FallSpeed, float Sideways);
	void Step(FFall& Fall, UInstancedStaticMeshComponent* Mesh, UMaterialInstanceDynamic* Material, float Wanted, float DeltaTime, const FVector& Eye, const FVector& Wind, float FallSpeed, float Sideways, const FVector2f& Size, bool bAlongFall);

	UPROPERTY(Transient)
	TObjectPtr<AActor> Holder;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> RainMesh;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SnowMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RainMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SnowMaterial;

	FFall Rain;
	FFall Snow;
	FRandomStream Random;
	float RoofTimer = 0.f;
	bool bUnderRoof = false;
	bool bFailed = false;
};
