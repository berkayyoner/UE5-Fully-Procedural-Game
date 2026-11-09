#include "UI/AedirUIStyle.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GlobalRenderResources.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Engine/Font.h"
#include "Styling/WidgetStyle.h"
#include "Textures/SlateShaderResource.h"
#include "UObject/Package.h"

namespace
{
	constexpr float UIStylePointsToPixels = 96.f / 72.f;
	constexpr int32 UIStyleTitleSpacing = 240;
	constexpr int32 UIStyleVignetteCells = 12;
	constexpr int32 UIStyleGlowSegments = 40;
	constexpr int32 UIStyleGlowRings = 5;
	constexpr float UIStyleDoubleLineGap = 5.f;
	constexpr float UIStyleShadowOffset = 1.2f;

	FLinearColor UIStyleClear(const FLinearColor& Color)
	{
		return FLinearColor(Color.R, Color.G, Color.B, 0.f);
	}

	float UIStyleTextAlignOffset(float Width, EAedirTextAlign Align)
	{
		switch (Align)
		{
		case EAedirTextAlign::Center: return Width * 0.5f;
		case EAedirTextAlign::Right: return Width;
		default: break;
		}
		return 0.f;
	}

	// FCanvasTextItem refuses to draw without a UFont even when it is given a Slate font, and only looks at its cache type.
	UFont* UIStyleCanvasFontTag()
	{
		static UFont* Tag = nullptr;
		if (!Tag)
		{
			Tag = NewObject<UFont>(GetTransientPackage());
			Tag->FontCacheType = EFontCacheType::Runtime;
			Tag->AddToRoot();
		}
		return Tag;
	}

	FSlateFontInfo UIStyleFont(FName Typeface, float Size, int32 LetterSpacing)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(Typeface, Size);
		Font.LetterSpacing = LetterSpacing;
		return Font;
	}
}

uint32 FAedirUIMesh::AddVertex(const FVector2f& Position, const FLinearColor& Color)
{
	return static_cast<uint32>(Vertices.Add({ Position, Color }));
}

void FAedirUIMesh::Reset()
{
	Vertices.Reset();
	Indices.Reset();
}

void FAedirUIMesh::AddQuad(const FVector2f& A, const FVector2f& B, const FVector2f& C, const FVector2f& D, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC, const FLinearColor& ColorD)
{
	const uint32 IndexA = AddVertex(A, ColorA);
	const uint32 IndexB = AddVertex(B, ColorB);
	const uint32 IndexC = AddVertex(C, ColorC);
	const uint32 IndexD = AddVertex(D, ColorD);
	Indices.Append({ IndexA, IndexB, IndexC, IndexA, IndexC, IndexD });
}

void FAedirUIMesh::AddRect(const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color)
{
	AddGradientRect(Min, Max, Color, Color, Color, Color);
}

void FAedirUIMesh::AddGradientRect(const FVector2f& Min, const FVector2f& Max, const FLinearColor& TopLeft, const FLinearColor& TopRight, const FLinearColor& BottomRight, const FLinearColor& BottomLeft)
{
	AddQuad(Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), TopLeft, TopRight, BottomRight, BottomLeft);
}

void FAedirUIMesh::AddSoftEdge(const FVector2f& A, const FVector2f& B, const FLinearColor& ColorA, const FLinearColor& ColorB)
{
	const FVector2f Direction = (B - A).GetSafeNormal();
	// Clockwise screen order means the outside is on the left of the walking direction.
	const FVector2f Outward = FVector2f(Direction.Y, -Direction.X) * Feather;
	AddQuad(A, B, B + Outward, A + Outward, ColorA, ColorB, UIStyleClear(ColorB), UIStyleClear(ColorA));
}

void FAedirUIMesh::AddConvex(TConstArrayView<FVector2f> Points, const FLinearColor& Color)
{
	if (Points.Num() < 3)
	{
		return;
	}

	const uint32 First = static_cast<uint32>(Vertices.Num());
	for (const FVector2f& Point : Points)
	{
		AddVertex(Point, Color);
	}
	for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
	{
		Indices.Append({ First, First + Index, First + Index + 1 });
	}
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		AddSoftEdge(Points[Index], Points[(Index + 1) % Points.Num()], Color, Color);
	}
}

