#include "HUD/AedirHUD.h"

#include "Settings/AedirSettingsSubsystem.h"
#include "HUD/AedirMarkerSubsystem.h"
#include "CanvasTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirMenuSubsystem.h"
#include "UI/AedirUIStyle.h"

namespace
{
	constexpr float HudPromptLift = 14.f;
	constexpr float HudPromptFontSize = 13.f;
	constexpr float HudBarFullThreshold = 0.999f;
	constexpr float HudBarVisibleThreshold = 0.01f;
	constexpr float HudBarTrailSpeed = 0.4f;
	constexpr float HudBarCenterFractions[3] = { 0.22f, 0.5f, 0.78f };
	constexpr float HudBarTaper = 16.f;
	constexpr float HudMarkerClampAlpha = 0.55f;
	constexpr float HudUndiscoveredAlpha = 0.45f;
	constexpr float HudMetersPerUnit = 0.01f;
	constexpr float HudMarkerLabelDegrees = 7.f;
	constexpr float HudCompassEndFade = 0.26f;
	constexpr float HudCompassTaper = 0.2f;
	constexpr float HudCompassPoleFontSize = 12.f;
	constexpr float HudCompassLabelFontSize = 9.f;
	constexpr float HudDeathOverlayAlpha = 0.72f;
	constexpr float HudDeathFontSize = 46.f;
	constexpr float HudNotificationLifetime = 4.f;
	constexpr float HudNotificationFadeIn = 0.3f;
	constexpr float HudNotificationFadeOut = 0.7f;
	constexpr float HudNotificationTop = 118.f;
	constexpr float HudNotificationSpacing = 42.f;
	constexpr float HudNotificationFontSize = 16.f;
	const FLinearColor HudMissionColor(FColor(226, 190, 112));
	// Out of breath the sight closes in from the edges. The middle stays visible even with nothing left.
	constexpr int32 HudFatigueCells = 28;
	constexpr float HudFatigueEdgeAlpha = 0.98f;
	constexpr float HudFatigueCenterAlpha = 0.45f;
	constexpr float HudFatigueClearStart = 0.9f;
	constexpr float HudFatigueClearEnd = 0.12f;
	constexpr float HudFatigueBlackAt = 0.95f;
	constexpr float HudFatiguePulseShare = 0.05f;
	constexpr float HudFatiguePulseSpeed = 2.4f;

	struct FHudCompassPole
	{
		const TCHAR* Text;
		float Bearing;
	};

	const FHudCompassPole HudCompassPoles[] =
	{
		{ TEXT("N"), 0.f },
		{ TEXT("E"), 90.f },
		{ TEXT("S"), 180.f },
		{ TEXT("W"), 270.f }
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

	// Full in the middle of the strip, nothing at its ends. Position runs from -1 to 1.
	float HudCompassFade(float Position)
	{
		return FMath::SmoothStep(0.f, HudCompassEndFade, 1.f - FMath::Abs(Position));
	}

	// Half height of the compass strip at a position from -1 to 1: full in the middle, narrowing towards the ends.
	float HudCompassHalfHeight(float Position, float FullHalfHeight)
	{
		return FullHalfHeight * FMath::Lerp(0.12f, 1.f, FMath::SmoothStep(0.f, HudCompassTaper, 1.f - FMath::Abs(Position)));
	}

	// Backing strip and its thin metal edge lines, built from columns so the ends taper and fade together.
	void HudAddCompassStrip(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Scale)
	{
		using namespace AedirUIStyle;

		constexpr int32 Columns = 36;
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const float PositionA = -1.f + 2.f * Column / Columns;
			const float PositionB = -1.f + 2.f * (Column + 1) / Columns;
			const float FadeA = HudCompassFade(PositionA);
			const float FadeB = HudCompassFade(PositionB);
			const float HeightA = HudCompassHalfHeight(PositionA, HalfHeight);
			const float HeightB = HudCompassHalfHeight(PositionB, HalfHeight);
			const float XA = Center.X + PositionA * HalfWidth;
			const float XB = Center.X + PositionB * HalfWidth;

			const FLinearColor MiddleA(0.f, 0.f, 0.f, 0.62f * FadeA);
			const FLinearColor MiddleB(0.f, 0.f, 0.f, 0.62f * FadeB);
			const FLinearColor EdgeA(0.f, 0.f, 0.f, 0.42f * FadeA);
			const FLinearColor EdgeB(0.f, 0.f, 0.f, 0.42f * FadeB);

			Mesh.AddQuad(FVector2f(XA, Center.Y - HeightA), FVector2f(XB, Center.Y - HeightB), FVector2f(XB, Center.Y), FVector2f(XA, Center.Y), EdgeA, EdgeB, MiddleB, MiddleA);
			Mesh.AddQuad(FVector2f(XA, Center.Y), FVector2f(XB, Center.Y), FVector2f(XB, Center.Y + HeightB), FVector2f(XA, Center.Y + HeightA), MiddleA, MiddleB, EdgeB, EdgeA);

			const FLinearColor LineA = WithAlpha(Metal, 0.8f * FadeA);
			const FLinearColor LineB = WithAlpha(Metal, 0.8f * FadeB);
			const float LineThickness = FMath::Max(1.f, Scale);
			Mesh.AddLine(FVector2f(XA, Center.Y - HeightA), FVector2f(XB, Center.Y - HeightB), LineThickness, LineA, LineB);
			Mesh.AddLine(FVector2f(XA, Center.Y + HeightA), FVector2f(XB, Center.Y + HeightB), LineThickness, LineA, LineB);
		}
	}

