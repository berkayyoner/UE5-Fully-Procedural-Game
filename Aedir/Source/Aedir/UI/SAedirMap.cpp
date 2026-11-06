#include "UI/SAedirMap.h"

#include "Creature/AedirCreature.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HUD/AedirMarkerSubsystem.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Travel/AedirTravelLogic.h"
#include "UI/AedirUIStyle.h"

namespace
{
	constexpr float MapPanPixelsPerSecond = 650.f;
	constexpr float MapMinCmPerPixel = 5.f;
	constexpr float MapMaxCmPerPixel = 3000.f;
	constexpr float MapDefaultCmPerPixel = 100.f;
	constexpr float MapZoomStep = 1.18f;
	constexpr float MapMarkerPickRadius = 16.f;
	constexpr float MapCustomRemoveRadius = 16.f;
	constexpr float MapDragThreshold = 5.f;
	constexpr float MapMessageSeconds = 2.5f;
	constexpr float MapFramePadding = 22.f;
	constexpr float MapGridMinPixels = 70.f;
	constexpr int32 MapVignetteSteps = 14;
	constexpr float MapVignetteStepPixels = 16.f;
	constexpr float MapDialogWidth = 480.f;
	constexpr float MapDialogHeight = 176.f;
	constexpr float MapButtonWidth = 150.f;
	constexpr float MapButtonHeight = 44.f;
	constexpr float MapMetersPerCm = 0.01f;

	const FLinearColor MapParchment(0.115f, 0.092f, 0.066f, 1.f);
	const FLinearColor MapGridMinor(0.9f, 0.76f, 0.45f, 0.07f);
	const FLinearColor MapGridMajor(0.9f, 0.76f, 0.45f, 0.16f);
	const FLinearColor MapMissionColor(1.f, 0.78f, 0.2f, 1.f);
	const FLinearColor MapLocationColor(0.92f, 0.86f, 0.7f, 1.f);
	const FLinearColor MapUnknownColor(0.55f, 0.5f, 0.42f, 0.9f);

	const float MapGridSteps[] = { 1000.f, 2500.f, 5000.f, 10000.f, 25000.f, 50000.f, 100000.f, 250000.f, 500000.f };

	float MapMeasureWidth(const FString& Text, const FSlateFontInfo& Font)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return 0.f;
		}
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font).X;
	}

	void MapDrawText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, bool bCenterX)
	{
		const float X = bCenterX ? Position.X - MapMeasureWidth(Text, Font) * 0.5f : Position.X;
		FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToOffsetPaintGeometry(FVector2f(X, Position.Y)), Text, Font, ESlateDrawEffect::None, Color);
	}

	void MapDrawLine(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& A, const FVector2f& B, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>{ A, B }, ESlateDrawEffect::None, Color, true, Thickness);
	}

	void MapDrawBox(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& Position, const FVector2f& Size, const FLinearColor& Color)
	{
		const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), WhiteBrush, ESlateDrawEffect::None, Color);
	}

	void MapDrawFrame(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), Min },
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	void MapDrawPolygon(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float Radius, int32 Sides, float StartDegrees, const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Sides + 1);
		for (int32 Index = 0; Index <= Sides; ++Index)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees + 360.f * Index / Sides);
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	// A filled square turned 45 degrees.
	void MapDrawDiamond(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float HalfDiagonal, const FLinearColor& Color)
	{
		const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		const float Side = HalfDiagonal * 1.4142f;
		FSlateDrawElement::MakeRotatedBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform(Center - FVector2f(Side, Side) * 0.5f)),
			WhiteBrush, ESlateDrawEffect::None, UE_PI * 0.25f, TOptional<FVector2D>(), FSlateDrawElement::RelativeToElement, Color);
	}

	FString MapFormatDistance(float Cm)
	{
		const float Meters = Cm * MapMetersPerCm;
		return Meters >= 1000.f ? FString::Printf(TEXT("%.1f km"), Meters / 1000.f) : FString::Printf(TEXT("%.0f m"), Meters);
	}

	// Rectangles of the confirmation dialog and its two buttons in widget space.
	struct FMapDialogLayout
	{
		FVector2f Position;
		FVector2f Size;
		FVector2f YesPosition;
		FVector2f NoPosition;
	};

	FMapDialogLayout MapGetDialogLayout(const FVector2f& ScreenSize)
	{
		FMapDialogLayout Layout;
		Layout.Size = FVector2f(MapDialogWidth, MapDialogHeight);
		Layout.Position = (ScreenSize - Layout.Size) * 0.5f;
		const float ButtonY = Layout.Position.Y + MapDialogHeight - MapButtonHeight - 24.f;
		Layout.YesPosition = FVector2f(Layout.Position.X + MapDialogWidth * 0.5f - MapButtonWidth - 14.f, ButtonY);
		Layout.NoPosition = FVector2f(Layout.Position.X + MapDialogWidth * 0.5f + 14.f, ButtonY);
		return Layout;
	}

	bool MapInside(const FVector2f& Point, const FVector2f& Position, const FVector2f& Size)
	{
		return Point.X >= Position.X && Point.Y >= Position.Y && Point.X <= Position.X + Size.X && Point.Y <= Position.Y + Size.Y;
	}
}

