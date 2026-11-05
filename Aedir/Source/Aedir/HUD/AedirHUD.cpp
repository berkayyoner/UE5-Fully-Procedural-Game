#include "HUD/AedirHUD.h"

#include "HUD/AedirMarkerSubsystem.h"
#include "CanvasItem.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "UI/AedirJournalSubsystem.h"

namespace
{
	constexpr float HudCrosshairFullSpeed = 600.f;
	constexpr float HudCrosshairMinGapScale = 0.6f;
	constexpr float HudCrosshairMaxGapScale = 1.6f;
	constexpr float HudCrosshairCombatExtraGap = 2.f;
	constexpr float HudCrosshairGapInterpSpeed = 10.f;
	constexpr float HudBarFullThreshold = 0.999f;
	constexpr float HudBarVisibleThreshold = 0.01f;
	constexpr float HudBarTrailSpeed = 0.4f;
	constexpr float HudBarCenterFractions[3] = { 0.22f, 0.5f, 0.78f };
	const TCHAR* const HudBarLabels[3] = { TEXT("HP"), TEXT("MP"), TEXT("SP") };
	constexpr float HudMarkerClampAlpha = 0.45f;
	constexpr float HudMetersPerUnit = 0.01f;
	constexpr float HudCompassMarkerRadius = 6.f;
	constexpr float HudCompassArrowSize = 7.f;
	constexpr float HudDeathOverlayAlpha = 0.6f;
	constexpr float HudDeathTextScale = 3.f;
	const FLinearColor HudMissionColor(1.f, 0.78f, 0.2f, 1.f);
	const FLinearColor HudNorthColor(1.f, 0.2f, 0.2f, 1.f);
	constexpr float HudNotificationLifetime = 4.f;
	constexpr float HudNotificationFadeIn = 0.3f;
	constexpr float HudNotificationFadeOut = 0.7f;
	constexpr float HudNotificationBottomOffset = 30.f;
	constexpr float HudNotificationSpacing = 6.f;

	struct FHudCompassLabel
	{
		const TCHAR* Text;
		float Bearing;
		bool bPole;
	};

	const FHudCompassLabel HudCompassLabels[] =
	{
		{ TEXT("N"), 0.f, true },
		{ TEXT("NE"), 45.f, false },
		{ TEXT("E"), 90.f, true },
		{ TEXT("SE"), 135.f, false },
		{ TEXT("S"), 180.f, true },
		{ TEXT("SW"), 225.f, false },
		{ TEXT("W"), 270.f, true },
		{ TEXT("NW"), 315.f, false }
	};

	float HudNormalizeDegrees(float Degrees)
	{
		float Result = FMath::Fmod(Degrees, 360.f);
		if (Result < 0.f)
		{
			Result += 360.f;
		}
		return Result;
	}

	// Maps any angle to the range [-180, 180).
	float HudSignedDegrees(float Degrees)
	{
		return HudNormalizeDegrees(Degrees + 180.f) - 180.f;
	}

	FLinearColor HudWithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}

	void HudDrawTriangle(UCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
	{
		FCanvasTriangleItem TriangleItem(A, B, C, GWhiteTexture);
		TriangleItem.SetColor(Color);
		Canvas->DrawItem(TriangleItem);
	}

	// Fills a convex polygon as a triangle fan around the first point.
	void HudFillPolygon(UCanvas* Canvas, const TArray<FVector2D>& Points, const FLinearColor& Color)
	{
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			HudDrawTriangle(Canvas, Points[0], Points[Index], Points[Index + 1], Color);
		}
	}

	TArray<FVector2D> HudRegularPolygon(const FVector2D& Center, float Radius, int32 Sides, float StartDegrees)
	{
		TArray<FVector2D> Points;
		Points.Reserve(Sides);
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees + 360.f * Index / Sides);
			Points.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}
}

void AAedirHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float DeltaSeconds = World ? World->GetDeltaSeconds() : 0.f;

	APawn* Pawn = GetOwningPawn();
	AAedirCreature* Creature = Cast<AAedirCreature>(Pawn);
	const UAedirStatsComponent* Stats = Creature ? Creature->Stats.Get() : nullptr;

	DrawCrosshair(DeltaSeconds, Creature);
	DrawCompass(Pawn);
	DrawBars(DeltaSeconds, Stats);
	UpdateSkillNotifications(DeltaSeconds, Stats);
	DrawSkillNotifications();
	DrawPrompt(Creature);

	if (Stats && Stats->IsDead())
	{
		DrawDeathOverlay();
	}
}

