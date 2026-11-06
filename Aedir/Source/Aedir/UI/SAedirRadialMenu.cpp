#include "UI/SAedirRadialMenu.h"

#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Layout/Geometry.h"
#include "UI/AedirUIStyle.h"

namespace
{
	constexpr float RadialVerticalReach = 250.f;
	constexpr float RadialHorizontalReach = 430.f;
	constexpr float RadialStarRadius = 92.f;
	constexpr float RadialDeadZone = 40.f;
	constexpr float RadialGlowSpeed = 7.f;
	constexpr float RadialLabelSize = 34.f;
	constexpr float RadialHintSize = 11.f;

	// Screen direction and label of each option, in enum order.
	const FVector2f RadialDirections[] = { FVector2f(0.f, -1.f), FVector2f(0.f, 1.f), FVector2f(-1.f, 0.f), FVector2f(1.f, 0.f) };
	const TCHAR* const RadialLabels[] = { TEXT("SKILLS"), TEXT("MAP"), TEXT("MAGIC"), TEXT("INVENTORY") };

	FVector2f RadialOptionCenter(int32 Index, const FVector2f& Size)
	{
		const FVector2f Direction = RadialDirections[Index];
		return Size * 0.5f + FVector2f(Direction.X * RadialHorizontalReach, Direction.Y * RadialVerticalReach);
	}

	// One long kite-shaped point of the compass star, split along its axis so one half can be lit like a facet.
	void RadialAddStarPoint(FAedirUIMesh& Mesh, const FVector2f& Center, const FVector2f& Direction, float Length, float Width, const FLinearColor& LitColor, const FLinearColor& ShadeColor)
	{
		const FVector2f Side(-Direction.Y, Direction.X);
		const FVector2f Tip = Center + Direction * Length;
		const FVector2f Waist = Center + Direction * (Width * 1.1f);
		const FVector2f LeftPoints[] = { Center, Tip, Waist + Side * Width };
		const FVector2f RightPoints[] = { Center, Waist - Side * Width, Tip };
		Mesh.AddConvex(LeftPoints, LitColor);
		Mesh.AddConvex(RightPoints, ShadeColor);
	}

	void RadialAddRing(FAedirUIMesh& Mesh, const FVector2f& Center, float Radius, const FLinearColor& Color)
	{
		constexpr int32 Segments = 72;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float AngleA = UE_TWO_PI * Index / Segments;
			const float AngleB = UE_TWO_PI * (Index + 1) / Segments;
			Mesh.AddLine(Center + FVector2f(FMath::Cos(AngleA), FMath::Sin(AngleA)) * Radius, Center + FVector2f(FMath::Cos(AngleB), FMath::Sin(AngleB)) * Radius, 1.f, Color, Color);
		}
	}
}

void SAedirRadialMenu::Construct(const FArguments& InArgs)
{
	ChosenDelegate = InArgs._OnChosen;
	CloseDelegate = InArgs._OnClose;
}

void SAedirRadialMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Glow); ++Index)
	{
		Glow[Index] = FMath::FInterpConstantTo(Glow[Index], Index == Selected ? 1.f : 0.f, InDeltaTime, RadialGlowSpeed);
	}
}