void FAedirUIMesh::AddLine(const FVector2f& A, const FVector2f& B, float Thickness, const FLinearColor& ColorA, const FLinearColor& ColorB)
{
	const FVector2f Direction = (B - A).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector2f Normal(-Direction.Y, Direction.X);
	// A line thinner than the soft edge covers less of each pixel, so it gets fainter instead of breaking up.
	const float Coverage = FMath::Clamp(Thickness / FMath::Max(Feather, UE_KINDA_SMALL_NUMBER), 0.f, 1.f);
	const FVector2f Core = Normal * FMath::Max(0.f, Thickness - Feather) * 0.5f;
	const FVector2f Outer = Core + Normal * Feather;
	const FLinearColor CoreA = AedirUIStyle::WithAlpha(ColorA, Coverage);
	const FLinearColor CoreB = AedirUIStyle::WithAlpha(ColorB, Coverage);

	if (!Core.IsNearlyZero())
	{
		AddQuad(A + Core, B + Core, B - Core, A - Core, CoreA, CoreB, CoreB, CoreA);
	}
	AddQuad(A + Outer, B + Outer, B + Core, A + Core, UIStyleClear(ColorA), UIStyleClear(ColorB), CoreB, CoreA);
	AddQuad(A - Core, B - Core, B - Outer, A - Outer, CoreA, CoreB, UIStyleClear(ColorB), UIStyleClear(ColorA));
}

FSlateFontInfo AedirUIStyle::LightFont(float Size, int32 LetterSpacing)
{
	return UIStyleFont(TEXT("Light"), Size, LetterSpacing);
}

FSlateFontInfo AedirUIStyle::RegularFont(float Size, int32 LetterSpacing)
{
	return UIStyleFont(TEXT("Regular"), Size, LetterSpacing);
}

FSlateFontInfo AedirUIStyle::BoldFont(float Size, int32 LetterSpacing)
{
	return UIStyleFont(TEXT("Bold"), Size, LetterSpacing);
}

FSlateFontInfo AedirUIStyle::TitleFont(float Size)
{
	return UIStyleFont(TEXT("Light"), Size, UIStyleTitleSpacing);
}

FVector2f AedirUIStyle::MeasureText(const FString& Text, const FSlateFontInfo& Font)
{
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return FVector2f::ZeroVector;
	}
	return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
}

void AedirUIStyle::AddDiamond(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, const FLinearColor& Color)
{
	const FVector2f Points[] =
	{
		Center + FVector2f(0.f, -HalfHeight),
		Center + FVector2f(HalfWidth, 0.f),
		Center + FVector2f(0.f, HalfHeight),
		Center + FVector2f(-HalfWidth, 0.f)
	};
	Mesh.AddConvex(Points, Color);
}

void AedirUIStyle::AddDiamondOutline(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Thickness, const FLinearColor& Color)
{
	const FVector2f Top = Center + FVector2f(0.f, -HalfHeight);
	const FVector2f Right = Center + FVector2f(HalfWidth, 0.f);
	const FVector2f Bottom = Center + FVector2f(0.f, HalfHeight);
	const FVector2f Left = Center + FVector2f(-HalfWidth, 0.f);
	Mesh.AddLine(Top, Right, Thickness, Color, Color);
	Mesh.AddLine(Right, Bottom, Thickness, Color, Color);
	Mesh.AddLine(Bottom, Left, Thickness, Color, Color);
	Mesh.AddLine(Left, Top, Thickness, Color, Color);
}

void AedirUIStyle::AddFadedLine(FAedirUIMesh& Mesh, const FVector2f& A, const FVector2f& B, float Thickness, const FLinearColor& Color, float FadeA, float FadeB)
{
	const FVector2f SolidA = FMath::Lerp(A, B, FMath::Clamp(FadeA, 0.f, 1.f));
	const FVector2f SolidB = FMath::Lerp(A, B, FMath::Clamp(1.f - FadeB, 0.f, 1.f));
	if (FadeA > 0.f)
	{
		Mesh.AddLine(A, SolidA, Thickness, UIStyleClear(Color), Color);
	}
	if (FadeA + FadeB < 1.f)
	{
		Mesh.AddLine(SolidA, SolidB, Thickness, Color, Color);
	}
	if (FadeB > 0.f)
	{
		Mesh.AddLine(SolidB, B, Thickness, Color, UIStyleClear(Color));
	}
}