void AAedirHUD::DrawCrosshair(float DeltaSeconds, const AAedirCreature* Creature)
{
	const float Speed = Creature ? static_cast<float>(Creature->GetVelocity().Size2D()) : 0.f;
	const float MoveAlpha = FMath::Clamp(Speed / HudCrosshairFullSpeed, 0.f, 1.f);
	const UAedirCombatComponent* Combat = Creature ? Creature->Combat.Get() : nullptr;
	const bool bCombatActive = Combat && (Combat->IsInFightStance() || Combat->LeftSwing.bActive || Combat->RightSwing.bActive);

	const float TargetGap = CrosshairGap * FMath::Lerp(HudCrosshairMinGapScale, HudCrosshairMaxGapScale, MoveAlpha)
		+ (bCombatActive ? HudCrosshairCombatExtraGap : 0.f);

	if (CrosshairCurrentGap < 0.f)
	{
		CrosshairCurrentGap = TargetGap;
	}
	CrosshairCurrentGap = FMath::FInterpTo(CrosshairCurrentGap, TargetGap, DeltaSeconds, HudCrosshairGapInterpSpeed);

	const float CenterX = static_cast<float>(Canvas->SizeX) * 0.5f;
	const float CenterY = static_cast<float>(Canvas->SizeY) * 0.5f;
	const float Gap = CrosshairCurrentGap;
	const float Size = CrosshairSize;
	const float Thickness = CrosshairThickness;

	DrawLine(CenterX - Gap - Size, CenterY, CenterX - Gap, CenterY, CrosshairColor, Thickness);
	DrawLine(CenterX + Gap, CenterY, CenterX + Gap + Size, CenterY, CrosshairColor, Thickness);
	DrawLine(CenterX, CenterY - Gap - Size, CenterX, CenterY - Gap, CrosshairColor, Thickness);
	DrawLine(CenterX, CenterY + Gap, CenterX, CenterY + Gap + Size, CrosshairColor, Thickness);
	DrawRect(CrosshairColor, CenterX - Thickness * 0.5f, CenterY - Thickness * 0.5f, Thickness, Thickness);
}