int32 SAedirRadialMenu::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace AedirUIStyle;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FVector2f Center = Size * 0.5f;
	FAedirUIMesh Mesh(1.f / FMath::Max(AllottedGeometry.Scale, UE_KINDA_SMALL_NUMBER));

	AddVignette(Mesh, Size, 0.62f, 0.94f);
	AddGlow(Mesh, Center, FVector2f(420.f, 300.f), FLinearColor(0.f, 0.f, 0.f, 0.35f));

	const FLinearColor MetalSoft = WithAlpha(Metal, 0.55f);
	const FLinearColor MetalFaint = WithAlpha(Metal, 0.28f);

	RadialAddRing(Mesh, Center, RadialStarRadius * 0.62f, MetalSoft);
	RadialAddRing(Mesh, Center, RadialStarRadius * 0.62f + 5.f, MetalFaint);

	// Diagonal points first, the main points lie on top of them.
	for (const FVector2f& Diagonal : { FVector2f(1.f, 1.f), FVector2f(1.f, -1.f), FVector2f(-1.f, 1.f), FVector2f(-1.f, -1.f) })
	{
		RadialAddStarPoint(Mesh, Center, Diagonal.GetSafeNormal(), RadialStarRadius * 0.52f, 6.f, WithAlpha(Metal, 0.5f), WithAlpha(Metal, 0.2f));
	}

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RadialDirections); ++Index)
	{
		const FVector2f Direction = RadialDirections[Index];
		const float Light = Glow[Index];
		const FVector2f Horizontal = FVector2f(FMath::Abs(Direction.X), 0.f);
		const float Reach = Horizontal.X > 0.f ? RadialHorizontalReach : RadialVerticalReach;
		const float Length = RadialStarRadius * (1.f + 0.14f * Light);
		const FLinearColor Lit = FMath::Lerp(WithAlpha(Metal, 0.82f), IvoryBright, Light);
		const FLinearColor Shade = FMath::Lerp(WithAlpha(Metal, 0.34f), WithAlpha(Ivory, 0.72f), Light);

		RadialAddStarPoint(Mesh, Center, Direction, Length, 9.f, Lit, Shade);

		// Line from the star to the label, stopping short of the text.
		const FVector2f LineStart = Center + Direction * (RadialStarRadius + 22.f);
		const FVector2f LineEnd = Center + Direction * (Reach - (Horizontal.X > 0.f ? 110.f : 44.f));
		AddFadedLine(Mesh, LineStart, LineEnd, 1.f, WithAlpha(Metal, 0.3f + 0.45f * Light), 0.f, 0.85f);
		AddDiamond(Mesh, Center + Direction * (RadialStarRadius + 14.f), 2.5f, 2.5f, WithAlpha(Metal, 0.6f + 0.4f * Light));

		if (Light > 0.f)
		{
			AddGlow(Mesh, RadialOptionCenter(Index, Size), FVector2f(210.f, 60.f), WithAlpha(Ivory, 0.12f * Light));
		}
	}

	AddDiamond(Mesh, Center, 9.f, 9.f, Ink);
	AddDiamondOutline(Mesh, Center, 9.f, 9.f, 1.f, Metal);
	AddDiamond(Mesh, Center, 3.f, 3.f, Metal);

	DrawMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh);

	const FSlateFontInfo LabelFont = TitleFont(RadialLabelSize);
	const int32 TextLayer = LayerId + 1;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RadialLabels); ++Index)
	{
		const float Light = Glow[Index];
		const FVector2f LabelCenter = RadialOptionCenter(Index, Size);
		const float Height = MeasureText(RadialLabels[Index], LabelFont).Y;
		const FLinearColor Color = FMath::Lerp(WithAlpha(Ivory, 0.5f), IvoryBright, Light);
		DrawText(OutDrawElements, TextLayer, AllottedGeometry, RadialLabels[Index], LabelFont, LabelCenter - FVector2f(0.f, Height * 0.5f), Color, EAedirTextAlign::Center, 0.7f);
	}

	DrawText(OutDrawElements, TextLayer, AllottedGeometry, TEXT("[W A S D]  SELECT      [ENTER]  OPEN      [TAB]  CLOSE"), RegularFont(RadialHintSize, 120),
		FVector2f(Center.X, Size.Y - 58.f), WithAlpha(TextDim, 0.85f), EAedirTextAlign::Center);

	return TextLayer;
}

int32 SAedirRadialMenu::FindOptionAt(const FVector2f& Local, const FVector2f& Size) const
{
	const FVector2f Offset = Local - Size * 0.5f;
	if (Offset.Size() < RadialDeadZone)
	{
		return INDEX_NONE;
	}

	// The screen is wider than tall, so the sectors are measured against the label distances.
	const FVector2f Normalized(Offset.X / RadialHorizontalReach, Offset.Y / RadialVerticalReach);
	if (FMath::Abs(Normalized.X) > FMath::Abs(Normalized.Y))
	{
		return static_cast<int32>(Normalized.X < 0.f ? EAedirRadialOption::Magic : EAedirRadialOption::Inventory);
	}
	return static_cast<int32>(Normalized.Y < 0.f ? EAedirRadialOption::Skills : EAedirRadialOption::Map);
}

void SAedirRadialMenu::Choose(EAedirRadialOption Option)
{
	ChosenDelegate.ExecuteIfBound(Option);
}

FReply SAedirRadialMenu::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Slate also sends moves with no motion when widgets change, a resting cursor must not undo the keyboard choice.
	if (MouseEvent.GetCursorDelta().IsNearlyZero())
	{
		return FReply::Handled();
	}

	const int32 Hovered = FindOptionAt(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MyGeometry.GetLocalSize());
	if (Hovered != INDEX_NONE)
	{
		Selected = Hovered;
	}
	return FReply::Handled();
}

FReply SAedirRadialMenu::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const int32 Clicked = FindOptionAt(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), MyGeometry.GetLocalSize());
		if (Clicked != INDEX_NONE)
		{
			Selected = Clicked;
			Choose(static_cast<EAedirRadialOption>(Clicked));
			return FReply::Handled();
		}
	}
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse);
}

FReply SAedirRadialMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Tab || Key == EKeys::Escape)
	{
		CloseDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::W || Key == EKeys::Up)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Skills);
		return FReply::Handled();
	}
	if (Key == EKeys::S || Key == EKeys::Down)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Map);
		return FReply::Handled();
	}
	if (Key == EKeys::A || Key == EKeys::Left)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Magic);
		return FReply::Handled();
	}
	if (Key == EKeys::D || Key == EKeys::Right)
	{
		Selected = static_cast<int32>(EAedirRadialOption::Inventory);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::E)
	{
		if (Selected != INDEX_NONE)
		{
			Choose(static_cast<EAedirRadialOption>(Selected));
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}