void SAedirMap::Construct(const FArguments& InArgs)
{
	Creature = InArgs._Creature;
	QueryDelegate = InArgs._OnQueryTravel;
	TravelDelegate = InArgs._OnTravel;
	CloseDelegate = InArgs._OnClose;

	CmPerPixel = MapDefaultCmPerPixel;
	CenterOnPlayer();
}

void SAedirMap::CenterOnPlayer()
{
	if (const AAedirCreature* Pawn = Creature.Get())
	{
		const FVector Location = Pawn->GetActorLocation();
		Center = FVector2D(Location.X, Location.Y);
	}
}

FVector2f SAedirMap::WorldToScreen(const FVector& World, const FVector2f& Size) const
{
	return FVector2f(
		Size.X * 0.5f + static_cast<float>((World.Y - Center.Y) / CmPerPixel),
		Size.Y * 0.5f - static_cast<float>((World.X - Center.X) / CmPerPixel));
}

FVector SAedirMap::WorldFromScreen(const FVector2f& Screen, const FVector2f& Size) const
{
	return FVector(
		Center.X - (Screen.Y - Size.Y * 0.5f) * CmPerPixel,
		Center.Y + (Screen.X - Size.X * 0.5f) * CmPerPixel,
		0.0);
}

int32 SAedirMap::FindMarkerAt(const FVector2f& Screen, const FVector2f& Size) const
{
	const AAedirCreature* Pawn = Creature.Get();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers)
	{
		return INDEX_NONE;
	}

	int32 Best = INDEX_NONE;
	float BestDistance = MapMarkerPickRadius;
	const TArray<FAedirMapMarker>& Entries = Markers->GetMarkers();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FAedirMapMarker& Marker = Entries[Index];
		if (!Marker.bRevealed || Marker.Type == EAedirMarkerType::Custom)
		{
			continue;
		}

		const float Distance = FVector2f::Distance(WorldToScreen(Marker.Location, Size), Screen);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

void SAedirMap::ShowMessage(const FText& Text)
{
	Message = Text;
	MessageRemaining = MapMessageSeconds;
}

void SAedirMap::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	LastSize = AllottedGeometry.GetLocalSize();

	MessageRemaining = FMath::Max(0.f, MessageRemaining - InDeltaTime);

	if (!PendingMarkerId.IsNone())
	{
		return;
	}

	const FVector2D Pan(
		(bPanRight ? 1.0 : 0.0) - (bPanLeft ? 1.0 : 0.0),
		(bPanUp ? 1.0 : 0.0) - (bPanDown ? 1.0 : 0.0));
	if (!Pan.IsZero())
	{
		const double Step = MapPanPixelsPerSecond * CmPerPixel * InDeltaTime;
		// Screen right is east (world Y), screen up is north (world X).
		Center += FVector2D(Pan.Y, Pan.X) * Step;
	}
}