void AAedirHUD::DrawCompass(const APawn* Pawn)
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!Canvas || !PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	const float Heading = HudNormalizeDegrees(PlayerController->PlayerCameraManager->GetCameraRotation().Yaw);
	const float HalfFov = FMath::Max(1.f, CompassFovDegrees * 0.5f);
	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float StripWidth = ScreenWidth * CompassWidthFraction;
	const float HalfStripWidth = StripWidth * 0.5f;
	const float StripLeft = (ScreenWidth - StripWidth) * 0.5f;
	const float StripRight = StripLeft + StripWidth;
	const float StripTop = CompassTopMargin;
	const float StripBottom = StripTop + CompassHeight;
	const float CenterX = ScreenWidth * 0.5f;
	const float CenterY = StripTop + CompassHeight * 0.5f;
	UFont* SmallFont = UEngine::GetSmallFont();
	UFont* MediumFont = UEngine::GetMediumFont();

	const auto BearingToScreenX = [CenterX, HalfStripWidth, HalfFov](float RelativeDegrees)
	{
		return CenterX + RelativeDegrees / HalfFov * HalfStripWidth;
	};

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), StripLeft, StripTop, StripWidth, CompassHeight);

	for (int32 Step = 0; Step < 24; ++Step)
	{
		const float Relative = HudSignedDegrees(Step * 15.f - Heading);
		if (FMath::Abs(Relative) > HalfFov)
		{
			continue;
		}

		const bool bMajor = Step % 3 == 0;
		const float X = BearingToScreenX(Relative);
		const float TickTop = StripBottom - (bMajor ? 12.f : 6.f);
		DrawLine(X, TickTop, X, StripBottom, FLinearColor(1.f, 1.f, 1.f, bMajor ? 0.8f : 0.45f), 1.f);
	}

	for (const FHudCompassLabel& Label : HudCompassLabels)
	{
		const float Relative = HudSignedDegrees(Label.Bearing - Heading);
		if (FMath::Abs(Relative) > HalfFov)
		{
			continue;
		}

		UFont* Font = Label.bPole ? MediumFont : SmallFont;
		const FString Text(Label.Text);
		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(Text, TextWidth, TextHeight, Font);

		const FLinearColor TextColor = FMath::IsNearlyZero(Label.Bearing) ? HudNorthColor : FLinearColor(1.f, 1.f, 1.f, 0.9f);
		DrawText(Text, TextColor, BearingToScreenX(Relative) - TextWidth * 0.5f, StripTop + 2.f, Font);
	}

	HudDrawTriangle(Canvas,
		FVector2D(CenterX, StripBottom + 1.f),
		FVector2D(CenterX - 5.f, StripBottom + 8.f),
		FVector2D(CenterX + 5.f, StripBottom + 8.f),
		FLinearColor(1.f, 1.f, 1.f, 0.9f));

	const FString HeadingText = FString::Printf(TEXT("%03d"), FMath::RoundToInt(Heading) % 360);
	float HeadingWidth = 0.f;
	float HeadingHeight = 0.f;
	GetTextSize(HeadingText, HeadingWidth, HeadingHeight, SmallFont);
	DrawText(HeadingText, FLinearColor(1.f, 1.f, 1.f, 0.9f), CenterX - HeadingWidth * 0.5f, StripBottom + 10.f, SmallFont);

	UWorld* World = GetWorld();
	UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers || !Pawn)
	{
		return;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	for (const FAedirMapMarker& Marker : Markers->GetMarkers())
	{
		const FVector Delta = Marker.Location - PawnLocation;
		const float Bearing = HudNormalizeDegrees(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X))));
		const float Relative = HudSignedDegrees(Bearing - Heading);
		const FLinearColor MarkerColor = Marker.Type == EAedirMarkerType::Mission ? HudMissionColor : Marker.Color;

		if (FMath::Abs(Relative) > HalfFov)
		{
			const bool bRight = Relative > 0.f;
			const float EdgeX = bRight ? StripRight - 2.f : StripLeft + 2.f;
			const float BaseX = bRight ? StripRight - HudCompassArrowSize - 2.f : StripLeft + HudCompassArrowSize + 2.f;
			HudDrawTriangle(Canvas,
				FVector2D(EdgeX, CenterY),
				FVector2D(BaseX, CenterY - 5.f),
				FVector2D(BaseX, CenterY + 5.f),
				HudWithAlpha(MarkerColor, HudMarkerClampAlpha));
			continue;
		}

		const float X = BearingToScreenX(Relative);
		if (Marker.Type == EAedirMarkerType::Mission)
		{
			HudFillPolygon(Canvas, HudRegularPolygon(FVector2D(X, CenterY), HudCompassMarkerRadius, 4, -90.f), MarkerColor);
		}
		else if (Marker.Type == EAedirMarkerType::Area)
		{
			HudFillPolygon(Canvas, HudRegularPolygon(FVector2D(X, CenterY), HudCompassMarkerRadius, 6, -90.f), MarkerColor);
		}
		else
		{
			DrawRect(MarkerColor, X - 3.f, CenterY - 3.f, 6.f, 6.f);
		}

		const FString LabelText = Marker.Label.ToString();
		float LabelWidth = 0.f;
		float LabelHeight = 0.f;
		GetTextSize(LabelText, LabelWidth, LabelHeight, SmallFont);
		DrawText(LabelText, MarkerColor, X - LabelWidth * 0.5f, StripBottom + 16.f, SmallFont);

		if (Marker.bShowDistance)
		{
			const FString DistanceText = FString::Printf(TEXT("%.0f m"), Delta.Size2D() * HudMetersPerUnit);
			float DistanceWidth = 0.f;
			float DistanceHeight = 0.f;
			GetTextSize(DistanceText, DistanceWidth, DistanceHeight, SmallFont);
			DrawText(DistanceText, FLinearColor(1.f, 1.f, 1.f, 0.85f), X - DistanceWidth * 0.5f, StripBottom + 16.f + LabelHeight, SmallFont);
		}
	}
}