void AedirUIStyle::AddOrnamentRule(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfLength, const FLinearColor& Color)
{
	constexpr float Gap = 9.f;
	AddFadedLine(Mesh, Center - FVector2f(HalfLength, 0.f), Center - FVector2f(Gap, 0.f), 1.f, Color, 0.8f, 0.f);
	AddFadedLine(Mesh, Center + FVector2f(Gap, 0.f), Center + FVector2f(HalfLength, 0.f), 1.f, Color, 0.f, 0.8f);
	AddDiamond(Mesh, Center, 4.f, 4.f, Color);
	AddDiamond(Mesh, Center - FVector2f(Gap + 3.f, 0.f), 1.6f, 1.6f, Color);
	AddDiamond(Mesh, Center + FVector2f(Gap + 3.f, 0.f), 1.6f, 1.6f, Color);
}

void AedirUIStyle::AddDoubleLine(FAedirUIMesh& Mesh, float X, float Top, float Bottom, const FLinearColor& Color)
{
	const float Middle = (Top + Bottom) * 0.5f;
	const float Half = UIStyleDoubleLineGap * 0.5f;
	const FLinearColor Inner = WithAlpha(Color, 0.55f);
	constexpr float DiamondClear = 12.f;

	for (const float Side : { -1.f, 1.f })
	{
		const FLinearColor& SideColor = Side < 0.f ? Color : Inner;
		const float LineX = X + Side * Half;
		AddFadedLine(Mesh, FVector2f(LineX, Top), FVector2f(LineX, Middle - DiamondClear), 1.f, SideColor, 0.12f, 0.f);
		AddFadedLine(Mesh, FVector2f(LineX, Middle + DiamondClear), FVector2f(LineX, Bottom), 1.f, SideColor, 0.f, 0.12f);
	}

	AddDiamond(Mesh, FVector2f(X, Middle), 5.f, 8.f, Color);
	AddDiamondOutline(Mesh, FVector2f(X, Middle), 9.f, 13.f, 1.f, WithAlpha(Color, 0.5f));
	AddDiamond(Mesh, FVector2f(X, Top), 2.5f, 5.f, WithAlpha(Color, 0.7f));
	AddDiamond(Mesh, FVector2f(X, Bottom), 2.5f, 5.f, WithAlpha(Color, 0.7f));
}

void AedirUIStyle::AddGradientPanel(FAedirUIMesh& Mesh, const FVector2f& Min, const FVector2f& Max, float FadeStartX, const FLinearColor& Color)
{
	const float StartX = FMath::Clamp(FadeStartX, Min.X, Max.X);
	if (StartX > Min.X)
	{
		Mesh.AddRect(Min, FVector2f(StartX, Max.Y), Color);
	}

	// Three pieces follow a smooth curve, a single linear ramp shows a visible edge where it starts.
	constexpr float CurveX[] = { 0.f, 0.3f, 0.65f, 1.f };
	constexpr float CurveAlpha[] = { 1.f, 0.72f, 0.24f, 0.f };
	for (int32 Index = 0; Index + 1 < UE_ARRAY_COUNT(CurveX); ++Index)
	{
		const float Left = FMath::Lerp(StartX, Max.X, CurveX[Index]);
		const float Right = FMath::Lerp(StartX, Max.X, CurveX[Index + 1]);
		const FLinearColor ColorLeft = WithAlpha(Color, CurveAlpha[Index]);
		const FLinearColor ColorRight = WithAlpha(Color, CurveAlpha[Index + 1]);
		Mesh.AddGradientRect(FVector2f(Left, Min.Y), FVector2f(Right, Max.Y), ColorLeft, ColorRight, ColorRight, ColorLeft);
	}
}

