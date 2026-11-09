#include "UI/AedirItemPreview.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InputCoreTypes.h"
#include "Items/AedirItemActor.h"
#include "Rendering/DrawElements.h"
#include "UI/AedirUIStyle.h"

namespace
{
	// About twice the size the menu draws it at on a 1080p screen, so the edges of the model come out smooth without any
	// anti-aliasing in the capture, and still sharp on a 4K screen.
	constexpr int32 PreviewTextureSize = 2048;
	constexpr float PreviewFovDegrees = 30.f;
	// The model is first seen turned a little towards the viewer from above.
	constexpr float PreviewTiltDegrees = 14.f;
	constexpr float PreviewTurnDegreesPerPixel = 0.45f;
	constexpr float PreviewZoomPerStep = 0.88f;
	// Distances as multiples of the nearest one, where the item's bounding sphere just fills the view. A little room is kept
	// even there, since the soft dark ground fades out towards the edges of the picture.
	constexpr float PreviewNearestMargin = 1.06f;
	constexpr float PreviewStartDistance = 1.3f;
	constexpr float PreviewFurthestDistance = 2.6f;
	// How quickly the shown turn and distance close on where the mouse sent them, as in FMath::FInterpTo.
	constexpr float PreviewTurnFollowSpeed = 12.f;
	constexpr float PreviewZoomFollowSpeed = 10.f;
	// Light on the model in lux at its middle: a strong key from the upper left, a soft fill from the right, a rim from behind.
	constexpr float PreviewKeyLux = 7.f;
	constexpr float PreviewFillLux = 2.2f;
	constexpr float PreviewRimLux = 4.5f;
	// Lighting channel 2 belongs to the studio alone: the sun of the level never reaches the model, and these lights nothing else.
	const FVector PreviewKeyDirection(-0.8f, -0.75f, 0.85f);
	const FVector PreviewFillDirection(-0.9f, 0.9f, -0.1f);
	const FVector PreviewRimDirection(0.9f, 0.25f, 0.7f);

	// What makes up the look of an item right now: which of its parts show. Wearing or laying it down changes it.
	uint32 PreviewLookOf(AAedirItemActor* Item)
	{
		uint32 Look = GetTypeHash(Item);
		TInlineComponentArray<UMeshComponent*> Parts(Item);
		for (const UMeshComponent* Part : Parts)
		{
			if (IsValid(Part) && Part->IsVisible())
			{
				Look = HashCombine(Look, GetTypeHash(Part));
			}
		}
		return Look;
	}

	UPointLightComponent* PreviewMakeLight(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UPointLightComponent* Light = Owner->CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(Parent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->IntensityUnits = ELightUnits::Candelas;
		Light->bUseInverseSquaredFalloff = true;
		Light->CastShadows = false;
		Light->LightingChannels.bChannel0 = false;
		Light->LightingChannels.bChannel1 = false;
		Light->LightingChannels.bChannel2 = true;
		Light->SourceRadius = 2.f;
		return Light;
	}
}

AAedirItemPreview::AAedirItemPreview()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	Studio = CreateDefaultSubobject<USceneComponent>(TEXT("Studio"));
	Studio->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Studio);

	Turntable = CreateDefaultSubobject<USceneComponent>(TEXT("Turntable"));
	Turntable->SetupAttachment(Studio);

	Model = CreateDefaultSubobject<USceneComponent>(TEXT("Model"));
	Model->SetupAttachment(Turntable);

	// The camera looks along +X at the turntable; only the copies of the item are drawn, with no sky, fog or light of the level.
	Camera = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Camera"));
	Camera->SetupAttachment(Studio);
	Camera->bCaptureEveryFrame = false;
	Camera->bCaptureOnMovement = false;
	Camera->bAlwaysPersistRenderingState = true;
	Camera->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	// Scene colour keeps how much of each pixel the model covers in its alpha, so the menu can draw it without a box around it.
	Camera->CaptureSource = SCS_SceneColorHDR;
	Camera->FOVAngle = PreviewFovDegrees;
	Camera->bOverride_CustomNearClippingPlane = true;
	Camera->CustomNearClippingPlane = 0.5f;
	Camera->ShowFlags.SetAtmosphere(false);
	Camera->ShowFlags.SetCloud(false);
	Camera->ShowFlags.SetFog(false);
	Camera->ShowFlags.SetVolumetricFog(false);
	Camera->ShowFlags.SetSkyLighting(false);
	Camera->ShowFlags.SetGlobalIllumination(false);
	Camera->ShowFlags.SetLumenGlobalIllumination(false);
	Camera->ShowFlags.SetLumenReflections(false);
	Camera->ShowFlags.SetScreenSpaceReflections(false);
	Camera->ShowFlags.SetAmbientOcclusion(false);
	Camera->ShowFlags.SetDistanceFieldAO(false);
	Camera->ShowFlags.SetEyeAdaptation(false);
	Camera->ShowFlags.SetMotionBlur(false);
	Camera->ShowFlags.SetBloom(false);
	Camera->ShowFlags.SetTemporalAA(false);
	// A fixed exposure: the model looks the same by day and by night.
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.f;
	Camera->PostProcessBlendWeight = 1.f;

	KeyLight = PreviewMakeLight(this, Studio, TEXT("KeyLight"));
	FillLight = PreviewMakeLight(this, Studio, TEXT("FillLight"));
	RimLight = PreviewMakeLight(this, Studio, TEXT("RimLight"));
}