	// Small metal ornament that closes one end of the strip. Side is -1 for the left end and 1 for the right.
	void HudAddCompassCap(FAedirUIMesh& Mesh, const FVector2f& Tip, float Side, float Scale)
	{
		using namespace AedirUIStyle;

		const FLinearColor Solid = WithAlpha(Metal, 0.95f);
		const FLinearColor Soft = WithAlpha(Metal, 0.5f);
		AddDiamond(Mesh, Tip, 3.2f * Scale, 6.f * Scale, Solid);
		AddDiamondOutline(Mesh, Tip, 6.5f * Scale, 10.f * Scale, Scale, Soft);
		AddDiamond(Mesh, Tip + FVector2f(Side * 11.f * Scale, 0.f), 3.5f * Scale, 1.6f * Scale, Solid);

		// Two barbs curling back towards the strip.
		const FVector2f BarbRoot = Tip - FVector2f(Side * 7.f * Scale, 0.f);
		for (const float Vertical : { -1.f, 1.f })
		{
			const FVector2f BarbEnd = BarbRoot + FVector2f(-Side * 15.f * Scale, Vertical * 7.f * Scale);
			Mesh.AddLine(BarbRoot, BarbEnd, Scale, Solid, WithAlpha(Metal, 0.f));
		}
	}

	// Arrow pointing down at the strip for quests.
	void HudAddQuestArrow(FAedirUIMesh& Mesh, const FVector2f& Tip, float Scale, const FLinearColor& Color)
	{
		const FVector2f ShadowOffset(0.f, 1.5f * Scale);
		const FVector2f Left = Tip + FVector2f(-7.f, -11.f) * Scale;
		const FVector2f Right = Tip + FVector2f(7.f, -11.f) * Scale;
		const FVector2f Notch = Tip + FVector2f(0.f, -7.f) * Scale;
		const FVector2f ShadowLeft[] = { Left + ShadowOffset, Notch + ShadowOffset, Tip + ShadowOffset };
		const FVector2f ShadowRight[] = { Notch + ShadowOffset, Right + ShadowOffset, Tip + ShadowOffset };
		const FLinearColor Shadow(0.f, 0.f, 0.f, 0.6f * Color.A);
		Mesh.AddConvex(ShadowLeft, Shadow);
		Mesh.AddConvex(ShadowRight, Shadow);
		const FVector2f LeftPoints[] = { Left, Notch, Tip };
		const FVector2f RightPoints[] = { Notch, Right, Tip };
		Mesh.AddConvex(LeftPoints, Color);
		Mesh.AddConvex(RightPoints, AedirUIStyle::WithAlpha(Color, 0.72f));
	}
}

