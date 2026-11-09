#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AedirSceneryActors.generated.h"

class UAudioComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class UStaticMeshComponent;

// A fire: a campfire or a torch's flame. Glowing tongues of flame that flicker and leap, the warm light they throw, the smoke
// that rises from them and their crackle.
UCLASS()
class AEDIR_API AAedirFlame : public AActor
{
	GENERATED_BODY()

public:
	AAedirFlame();

	// Sets how big the fire is: the height of its flames, how bright its light and whether it smokes.
	void Configure(float InFlameHeightCm, float InLightCandelas, bool bInSmoke);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Tongues;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TongueMaterial;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> Smoke;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAudioComponent> Crackle;

	float FlameHeightCm = 40.f;
	float LightCandelas = 2500.f;
	bool bSmoke = true;
	float Phase = 0.f;
};

// A waterfall: a sheet of water falling from a ledge into a pool, throwing up spray where it lands, roaring.
UCLASS()
class AEDIR_API AAedirWaterfall : public AActor
{
	GENERATED_BODY()

public:
	AAedirWaterfall();

	// The water leaves the ledge at Top and lands at Bottom; the sheet is Width wide.
	void Configure(const FVector& Top, const FVector& Bottom, float WidthCm);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Sheet;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UAudioComponent> Roar;

	FVector Landing = FVector::ZeroVector;
	FVector Across = FVector::RightVector;
	float Width = 400.f;
	float NextSpray = 0.f;
};