UTextureRenderTarget2D* AAedirItemPreview::GetTexture() const
{
	return Texture;
}

FVector AAedirItemPreview::GetStudioLocation()
{
	// Nine kilometres up: above any terrain and cloud layer, still inside the classic world bounds.
	return FVector(0.f, 0.f, 900000.f);
}

void AAedirItemPreview::EnsureTexture()
{
	if (Texture)
	{
		return;
	}
	Texture = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	// Cleared to nothing covered, which the inverted alpha shows as fully clear.
	Texture->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
	Texture->InitCustomFormat(PreviewTextureSize, PreviewTextureSize, PF_FloatRGBA, true);
	Camera->TextureTarget = Texture;
}

void AAedirItemPreview::ClearCopies()
{
	Camera->ClearShowOnlyComponents();
	for (const TObjectPtr<UMeshComponent>& Copy : Copies)
	{
		if (Copy)
		{
			Copy->DestroyComponent();
		}
	}
	Copies.Reset();
}

void AAedirItemPreview::ShowItem(AAedirItemActor* Item)
{
	const uint32 Look = IsValid(Item) ? PreviewLookOf(Item) : 0;
	if (Item == ShownItem.Get() && Look == ShownLook)
	{
		return;
	}

	ClearCopies();
	ShownItem = Item;
	ShownLook = Look;
	if (!IsValid(Item))
	{
		return;
	}
	EnsureTexture();

	// Every visible part is copied where it sits on the item. A piece worn on a body is shown alone, standing as it was made.
	const FTransform ItemFrame(Item->GetActorQuat(), Item->GetActorLocation());
	FBox Fit(ForceInit);
	bool bLaidOut = false;
	bool bWorn = false;
	TInlineComponentArray<UMeshComponent*> Parts(Item);
	for (UMeshComponent* Part : Parts)
	{
		if (!IsValid(Part) || !Part->IsRegistered() || !Part->IsVisible())
		{
			continue;
		}

		const USceneComponent* Parent = Part->GetAttachParent();
		const bool bOnBody = Parent && Parent->GetOwner() != Item;
		const FTransform Relative = bOnBody ? FTransform::Identity : Part->GetComponentTransform().GetRelativeTransform(ItemFrame);

		UMeshComponent* Copy = nullptr;
		const USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Part);
		if (const UStaticMeshComponent* Solid = Cast<UStaticMeshComponent>(Part))
		{
			if (!Solid->GetStaticMesh() || Part->IsA<UInstancedStaticMeshComponent>())
			{
				continue;
			}
			UStaticMeshComponent* SolidCopy = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
			SolidCopy->SetStaticMesh(Solid->GetStaticMesh());
			Copy = SolidCopy;
		}
		else if (Skinned && Skinned->GetSkinnedAsset())
		{
			UPoseableMeshComponent* PoseCopy = NewObject<UPoseableMeshComponent>(this, NAME_None, RF_Transient);
			PoseCopy->SetSkinnedAssetAndUpdate(Skinned->GetSkinnedAsset());
			Copy = PoseCopy;
		}
		if (!Copy)
		{
			continue;
		}

		Copy->SetupAttachment(Model);
		Copy->SetRelativeTransform(Relative);
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetCastShadow(false);
		Copy->SetLightingChannels(false, false, true);
		Copy->SetVisibleInSceneCaptureOnly(true);
		// Its textures load in full, though nothing in the level is near it.
		Copy->bForceMipStreaming = true;
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			Copy->SetMaterial(Slot, Part->GetMaterial(Slot));
		}
		Copy->RegisterComponent();
		Copies.Add(Copy);
		Camera->ShowOnlyComponent(Copy);

		if (Skinned)
		{
			// Bones the item hides (the other half of a pair laid side by side) stay hidden on the copy.
			const FReferenceSkeleton& Bones = Skinned->GetSkinnedAsset()->GetRefSkeleton();
			UPoseableMeshComponent* PoseCopy = CastChecked<UPoseableMeshComponent>(Copy);
			for (int32 Bone = 0; Bone < Bones.GetNum(); ++Bone)
			{
				if (Skinned->IsBoneHidden(Bone))
				{
					PoseCopy->HideBone(Bone, PBO_None);
				}
			}
			// A worn model is measured by itself; a laid-out piece by the item's own size, its model's bounds cover the whole body.
			if (bOnBody)
			{
				bWorn = true;
				Fit += Copy->CalcBounds(Relative).GetBox();
			}
			else if (Item->Mesh)
			{
				bLaidOut = true;
				Fit += Item->Mesh->CalcBounds(Item->Mesh->GetComponentTransform().GetRelativeTransform(ItemFrame)).GetBox();
			}
		}
		else
		{
			Fit += Copy->CalcBounds(Relative).GetBox();
		}
	}

	if (Copies.IsEmpty() || !Fit.IsValid)
	{
		ClearCopies();
		return;
	}

	// The middle of the item turns on the spot. The sphere around its box holds it however it is turned, so a camera no nearer
	// than where that sphere touches the edges of the (square) view never cuts any of it off.
	Model->SetRelativeLocation(-Fit.GetCenter());
	const float Radius = FMath::Max(static_cast<float>(Fit.GetExtent().Size()), 1.f);
	NearestDistance = Radius / FMath::Sin(FMath::DegreesToRadians(PreviewFovDegrees * 0.5f)) * PreviewNearestMargin;
	TargetDistance = NearestDistance * PreviewStartDistance;
	Distance = TargetDistance;

	// Long items lie across the view; a laid-out piece of armour is stood up with its front to the viewer, a worn one turned to face it.
	// A new item is shown at once in its starting pose, not swung there from the last one.
	const FRotator Facing = bLaidOut && !bWorn ? FRotator(90.f, 0.f, 0.f) : FRotator(0.f, 90.f, 0.f);
	TargetSpin = FQuat(FRotator(PreviewTiltDegrees, 0.f, 0.f)) * FQuat(Facing);
	Spin = TargetSpin;
	PlaceCameraAndLights();
}