void AAedirHUD::DrawFatigue(const UAedirStatsComponent* Stats)
{
	using namespace AedirUIStyle;

	const float Fatigue = Stats && !Stats->IsDead() ? Stats->GetFatigue() : 0.f;
	if (!Canvas || !Canvas->Canvas || Fatigue <= 0.f)
	{
		return;
	}

	const FVector2f Size(static_cast<float>(Canvas->SizeX), static_cast<float>(Canvas->SizeY));
	const UWorld* World = GetWorld();
	const float Pulse = FMath::Sin((World ? World->GetTimeSeconds() : 0.f) * HudFatiguePulseSpeed) * HudFatiguePulseShare * Fatigue;
	const float Clear = FMath::Lerp(HudFatigueClearStart, HudFatigueClearEnd, Fatigue) + Pulse;
	const float EdgeAlpha = HudFatigueEdgeAlpha * FMath::SmoothStep(0.f, 0.2f, Fatigue);
	const float CenterAlpha = HudFatigueCenterAlpha * Fatigue * Fatigue;
	const auto ColorAt = [&](int32 CellX, int32 CellY)
	{
		const FVector2f Offset(CellX * 2.f / HudFatigueCells - 1.f, CellY * 2.f / HudFatigueCells - 1.f);
		const float Dark = FMath::SmoothStep(Clear, FMath::Max(HudFatigueBlackAt, Clear + 0.3f), Offset.Size());
		return FLinearColor(0.f, 0.f, 0.f, FMath::Lerp(CenterAlpha, EdgeAlpha, Dark));
	};

	FAedirUIMesh Mesh(1.f);
	for (int32 CellY = 0; CellY < HudFatigueCells; ++CellY)
	{
		for (int32 CellX = 0; CellX < HudFatigueCells; ++CellX)
		{
			const FVector2f Min(Size.X * CellX / HudFatigueCells, Size.Y * CellY / HudFatigueCells);
			const FVector2f Max(Size.X * (CellX + 1) / HudFatigueCells, Size.Y * (CellY + 1) / HudFatigueCells);
			Mesh.AddGradientRect(Min, Max, ColorAt(CellX, CellY), ColorAt(CellX + 1, CellY), ColorAt(CellX + 1, CellY + 1), ColorAt(CellX, CellY + 1));
		}
	}
	DrawMesh(*Canvas->Canvas, Mesh);
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

	// Menus cover the screen on their own, like in the games the HUD steps aside while one is open.
	const APlayerController* PlayerController = GetOwningPlayerController();
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	const UAedirMenuSubsystem* Menus = LocalPlayer ? LocalPlayer->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
	const bool bMenuOpen = Menus && (Menus->IsMenuOpen() || Menus->IsCharacterCreationOpen());

	UpdateSkillNotifications(DeltaSeconds, Stats);
	if (bMenuOpen)
	{
		return;
	}

	DrawFatigue(Stats);

	const FAedirGameplaySettings& Shown = UAedirSettingsSubsystem::GetValues();
	if (Shown.bShowCompass)
	{
		DrawCompass(Pawn);
	}
	if (Shown.bShowBars)
	{
		DrawBars(DeltaSeconds, Stats);
	}
	DrawSkillNotifications();
	DrawPrompt(Creature);

	if (Stats && Stats->IsDead())
	{
		DrawDeathOverlay();
	}
}

