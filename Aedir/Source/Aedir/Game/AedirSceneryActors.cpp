#include "Game/AedirSceneryActors.h"

#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

namespace
{
	const TCHAR* const ActorsPlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
	const TCHAR* const SceneryGlowMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirPrecip.M_AedirPrecip");
	const TCHAR* const SceneryWaterMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirStream.M_AedirStream");
	const TCHAR* const ScenerySmokePath = TEXT("/Game/Effects/NS_WellSmoke.NS_WellSmoke");
	const TCHAR* const SceneryFireSoundPath = TEXT("/Game/Audio/_Content/SFX/Fire/wav/Amb_Fire_Burn_Large_Dist_LP_01.Amb_Fire_Burn_Large_Dist_LP_01");
	const TCHAR* const SceneryWaterSoundPath = TEXT("/Game/Aedir/Sounds/S_Rain_Loop.S_Rain_Loop");
	const TCHAR* const ScenerySprayPath = TEXT("/Game/A_Surface_Footstep/Niagara_FX/ParticleSystems/PSN_WaterHeavy_Surface.PSN_WaterHeavy_Surface");
	// The glow of a flame's tongue, hot white-yellow at full and orange as it dies down; a few tongues crossed around the fire.
	const FLinearColor SceneryFlameColor(2.2f, 0.75f, 0.16f);
	constexpr int32 SceneryTongues = 4;
	constexpr float ActorsPlaneCm = 100.f;
	// Water and fire are heard where they are: full near, fading out over a few tens of metres, never inside the head.
	constexpr float SceneryWaterHeardCm = 600.f;
	constexpr float SceneryWaterFadeCm = 4500.f;
	constexpr float SceneryFireHeardCm = 150.f;
	constexpr float SceneryFireFadeCm = 1800.f;

	void MakeHeardInPlace(UAudioComponent* Audio, float InnerCm, float FadeCm)
	{
		FSoundAttenuationSettings Settings;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(InnerCm, 0.f, 0.f);
		Settings.FalloffDistance = FadeCm;
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Settings.bAttenuateWithLPF = true;
		Settings.LPFRadiusMin = InnerCm;
		Settings.LPFRadiusMax = InnerCm + FadeCm;
		Settings.NonSpatializedRadiusStart = InnerCm * 0.5f;
		Settings.NonSpatializedRadiusEnd = InnerCm * 0.25f;
		Audio->bOverrideAttenuation = true;
		Audio->AttenuationOverrides = Settings;
		Audio->bAllowSpatialization = true;
	}

	template <typename T>
	T* ActorsLoad(const TCHAR* Path)
	{
		return LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

AAedirFlame::AAedirFlame()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(RootComponent);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.22f));
	Light->SetAttenuationRadius(1600.f);
	Light->SetSourceRadius(12.f);
	Light->SetCastShadows(true);

	Tongues = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Tongues"));
	Tongues->SetupAttachment(RootComponent);
	Tongues->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tongues->SetCastShadow(false);

	Smoke = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Smoke"));
	Smoke->SetupAttachment(RootComponent);
	Smoke->SetAutoActivate(false);

	Crackle = CreateDefaultSubobject<UAudioComponent>(TEXT("Crackle"));
	Crackle->SetupAttachment(RootComponent);
	Crackle->bAutoActivate = false;
}

void AAedirFlame::Configure(float InFlameHeightCm, float InLightCandelas, bool bInSmoke)
{
	FlameHeightCm = InFlameHeightCm;
	LightCandelas = InLightCandelas;
	bSmoke = bInSmoke;
}

void AAedirFlame::BeginPlay()
{
	Super::BeginPlay();
	Phase = FMath::FRandRange(0.f, 100.f);
	Light->SetRelativeLocation(FVector(0.f, 0.f, FlameHeightCm * 0.6f));
	Light->SetAttenuationRadius(FMath::Max(900.f, FlameHeightCm * 40.f));

	if (UStaticMesh* Plane = ActorsLoad<UStaticMesh>(ActorsPlanePath))
	{
		Tongues->SetStaticMesh(Plane);
		if (UMaterialInterface* Glow = ActorsLoad<UMaterialInterface>(SceneryGlowMaterialPath))
		{
			TongueMaterial = UMaterialInstanceDynamic::Create(Glow, this);
			TongueMaterial->SetVectorParameterValue(TEXT("Color"), SceneryFlameColor);
			Tongues->SetMaterial(0, TongueMaterial);
		}
		// Upright cards crossed around the middle, so the fire looks round from every side.
		for (int32 Index = 0; Index < SceneryTongues; ++Index)
		{
			Tongues->AddInstance(FTransform(FRotator(0.f, Index * 180.f / SceneryTongues, 90.f), FVector::ZeroVector, FVector(0.01f)));
		}
	}
	if (bSmoke)
	{
		if (UNiagaraSystem* SmokeSystem = ActorsLoad<UNiagaraSystem>(ScenerySmokePath))
		{
			Smoke->SetAsset(SmokeSystem);
			Smoke->SetRelativeLocation(FVector(0.f, 0.f, FlameHeightCm));
			Smoke->SetRelativeScale3D(FVector(FlameHeightCm / 120.f));
			Smoke->Activate(true);
		}
	}
	if (USoundWave* Sound = ActorsLoad<USoundWave>(SceneryFireSoundPath))
	{
		Sound->bLooping = true;
		Crackle->SetSound(Sound);
		MakeHeardInPlace(Crackle, SceneryFireHeardCm, SceneryFireFadeCm);
		Crackle->SetVolumeMultiplier(FMath::Clamp(FlameHeightCm / 60.f, 0.2f, 1.f));
		Crackle->Play(FMath::FRandRange(0.f, 5.f));
	}
}

