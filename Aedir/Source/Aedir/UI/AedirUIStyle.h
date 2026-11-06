#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

class FCanvas;
class FSlateWindowElementList;
class UCanvas;
struct FGeometry;

// Triangle list with per-vertex colours. Ornaments are built once into a mesh and sent to either Slate or the canvas HUD.
struct AEDIR_API FAedirUIMesh
{
	struct FVertex
	{
		FVector2f Position;
		FLinearColor Color;
	};

	// Feather is the width of the soft edge in mesh units, about one physical pixel.
	explicit FAedirUIMesh(float InFeather = 1.f) : Feather(InFeather) {}

	void AddQuad(const FVector2f& A, const FVector2f& B, const FVector2f& C, const FVector2f& D, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC, const FLinearColor& ColorD);
	void AddRect(const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color);
	void AddGradientRect(const FVector2f& Min, const FVector2f& Max, const FLinearColor& TopLeft, const FLinearColor& TopRight, const FLinearColor& BottomRight, const FLinearColor& BottomLeft);
	// Fills a convex polygon given in clockwise screen order and softens its edge.
	void AddConvex(TConstArrayView<FVector2f> Points, const FLinearColor& Color);
	// Line with soft edges. Thickness may be below a pixel, it then just gets fainter.
	void AddLine(const FVector2f& A, const FVector2f& B, float Thickness, const FLinearColor& ColorA, const FLinearColor& ColorB);
	// Outward facing transparent strip along the edge from A to B.
	void AddSoftEdge(const FVector2f& A, const FVector2f& B, const FLinearColor& ColorA, const FLinearColor& ColorB);
	bool IsEmpty() const { return Indices.IsEmpty(); }
	void Reset();

	TArray<FVertex> Vertices;
	TArray<uint32> Indices;
	float Feather = 1.f;

private:
	uint32 AddVertex(const FVector2f& Position, const FLinearColor& Color);
};

enum class EAedirTextAlign : uint8
{
	Left,
	Center,
	Right
};

// Shared look of the game UI, modelled on the Elder Scrolls menus: pale text, thin metal lines, no boxes.
namespace AedirUIStyle
{
	// Colours are linear, both Slate and the canvas convert them to display space.
	inline const FLinearColor Ivory(FColor(232, 226, 208));
	inline const FLinearColor IvoryBright(FColor(252, 249, 238));
	inline const FLinearColor Metal(FColor(204, 197, 176));
	inline const FLinearColor Ink(FColor(8, 7, 6));

	inline const FLinearColor Panel(0.f, 0.f, 0.f, 0.86f);
	inline const FLinearColor PanelSelected(FColor(58, 52, 40, 242));
	inline const FLinearColor Accent(FColor(240, 228, 192));
	inline const FLinearColor TextColor = Ivory;
	inline const FLinearColor TextDim(FColor(150, 146, 134));
	inline const FLinearColor LineColor(FColor(204, 197, 176, 120));

	// Slate widgets in the game viewport already get this from the DPI curve, the canvas HUD applies it by hand.
	constexpr float ReferenceHeight = 1080.f;

	// Where the double line between list and details stands in the list menus, as a fraction of the width.
	constexpr float ListSeparatorFraction = 0.345f;

	inline float ScaleForHeight(float ViewportHeight)
	{
		return ViewportHeight > 0.f ? ViewportHeight / ReferenceHeight : 1.f;
	}

	inline FLinearColor WithAlpha(const FLinearColor& Color, float AlphaMultiplier)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * AlphaMultiplier);
	}

	// Roboto is the only usable family the engine ships. Its light weight with wide tracking sits close to the Elder Scrolls menu type.
	AEDIR_API FSlateFontInfo LightFont(float Size, int32 LetterSpacing = 0);
	AEDIR_API FSlateFontInfo RegularFont(float Size, int32 LetterSpacing = 0);
	// Spaced uppercase for titles and labels.
	AEDIR_API FSlateFontInfo TitleFont(float Size);

	AEDIR_API FVector2f MeasureText(const FString& Text, const FSlateFontInfo& Font);

	AEDIR_API void AddDiamond(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, const FLinearColor& Color);
	AEDIR_API void AddDiamondOutline(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Thickness, const FLinearColor& Color);
	// Straight line whose ends fade to nothing. The fades are fractions of the length.
	AEDIR_API void AddFadedLine(FAedirUIMesh& Mesh, const FVector2f& A, const FVector2f& B, float Thickness, const FLinearColor& Color, float FadeA = 0.25f, float FadeB = 0.25f);
	// Horizontal rule with a diamond in the middle.
	AEDIR_API void AddOrnamentRule(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfLength, const FLinearColor& Color);
	// Two close vertical lines with diamonds at the ends and in the middle.
	AEDIR_API void AddDoubleLine(FAedirUIMesh& Mesh, float X, float Top, float Bottom, const FLinearColor& Color);
	// Dark from the left edge to FadeStartX, then fading out towards Max.X.
	AEDIR_API void AddGradientPanel(FAedirUIMesh& Mesh, const FVector2f& Min, const FVector2f& Max, float FadeStartX, const FLinearColor& Color);
	// Full screen darkening, stronger towards the edges.
	AEDIR_API void AddVignette(FAedirUIMesh& Mesh, const FVector2f& Size, float CenterAlpha, float EdgeAlpha);
	// Soft elliptical light that fades to nothing at the radii.
	AEDIR_API void AddGlow(FAedirUIMesh& Mesh, const FVector2f& Center, const FVector2f& Radii, const FLinearColor& Color);
	// Long horizontal bar with pointed ends and a vertical colour gradient.
	AEDIR_API void AddTaperedBar(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Taper, const FLinearColor& Top, const FLinearColor& Middle, const FLinearColor& Bottom);
	AEDIR_API void AddTaperedFrame(FAedirUIMesh& Mesh, const FVector2f& Center, float HalfWidth, float HalfHeight, float Taper, float Thickness, const FLinearColor& Color);
	// Dark band across the screen for yes/no questions.
	AEDIR_API void AddModalBand(FAedirUIMesh& Mesh, const FVector2f& Size, float CenterY, float HalfHeight);

	AEDIR_API void DrawMesh(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FAedirUIMesh& Mesh, float Opacity = 1.f);
	AEDIR_API void DrawMesh(FCanvas& Canvas, const FAedirUIMesh& Mesh);

	AEDIR_API void DrawText(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, EAedirTextAlign Align = EAedirTextAlign::Left, float ShadowAlpha = 0.f);
	// Canvas text with a soft shadow. Returns the width and height of the text.
	AEDIR_API FVector2f DrawText(UCanvas& Canvas, const FString& Text, const FSlateFontInfo& Font, const FVector2f& Position, const FLinearColor& Color, EAedirTextAlign Align = EAedirTextAlign::Left, float ShadowAlpha = 0.f);
}

// Paints a mesh built by a callback, for ornaments placed inside regular Slate layouts.
class AEDIR_API SAedirUIPaint : public SLeafWidget
{
public:
	using FPainter = TFunction<void(const FVector2f& Size, FAedirUIMesh& Mesh)>;

	SLATE_BEGIN_ARGS(SAedirUIPaint)
		: _DesiredSize(FVector2D(1.f, 1.f))
	{
		_Visibility = EVisibility::HitTestInvisible;
	}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
		SLATE_ARGUMENT(FPainter, Painter)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return DesiredSize; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FVector2D DesiredSize = FVector2D(1.f, 1.f);
	FPainter Painter;
};