void AedirUIStyle::AddVignette(FAedirUIMesh& Mesh, const FVector2f& Size, float CenterAlpha, float EdgeAlpha)
{
	const auto AlphaAt = [&](int32 CellX, int32 CellY)
	{
		const FVector2f Offset(CellX * 2.f / UIStyleVignetteCells - 1.f, CellY * 2.f / UIStyleVignetteCells - 1.f);
		const float Distance = FMath::Clamp(Offset.Size() / 1.3f, 0.f, 1.f);
		return FLinearColor(0.f, 0.f, 0.f, FMath::Lerp(CenterAlpha, EdgeAlpha, Distance * Distance));
	};

	for (int32 CellY = 0; CellY < UIStyleVignetteCells; ++CellY)
	{
		for (int32 CellX = 0; CellX < UIStyleVignetteCells; ++CellX)
		{
			const FVector2f Min(Size.X * CellX / UIStyleVignetteCells, Size.Y * CellY / UIStyleVignetteCells);
			const FVector2f Max(Size.X * (CellX + 1) / UIStyleVignetteCells, Size.Y * (CellY + 1) / UIStyleVignetteCells);
			Mesh.AddGradientRect(Min, Max, AlphaAt(CellX, CellY), AlphaAt(CellX + 1, CellY), AlphaAt(CellX + 1, CellY + 1), AlphaAt(CellX, CellY + 1));
		}
	}
}

void AedirUIStyle::AddGlow(FAedirUIMesh& Mesh, const FVector2f& Center, const FVector2f& Radii, const FLinearColor& Color)
{
	const auto PointAt = [&](int32 Segment, float Ring)
	{
		const float Angle = UE_TWO_PI * Segment / UIStyleGlowSegments;
		return Center + FVector2f(FMath::Cos(Angle) * Radii.X, FMath::Sin(Angle) * Radii.Y) * Ring;
	};

	for (int32 Ring = 0; Ring < UIStyleGlowRings; ++Ring)
	{
		const float Inner = static_cast<float>(Ring) / UIStyleGlowRings;
		const float Outer = static_cast<float>(Ring + 1) / UIStyleGlowRings;
		// Smooth falloff, a plain linear cone has a visible point in the middle.
		const FLinearColor InnerColor = WithAlpha(Color, FMath::Square(1.f - Inner * Inner));
		const FLinearColor OuterColor = WithAlpha(Color, FMath::Square(1.f - Outer * Outer));
		for (int32 Segment = 0; Segment < UIStyleGlowSegments; ++Segment)
		{
			Mesh.AddQuad(PointAt(Segment, Inner), PointAt(Segment + 1, Inner), PointAt(Segment + 1, Outer), PointAt(Segment, Outer), InnerColor, InnerColor, OuterColor, OuterColor);
		}
	}
}

void AedirUIStyle::AddTaperedBar(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Taper, const FLinearColor& Top, const FLinearColor& Middle, const FLinearColor& Bottom)
{
	if (HalfWidth <= 0.f)
	{
		return;
	}

	const float ClampedTaper = FMath::Min(Taper, HalfWidth);
	const float Body = HalfWidth - ClampedTaper;
	const FVector2f TipLeft = Center - FVector2f(HalfWidth, 0.f);
	const FVector2f TipRight = Center + FVector2f(HalfWidth, 0.f);
	const FVector2f TopLeft = Center + FVector2f(-Body, -HalfHeight);
	const FVector2f TopRight = Center + FVector2f(Body, -HalfHeight);
	const FVector2f BottomLeft = Center + FVector2f(-Body, HalfHeight);
	const FVector2f BottomRight = Center + FVector2f(Body, HalfHeight);
	const FVector2f MiddleLeft = Center - FVector2f(Body, 0.f);
	const FVector2f MiddleRight = Center + FVector2f(Body, 0.f);

	Mesh.AddQuad(MiddleLeft, TopLeft, TopRight, MiddleRight, Middle, Top, Top, Middle);
	Mesh.AddQuad(MiddleLeft, MiddleRight, BottomRight, BottomLeft, Middle, Middle, Bottom, Bottom);
	Mesh.AddQuad(TipLeft, TopLeft, MiddleLeft, BottomLeft, Middle, Top, Middle, Bottom);
	Mesh.AddQuad(TipRight, BottomRight, MiddleRight, TopRight, Middle, Bottom, Middle, Top);

	Mesh.AddSoftEdge(TipLeft, TopLeft, Middle, Top);
	Mesh.AddSoftEdge(TopLeft, TopRight, Top, Top);
	Mesh.AddSoftEdge(TopRight, TipRight, Top, Middle);
	Mesh.AddSoftEdge(TipRight, BottomRight, Middle, Bottom);
	Mesh.AddSoftEdge(BottomRight, BottomLeft, Bottom, Bottom);
	Mesh.AddSoftEdge(BottomLeft, TipLeft, Bottom, Middle);
}

