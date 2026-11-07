#include "Game/AedirMenuGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Graphics/AedirScannedMaterial.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "UI/AedirMenuSubsystem.h"

namespace
{
	const FVector MenuEmblemLocation(520.f, -165.f, 45.f);
	constexpr float MenuEmblemTurnPerSecond = 9.f;

	// A band swept along a closed or open path in the X-Z plane, with four-sided section.
	void MenuAddBand(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UV, const TArray<FVector>& Path, const TArray<float>& Widths, float Depth, bool bClosed)
	{
		const int32 Count = Path.Num();
		const FVector Across(0.f, 1.f, 0.f);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 First = Vertices.Num();
			for (int32 Index = 0; Index <= (bClosed ? Count : Count - 1); ++Index)
			{
				const int32 At = Index % Count;
				const FVector Tangent = (Path[(At + 1) % Count] - Path[(At + Count - 1) % Count]).GetSafeNormal();
				const FVector Out = FVector::CrossProduct(Tangent, Across).GetSafeNormal();
				const float Width = Widths[At];
				const FVector Corners[4] = { Out * Width + Across * Depth, Out * Width - Across * Depth, -Out * Width - Across * Depth, -Out * Width + Across * Depth };
				const FVector NormalsOfSide[4] = { Out, -Across, -Out, Across };
				for (int32 Edge = 0; Edge < 2; ++Edge)
				{
					Vertices.Add(Path[At] + Corners[(Side + Edge) % 4]);
					Normals.Add(NormalsOfSide[Side]);
					UV.Add(FVector2D(Index * 0.15, Edge * 0.3));
				}
			}
			for (int32 Index = 0; Index < (bClosed ? Count : Count - 1); ++Index)
			{
				const int32 A = First + Index * 2;
				Triangles.Append({ A, A + 2, A + 1, A + 1, A + 2, A + 3, A, A + 1, A + 2, A + 1, A + 3, A + 2 });
			}
		}
	}
}

AAedirMenuEmblem::AAedirMenuEmblem()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
}

void AAedirMenuEmblem::BeginPlay()
{
	Super::BeginPlay();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV;

	// The ring, swelling and thinning four times round.
	TArray<FVector> Ring;
	TArray<float> RingWidth;
	for (int32 Index = 0; Index < 96; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 96.f;
		Ring.Add(FVector(FMath::Cos(Angle), 0.f, FMath::Sin(Angle)) * 62.f);
		RingWidth.Add(4.f + 2.2f * FMath::Square(FMath::Cos(Angle * 2.f)));
	}
	MenuAddBand(Vertices, Triangles, Normals, UV, Ring, RingWidth, 3.f, true);

	// The blade, point down.
	TArray<FVector> Blade;
	TArray<float> BladeWidth;
	for (int32 Index = 0; Index <= 24; ++Index)
	{
		const float Along = Index / 24.f;
		Blade.Add(FVector(0.f, 0.f, FMath::Lerp(84.f, -96.f, Along)));
		BladeWidth.Add(Along < 0.85f ? FMath::Lerp(6.f, 4.5f, Along) : FMath::Lerp(4.5f, 0.2f, (Along - 0.85f) / 0.15f));
	}
	MenuAddBand(Vertices, Triangles, Normals, UV, Blade, BladeWidth, 1.2f, false);

	// Its guard.
	MenuAddBand(Vertices, Triangles, Normals, UV, { FVector(-34.f, 0.f, 62.f), FVector(-15.f, 0.f, 64.f), FVector(15.f, 0.f, 64.f), FVector(34.f, 0.f, 62.f) }, { 1.5f, 3.f, 3.f, 1.5f }, 2.f, false);

	TArray<FProcMeshTangent> Tangents;
	Mesh->CreateMeshSection(0, Vertices, Triangles, Normals, UV, TArray<FColor>(), Tangents, false);
	AedirScannedMaterial::Apply(Mesh, TEXT("OldMetalPotA"));
	if (UMaterialInstanceDynamic* Iron = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		Iron->SetScalarParameterValue(TEXT("MetalScale"), 0.2f);
	}
}

