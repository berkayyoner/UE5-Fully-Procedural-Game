#include "UI/SAedirMap.h"

#include "Creature/AedirCreature.h"
#include "Engine/World.h"
#include "HUD/AedirMarkerSubsystem.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Rendering/DrawElements.h"
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
	constexpr float MapDialogWidth = 480.f;
	constexpr float MapDialogHeight = 208.f;
	constexpr float MapButtonWidth = 150.f;
	constexpr float MapButtonHeight = 40.f;
	constexpr float MapMetersPerCm = 0.01f;

	const FLinearColor MapParchment(FColor(78, 66, 48));
	const FLinearColor MapParchmentDark(FColor(38, 31, 22));
	const FLinearColor MapGridMinor(FColor(204, 186, 146, 22));
	const FLinearColor MapGridMajor(FColor(204, 186, 146, 54));
	const FLinearColor MapMissionColor(FColor(226, 190, 112));
	const FLinearColor MapLocationColor = AedirUIStyle::Ivory;
	const FLinearColor MapUnknownColor(FColor(168, 158, 136, 220));

	const float MapGridSteps[] = { 1000.f, 2500.f, 5000.f, 10000.f, 25000.f, 50000.f, 100000.f, 250000.f, 500000.f };

	float MapMeasureWidth(const FString& Text, const FSlateFontInfo& Font)
	{
		return AedirUIStyle::MeasureText(Text, Font).X;
	}

	void MapDrawText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, bool bCenterX)
	{
		AedirUIStyle::DrawText(Elements, Layer, Geometry, Text, Font, Position, Color, bCenterX ? EAedirTextAlign::Center : EAedirTextAlign::Left, 0.75f);
	}

	// Frame of the map: two faded lines on each side and a diamond ornament in each corner.
	void MapAddFrame(FAedirUIMesh& Mesh, const FVector2f& Min, const FVector2f& Max)
	{
		using namespace AedirUIStyle;

		constexpr float Inset = 6.f;
		constexpr float CornerClear = 16.f;
		const FLinearColor Outer = WithAlpha(Metal, 0.9f);
		const FLinearColor Inner = WithAlpha(Metal, 0.42f);

		for (const float Offset : { 0.f, Inset })
		{
			const FLinearColor& Color = Offset == 0.f ? Outer : Inner;
			const FVector2f A = Min + FVector2f(Offset, Offset);
			const FVector2f B = Max - FVector2f(Offset, Offset);
			Mesh.AddLine(FVector2f(A.X + CornerClear, A.Y), FVector2f(B.X - CornerClear, A.Y), 1.f, Color, Color);
			Mesh.AddLine(FVector2f(A.X + CornerClear, B.Y), FVector2f(B.X - CornerClear, B.Y), 1.f, Color, Color);
			Mesh.AddLine(FVector2f(A.X, A.Y + CornerClear), FVector2f(A.X, B.Y - CornerClear), 1.f, Color, Color);
			Mesh.AddLine(FVector2f(B.X, A.Y + CornerClear), FVector2f(B.X, B.Y - CornerClear), 1.f, Color, Color);
		}

		for (const FVector2f& Corner : { Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y) })
		{
			const FVector2f Toward = FVector2f(Corner.X < (Min.X + Max.X) * 0.5f ? 1.f : -1.f, Corner.Y < (Min.Y + Max.Y) * 0.5f ? 1.f : -1.f);
			const FVector2f Spot = Corner + Toward * (Inset * 0.5f);
			AddDiamond(Mesh, Spot, 5.f, 5.f, Outer);
			AddDiamondOutline(Mesh, Spot, 11.f, 11.f, 1.f, Inner);
		}

		// A small break with a diamond in the middle of the long sides, where the map names its directions.
		for (const FVector2f& Spot : { FVector2f((Min.X + Max.X) * 0.5f, Min.Y), FVector2f((Min.X + Max.X) * 0.5f, Max.Y), FVector2f(Min.X, (Min.Y + Max.Y) * 0.5f), FVector2f(Max.X, (Min.Y + Max.Y) * 0.5f) })
		{
			AddDiamond(Mesh, Spot, 3.5f, 3.5f, Outer);
		}
	}

	void MapDrawLine(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& A, const FVector2f& B, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), TArray<FVector2f>{ A, B }, ESlateDrawEffect::None, Color, true, Thickness);
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
	using namespace AedirUIStyle;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float Feather = 1.f / FMath::Max(AllottedGeometry.Scale, UE_KINDA_SMALL_NUMBER);
	const FSlateFontInfo SmallFont = RegularFont(11.f, 30);
	const FSlateFontInfo LabelFont = LightFont(14.f, 40);
	const FSlateFontInfo BigFont = LightFont(26.f, 30);
	const FSlateFontInfo DirectionFont = RegularFont(17.f);

	const AAedirCreature* Pawn = Creature.Get();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;

	const int32 BackgroundLayer = LayerId;
	const int32 GridLayer = LayerId + 1;
	const int32 MarkerLayer = LayerId + 3;
	const int32 FrameLayer = LayerId + 6;
	const int32 DialogLayer = LayerId + 10;

	{
		// Aged paper: a lighter middle under a darker top and bottom. The two patches break up the flat colour.
		FAedirUIMesh Paper(Feather);
		const FVector2f Middle(Size.X, Size.Y * 0.5f);
		Paper.AddGradientRect(FVector2f::ZeroVector, Middle, MapParchmentDark, MapParchmentDark, MapParchment, MapParchment);
		Paper.AddGradientRect(FVector2f(0.f, Size.Y * 0.5f), Size, MapParchment, MapParchment, MapParchmentDark, MapParchmentDark);
		AddGlow(Paper, Size * FVector2f(0.32f, 0.38f), Size * 0.5f, FLinearColor(0.f, 0.f, 0.f, 0.16f));
		AddGlow(Paper, Size * FVector2f(0.72f, 0.64f), Size * 0.42f, WithAlpha(MapParchment, 0.45f));
		DrawMesh(OutDrawElements, BackgroundLayer, AllottedGeometry, Paper);
	}

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

	{
		FAedirUIMesh Edges(Feather);
		AddVignette(Edges, Size, 0.f, 0.88f);
		DrawMesh(OutDrawElements, GridLayer + 1, AllottedGeometry, Edges);
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
				MapDrawLine(OutDrawElements, MarkerLayer, AllottedGeometry, PlayerScreen + Unit * Offset, PlayerScreen + Unit * FMath::Min(Offset + 8.f, Length), WithAlpha(Ivory, 0.7f), 1.5f);
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
			FAedirUIMesh MarkerMesh(Feather);

			if (Marker.Type == EAedirMarkerType::Custom)
			{
				const FVector2f Top = Screen + FVector2f(0.f, -22.f);
				const FVector2f FlagPoints[] = { Top, Screen + FVector2f(13.f, -17.f), Screen + FVector2f(0.f, -12.f) };
				MarkerMesh.AddLine(Screen, Top, 1.6f, Marker.Color, Marker.Color);
				MarkerMesh.AddConvex(FlagPoints, Marker.Color);
				DrawMesh(OutDrawElements, MarkerLayer, AllottedGeometry, MarkerMesh);
				continue;
			}

			if (!Marker.bDiscovered)
			{
				const FLinearColor UnknownColor = bHovered ? IvoryBright : MapUnknownColor;
				MapDrawText(OutDrawElements, MarkerLayer, AllottedGeometry, TEXT("?"), BigFont, Screen - FVector2f(0.f, 18.f), UnknownColor, true);
				continue;
			}

			const FLinearColor Base = Marker.Type == EAedirMarkerType::Mission ? MapMissionColor : MapLocationColor;
			const FLinearColor Color = bHovered ? IvoryBright : Base;
			const float Radius = bHovered ? 11.f : 8.f;
			AddDiamond(MarkerMesh, Screen, Radius + 3.f, Radius + 3.f, FLinearColor(0.f, 0.f, 0.f, 0.55f));
			if (Marker.Type == EAedirMarkerType::Mission)
			{
				AddDiamond(MarkerMesh, Screen, Radius, Radius, Color);
			}
			else
			{
				AddDiamondOutline(MarkerMesh, Screen, Radius, Radius, 1.4f, Color);
				AddDiamond(MarkerMesh, Screen, Radius * 0.4f, Radius * 0.4f, Color);
			}
			DrawMesh(OutDrawElements, MarkerLayer, AllottedGeometry, MarkerMesh);
			MapDrawText(OutDrawElements, MarkerLayer, AllottedGeometry, LabelText, bHovered ? LabelFont : SmallFont, Screen + FVector2f(0.f, 15.f), bHovered ? IvoryBright : Ivory, true);
		}

		// Tooltip for the hovered marker.
		if (Entries.IsValidIndex(HoveredIndex))
		{
			const FAedirMapMarker& Marker = Entries[HoveredIndex];
			const FString Title = Marker.bDiscovered ? Marker.Label.ToString() : FString(TEXT("Undiscovered location"));
			const FString Detail = Marker.bDiscovered
				? FString::Printf(TEXT("%s away   -   click to fast travel"), *MapFormatDistance(static_cast<float>((Marker.Location - PlayerLocation).Size2D())))
				: FString(TEXT("Get closer to discover it"));
			const float TipWidth = FMath::Max(MapMeasureWidth(Title, LabelFont), MapMeasureWidth(Detail, SmallFont)) + 40.f;
			FVector2f TipPosition = WorldToScreen(Marker.Location, Size) + FVector2f(18.f, -58.f);
			TipPosition.X = FMath::Clamp(TipPosition.X, 8.f, Size.X - TipWidth - 8.f);
			TipPosition.Y = FMath::Clamp(TipPosition.Y, 8.f, Size.Y - 60.f);

			FAedirUIMesh Tip(Feather);
			const FLinearColor TipDark(0.f, 0.f, 0.f, 0.8f);
			AddGradientPanel(Tip, TipPosition, TipPosition + FVector2f(TipWidth + 30.f, 50.f), TipPosition.X + TipWidth * 0.7f, TipDark);
			AddFadedLine(Tip, TipPosition + FVector2f(0.f, 27.f), TipPosition + FVector2f(TipWidth, 27.f), 1.f, Metal, 0.f, 0.7f);
			DrawMesh(OutDrawElements, MarkerLayer + 1, AllottedGeometry, Tip);

			MapDrawText(OutDrawElements, MarkerLayer + 2, AllottedGeometry, Title, LabelFont, TipPosition + FVector2f(12.f, 3.f), IvoryBright, false);
			MapDrawText(OutDrawElements, MarkerLayer + 2, AllottedGeometry, Detail, SmallFont, TipPosition + FVector2f(12.f, 30.f), Ivory, false);
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
		FAedirUIMesh Arrow(Feather);
		const FVector2f ShadowOffset(0.f, 2.f);
		const FVector2f ShadowLeft[] = { Tip + ShadowOffset, Notch + ShadowOffset, BackLeft + ShadowOffset };
		const FVector2f ShadowRight[] = { Tip + ShadowOffset, BackRight + ShadowOffset, Notch + ShadowOffset };
		const FVector2f LeftWing[] = { Tip, Notch, BackLeft };
		const FVector2f RightWing[] = { Tip, BackRight, Notch };
		Arrow.AddConvex(ShadowLeft, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Arrow.AddConvex(ShadowRight, FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Arrow.AddConvex(LeftWing, IvoryBright);
		Arrow.AddConvex(RightWing, Metal);
		DrawMesh(OutDrawElements, MarkerLayer + 1, AllottedGeometry, Arrow);
	}

	// Frame, title and compass directions.
	const FVector2f FrameMin(MapFramePadding, MapFramePadding);
	const FVector2f FrameMax = Size - FrameMin;
	{
		FAedirUIMesh Frame(Feather);
		MapAddFrame(Frame, FrameMin, FrameMax);

		// Soft dark bands so the title and the hints stay readable over any marker.
		const FLinearColor BandDark(0.f, 0.f, 0.f, 0.6f);
		const FLinearColor BandClear(0.f, 0.f, 0.f, 0.f);
		const float Middle = Size.X * 0.5f;
		Frame.AddGradientRect(FVector2f(Middle - 420.f, FrameMin.Y + 8.f), FVector2f(Middle, FrameMin.Y + 76.f), BandClear, BandDark, BandClear, BandClear);
		Frame.AddGradientRect(FVector2f(Middle, FrameMin.Y + 8.f), FVector2f(Middle + 420.f, FrameMin.Y + 76.f), BandDark, BandClear, BandClear, BandClear);
		AddOrnamentRule(Frame, FVector2f(Middle, FrameMin.Y + 62.f), 240.f, Metal);
		DrawMesh(OutDrawElements, FrameLayer, AllottedGeometry, Frame);
	}

	const int32 FrameTextLayer = FrameLayer + 1;
	DrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("AEDIR"), TitleFont(22.f), FVector2f(Size.X * 0.5f, FrameMin.Y + 16.f), IvoryBright, EAedirTextAlign::Center, 0.8f);
	MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("N"), DirectionFont, FVector2f(Size.X * 0.5f, FrameMin.Y + 76.f), Ivory, true);
	MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("S"), DirectionFont, FVector2f(Size.X * 0.5f, FrameMax.Y - 62.f), Ivory, true);
	MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("W"), DirectionFont, FVector2f(FrameMin.X + 18.f, Size.Y * 0.5f - 12.f), Ivory, false);
	MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("E"), DirectionFont, FVector2f(FrameMax.X - 34.f, Size.Y * 0.5f - 12.f), Ivory, false);

	// Scale bar.
	const float BarMeters = [this]()
	{
		const float Target = CmPerPixel * MapMetersPerCm * 140.f;
		const float Magnitude = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, FMath::Max(Target, 1.f))));
		return Target / Magnitude >= 5.f ? Magnitude * 5.f : (Target / Magnitude >= 2.f ? Magnitude * 2.f : Magnitude);
	}();
	const float BarPixels = BarMeters / (CmPerPixel * MapMetersPerCm);
	const FVector2f BarStart(FrameMin.X + 34.f, FrameMax.Y - 46.f);
	{
		FAedirUIMesh Scale(Feather);
		Scale.AddLine(BarStart, BarStart + FVector2f(BarPixels, 0.f), 1.f, Ivory, Ivory);
		AddDiamond(Scale, BarStart, 2.5f, 5.f, Ivory);
		AddDiamond(Scale, BarStart + FVector2f(BarPixels, 0.f), 2.5f, 5.f, Ivory);
		DrawMesh(OutDrawElements, FrameLayer, AllottedGeometry, Scale);
	}
	MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, MapFormatDistance(BarMeters / MapMetersPerCm), SmallFont, BarStart + FVector2f(0.f, 7.f), Ivory, false);

	DrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, TEXT("[WASD] PAN     [WHEEL] ZOOM     [LMB] FAST TRAVEL     [RMB] PLACE MARKER     [SPACE] CENTRE     [ESC] CLOSE"), RegularFont(10.f, 60),
		FVector2f(Size.X * 0.5f, FrameMax.Y - 30.f), TextDim, EAedirTextAlign::Center, 0.8f);

	if (MessageRemaining > 0.f)
	{
		const float Alpha = FMath::Clamp(MessageRemaining / 0.5f, 0.f, 1.f);
		MapDrawText(OutDrawElements, FrameTextLayer, AllottedGeometry, Message.ToString(), LabelFont, FVector2f(Size.X * 0.5f, FrameMin.Y + 104.f), WithAlpha(IvoryBright, Alpha), true);
	}

	// Confirmation dialog.
	if (bDialogOpen)
	{
		const FMapDialogLayout Layout = MapGetDialogLayout(Size);
		const FVector2f ButtonSize(MapButtonWidth, MapButtonHeight);
		const FVector2f ButtonPositions[] = { Layout.YesPosition, Layout.NoPosition };
		const TCHAR* const ButtonLabels[] = { TEXT("YES"), TEXT("NO") };

		FAedirUIMesh Dialog(Feather);
		AddModalBand(Dialog, Size, Size.Y * 0.5f, MapDialogHeight * 0.5f);
		for (int32 Button = 0; Button < 2; ++Button)
		{
			if (MapInside(MousePosition, ButtonPositions[Button], ButtonSize))
			{
				AddDiamond(Dialog, ButtonPositions[Button] + FVector2f(ButtonSize.X * 0.5f - 44.f, ButtonSize.Y * 0.5f), 4.5f, 4.5f, IvoryBright);
			}
		}
		DrawMesh(OutDrawElements, DialogLayer, AllottedGeometry, Dialog);

		const FAedirMapMarker* Target = Markers ? Markers->FindMarker(PendingMarkerId) : nullptr;
		if (Target)
		{
			DrawText(OutDrawElements, DialogLayer + 1, AllottedGeometry, Target->Label.ToString().ToUpper(), RegularFont(11.f, 140), FVector2f(Size.X * 0.5f, Layout.Position.Y + 28.f), Metal, EAedirTextAlign::Center);
		}
		MapDrawText(OutDrawElements, DialogLayer + 1, AllottedGeometry, TEXT("Fast travel here?"), BigFont, FVector2f(Size.X * 0.5f, Layout.Position.Y + 52.f), IvoryBright, true);

		for (int32 Button = 0; Button < 2; ++Button)
		{
			const bool bHot = MapInside(MousePosition, ButtonPositions[Button], ButtonSize);
			DrawText(OutDrawElements, DialogLayer + 1, AllottedGeometry, ButtonLabels[Button], TitleFont(18.f), ButtonPositions[Button] + FVector2f(ButtonSize.X * 0.5f, 5.f), bHot ? IvoryBright : Ivory, EAedirTextAlign::Center);
		}
	}

	return LayerId + 20;
}
