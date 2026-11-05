#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AedirHUD.generated.h"

class AAedirCreature;
class APawn;
class UAedirStatsComponent;

UCLASS(Blueprintable)
class AEDIR_API AAedirHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CrosshairSize = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CrosshairGap = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CrosshairThickness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor CrosshairColor = FLinearColor(1.f, 1.f, 1.f, 0.85f);

	// Fraction of the screen width used by the compass strip.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassWidthFraction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassTopMargin = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassHeight = 34.f;

	// Visible heading range of the compass in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassFovDegrees = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarWidth = 280.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarHeight = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarBottomMargin = 36.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeInSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeOutSpeed = 1.5f;

	// Seconds a stat must stay full before its bar fades out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeOutDelay = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor HealthBarColor = FLinearColor(0.85f, 0.12f, 0.12f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor ManaBarColor = FLinearColor(0.2f, 0.45f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor StaminaBarColor = FLinearColor(0.25f, 0.85f, 0.3f, 1.f);

private:
	struct FAedirHudBarState
	{
		float Alpha = 0.f;
		// Lagging segment that shows recent loss. Negative means not initialized yet.
		float Trail = -1.f;
		// Seconds the stat has been full. Starts saturated so bars stay hidden until a stat drops.
		float FullTime = 1000.f;
	};

	void DrawCrosshair(float DeltaSeconds, const AAedirCreature* Creature);
	void DrawCompass(const APawn* Pawn);
	void DrawBars(float DeltaSeconds, const UAedirStatsComponent* Stats);
	void DrawPrompt(const AAedirCreature* Creature);
	void DrawDeathOverlay();

	FAedirHudBarState BarStates[3];
	float CrosshairCurrentGap = -1.f;
};