void AAedirHUD::DrawCompass(const APawn* Pawn)
{
	using namespace AedirUIStyle;

	APlayerController* PlayerController = GetOwningPlayerController();
	if (!Canvas || !Canvas->Canvas || !PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	const float Scale = ScaleForHeight(static_cast<float>(Canvas->SizeY));
	const float Heading = HudNormalizeDegrees(PlayerController->PlayerCameraManager->GetCameraRotation().Yaw);
	const float HalfFov = FMath::Max(1.f, CompassFovDegrees * 0.5f);
	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float HalfWidth = ScreenWidth * CompassWidthFraction * 0.5f;
	const float HalfHeight = CompassHeight * 0.5f * Scale;
	const FVector2f Center(ScreenWidth * 0.5f, (CompassTopMargin + CompassHeight * 0.5f) * Scale);
	const float StripBottom = Center.Y + HalfHeight;

	FAedirUIMesh Mesh(1.f);
	HudAddCompassStrip(Mesh, Center, HalfWidth, HalfHeight, Scale);
	HudAddCompassCap(Mesh, Center - FVector2f(HalfWidth, 0.f), -1.f, Scale);
	HudAddCompassCap(Mesh, Center + FVector2f(HalfWidth, 0.f), 1.f, Scale);

	// Heading pointer in the middle of the upper edge.
	const FVector2f PointerTip(Center.X, Center.Y - HalfHeight + 4.f * Scale);
	const FVector2f PointerPoints[] = { PointerTip, FVector2f(Center.X - 3.5f * Scale, PointerTip.Y - 4.f * Scale), FVector2f(Center.X + 3.5f * Scale, PointerTip.Y - 4.f * Scale) };
	Mesh.AddConvex(PointerPoints, WithAlpha(Metal, 0.85f));

	for (int32 Step = 0; Step < 24; ++Step)
	{
		const float Relative = HudSignedDegrees(Step * 15.f - Heading);
		if (FMath::Abs(Relative) > HalfFov || Step % 6 == 0)
		{
			continue;
		}

		const float Position = Relative / HalfFov;
		const float Fade = HudCompassFade(Position);
		const float X = Center.X + Position * HalfWidth;
		if (Step % 3 == 0)
		{
			AddDiamond(Mesh, FVector2f(X, Center.Y), 2.6f * Scale, 4.2f * Scale, WithAlpha(Metal, 0.75f * Fade));
		}
		else
		{
			const FVector2f TickBottom(X, Center.Y + HalfHeight * 0.72f);
			const FLinearColor TickColor = WithAlpha(Metal, 0.5f * Fade);
			Mesh.AddLine(TickBottom, TickBottom - FVector2f(0.f, 5.f * Scale), Scale, TickColor, TickColor);
		}
	}

	const FAedirMapMarker* LabelledMarker = nullptr;
	float LabelledRelative = HudMarkerLabelDegrees;
	UWorld* World = GetWorld();
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

	if (Markers && Pawn)
	{
		for (const FAedirMapMarker& Marker : Markers->GetMarkers())
		{
			if (!Marker.bRevealed)
			{
				continue;
			}

			const FVector Delta = Marker.Location - PawnLocation;
			const float Bearing = HudNormalizeDegrees(static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X))));
			const float Relative = HudSignedDegrees(Bearing - Heading);
			const FLinearColor BaseColor = Marker.Type == EAedirMarkerType::Mission ? HudMissionColor : (Marker.Type == EAedirMarkerType::Area ? Ivory : Marker.Color);
			const FLinearColor Color = Marker.bDiscovered ? BaseColor : WithAlpha(BaseColor, HudUndiscoveredAlpha);

			if (FMath::Abs(Relative) > HalfFov * 0.92f)
			{
				// Behind the player: quests rest on the end of the strip they are closest to.
				if (Marker.Type == EAedirMarkerType::Mission)
				{
					const float Side = Relative > 0.f ? 1.f : -1.f;
					AddDiamond(Mesh, FVector2f(Center.X + Side * HalfWidth * 0.9f, Center.Y), 3.5f * Scale, 3.5f * Scale, WithAlpha(Color, HudMarkerClampAlpha));
				}
				continue;
			}

			const FVector2f Spot(Center.X + Relative / HalfFov * HalfWidth, Center.Y);
			if (Marker.Type == EAedirMarkerType::Mission)
			{
				HudAddQuestArrow(Mesh, Spot + FVector2f(0.f, HalfHeight * 0.6f), Scale, Color);
			}
			else if (Marker.Type == EAedirMarkerType::Area)
			{
				AddDiamond(Mesh, Spot, 7.f * Scale, 7.f * Scale, FLinearColor(0.f, 0.f, 0.f, 0.6f * Color.A));
				AddDiamondOutline(Mesh, Spot, 7.f * Scale, 7.f * Scale, 1.2f * Scale, Color);
				if (Marker.bDiscovered)
				{
					AddDiamond(Mesh, Spot, 2.5f * Scale, 2.5f * Scale, Color);
				}
			}
			else
			{
				const FVector2f FlagPoints[] = { Spot + FVector2f(0.f, -7.f) * Scale, Spot + FVector2f(6.f, -3.f) * Scale, Spot + FVector2f(0.f, 1.f) * Scale };
				Mesh.AddConvex(FlagPoints, Color);
				Mesh.AddLine(Spot + FVector2f(0.f, -7.f) * Scale, Spot + FVector2f(0.f, 7.f) * Scale, 1.2f * Scale, Color, Color);
			}

			// Only the marker the player is facing gets its name, otherwise labels pile up under the strip.
			if (FMath::Abs(Relative) < LabelledRelative && !Marker.Label.IsEmpty())
			{
				LabelledRelative = FMath::Abs(Relative);
				LabelledMarker = &Marker;
			}
		}
	}

	DrawMesh(*Canvas->Canvas, Mesh);

	const FSlateFontInfo PoleFont = RegularFont(HudCompassPoleFontSize * Scale);
	const float PoleHeight = MeasureText(TEXT("N"), PoleFont).Y;
	for (const FHudCompassPole& Pole : HudCompassPoles)
	{
		const float Relative = HudSignedDegrees(Pole.Bearing - Heading);
		if (FMath::Abs(Relative) > HalfFov)
		{
			continue;
		}

		const float Position = Relative / HalfFov;
		const FLinearColor Color = WithAlpha(Pole.Bearing == 0.f ? IvoryBright : Ivory, HudCompassFade(Position));
		AedirUIStyle::DrawText(*Canvas, Pole.Text, PoleFont, FVector2f(Center.X + Position * HalfWidth, Center.Y - PoleHeight * 0.5f), Color, EAedirTextAlign::Center, 0.8f);
	}

	if (LabelledMarker)
	{
		const FSlateFontInfo LabelFont = RegularFont(HudCompassLabelFontSize * Scale, 70);
		const FString Text = LabelledMarker->bShowDistance
			? FString::Printf(TEXT("%s   %.0f m"), *LabelledMarker->Label.ToString(), (LabelledMarker->Location - PawnLocation).Size2D() * HudMetersPerUnit)
			: LabelledMarker->Label.ToString();
		AedirUIStyle::DrawText(*Canvas, Text, LabelFont, FVector2f(Center.X, StripBottom + 6.f * Scale), Ivory, EAedirTextAlign::Center, 0.9f);
	}
}