void AAedirHUD::DrawBars(float DeltaSeconds, const UAedirStatsComponent* Stats)
{
	if (!Canvas || !Stats)
	{
		return;
	}

	const float Values[3] = { Stats->GetHealth(), Stats->GetMana(), Stats->GetStamina() };
	const float Maxima[3] = { Stats->GetMaxHealth(), Stats->GetMaxMana(), Stats->GetMaxStamina() };
	const FLinearColor BarColors[3] = { HealthBarColor, ManaBarColor, StaminaBarColor };
	UFont* SmallFont = UEngine::GetSmallFont();
	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float BarTop = static_cast<float>(Canvas->SizeY) - BarBottomMargin - BarHeight;

	for (int32 Index = 0; Index < 3; ++Index)
	{
		FAedirHudBarState& State = BarStates[Index];
		const float Fraction = Maxima[Index] > 0.f ? FMath::Clamp(Values[Index] / Maxima[Index], 0.f, 1.f) : 0.f;
		const bool bFull = Fraction >= HudBarFullThreshold;

		if (bFull)
		{
			State.FullTime = FMath::Min(State.FullTime + DeltaSeconds, BarFadeOutDelay);
		}
		else
		{
			State.FullTime = 0.f;
		}

		const float TargetAlpha = (!bFull || State.FullTime < BarFadeOutDelay) ? 1.f : 0.f;
		State.Alpha = FMath::FInterpConstantTo(State.Alpha, TargetAlpha, DeltaSeconds, TargetAlpha > State.Alpha ? BarFadeInSpeed : BarFadeOutSpeed);

		if (State.Trail < 0.f || Fraction > State.Trail)
		{
			State.Trail = Fraction;
		}
		else
		{
			State.Trail = FMath::FInterpConstantTo(State.Trail, Fraction, DeltaSeconds, HudBarTrailSpeed);
		}

		if (State.Alpha < HudBarVisibleThreshold)
		{
			continue;
		}

		const float A = State.Alpha;
		const float BarLeft = ScreenWidth * HudBarCenterFractions[Index] - BarWidth * 0.5f;
		const FLinearColor Base = BarColors[Index];

		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * A), BarLeft - 2.f, BarTop - 2.f, BarWidth + 4.f, BarHeight + 4.f);
		DrawRect(FLinearColor(0.02f, 0.02f, 0.02f, 0.7f * A), BarLeft, BarTop, BarWidth, BarHeight);
		DrawRect(FLinearColor(Base.R * 0.5f + 0.5f, Base.G * 0.5f + 0.5f, Base.B * 0.5f + 0.5f, 0.8f * A), BarLeft, BarTop, BarWidth * State.Trail, BarHeight);
		DrawRect(FLinearColor(Base.R, Base.G, Base.B, Base.A * A), BarLeft, BarTop, BarWidth * Fraction, BarHeight);

		const FLinearColor BorderColor(1.f, 1.f, 1.f, 0.35f * A);
		const float BorderLeft = BarLeft - 1.f;
		const float BorderRight = BarLeft + BarWidth;
		const float BorderTop = BarTop - 1.f;
		const float BorderBottom = BarTop + BarHeight;
		DrawLine(BorderLeft, BorderTop, BorderRight, BorderTop, BorderColor, 1.f);
		DrawLine(BorderLeft, BorderBottom, BorderRight, BorderBottom, BorderColor, 1.f);
		DrawLine(BorderLeft, BorderTop, BorderLeft, BorderBottom, BorderColor, 1.f);
		DrawLine(BorderRight, BorderTop, BorderRight, BorderBottom, BorderColor, 1.f);

		const FString NumericText = FString::Printf(TEXT("%d/%d"), FMath::RoundToInt(Values[Index]), FMath::RoundToInt(Maxima[Index]));
		float NumericWidth = 0.f;
		float NumericHeight = 0.f;
		GetTextSize(NumericText, NumericWidth, NumericHeight, SmallFont);
		const float TextY = BarTop - NumericHeight - 3.f;
		const FLinearColor TextColor(1.f, 1.f, 1.f, 0.9f * A);
		DrawText(FString(HudBarLabels[Index]), TextColor, BarLeft, TextY, SmallFont);
		DrawText(NumericText, TextColor, BarLeft + BarWidth - NumericWidth, TextY, SmallFont);
	}
}