void AAedirItemPreview::PlaceCameraAndLights()
{
	Turntable->SetRelativeRotation(Spin);
	Camera->SetRelativeLocationAndRotation(FVector(-Distance, 0.f, 0.f), FRotator::ZeroRotator);

	// The lights keep their brightness on the model however near the camera comes.
	const auto Place = [this](UPointLightComponent* Light, const FVector& Direction, float Lux)
	{
		const FVector At = Direction.GetSafeNormal() * Distance * 1.2f;
		const float Metres = static_cast<float>(At.Size()) / 100.f;
		Light->SetRelativeLocation(At);
		Light->SetIntensity(Lux * Metres * Metres);
		Light->SetAttenuationRadius(Distance * 4.f);
	};
	Place(KeyLight, PreviewKeyDirection, PreviewKeyLux);
	Place(FillLight, PreviewFillDirection, PreviewFillLux);
	Place(RimLight, PreviewRimDirection, PreviewRimLux);
}

void AAedirItemPreview::Turn(const FVector2D& Delta)
{
	if (!HasModel())
	{
		return;
	}
	// Sideways turns it about the upright axis, up and down tips it towards or away from the viewer.
	const FQuat Yaw(FRotator(0.f, static_cast<float>(-Delta.X) * PreviewTurnDegreesPerPixel, 0.f));
	const FQuat Pitch(FRotator(static_cast<float>(Delta.Y) * PreviewTurnDegreesPerPixel, 0.f, 0.f));
	TargetSpin = (Yaw * Pitch * TargetSpin).GetNormalized();
}

