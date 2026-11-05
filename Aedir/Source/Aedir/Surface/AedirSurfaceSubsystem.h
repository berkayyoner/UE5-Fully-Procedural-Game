#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirSurfaceSubsystem.generated.h"

class AActor;
class UAedirPhysicalMaterial;
class UWorld;

struct FAedirSurfaceCell
{
	float Wetness = 0.f;
	float Snow = 0.f;
	float DryRate = 0.f;
	float MeltRate = 0.f;
};

UCLASS()
class AEDIR_API UAedirSurfaceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintPure, Category="Aedir|Surface")
	float GetWetness(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Surface")
	float GetSnow(const FVector& WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Surface")
	void AddWetness(const FVector& WorldLocation, float Amount, float DryRatePerSecond);

	UFUNCTION(BlueprintCallable, Category="Aedir|Surface")
	void AddSnow(const FVector& WorldLocation, float Amount, float MeltRatePerSecond);

	UFUNCTION(BlueprintCallable, Category="Aedir|Surface")
	void AddWetnessInRadius(const FVector& Center, float Radius, float Amount, float DryRate);

	UFUNCTION(BlueprintCallable, Category="Aedir|Surface")
	void SetGlobalWeatherWetness(float Value01);

	UFUNCTION(BlueprintCallable, Category="Aedir|Surface")
	void SetGlobalWeatherSnow(float Value01);

	static bool QuerySurface(UWorld* World, const FVector& Location, const AActor* Ignore, FHitResult& OutHit);
	static const UAedirPhysicalMaterial* GetAedirMaterial(const FHitResult& Hit);

private:
	FIntPoint GetCellCoord(const FVector& WorldLocation) const;
	void TrimToCellLimit();

	TMap<FIntPoint, FAedirSurfaceCell> Cells;
	float GlobalWetness = 0.f;
	float GlobalSnow = 0.f;
};