void AedirUIStyle::AddTaperedFrame(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Taper, float Thickness, const FLinearColor& Color)
{
	const float Body = HalfWidth - Taper;
	const FVector2f Points[] =
	{
		Center - FVector2f(HalfWidth, 0.f),
		Center + FVector2f(-Body, -HalfHeight),
		Center + FVector2f(Body, -HalfHeight),
		Center + FVector2f(HalfWidth, 0.f),
		Center + FVector2f(Body, HalfHeight),
		Center + FVector2f(-Body, HalfHeight)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
	{
		Mesh.AddLine(Points[Index], Points[(Index + 1) % UE_ARRAY_COUNT(Points)], Thickness, Color, Color);
	}
}

void AedirUIStyle::AddModalBand(FAedirUIMesh& Mesh, const FVector2f& Size, float CenterY, float HalfHeight)
{
	Mesh.AddRect(FVector2f::ZeroVector, Size, FLinearColor(0.f, 0.f, 0.f, 0.55f));

	const float Top = CenterY - HalfHeight;
	const float Bottom = CenterY + HalfHeight;
	const FLinearColor Band(0.f, 0.f, 0.f, 0.82f);
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);
	const float Middle = Size.X * 0.5f;
	Mesh.AddGradientRect(FVector2f(0.f, Top), FVector2f(Middle, Bottom), Clear, Band, Band, Clear);
	Mesh.AddGradientRect(FVector2f(Middle, Top), FVector2f(Size.X, Bottom), Band, Clear, Clear, Band);

	AddOrnamentRule(Mesh, FVector2f(Middle, Top), Size.X * 0.34f, Metal);
	AddOrnamentRule(Mesh, FVector2f(Middle, Bottom), Size.X * 0.34f, Metal);
}

void AedirUIStyle::DrawMesh(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FAedirUIMesh& Mesh, float Opacity)
{
	if (Mesh.IsEmpty() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return;
	}

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	const FSlateShaderResourceProxy* Proxy = Handle.GetResourceProxy();

	// The brush may live in a texture atlas, so the white texel is looked up through its proxy.
	const FVector2f TexCoord = Proxy ? Proxy->StartUV + Proxy->SizeUV * 0.5f : FVector2f(0.5f, 0.5f);
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();

	TArray<FSlateVertex> Vertices;
	Vertices.Reserve(Mesh.Vertices.Num());
	for (const FAedirUIMesh::FVertex& Vertex : Mesh.Vertices)
	{
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Vertex.Position, TexCoord, FVector2f(1.f, 1.f), WithAlpha(Vertex.Color, Opacity).ToFColor(true)));
	}

	TArray<SlateIndex> Indices;
	Indices.Reserve(Mesh.Indices.Num());
	for (const uint32 Index : Mesh.Indices)
	{
		Indices.Add(static_cast<SlateIndex>(Index));
	}

	FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Vertices, Indices, nullptr, 0, 0);
}

void AedirUIStyle::DrawMesh(FCanvas& Canvas, const FAedirUIMesh& Mesh)
{
	if (Mesh.IsEmpty())
	{
		return;
	}

	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Mesh.Indices.Num() / 3);
	for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
	{
		const FAedirUIMesh::FVertex& A = Mesh.Vertices[Mesh.Indices[Index]];
		const FAedirUIMesh::FVertex& B = Mesh.Vertices[Mesh.Indices[Index + 1]];
		const FAedirUIMesh::FVertex& C = Mesh.Vertices[Mesh.Indices[Index + 2]];

		FCanvasUVTri Triangle;
		Triangle.V0_Pos = FVector2D(A.Position);
		Triangle.V1_Pos = FVector2D(B.Position);
		Triangle.V2_Pos = FVector2D(C.Position);
		Triangle.V0_UV = FVector2D::ZeroVector;
		Triangle.V1_UV = FVector2D::ZeroVector;
		Triangle.V2_UV = FVector2D::ZeroVector;
		Triangle.V0_Color = A.Color;
		Triangle.V1_Color = B.Color;
		Triangle.V2_Color = C.Color;
		Triangles.Add(Triangle);
	}

	FCanvasTriangleItem Item(Triangles, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas.DrawItem(Item);
}

