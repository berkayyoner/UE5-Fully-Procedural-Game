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

	// Fraction of the screen width used by the compass strip. The sizes below are for a 1080p screen and scale with its height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassWidthFraction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassTopMargin = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassHeight = 22.f;

	// Visible heading range of the compass in degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float CompassFovDegrees = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarWidth = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarHeight = 13.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarBottomMargin = 46.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeInSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeOutSpeed = 1.5f;

	// Seconds a stat must stay full before its bar fades out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	float BarFadeOutDelay = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor HealthBarColor = FLinearColor(0.34f, 0.012f, 0.01f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor ManaBarColor = FLinearColor(0.025f, 0.1f, 0.42f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|HUD")
	FLinearColor StaminaBarColor = FLinearColor(0.035f, 0.3f, 0.065f, 1.f);

private:
	struct FAedirHudBarState
	{
		float Alpha = 0.f;
		// Lagging segment that shows recent loss. Negative means not initialized yet.
		float Trail = -1.f;
		// Seconds the stat has been full. Starts saturated so bars stay hidden until a stat drops.
		float FullTime = 1000.f;
	};

	void DrawCompass(const APawn* Pawn);
	void DrawBars(float DeltaSeconds, const UAedirStatsComponent* Stats);
	void DrawPrompt(const AAedirCreature* Creature);
	void DrawLookTarget(const AAedirCreature* Creature);
	void DrawPointMarker(const AAedirCreature* Creature);
	void DrawConsoleSelection(const AAedirCreature* Creature);
	void DrawNeeds(const UAedirStatsComponent* Stats);
	void DrawDeathOverlay();
	void DrawFatigue(const UAedirStatsComponent* Stats);
	float FatigueShown = 0.f;
	void UpdateSkillNotifications(float DeltaSeconds, const UAedirStatsComponent* Stats);
	void DrawSkillNotifications();

	struct FAedirHudNotification
	{
		FString Text;
		float Age = 0.f;
	};

	FAedirHudBarState BarStates[3];
	TArray<FAedirHudNotification> SkillNotifications;
	TArray<int32> LastSkillLevels;
	TWeakObjectPtr<const UAedirStatsComponent> NotificationStats;
};