void AAedirMenuEmblem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AddActorLocalRotation(FRotator(0.f, MenuEmblemTurnPerSecond * DeltaSeconds, 0.f));
}

AAedirMenuGameMode::AAedirMenuGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}

void AAedirMenuGameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	APlayerController* Player = World->GetFirstPlayerController();
	if (!Player)
	{
		return;
	}

	ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetFieldOfView(50.f);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	// The eye settles on the lit iron, and the room around it stays black.
	FPostProcessSettings& Look = Camera->GetCameraComponent()->PostProcessSettings;
	Look.bOverride_AutoExposureMinBrightness = true;
	Look.AutoExposureMinBrightness = 0.5f;
	Look.bOverride_AutoExposureMaxBrightness = true;
	Look.AutoExposureMaxBrightness = 3.f;
	Camera->GetCameraComponent()->PostProcessBlendWeight = 1.f;
	Player->SetViewTarget(Camera);

	World->SpawnActor<AAedirMenuEmblem>(MenuEmblemLocation, FRotator::ZeroRotator);

	// One hard light from above and to the side and a faint cold one from the front. Everything else is dark.
	if (ADirectionalLight* Key = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-52.f, 35.f, 0.f)))
	{
		Key->SetMobility(EComponentMobility::Movable);
		Key->GetLightComponent()->SetIntensity(14.f);
		Key->GetLightComponent()->SetLightColor(FLinearColor(1.f, 0.88f, 0.72f));
	}
	if (ADirectionalLight* Fill = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-5.f, -5.f, 0.f)))
	{
		Fill->SetMobility(EComponentMobility::Movable);
		Fill->GetLightComponent()->SetIntensity(1.6f);
		if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Fill->GetLightComponent()))
		{
			Light->ForwardShadingPriority = -1;
			Light->MarkRenderStateDirty();
		}
		Fill->GetLightComponent()->SetLightColor(FLinearColor(0.55f, 0.62f, 0.8f));
	}

	if (UNiagaraSystem* Smoke = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Effects/NS_WellSmoke.NS_WellSmoke"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, Smoke, MenuEmblemLocation + FVector(30.f, 0.f, -120.f));
	}
	if (USoundBase* Wind = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/_Content/SFX/Amb_Stereo/wav/sfx_Ambient_SpookyTrees_01.sfx_Ambient_SpookyTrees_01"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UGameplayStatics::SpawnSound2D(World, Wind, 0.5f);
	}

	if (const ULocalPlayer* Local = Player->GetLocalPlayer())
	{
		if (UAedirMenuSubsystem* Menus = Local->GetSubsystem<UAedirMenuSubsystem>())
		{
			Menus->OpenMainMenu();
		}
	}

#if !UE_BUILD_SHIPPING
	// Test helper: "-AedirMenuScript=Co-op;Host;New World" picks rows by name, one per second. "!name" takes a screenshot instead.
	FString Script;
	if (FParse::Value(FCommandLine::Get(), TEXT("AedirMenuScript="), Script, false))
	{
		TArray<FString> Steps;
		Script.ParseIntoArray(Steps, TEXT(";"), true);
		float Delay = 2.f;
		for (const FString& Step : Steps)
		{
			FTimerHandle Timer;
			World->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this, Step]()
			{
				APlayerController* Controller = GetWorld()->GetFirstPlayerController();
				const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
				UAedirMenuSubsystem* Menus = Local ? Local->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
				if (Step.StartsWith(TEXT("!")))
				{
					FScreenshotRequest::RequestScreenshot(Step.RightChop(1), true, false);
				}
				else if (Menus)
				{
					Menus->ChooseMainMenuRow(Step);
				}
			}), Delay, false);
			Delay += 1.f;
		}
	}
#endif
}