void AedirUIStyle::DrawText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, EAedirTextAlign Align, float ShadowAlpha)
{
	// MakeText does not shape text, so letter spacing has to be applied by placing the characters one by one.
	FSlateFontInfo PlainFont = Font;
	PlainFont.LetterSpacing = 0;
	const float Spacing = Font.Size * UIStylePointsToPixels * Font.LetterSpacing / 1000.f;
	const float Width = MeasureText(Text, PlainFont).X + Spacing * FMath::Max(0, Text.Len() - 1);
	const FVector2f Origin(Position.X - UIStyleTextAlignOffset(Width, Align), Position.Y);

	const auto DrawRun = [&](const FString& Run, const FVector2f& RunPosition)
	{
		if (ShadowAlpha > 0.f)
		{
			const FLinearColor Shadow(0.f, 0.f, 0.f, ShadowAlpha * Color.A);
			FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToOffsetPaintGeometry(RunPosition + FVector2f(UIStyleShadowOffset, UIStyleShadowOffset)), Run, PlainFont, ESlateDrawEffect::None, Shadow);
		}
		FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToOffsetPaintGeometry(RunPosition), Run, PlainFont, ESlateDrawEffect::None, Color);
	};

	if (Spacing <= 0.f)
	{
		DrawRun(Text, Origin);
		return;
	}

	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		// Measuring the whole prefix keeps the kerning of the font.
		const float Offset = MeasureText(Text.Left(Index + 1), PlainFont).X - MeasureText(Text.Mid(Index, 1), PlainFont).X + Spacing * Index;
		DrawRun(Text.Mid(Index, 1), Origin + FVector2f(Offset, 0.f));
	}
}

FVector2f AedirUIStyle::DrawText(UCanvas& Canvas, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, EAedirTextAlign Align, float ShadowAlpha)
{
	// The canvas does not shape text, so letter spacing has to go through its per-character adjustment.
	FSlateFontInfo PlainFont = Font;
	PlainFont.LetterSpacing = 0;
	const float Spacing = Font.Size * UIStylePointsToPixels * Font.LetterSpacing / 1000.f;

	FVector2f Size = MeasureText(Text, PlainFont);
	Size.X += Spacing * FMath::Max(0, Text.Len() - 1);
	const FVector2f Origin(Position.X - UIStyleTextAlignOffset(Size.X, Align), Position.Y);

	FCanvasTextItem Item(FVector2D(Origin), FText::FromString(Text), PlainFont, Color);
	Item.HorizSpacingAdjust = Spacing;
	Item.Font = UIStyleCanvasFontTag();

	if (ShadowAlpha > 0.f && Canvas.Canvas)
	{
		// Four faint copies read as a soft shadow, one hard copy looks like an old bitmap font.
		const float Reach = FMath::Max(1.f, Font.Size * 0.07f);
		const FVector2f Offsets[] = { FVector2f(Reach, Reach), FVector2f(-Reach * 0.6f, Reach * 0.6f), FVector2f(Reach * 0.6f, -Reach * 0.6f), FVector2f(0.f, Reach * 1.6f) };
		Item.SetColor(FLinearColor(0.f, 0.f, 0.f, ShadowAlpha * Color.A * 0.45f));
		for (const FVector2f& Offset : Offsets)
		{
			Item.Position = FVector2D(Origin + Offset);
			Canvas.Canvas->DrawItem(Item);
		}
	}

	if (Canvas.Canvas)
	{
		Item.Position = FVector2D(Origin);
		Item.SetColor(Color);
		Canvas.Canvas->DrawItem(Item);
	}
	return Size;
}

void SAedirUIPaint::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	Painter = InArgs._Painter;
}

int32 SAedirUIPaint::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (Painter)
	{
		FAedirUIMesh Mesh(1.f / FMath::Max(AllottedGeometry.Scale, UE_KINDA_SMALL_NUMBER));
		Painter(AllottedGeometry.GetLocalSize(), Mesh);
		AedirUIStyle::DrawMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh, InWidgetStyle.GetColorAndOpacityTint().A);
	}
	return LayerId;
}