FReply SAedirMap::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (!PendingMarkerId.IsNone())
	{
		if (Key == EKeys::Y || Key == EKeys::Enter)
		{
			ConfirmTravel();
		}
		else if (Key == EKeys::N || Key == EKeys::Escape)
		{
			CancelTravel();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape || Key == EKeys::Tab || Key == EKeys::M)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		bPanUp = true;
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		bPanDown = true;
		return FReply::Handled();
	}
	if (Key == EKeys::A || Key == EKeys::Left)
	{
		bPanLeft = true;
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		bPanRight = true;
		return FReply::Handled();
	}
	if (Key == EKeys::SpaceBar)
	{
		CenterOnPlayer();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SAedirMap::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		bPanUp = false;
	}
	else if (Key == EKeys::S || Key == EKeys::Down)
	{
		bPanDown = false;
	}
	else if (Key == EKeys::A || Key == EKeys::Left)
	{
		bPanLeft = false;
	}
	else if (Key == EKeys::D || Key == EKeys::Right)
	{
		bPanRight = false;
	}
	else
	{
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

void SAedirMap::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bPanUp = bPanDown = bPanLeft = bPanRight = false;
}

FReply SAedirMap::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Size = MyGeometry.GetLocalSize();
	MousePosition = Local;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bLeftDown = true;
		bDragged = false;
		PressPosition = Local;
		return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse).CaptureMouse(AsShared());
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && PendingMarkerId.IsNone())
	{
		ToggleCustomMarker(Local, Size);
	}
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirMap::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bLeftDown)
	{
		return FReply::Handled();
	}

	bLeftDown = false;
	if (!bDragged)
	{
		HandleClick(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MyGeometry.GetLocalSize());
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SAedirMap::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Delta = Local - MousePosition;
	MousePosition = Local;

	if (bLeftDown && PendingMarkerId.IsNone())
	{
		if (!bDragged && FVector2f::Distance(Local, PressPosition) > MapDragThreshold)
		{
			bDragged = true;
		}
		if (bDragged)
		{
			Center.X += Delta.Y * CmPerPixel;
			Center.Y -= Delta.X * CmPerPixel;
		}
	}
	return FReply::Handled();
}

FReply SAedirMap::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!PendingMarkerId.IsNone())
	{
		return FReply::Handled();
	}

	const FVector2f Size = MyGeometry.GetLocalSize();
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	// Zooming keeps the world point under the cursor where it is.
	const FVector Before = WorldFromScreen(Local, Size);
	CmPerPixel = FMath::Clamp(CmPerPixel * FMath::Pow(MapZoomStep, -MouseEvent.GetWheelDelta()), MapMinCmPerPixel, MapMaxCmPerPixel);
	const FVector After = WorldFromScreen(Local, Size);
	Center.X += Before.X - After.X;
	Center.Y += Before.Y - After.Y;
	return FReply::Handled();
}

void SAedirMap::HandleClick(const FVector2f& Screen, const FVector2f& Size)
{
	if (!PendingMarkerId.IsNone())
	{
		const FMapDialogLayout Layout = MapGetDialogLayout(Size);
		const FVector2f ButtonSize(MapButtonWidth, MapButtonHeight);
		if (MapInside(Screen, Layout.YesPosition, ButtonSize))
		{
			ConfirmTravel();
		}
		else if (MapInside(Screen, Layout.NoPosition, ButtonSize))
		{
			CancelTravel();
		}
		return;
	}

	const AAedirCreature* Pawn = Creature.Get();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	const int32 Index = FindMarkerAt(Screen, Size);
	if (!Markers || !Markers->GetMarkers().IsValidIndex(Index))
	{
		return;
	}

	const FAedirMapMarker& Marker = Markers->GetMarkers()[Index];
	if (!AedirTravelLogic::CanFastTravelTo(Marker.Type, Marker.bDiscovered))
	{
		ShowMessage(FText::FromString(TEXT("Not discovered yet")));
		return;
	}

	const FText Reason = QueryDelegate.IsBound() ? QueryDelegate.Execute(Marker.Id) : FText::GetEmpty();
	if (!Reason.IsEmpty())
	{
		ShowMessage(Reason);
		return;
	}

	PendingMarkerId = Marker.Id;
	bPanUp = bPanDown = bPanLeft = bPanRight = false;
}

void SAedirMap::ToggleCustomMarker(const FVector2f& Screen, const FVector2f& Size)
{
	const AAedirCreature* Pawn = Creature.Get();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers)
	{
		return;
	}

	const FAedirMapMarker* Existing = Markers->FindMarker(AedirCustomMarkerId);
	if (Existing && FVector2f::Distance(WorldToScreen(Existing->Location, Size), Screen) <= MapCustomRemoveRadius)
	{
		Markers->RemoveMarker(AedirCustomMarkerId);
		return;
	}

	Markers->SetCustomMarker(WorldFromScreen(Screen, Size));
}