void AAedirFlame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	// Fire breathes: a slow swell, a quicker flutter, and now and then a leap.
	const float Swell = 0.5f + 0.5f * FMath::Sin(Phase * 2.1f) * FMath::Sin(Phase * 0.7f + 1.3f);
	const float Flutter = FMath::PerlinNoise1D(Phase * 9.f);
	const float Flicker = FMath::Clamp(0.75f + 0.18f * Swell + 0.22f * Flutter, 0.35f, 1.2f);
	Light->SetIntensity(LightCandelas * Flicker);
	if (TongueMaterial)
	{
		TongueMaterial->SetScalarParameterValue(TEXT("Strength"), Flicker);
	}
	for (int32 Index = 0; Index < Tongues->GetInstanceCount(); ++Index)
	{
		const float Own = FMath::PerlinNoise1D(Phase * 6.f + Index * 3.7f);
		const float Height = FlameHeightCm * (0.75f + 0.35f * Own + 0.15f * Swell);
		const float Width = FlameHeightCm * (0.45f - 0.08f * Own);
		const FRotator Turn(0.f, Index * 180.f / SceneryTongues + 8.f * FMath::Sin(Phase * 1.3f + Index), 90.f);
		Tongues->UpdateInstanceTransform(Index, FTransform(Turn, FVector(0.f, 0.f, Height * 0.5f), FVector(Width / ActorsPlaneCm, 1.f, Height / ActorsPlaneCm) * FVector(1.f, 1.f, 1.f)), false, Index == Tongues->GetInstanceCount() - 1, true);
	}
}

AAedirWaterfall::AAedirWaterfall()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Sheet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sheet"));
	Sheet->SetupAttachment(RootComponent);
	Sheet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sheet->SetCastShadow(false);
	Roar = CreateDefaultSubobject<UAudioComponent>(TEXT("Roar"));
	Roar->SetupAttachment(RootComponent);
	Roar->bAutoActivate = false;
}

void AAedirWaterfall::Configure(const FVector& Top, const FVector& Bottom, float WidthCm)
{
	SetActorLocation(Top);
	Landing = Bottom;
	Width = WidthCm;
	const FVector Fall = Bottom - Top;
	const FVector Down = Fall.GetSafeNormal();
	Across = FVector::CrossProduct(Down, FVector::UpVector).GetSafeNormal();
	if (Across.IsNearlyZero())
	{
		Across = FVector::RightVector;
	}
	if (UStaticMesh* Plane = ActorsLoad<UStaticMesh>(ActorsPlanePath))
	{
		Sheet->SetStaticMesh(Plane);
		if (UMaterialInterface* Water = ActorsLoad<UMaterialInterface>(SceneryWaterMaterialPath))
		{
			Sheet->SetMaterial(0, Water);
		}
		// The sheet runs from the ledge to the pool along its length, as wide as the fall.
		const FVector Normal = FVector::CrossProduct(Across, Down).GetSafeNormal();
		Sheet->SetWorldRotation(FRotationMatrix::MakeFromXZ(Down, Normal).Rotator());
		Sheet->SetWorldLocation((Top + Bottom) * 0.5);
		Sheet->SetWorldScale3D(FVector(Fall.Size() / ActorsPlaneCm, Width / ActorsPlaneCm, 1.f));
	}
	if (USoundWave* Sound = ActorsLoad<USoundWave>(SceneryWaterSoundPath))
	{
		Sound->bLooping = true;
		Roar->SetSound(Sound);
		MakeHeardInPlace(Roar, SceneryWaterHeardCm, SceneryWaterFadeCm);
		Roar->SetWorldLocation(Bottom);
		Roar->SetVolumeMultiplier(1.f);
		Roar->Play();
	}
}

void AAedirWaterfall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	NextSpray -= DeltaSeconds;
	if (NextSpray > 0.f)
	{
		return;
	}
	// Where the water lands it bursts up again and again along the width of the fall.
	NextSpray = FMath::FRandRange(0.08f, 0.2f);
	if (UNiagaraSystem* Spray = ActorsLoad<UNiagaraSystem>(ScenerySprayPath))
	{
		const FVector Where = Landing + Across * FMath::FRandRange(-0.45f, 0.45f) * Width;
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Spray, Where, FRotator::ZeroRotator, FVector(FMath::FRandRange(0.5f, 0.9f)));
	}
}
