#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "AedirItemPreview.generated.h"

class AAedirItemActor;
class UMeshComponent;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// The inventory's close look at one item, as in Skyrim: a copy of its model stands far above the world, lit by its own lights
// only, and is filmed into a texture the menu draws. Works while the game is paused, since the menu films it every frame itself.
UCLASS(NotPlaceable, Transient)
class AEDIR_API AAedirItemPreview : public AActor
{
	GENERATED_BODY()

public:
	AAedirItemPreview();

	// Shows this item, or nothing. A new item, or a new look of the same item, starts from the front at the fitted distance.
	void ShowItem(AAedirItemActor* Item);
	bool HasModel() const { return !Copies.IsEmpty(); }

	// Turns the model by a mouse drag, in screen pixels. Sets where the turn is going; the model follows over the next frames.
	void Turn(const FVector2D& Delta);
	// Brings the model nearer for positive steps of the wheel, further for negative ones. Never so near that any of it leaves
	// the picture; the camera glides to the new distance over the next frames.
	void Zoom(float Steps);

	// Moves the model and camera that much of the way towards where the mouse has sent them, then films the model into the
	// texture. Called every frame by the menu, which keeps running while the game is paused.
	void Capture(float DeltaSeconds);

	UTextureRenderTarget2D* GetTexture() const;

	// Where the studio stands: far above and away from anything in a level.
	static FVector GetStudioLocation();

private:
	void ClearCopies();
	void PlaceCameraAndLights();
	void EnsureTexture();

	UPROPERTY()
	TObjectPtr<USceneComponent> Studio;

	// Turns with the mouse; the lights and the camera stay put.
	UPROPERTY()
	TObjectPtr<USceneComponent> Turntable;

	// Carries the copies, moved so the middle of the item sits on the turntable's axis.
	UPROPERTY()
	TObjectPtr<USceneComponent> Model;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Camera;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> RimLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Texture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> Copies;

	TWeakObjectPtr<AAedirItemActor> ShownItem;
	uint32 ShownLook = 0;
	// Shown turn and camera distance, and where the mouse has sent them: the shown ones ease towards the targets every frame.
	FQuat Spin = FQuat::Identity;
	FQuat TargetSpin = FQuat::Identity;
	float Distance = 100.f;
	float TargetDistance = 100.f;
	// The nearest the camera may come: the item's bounding sphere then just touches the edges of the view.
	float NearestDistance = 100.f;
};

// Draws the studio's picture over a soft dark ground; a left drag turns the model, the wheel brings it nearer or further.
class AEDIR_API SAedirItemPreviewView : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAedirItemPreviewView)
		: _DesiredSize(FVector2D(600.f, 600.f))
	{}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPreview(AAedirItemPreview* InPreview) { Preview = InPreview; }
	bool IsTurning() const { return bTurning; }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return DesiredSize; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	TWeakObjectPtr<AAedirItemPreview> Preview;
	FVector2D DesiredSize = FVector2D(600.f, 600.f);
	mutable FSlateBrush Brush;
	bool bTurning = false;
};
