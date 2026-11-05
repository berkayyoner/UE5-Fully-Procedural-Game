#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AedirUpscalerSubsystem.generated.h"

UENUM(BlueprintType)
enum class EAedirUpscaler : uint8
{
	None,
	TSR,
	DLSS,
	FSR,
	XeSS
};

UENUM(BlueprintType)
enum class EAedirUpscalerQuality : uint8
{
	Native,
	Quality,
	Balanced,
	Performance,
	UltraPerformance
};

UCLASS()
class AEDIR_API UAedirUpscalerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category="Aedir|Graphics")
	TArray<EAedirUpscaler> GetAvailableUpscalers() const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Graphics")
	bool SetUpscaler(EAedirUpscaler Upscaler, EAedirUpscalerQuality Quality);

	UFUNCTION(BlueprintPure, Category="Aedir|Graphics")
	EAedirUpscaler GetCurrentUpscaler() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Graphics")
	EAedirUpscalerQuality GetCurrentQuality() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Graphics")
	FText GetUpscalerDisplayName(EAedirUpscaler Upscaler) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Graphics")
	void ApplyOptimizedRenderDefaults();

private:
	EAedirUpscaler CurrentUpscaler = EAedirUpscaler::TSR;
	EAedirUpscalerQuality CurrentQuality = EAedirUpscalerQuality::Quality;
};