void AAedirHUD::UpdateSkillNotifications(float DeltaSeconds, const UAedirStatsComponent* Stats)
{
	if (!Stats)
	{
		NotificationStats.Reset();
		LastSkillLevels.Reset();
		SkillNotifications.Reset();
		return;
	}

	if (NotificationStats.Get() != Stats)
	{
		NotificationStats = Stats;
		LastSkillLevels.Reset();
		SkillNotifications.Reset();
	}

	const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
	const bool bInitialized = LastSkillLevels.Num() == SkillCount;
	if (!bInitialized)
	{
		LastSkillLevels.SetNumZeroed(SkillCount);
	}

	for (int32 Index = 0; Index < SkillCount; ++Index)
	{
		const EAedirSkill Skill = static_cast<EAedirSkill>(Index);
		const int32 Level = FMath::FloorToInt(Stats->GetSkill(Skill));
		if (bInitialized && Level > LastSkillLevels[Index])
		{
			FAedirHudNotification Notification;
			Notification.Text = FString::Printf(TEXT("%s increased to %d"), *UAedirJournalSubsystem::GetSkillDisplayName(Skill), Level);
			SkillNotifications.Add(Notification);
		}
		LastSkillLevels[Index] = Level;
	}

	for (FAedirHudNotification& Notification : SkillNotifications)
	{
		Notification.Age += DeltaSeconds;
	}
	SkillNotifications.RemoveAll([](const FAedirHudNotification& Notification)
	{
		return Notification.Age >= HudNotificationLifetime;
	});
}

void AAedirHUD::DrawSkillNotifications()
{
	if (!Canvas || SkillNotifications.IsEmpty())
	{
		return;
	}

	UFont* SmallFont = UEngine::GetSmallFont();
	const float ScreenHeight = static_cast<float>(Canvas->SizeY);
	const float BarTop = ScreenHeight - BarBottomMargin - BarHeight;
	const float X = static_cast<float>(Canvas->SizeX) * HudBarCenterFractions[0] - BarWidth * 0.5f;
	float Y = BarTop - HudNotificationBottomOffset;

	// Newest notification sits lowest, older ones stack upward.
	for (int32 Index = SkillNotifications.Num() - 1; Index >= 0; --Index)
	{
		const FAedirHudNotification& Notification = SkillNotifications[Index];
		const float FadeIn = FMath::Clamp(Notification.Age / HudNotificationFadeIn, 0.f, 1.f);
		const float FadeOut = FMath::Clamp((HudNotificationLifetime - Notification.Age) / HudNotificationFadeOut, 0.f, 1.f);
		const float Alpha = FMath::Min(FadeIn, FadeOut);

		float TextWidth = 0.f;
		float TextHeight = 0.f;
		GetTextSize(Notification.Text, TextWidth, TextHeight, SmallFont);

		DrawText(Notification.Text, FLinearColor(1.f, 1.f, 1.f, 0.95f * Alpha), X, Y - TextHeight, SmallFont);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.4f * Alpha), X, Y + 1.f, TextWidth, 1.f);

		Y -= TextHeight + HudNotificationSpacing;
	}
}

void AAedirHUD::DrawPrompt(const AAedirCreature* Creature)
{
	const UAedirInteractionComponent* Interaction = Creature ? Creature->Interaction.Get() : nullptr;
	if (!Canvas || !Interaction)
	{
		return;
	}

	const AAedirItemActor* Item = Interaction->GetHighlightedItem();
	if (!IsValid(Item))
	{
		return;
	}

	const FString PromptText = FString::Printf(TEXT("[Q] / [E]  %s"), *Item->ItemName.ToString());
	UFont* MediumFont = UEngine::GetMediumFont();
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(PromptText, TextWidth, TextHeight, MediumFont);

	const float CenterX = static_cast<float>(Canvas->SizeX) * 0.5f;
	const float CenterY = static_cast<float>(Canvas->SizeY) * 0.5f;
	const float X = CenterX - TextWidth * 0.5f;
	const float Y = CenterY + FMath::Max(CrosshairCurrentGap, CrosshairGap) + CrosshairSize + 10.f;

	DrawText(PromptText, FLinearColor(0.f, 0.f, 0.f, 0.8f), X + 1.f, Y + 1.f, MediumFont);
	DrawText(PromptText, FLinearColor(1.f, 1.f, 1.f, 0.95f), X, Y, MediumFont);
}

void AAedirHUD::DrawDeathOverlay()
{
	if (!Canvas)
	{
		return;
	}

	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float ScreenHeight = static_cast<float>(Canvas->SizeY);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, HudDeathOverlayAlpha), 0.f, 0.f, ScreenWidth, ScreenHeight);

	const FString DeathText = TEXT("YOU DIED");
	UFont* MediumFont = UEngine::GetMediumFont();
	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(DeathText, TextWidth, TextHeight, MediumFont, HudDeathTextScale);
	DrawText(DeathText, FLinearColor(0.85f, 0.05f, 0.05f, 1.f), ScreenWidth * 0.5f - TextWidth * 0.5f, ScreenHeight * 0.5f - TextHeight * 0.5f, MediumFont, HudDeathTextScale);
}