void SAedirMap::ConfirmTravel()
{
	const FName Id = PendingMarkerId;
	PendingMarkerId = NAME_None;
	TravelDelegate.ExecuteIfBound(Id);
}

void SAedirMap::CancelTravel()
{
	PendingMarkerId = NAME_None;
}

int32 SAedirMap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
	const FSlateFontInfo BigFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	const FSlateFontInfo DirectionFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);

	const AAedirCreature* Pawn = Creature.Get();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;

	const int32 BackgroundLayer = LayerId;
	const int32 GridLayer = LayerId + 1;
	const int32 MarkerLayer = LayerId + 3;
	const int32 FrameLayer = LayerId + 6;
	const int32 DialogLayer = LayerId + 10;

	MapDrawBox(OutDrawElements, BackgroundLayer, AllottedGeometry, FVector2f::ZeroVector, Size, MapParchment);

	// Grid in world space, spacing chosen so the lines stay readable at every zoom.
	float GridStep = MapGridSteps[UE_ARRAY_COUNT(MapGridSteps) - 1];
	for (const float Candidate : MapGridSteps)
	{
		if (Candidate / CmPerPixel >= MapGridMinPixels)
		{
			GridStep = Candidate;
			break;
		}
	}

	const FVector TopLeft = WorldFromScreen(FVector2f::ZeroVector, Size);
	const FVector BottomRight = WorldFromScreen(Size, Size);
	const int64 FirstX = static_cast<int64>(FMath::FloorToDouble(BottomRight.X / GridStep));
	const int64 LastX = static_cast<int64>(FMath::CeilToDouble(TopLeft.X / GridStep));
	const int64 FirstY = static_cast<int64>(FMath::FloorToDouble(TopLeft.Y / GridStep));
	const int64 LastY = static_cast<int64>(FMath::CeilToDouble(BottomRight.Y / GridStep));
	for (int64 Line = FirstX; Line <= LastX; ++Line)
	{
		const float Y = WorldToScreen(FVector(Line * GridStep, 0.0, 0.0), Size).Y;
		MapDrawLine(OutDrawElements, GridLayer, AllottedGeometry, FVector2f(0.f, Y), FVector2f(Size.X, Y), Line % 5 == 0 ? MapGridMajor : MapGridMinor, 1.f);
	}
	for (int64 Line = FirstY; Line <= LastY; ++Line)
	{
		const float X = WorldToScreen(FVector(0.0, Line * GridStep, 0.0), Size).X;
		MapDrawLine(OutDrawElements, GridLayer, AllottedGeometry, FVector2f(X, 0.f), FVector2f(X, Size.Y), Line % 5 == 0 ? MapGridMajor : MapGridMinor, 1.f);
	}

	// Darkened edges.
	for (int32 Step = 0; Step < MapVignetteSteps; ++Step)
	{
		const float Fade = 1.f - static_cast<float>(Step) / MapVignetteSteps;
		const FLinearColor Shade(0.f, 0.f, 0.f, 0.5f * Fade * Fade);
		const float Offset = Step * MapVignetteStepPixels;
		MapDrawBox(OutDrawElements, GridLayer + 1, AllottedGeometry, FVector2f(0.f, Offset), FVector2f(Size.X, MapVignetteStepPixels), Shade);
		MapDrawBox(OutDrawElements, GridLayer + 1, AllottedGeometry, FVector2f(0.f, Size.Y - Offset - MapVignetteStepPixels), FVector2f(Size.X, MapVignetteStepPixels), Shade);
		MapDrawBox(OutDrawElements, GridLayer + 1, AllottedGeometry, FVector2f(Offset, 0.f), FVector2f(MapVignetteStepPixels, Size.Y), Shade);
		MapDrawBox(OutDrawElements, GridLayer + 1, AllottedGeometry, FVector2f(Size.X - Offset - MapVignetteStepPixels, 0.f), FVector2f(MapVignetteStepPixels, Size.Y), Shade);
	}

	const bool bDialogOpen = !PendingMarkerId.IsNone();
	const int32 HoveredIndex = bDialogOpen ? INDEX_NONE : FindMarkerAt(MousePosition, Size);
	const FVector PlayerLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const FVector2f PlayerScreen = WorldToScreen(PlayerLocation, Size);

	if (Markers)
	{
		const TArray<FAedirMapMarker>& Entries = Markers->GetMarkers();

		// Dashed route to the hovered destination.
		if (Entries.IsValidIndex(HoveredIndex) && Entries[HoveredIndex].bDiscovered && Pawn)
		{
			const FVector2f Target = WorldToScreen(Entries[HoveredIndex].Location, Size);
			const FVector2f Direction = Target - PlayerScreen;
			const float Length = Direction.Size();
			const FVector2f Unit = Length > 1.f ? Direction / Length : FVector2f::ZeroVector;
			for (float Offset = 0.f; Offset < Length; Offset += 16.f)
			{
				MapDrawLine(OutDrawElements, MarkerLayer, AllottedGeometry, PlayerScreen + Unit * Offset, PlayerScreen + Unit * FMath::Min(Offset + 8.f, Length), FLinearColor(AedirUIStyle::Accent.R, AedirUIStyle::Accent.G, AedirUIStyle::Accent.B, 0.7f), 2.f);
			}
		}

		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FAedirMapMarker& Marker = Entries[Index];
			if (!Marker.bRevealed)
			{
				continue;
			}

			const FVector2f Screen = WorldToScreen(Marker.Location, Size);
			if (Screen.X < -40.f || Screen.Y < -40.f || Screen.X > Size.X + 40.f || Screen.Y > Size.Y + 40.f)
			{
				continue;
			}

			const bool bHovered = Index == HoveredIndex;
			const FString LabelText = Marker.Label.ToString();

			if (Marker.Type == EAedirMarkerType::Custom)
			{
				const FLinearColor FlagColor = Marker.Color;
				MapDrawLine(OutDrawElements, MarkerLayer, AllottedGeometry, Screen, Screen + FVector2f(0.f, -22.f), FlagColor, 2.f);
				FSlateDrawElement::MakeLines(OutDrawElements, MarkerLayer, AllottedGeometry.ToPaintGeometry(),
					TArray<FVector2f>{ Screen + FVector2f(0.f, -22.f), Screen + FVector2f(13.f, -17.f), Screen + FVector2f(0.f, -12.f) },
					ESlateDrawEffect::None, FlagColor, true, 2.f);
				continue;
			}

			if (!Marker.bDiscovered)
			{
				const FLinearColor UnknownColor = bHovered ? AedirUIStyle::Accent : MapUnknownColor;
				MapDrawText(OutDrawElements, MarkerLayer, AllottedGeometry, TEXT("?"), BigFont, Screen - FVector2f(0.f, 14.f), UnknownColor, true);
				continue;
			}

			const FLinearColor Base = Marker.Type == EAedirMarkerType::Mission ? MapMissionColor : MapLocationColor;
			const FLinearColor Color = bHovered ? FLinearColor::White : Base;
			const float Radius = bHovered ? 11.f : 8.f;
			if (Marker.Type == EAedirMarkerType::Mission)
			{
				MapDrawDiamond(OutDrawElements, MarkerLayer, AllottedGeometry, Screen, Radius, Color);
			}
			else
			{
				MapDrawPolygon(OutDrawElements, MarkerLayer, AllottedGeometry, Screen, Radius, 16, 0.f, Color, 2.f);
				MapDrawDiamond(OutDrawElements, MarkerLayer, AllottedGeometry, Screen, 4.f, Color);
			}
			MapDrawText(OutDrawElements, MarkerLayer, AllottedGeometry, LabelText, bHovered ? LabelFont : SmallFont, Screen + FVector2f(0.f, 14.f), bHovered ? FLinearColor::White : AedirUIStyle::Text, true);
		}

		// Tooltip for the hovered marker.
		if (Entries.IsValidIndex(HoveredIndex))
		{
			const FAedirMapMarker& Marker = Entries[HoveredIndex];
			const FString Title = Marker.bDiscovered ? Marker.Label.ToString() : FString(TEXT("Undiscovered location"));
			const FString Detail = Marker.bDiscovered
				? FString::Printf(TEXT("%s away   -   click to fast travel"), *MapFormatDistance(static_cast<float>((Marker.Location - PlayerLocation).Size2D())))
				: FString(TEXT("Get closer to discover it"));
			const float TipWidth = FMath::Max(MapMeasureWidth(Title, LabelFont), MapMeasureWidth(Detail, SmallFont)) + 24.f;
			FVector2f TipPosition = WorldToScreen(Marker.Location, Size) + FVector2f(18.f, -52.f);
			TipPosition.X = FMath::Clamp(TipPosition.X, 8.f, Size.X - TipWidth - 8.f);
			TipPosition.Y = FMath::Clamp(TipPosition.Y, 8.f, Size.Y - 60.f);
			MapDrawBox(OutDrawElements, MarkerLayer + 1, AllottedGeometry, TipPosition, FVector2f(TipWidth, 46.f), FLinearColor(0.f, 0.f, 0.f, 0.85f));
			MapDrawFrame(OutDrawElements, MarkerLayer + 2, AllottedGeometry, TipPosition, TipPosition + FVector2f(TipWidth, 46.f), AedirUIStyle::Line, 1.f);
			MapDrawText(OutDrawElements, MarkerLayer + 2, AllottedGeometry, Title, LabelFont, TipPosition + FVector2f(12.f, 4.f), AedirUIStyle::Accent, false);
			MapDrawText(OutDrawElements, MarkerLayer + 2, AllottedGeometry, Detail, SmallFont, TipPosition + FVector2f(12.f, 25.f), AedirUIStyle::Text, false);
		}
	}

	// Player arrow.
	if (Pawn)
	{
		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector2f Direction = FVector2f(static_cast<float>(Forward.Y), static_cast<float>(-Forward.X)).GetSafeNormal();
		const FVector2f Side(-Direction.Y, Direction.X);
		const FVector2f Tip = PlayerScreen + Direction * 15.f;
		const FVector2f BackLeft = PlayerScreen - Direction * 9.f + Side * 9.f;
		const FVector2f BackRight = PlayerScreen - Direction * 9.f - Side * 9.f;
		const FVector2f Notch = PlayerScreen - Direction * 4.f;
		FSlateDrawElement::MakeLines(OutDrawElements, MarkerLayer + 1, AllottedGeometry.ToPaintGeometry(),
			TArray<FVector2f>{ Tip, BackLeft, Notch, BackRight, Tip }, ESlateDrawEffect::None, FLinearColor::White, true, 3.f);
	}

	// Frame, title and compass directions.
	const FVector2f FrameMin(MapFramePadding, MapFramePadding);
	const FVector2f FrameMax = Size - FrameMin;
	MapDrawFrame(OutDrawElements, FrameLayer, AllottedGeometry, FrameMin, FrameMax, AedirUIStyle::Accent, 2.f);
	MapDrawFrame(OutDrawElements, FrameLayer, AllottedGeometry, FrameMin + FVector2f(6.f, 6.f), FrameMax - FVector2f(6.f, 6.f), AedirUIStyle::Line, 1.f);
	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, TEXT("N"), DirectionFont, FVector2f(Size.X * 0.5f, FrameMin.Y + 12.f), AedirUIStyle::Accent, true);
	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, TEXT("S"), DirectionFont, FVector2f(Size.X * 0.5f, FrameMax.Y - 34.f), AedirUIStyle::Accent, true);
	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, TEXT("W"), DirectionFont, FVector2f(FrameMin.X + 18.f, Size.Y * 0.5f - 10.f), AedirUIStyle::Accent, false);
	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, TEXT("E"), DirectionFont, FVector2f(FrameMax.X - 30.f, Size.Y * 0.5f - 10.f), AedirUIStyle::Accent, false);

	// Scale bar.
	const float BarMeters = [this]()
	{
		const float Target = CmPerPixel * MapMetersPerCm * 140.f;
		const float Magnitude = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, FMath::Max(Target, 1.f))));
		return Target / Magnitude >= 5.f ? Magnitude * 5.f : (Target / Magnitude >= 2.f ? Magnitude * 2.f : Magnitude);
	}();
	const float BarPixels = BarMeters / (CmPerPixel * MapMetersPerCm);
	const FVector2f BarStart(FrameMin.X + 28.f, FrameMax.Y - 44.f);
	MapDrawLine(OutDrawElements, FrameLayer, AllottedGeometry, BarStart, BarStart + FVector2f(BarPixels, 0.f), AedirUIStyle::Text, 2.f);
	MapDrawLine(OutDrawElements, FrameLayer, AllottedGeometry, BarStart + FVector2f(0.f, -5.f), BarStart + FVector2f(0.f, 5.f), AedirUIStyle::Text, 2.f);
	MapDrawLine(OutDrawElements, FrameLayer, AllottedGeometry, BarStart + FVector2f(BarPixels, -5.f), BarStart + FVector2f(BarPixels, 5.f), AedirUIStyle::Text, 2.f);
	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, MapFormatDistance(BarMeters / MapMetersPerCm), SmallFont, BarStart + FVector2f(0.f, 8.f), AedirUIStyle::Text, false);

	MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, TEXT("[WASD] Pan   [Wheel] Zoom   [LMB] Fast travel   [RMB] Place marker   [Space] Centre   [Esc] Close"), SmallFont,
		FVector2f(Size.X * 0.5f, FrameMax.Y - 30.f), AedirUIStyle::TextDim, true);

	if (MessageRemaining > 0.f)
	{
		const float Alpha = FMath::Clamp(MessageRemaining / 0.5f, 0.f, 1.f);
		MapDrawText(OutDrawElements, FrameLayer, AllottedGeometry, Message.ToString(), LabelFont, FVector2f(Size.X * 0.5f, FrameMin.Y + 54.f), FLinearColor(1.f, 0.82f, 0.5f, Alpha), true);
	}

	// Confirmation dialog.
	if (bDialogOpen)
	{
		MapDrawBox(OutDrawElements, DialogLayer, AllottedGeometry, FVector2f::ZeroVector, Size, FLinearColor(0.f, 0.f, 0.f, 0.6f));

		const FMapDialogLayout Layout = MapGetDialogLayout(Size);
		MapDrawBox(OutDrawElements, DialogLayer + 1, AllottedGeometry, Layout.Position, Layout.Size, FLinearColor(0.05f, 0.04f, 0.03f, 0.98f));
		MapDrawFrame(OutDrawElements, DialogLayer + 2, AllottedGeometry, Layout.Position, Layout.Position + Layout.Size, AedirUIStyle::Accent, 2.f);

		const FAedirMapMarker* Target = Markers ? Markers->FindMarker(PendingMarkerId) : nullptr;
		if (Target)
		{
			MapDrawText(OutDrawElements, DialogLayer + 2, AllottedGeometry, Target->Label.ToString(), LabelFont, FVector2f(Size.X * 0.5f, Layout.Position.Y + 16.f), AedirUIStyle::Accent, true);
		}
		MapDrawText(OutDrawElements, DialogLayer + 2, AllottedGeometry, TEXT("Are you sure you want to fast travel?"), BigFont, FVector2f(Size.X * 0.5f, Layout.Position.Y + 44.f), AedirUIStyle::Text, true);

		const FVector2f ButtonSize(MapButtonWidth, MapButtonHeight);
		const FVector2f ButtonPositions[] = { Layout.YesPosition, Layout.NoPosition };
		const TCHAR* const ButtonLabels[] = { TEXT("Yes  [Y]"), TEXT("No  [N]") };
		for (int32 Button = 0; Button < 2; ++Button)
		{
			const bool bHot = MapInside(MousePosition, ButtonPositions[Button], ButtonSize);
			MapDrawBox(OutDrawElements, DialogLayer + 2, AllottedGeometry, ButtonPositions[Button], ButtonSize, bHot ? AedirUIStyle::PanelSelected : AedirUIStyle::Panel);
			MapDrawFrame(OutDrawElements, DialogLayer + 3, AllottedGeometry, ButtonPositions[Button], ButtonPositions[Button] + ButtonSize, bHot ? AedirUIStyle::Accent : AedirUIStyle::Line, 1.f);
			MapDrawText(OutDrawElements, DialogLayer + 3, AllottedGeometry, ButtonLabels[Button], LabelFont, ButtonPositions[Button] + FVector2f(ButtonSize.X * 0.5f, 12.f), bHot ? FLinearColor::White : AedirUIStyle::Text, true);
		}
	}

	return LayerId + 20;
}