void AAedirHUD::DrawBars(float DeltaSeconds, const UAedirStatsComponent* Stats)
{
	using namespace AedirUIStyle;

	if (!Canvas || !Canvas->Canvas || !Stats)
	{
		return;
	}

	const float Values[3] = { Stats->GetHealth(), Stats->GetMana(), Stats->GetStamina() };
	const float Maxima[3] = { Stats->GetMaxHealth(), Stats->GetMaxMana(), Stats->GetMaxStamina() };
	const FLinearColor BarColors[3] = { HealthBarColor, ManaBarColor, StaminaBarColor };
	const float Scale = ScaleForHeight(static_cast<float>(Canvas->SizeY));
	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float CenterY = static_cast<float>(Canvas->SizeY) - (BarBottomMargin + BarHeight * 0.5f) * Scale;
	const float HalfWidth = BarWidth * 0.5f * Scale;
	const float HalfHeight = BarHeight * 0.5f * Scale;
	const float Taper = HudBarTaper * Scale;

	FAedirUIMesh Mesh(1.f);

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
		const FVector2f Center(ScreenWidth * HudBarCenterFractions[Index], CenterY);
		const FLinearColor Base = BarColors[Index];
		const FLinearColor Bright(FMath::Min(Base.R * 2.4f + 0.02f, 1.f), FMath::Min(Base.G * 2.4f + 0.02f, 1.f), FMath::Min(Base.B * 2.4f + 0.02f, 1.f), A);
		const FLinearColor Dark(Base.R * 0.42f, Base.G * 0.42f, Base.B * 0.42f, A);
		const FLinearColor Core(FMath::Min(Base.R * 3.5f + 0.18f, 1.f), FMath::Min(Base.G * 3.5f + 0.18f, 1.f), FMath::Min(Base.B * 3.5f + 0.18f, 1.f), 1.f);

		const FLinearColor BackEdge(0.f, 0.f, 0.f, 0.5f * A);
		const FLinearColor BackMiddle(0.f, 0.f, 0.f, 0.72f * A);
		AddTaperedBar(Mesh, Center, HalfWidth, HalfHeight, Taper, BackEdge, BackMiddle, BackEdge);

		// Fill and trail keep a small gap to the frame and shrink from both ends towards the centre.
		const float InnerHalfHeight = HalfHeight - 1.5f * Scale;
		const float InnerHalfWidth = HalfWidth - 3.f * Scale;
		const float TrailHalfWidth = InnerHalfWidth * State.Trail;
		const float FillHalfWidth = InnerHalfWidth * Fraction;
		const float InnerTaper = Taper * (InnerHalfHeight / HalfHeight);

		if (State.Trail > Fraction + 0.002f)
		{
			const FLinearColor Lag(Bright.R * 0.8f + 0.1f, Bright.G * 0.8f + 0.1f, Bright.B * 0.8f + 0.1f, 0.42f * A);
			AddTaperedBar(Mesh, Center, TrailHalfWidth, InnerHalfHeight, InnerTaper, Lag, Lag, Lag);
		}
		if (FillHalfWidth > 0.5f)
		{
			AddTaperedBar(Mesh, Center, FillHalfWidth, InnerHalfHeight, InnerTaper, Dark, FLinearColor(Base.R, Base.G, Base.B, A), Dark);

			const float SheenHalf = FMath::Max(0.f, FillHalfWidth - InnerTaper * 0.7f);
			AddFadedLine(Mesh, Center + FVector2f(-SheenHalf, -InnerHalfHeight * 0.45f), Center + FVector2f(SheenHalf, -InnerHalfHeight * 0.45f), FMath::Max(1.f, 2.f * Scale), WithAlpha(Bright, 0.5f), 0.08f, 0.08f);
			AddFadedLine(Mesh, Center - FVector2f(SheenHalf, 0.f), Center + FVector2f(SheenHalf, 0.f), Scale, WithAlpha(Core, 0.42f * A), 0.28f, 0.28f);
		}

		AddTaperedFrame(Mesh, Center, HalfWidth, HalfHeight, Taper, FMath::Max(1.f, Scale), WithAlpha(Metal, 0.95f * A));

		const float EndOffset = HalfWidth + 7.f * Scale;
		for (const float Side : { -1.f, 1.f })
		{
			AddDiamond(Mesh, Center + FVector2f(Side * EndOffset, 0.f), 5.f * Scale, 2.4f * Scale, WithAlpha(Metal, 0.85f * A));
		}
	}

	DrawMesh(*Canvas->Canvas, Mesh);
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
	using namespace AedirUIStyle;

	if (!Canvas || !Canvas->Canvas || SkillNotifications.IsEmpty())
	{
		return;
	}

	const float Scale = ScaleForHeight(static_cast<float>(Canvas->SizeY));
	const FSlateFontInfo Font = LightFont(HudNotificationFontSize * Scale, 60);
	const float CenterX = static_cast<float>(Canvas->SizeX) * 0.5f;
	float Y = HudNotificationTop * Scale;

	// Newest notification sits on top, older ones move down.
	for (int32 Index = SkillNotifications.Num() - 1; Index >= 0; --Index)
	{
		const FAedirHudNotification& Notification = SkillNotifications[Index];
		const float FadeIn = FMath::Clamp(Notification.Age / HudNotificationFadeIn, 0.f, 1.f);
		const float FadeOut = FMath::Clamp((HudNotificationLifetime - Notification.Age) / HudNotificationFadeOut, 0.f, 1.f);
		const float Alpha = FMath::Min(FadeIn, FadeOut);

		const FVector2f TextSize = AedirUIStyle::DrawText(*Canvas, Notification.Text, Font, FVector2f(CenterX, Y), WithAlpha(Ivory, Alpha), EAedirTextAlign::Center, 0.9f);

		FAedirUIMesh Mesh(1.f);
		const FLinearColor RuleColor = WithAlpha(Metal, 0.8f * Alpha);
		const float RuleY = Y + TextSize.Y + 3.f * Scale;
		const float RuleHalf = FMath::Max(TextSize.X * 0.65f, 120.f * Scale);
		AddFadedLine(Mesh, FVector2f(CenterX - RuleHalf, RuleY), FVector2f(CenterX + RuleHalf, RuleY), Scale, RuleColor, 0.45f, 0.45f);
		AddDiamond(Mesh, FVector2f(CenterX, RuleY), 3.f * Scale, 3.f * Scale, RuleColor);
		DrawMesh(*Canvas->Canvas, Mesh);

		Y += HudNotificationSpacing * Scale;
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
	if (!IsValid(Item) || !Item->Mesh)
	{
		return;
	}

	FString Name = Item->ItemName.ToString();
	if (Name.IsEmpty())
	{
		Name = Item->GetClass()->GetName();
	}

	const FBoxSphereBounds& Bounds = Item->Mesh->Bounds;
	const FVector Projected = Canvas->Project(Bounds.Origin + FVector(0.0, 0.0, Bounds.BoxExtent.Z));
	const bool bOnScreen = Projected.Z > 0.0
		&& Projected.X >= 0.0 && Projected.X <= Canvas->SizeX
		&& Projected.Y >= 0.0 && Projected.Y <= Canvas->SizeY;
	if (!bOnScreen)
	{
		return;
	}

	const float Scale = AedirUIStyle::ScaleForHeight(static_cast<float>(Canvas->SizeY));
	const FSlateFontInfo Font = AedirUIStyle::RegularFont(HudPromptFontSize * Scale, 40);
	const float TextHeight = AedirUIStyle::MeasureText(Name, Font).Y;
	const FVector2f Position(static_cast<float>(Projected.X), static_cast<float>(Projected.Y) - TextHeight - HudPromptLift * Scale);
	AedirUIStyle::DrawText(*Canvas, Name, Font, Position, AedirUIStyle::Ivory, EAedirTextAlign::Center, 1.f);
}

void AAedirHUD::DrawDeathOverlay()
{
	using namespace AedirUIStyle;

	if (!Canvas || !Canvas->Canvas)
	{
		return;
	}

	const FVector2f Size(static_cast<float>(Canvas->SizeX), static_cast<float>(Canvas->SizeY));
	const float Scale = ScaleForHeight(Size.Y);

	FAedirUIMesh Mesh(1.f);
	AddVignette(Mesh, Size, HudDeathOverlayAlpha * 0.8f, 1.f);
	AddOrnamentRule(Mesh, FVector2f(Size.X * 0.5f, Size.Y * 0.5f + 44.f * Scale), 300.f * Scale, Metal);
	DrawMesh(*Canvas->Canvas, Mesh);

	const FSlateFontInfo Font = TitleFont(HudDeathFontSize * Scale);
	const float TextHeight = MeasureText(TEXT("YOU DIED"), Font).Y;
	AedirUIStyle::DrawText(*Canvas, TEXT("YOU DIED"), Font, FVector2f(Size.X * 0.5f, Size.Y * 0.5f - TextHeight * 0.5f - 8.f * Scale), FLinearColor(FColor(176, 40, 32)), EAedirTextAlign::Center, 1.f);
}
