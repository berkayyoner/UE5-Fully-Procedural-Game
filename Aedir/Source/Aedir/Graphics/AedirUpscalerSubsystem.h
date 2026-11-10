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

	// Frame generation of the chosen upscaler (DLSS-G, FSR frame interpolation, XeSS-FG); none for TSR or no upscaler.
	bool IsFrameGenerationAvailable() const;
	bool IsFrameGenerationOn() const;
	void SetFrameGeneration(bool bOn);
	// NVIDIA Reflex: 0 off, 1 on, 2 on with boost. At least on while DLSS frame generation runs.
	bool IsReflexAvailable() const;
	int32 GetReflexMode() const;
	void SetReflexMode(int32 Mode);

private:
	const TCHAR* GetFrameGenerationCVar() const;
	void ApplyFrameGeneration();
	void ApplyReflex();
	bool bFrameGenerationWanted = false;
	int32 ReflexMode = 0;
	EAedirUpscaler CurrentUpscaler = EAedirUpscaler::TSR;
	EAedirUpscalerQuality CurrentQuality = EAedirUpscalerQuality::Quality;
};