void AAedirItemPreview::Zoom(float Steps)
{
	if (!HasModel())
	{
		return;
	}
	TargetDistance = FMath::Clamp(TargetDistance * FMath::Pow(PreviewZoomPerStep, Steps), NearestDistance, NearestDistance * PreviewFurthestDistance);
}

void AAedirItemPreview::Capture(float DeltaSeconds)
{
	if (!HasModel() || !Texture || !Camera->IsRegistered())
	{
		return;
	}

	// The same share of the remaining way every frame, as FMath::FInterpTo takes it: quick at first, settling softly.
	const float TurnShare = FMath::Clamp(DeltaSeconds * PreviewTurnFollowSpeed, 0.f, 1.f);
	Spin = FQuat::Slerp(Spin, TargetSpin, TurnShare).GetNormalized();
	Distance = FMath::FInterpTo(Distance, TargetDistance, DeltaSeconds, PreviewZoomFollowSpeed);
	PlaceCameraAndLights();
	Camera->CaptureScene();
}

void SAedirItemPreviewView::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
}

int32 SAedirItemPreviewView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AAedirItemPreview* Studio = Preview.Get();
	UTextureRenderTarget2D* Picture = Studio ? Studio->GetTexture() : nullptr;
	if (!Studio || !Studio->HasModel() || !IsValid(Picture))
	{
		return LayerId;
	}

	// A soft dark ground behind the model, so it stands clear of the world behind the menu without a frame.
	// The area is wider than tall; the ground is round under the square picture, not stretched across the whole area.
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const float Side = FMath::Min(Size.X, Size.Y);
	FAedirUIMesh Ground(1.f / FMath::Max(AllottedGeometry.Scale, UE_KINDA_SMALL_NUMBER));
	AedirUIStyle::AddGlow(Ground, Size * 0.5f, FVector2f(Side, Side) * 0.5f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
	AedirUIStyle::DrawMesh(OutDrawElements, LayerId, AllottedGeometry, Ground, InWidgetStyle.GetColorAndOpacityTint().A);

	// The capture's alpha says how much of a pixel is empty: inverted it lays the model over the menu with soft, clear edges.
	Brush.SetResourceObject(Picture);
	Brush.ImageSize = FVector2D(static_cast<double>(Picture->SizeX), static_cast<double>(Picture->SizeY));
	Brush.DrawAs = ESlateBrushDrawType::Image;
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		AllottedGeometry.ToPaintGeometry(FVector2f(Side, Side), FSlateLayoutTransform((Size - FVector2f(Side, Side)) * 0.5f)),
		&Brush, ESlateDrawEffect::InvertAlpha | ESlateDrawEffect::PreMultipliedAlpha, FLinearColor::White);
	return LayerId + 1;
}

FReply SAedirItemPreviewView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Preview.IsValid() || !Preview->HasModel())
	{
		return FReply::Unhandled();
	}
	bTurning = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SAedirItemPreviewView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bTurning)
	{
		return FReply::Unhandled();
	}
	bTurning = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SAedirItemPreviewView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bTurning || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	if (AAedirItemPreview* Studio = Preview.Get())
	{
		Studio->Turn(MouseEvent.GetCursorDelta());
	}
	return FReply::Handled();
}

FReply SAedirItemPreviewView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	AAedirItemPreview* Studio = Preview.Get();
	if (!Studio || !Studio->HasModel())
	{
		return FReply::Unhandled();
	}
	Studio->Zoom(MouseEvent.GetWheelDelta());
	return FReply::Handled();
}

void SAedirItemPreviewView::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bTurning = false;
}
