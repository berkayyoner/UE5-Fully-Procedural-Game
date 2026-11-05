#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AedirPhysicalMaterial.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

UCLASS(BlueprintType)
class AEDIR_API UAedirPhysicalMaterial : public UPhysicalMaterial
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// 0 = grippy, 1 = ice-like.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0, ClampMax=1))
	float Slipperiness = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0, ClampMax=1))
	float WetSlipperinessBonus = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0, ClampMax=1))
	float SnowSlipperinessBonus = 0.25f;

	// Multiplier on creature ground speed while standing on this surface, e.g. 0.8 for mud.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0))
	float SpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface")
	TSoftObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface")
	TSoftObjectPtr<USoundBase> WetFootstepSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface")
	TSoftObjectPtr<UNiagaraSystem> FootstepParticle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface")
	TSoftObjectPtr<UMaterialInterface> FootprintDecalMaterial;

	// False for surfaces that never become wet, such as absorbing grass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface")
	bool bAllowsWetness = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0))
	float WetnessDryRatePerSecond = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0))
	float SnowMeltRatePerSecond = 0.004f;

	void ApplySlipperinessToPhysics();
};
