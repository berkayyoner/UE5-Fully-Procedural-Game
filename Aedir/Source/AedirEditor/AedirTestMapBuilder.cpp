#include "AedirTestMapBuilder.h"

#include "AedirEditor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const TestMapPath = TEXT("/Game/Aedir/Maps/Lvl_AedirTest");
	const TCHAR* const TestMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirLandscape");
	const TCHAR* const TestSurfaceRoot = TEXT("/Game/Materials/Landscape/Textures/");
	const TCHAR* const TestLayerInfoPath = TEXT("/Game/Aedir/Materials/LI_AedirGround");
	// The one paint layer the land carries. Without a painted layer the landscape gives its components no material permutation
	// of their own and draws them with the engine default instead.
	const FName TestGroundLayer(TEXT("Ground"));

	// The scanned surfaces in the order the blend treats them: the ground cover first, the rock last. Each has its albedo in
	// A, its displacement in D, its normal in N and its roughness in R.
	const TCHAR* const TestSurfaces[] = { TEXT("GrassyLayer"), TEXT("DrySoil"), TEXT("TrampledMud"), TEXT("Mud"), TEXT("WetSoil"), TEXT("StoneyGround") };
	const TCHAR* const TestMaps[] = { TEXT("D"), TEXT("N"), TEXT("R"), TEXT("A") };

	// 16 by 16 components of 63 quads at a metre each: about a kilometre square.
	constexpr int32 TestQuadsPerComponent = 63;
	constexpr int32 TestComponents = 16;
	constexpr int32 TestVerts = TestQuadsPerComponent * TestComponents + 1;
	constexpr float TestScaleXY = 100.f;
	constexpr float TestScaleZ = 100.f;
	constexpr float TestClearingRadiusM = 55.f;
	constexpr float TestClearingEdgeM = 140.f;

	template <typename TExpression>
	TExpression* TestNode(UMaterial* Material, int32 X, int32 Y)
	{
		return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
	}

	void TestDeleteAsset(const TCHAR* PackagePath)
	{
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		if (IFileManager::Get().FileExists(*File))
		{
			IFileManager::Get().Delete(*File, false, true);
		}
	}

	// Height in metres at a point given in metres from the middle of the land.
	float TestHeightAt(float X, float Y)
	{
		const FVector2D P(X, Y);
		const auto Fbm = [](FVector2D Q, int32 Octaves)
		{
			float Sum = 0.f;
			float Amplitude = 1.f;
			float Norm = 0.f;
			for (int32 Octave = 0; Octave < Octaves; ++Octave)
			{
				Sum += FMath::PerlinNoise2D(Q) * Amplitude;
				Norm += Amplitude;
				Q *= 2.03;
				Amplitude *= 0.5f;
			}
			return Sum / Norm;
		};
		const float Distance = static_cast<float>(P.Size());
		// Broad rolling hills and smaller lumps, rising into ridges towards the edge of the land.
		float Height = Fbm(P / 260.0 + FVector2D(13.1, 7.7), 5) * 34.f + Fbm(P / 70.0 + FVector2D(3.3, 1.9), 3) * 5.f;
		Height += FMath::Square(FMath::Max(Distance - 250.f, 0.f) / 250.f) * 70.f;
		// A stream winds past the clearing on the north side.
		const float StreamY = 95.f + 40.f * FMath::Sin(X / 85.f);
		const float FromStream = FMath::Abs(Y - StreamY);
		Height -= 5.5f * FMath::Exp(-FMath::Square(FromStream / 9.f));
		// The clearing in the middle is nearly flat, easing into the hills around it.
		const float Wild = FMath::SmoothStep(TestClearingRadiusM, TestClearingEdgeM, Distance);
		return Height * Wild + 0.4f * Fbm(P / 12.0, 2) * (1.f - Wild);
	}
}

void AedirTestMap::BuildLandscapeMaterial(TArray<UPackage*>& ToSave)
{
	// A package already on disk would be saved again as it was, not as built here, so the old one goes first.
	TestDeleteAsset(TestMaterialPath);
	TestDeleteAsset(TestLayerInfoPath);
	UPackage* Package = CreatePackage(TestMaterialPath);
	UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_AedirLandscape"), RF_Public | RF_Standalone | RF_Transactional);
	ToSave.Add(Package);

	UMaterialExpressionCustom* Blend = TestNode<UMaterialExpressionCustom>(Material, -300, 0);
	Blend->Description = TEXT("AedirGroundBlend");
	Blend->OutputType = CMOT_Float3;
	Blend->Inputs.Reset();

	const auto AddInput = [&](const FString& Name, UMaterialExpression* From, const TCHAR* Output = TEXT(""))
	{
		FCustomInput& Input = Blend->Inputs.AddDefaulted_GetRef();
		Input.InputName = FName(*Name);
		Input.Input.Connect(0, From);
		(void)Output;
	};

	// Where on the ground, which way it faces, and two scales of noise: broad patches and smaller ones, from the world position.
	UMaterialExpressionWorldPosition* Position = TestNode<UMaterialExpressionWorldPosition>(Material, -1600, -400);
	AddInput(TEXT("WP"), Position);
	UMaterialExpressionVertexNormalWS* Normal = TestNode<UMaterialExpressionVertexNormalWS>(Material, -1600, -300);
	AddInput(TEXT("WN"), Normal);
	const auto AddNoise = [&](const TCHAR* Name, float Scale, int32 Y)
	{
		UMaterialExpressionNoise* Noise = TestNode<UMaterialExpressionNoise>(Material, -1400, Y);
		Noise->Scale = Scale;
		Noise->Levels = 4;
		Noise->OutputMin = 0.f;
		Noise->OutputMax = 1.f;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Position, TEXT(""), Noise, TEXT("Position"));
		AddInput(Name, Noise);
	};
	AddNoise(TEXT("Broad"), 0.00025f, -200);
	AddNoise(TEXT("Small"), 0.0012f, -100);
	UMaterialExpressionLandscapeLayerSample* Ground = TestNode<UMaterialExpressionLandscapeLayerSample>(Material, -1400, -500);
	Ground->ParameterName = TestGroundLayer;
	Ground->PreviewWeight = 1.f;
	AddInput(TEXT("Ground"), Ground);

	int32 Y = 0;
	for (int32 Surface = 0; Surface < UE_ARRAY_COUNT(TestSurfaces); ++Surface)
	{
		for (int32 Map = 0; Map < UE_ARRAY_COUNT(TestMaps); ++Map)
		{
			const FString TexName = FString::Printf(TEXT("T_%s_%s"), TestSurfaces[Surface], TestMaps[Map]);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s%s/%s.%s"), TestSurfaceRoot, TestSurfaces[Surface], *TexName, *TexName));
			UMaterialExpressionTextureObject* Object = TestNode<UMaterialExpressionTextureObject>(Material, -1000, Y);
			Y += 60;
			if (Texture)
			{
				Object->Texture = Texture;
				Object->SamplerType = Map == 1 && Texture->CompressionSettings == TC_Normalmap ? SAMPLERTYPE_Normal : (Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor);
			}
			AddInput(FString::Printf(TEXT("T%d%s"), Surface, TestMaps[Map]), Object);
		}
	}

	// Each surface is read twice, at its own scale and at a turned, larger one, and the two are mixed by the broad noise so no
	// tile repeats; the weights come from slope, height above the stream and the noise patches, and the surfaces meet by
	// their own heights so stones stand out of the mud rather than fading into it.
	Blend->Code = TEXT(R"(
float3 n = normalize(WN);
float2 uvA = WP.xy / 280.0;
float2x2 turn = float2x2(0.8, -0.6, 0.6, 0.8);
float2 uvB = mul(turn, WP.xy / 760.0) + float2(0.37, 0.71);
float mixB = saturate((Small - 0.35) * 2.5) * 0.65;
float slope = saturate((1.0 - n.z - 0.28) * 5.0);
float low = saturate((-WP.z - 120.0) / 380.0);
float w[6];
w[0] = 1.0;
w[1] = saturate((Broad - 0.6) * 4.0) * 0.8 + saturate((1.0 - n.z - 0.12) * 4.0) * 0.5;
w[2] = saturate((Small - 0.62) * 5.0) * (1.0 - low);
w[3] = saturate(low * 1.8 - 0.2);
w[4] = saturate(low * 2.6 - 1.4);
w[5] = slope;
float3 col[6]; float3 nrm[6]; float rough[6]; float h[6];
// The scans come grey and are coloured here, as the sample's own material does: green-brown grass, pale dry soil, brown
// trodden and wet mud, grey stone.
float3 tint[6];
tint[0] = float3(0.25, 0.35, 0.11);
tint[1] = float3(0.50, 0.41, 0.30);
tint[2] = float3(0.40, 0.32, 0.23);
tint[3] = float3(0.30, 0.23, 0.16);
tint[4] = float3(0.22, 0.17, 0.12);
tint[5] = float3(0.46, 0.44, 0.41);
#define AEDIR_READ(I, TD, TN, TR, TA) \
	{ float3 d = lerp(Texture2DSample(TA, View.MaterialTextureBilinearWrapedSampler, uvA).rgb, Texture2DSample(TA, View.MaterialTextureBilinearWrapedSampler, uvB).rgb, mixB); \
	  float2 nn = lerp(Texture2DSample(TN, View.MaterialTextureBilinearWrapedSampler, uvA).rg, Texture2DSample(TN, View.MaterialTextureBilinearWrapedSampler, uvB).rg, mixB) * 2.0 - 1.0; \
	  float r = lerp(Texture2DSample(TR, View.MaterialTextureBilinearWrapedSampler, uvA).r, Texture2DSample(TR, View.MaterialTextureBilinearWrapedSampler, uvB).r, mixB); \
	  float a = lerp(Texture2DSample(TD, View.MaterialTextureBilinearWrapedSampler, uvA).r, Texture2DSample(TD, View.MaterialTextureBilinearWrapedSampler, uvB).r, mixB); \
	  col[I] = saturate(dot(d, float3(0.3, 0.59, 0.11)) * tint[I] * 2.4); nrm[I] = float3(nn, sqrt(saturate(1.0 - dot(nn, nn)))); rough[I] = pow(saturate(r), 0.4545); h[I] = pow(saturate(a), 0.4545); }
AEDIR_READ(0, T0D, T0N, T0R, T0A)
AEDIR_READ(1, T1D, T1N, T1R, T1A)
AEDIR_READ(2, T2D, T2N, T2R, T2A)
AEDIR_READ(3, T3D, T3N, T3R, T3A)
AEDIR_READ(4, T4D, T4N, T4R, T4A)
AEDIR_READ(5, T5D, T5N, T5R, T5A)
float top = 0.0;
float b[6];
for (int i = 0; i < 6; i++) { b[i] = w[i] > 0.001 ? w[i] + h[i] * 0.6 + i * 0.02 : -10.0; top = max(top, b[i]); }
float sum = 0.0;
for (int j = 0; j < 6; j++) { b[j] = max(b[j] - top + 0.22, 0.0); sum += b[j]; }
float3 outColor = 0; float3 outNormal = 0; float outRough = 0;
for (int k = 0; k < 6; k++) { float s = b[k] / max(sum, 0.0001); outColor += col[k] * s; outNormal += nrm[k] * s; outRough += rough[k] * s; }
outColor *= lerp(0.84, 1.1, Broad) * lerp(0.94, 1.04, Small) * max(Ground, 1.0);
GroundNormal = normalize(outNormal);
GroundRoughness = saturate(outRough);
return outColor;
)");
	FString DebugFile;
	FString DebugCode;
	if (FParse::Value(FCommandLine::Get(), TEXT("LandDebugFile="), DebugFile) && FFileHelper::LoadFileToString(DebugCode, *DebugFile))
	{
		Blend->Code = DebugCode;
		UE_LOG(LogAedirEditor, Display, TEXT("Land material debug code from %s"), *DebugFile);
	}
	FCustomOutput& NormalOut = Blend->AdditionalOutputs.AddDefaulted_GetRef();
	NormalOut.OutputName = TEXT("GroundNormal");
	NormalOut.OutputType = CMOT_Float3;
	FCustomOutput& RoughOut = Blend->AdditionalOutputs.AddDefaulted_GetRef();
	RoughOut.OutputName = TEXT("GroundRoughness");
	RoughOut.OutputType = CMOT_Float1;
	Blend->RebuildOutputs();

	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT(""), MP_BaseColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundNormal"), MP_Normal);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundRoughness"), MP_Roughness);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();
	// The blend is hand-written, so its compile errors are reported here rather than found as a grey default in the game.
	if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
	{
		Resource->FinishCompilation();
		for (const FString& Error : Resource->GetCompileErrors())
		{
			UE_LOG(LogAedirEditor, Error, TEXT("M_AedirLandscape: %s"), *Error);
		}
	}
	int32 Customs = 0;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
		{
			++Customs;
			UE_LOG(LogAedirEditor, Display, TEXT("Custom %s in %s: %s"), *Custom->GetName(), *GetPathNameSafe(Custom->GetOuter()), *Custom->Code.Left(120).Replace(TEXT("\n"), TEXT(" ")));
		}
	}
	UE_LOG(LogAedirEditor, Display, TEXT("Built %s (%s) with %d expressions, %d custom."), *Material->GetPathName(), *GetNameSafe(Package), Material->GetExpressions().Num(), Customs);
}

void AedirTestMap::BuildLevel()
{
	UPackage* Package = CreatePackage(TestMapPath);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Lvl_AedirTest"), Package);
	if (!World)
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Could not create the test world."));
		return;
	}
	World->SetFlags(RF_Public | RF_Standalone);

	// The land.
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(TestVerts * TestVerts);
	const float Half = (TestVerts - 1) * 0.5f;
	for (int32 Y = 0; Y < TestVerts; ++Y)
	{
		for (int32 X = 0; X < TestVerts; ++X)
		{
			const float Metres = TestHeightAt((X - Half) * TestScaleXY / 100.f, (Y - Half) * TestScaleXY / 100.f);
			const float Units = Metres * 100.f / (TestScaleZ / 128.f);
			Heights[Y * TestVerts + X] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(32768.f + Units), 0, 65535));
		}
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-Half * TestScaleXY, -Half * TestScaleXY, 0.f), FRotator::ZeroRotator);
	Landscape->SetActorScale3D(FVector(TestScaleXY, TestScaleXY, TestScaleZ));
	Landscape->LandscapeMaterial = FParse::Param(FCommandLine::Get(), TEXT("SampleLand")) ? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/Landscape/M_Landscape_Master.M_Landscape_Master")) : LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.M_AedirLandscape"), TestMaterialPath));
	TMap<FGuid, TArray<uint16>> HeightData;
	HeightData.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
	// The ground layer, painted at full weight everywhere; the material does the blending of the surfaces itself.
	UPackage* LayerPackage = CreatePackage(TestLayerInfoPath);
	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(LayerPackage, TEXT("LI_AedirGround"), RF_Public | RF_Standalone | RF_Transactional);
	LayerInfo->SetLayerName(TestGroundLayer, false);
	UEditorLoadingAndSavingUtils::SavePackages({ LayerPackage }, false);
	FLandscapeImportLayerInfo GroundImport(TestGroundLayer);
	GroundImport.LayerInfo = LayerInfo;
	GroundImport.LayerData.Init(255, TestVerts * TestVerts);
	LayerData.Add(FGuid(), TArray<FLandscapeImportLayerInfo>{ GroundImport });
	Landscape->Import(FGuid::NewGuid(), 0, 0, TestVerts - 1, TestVerts - 1, 1, TestQuadsPerComponent, HeightData, nullptr, LayerData, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	Landscape->SetActorLabel(TEXT("AedirLand"));
	if (!Landscape->HasTargetLayer(LayerInfo))
	{
		Landscape->AddTargetLayer(TestGroundLayer, FLandscapeTargetLayerSettings(LayerInfo), false);
	}
	// Set the way the editor sets it, so every component gets its material instance.
	if (FProperty* MaterialProperty = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)))
	{
		Landscape->PreEditChange(MaterialProperty);
		FPropertyChangedEvent Changed(MaterialProperty);
		Landscape->PostEditChangeProperty(Changed);
	}
	Landscape->CreateLandscapeInfo();
	Landscape->UpdateAllComponentMaterialInstances(true);
	int32 Instanced = 0;
	for (const ULandscapeComponent* Component : Landscape->LandscapeComponents)
	{
		Instanced += Component && Component->MaterialInstances.Num() > 0 && Component->MaterialInstances[0] ? 1 : 0;
	}
	UE_LOG(LogAedirEditor, Display, TEXT("Landscape material %s on %d components, %d with their own instance."), *GetNameSafe(Landscape->GetLandscapeMaterial()), Landscape->LandscapeComponents.Num(), Instanced);

	// Sky, sun, air and cloud; the weather drives them in the game.
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 5000.f), FRotator(-38.f, 35.f, 0.f));
	Sun->GetComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetComponent()->SetAtmosphereSunLight(true);
	Sun->GetComponent()->SetIntensity(9.f);
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.f, 0.f, 4000.f), FRotator::ZeroRotator);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator);
	Fog->GetComponent()->SetFogDensity(0.012f);
	Fog->GetComponent()->SetVolumetricFog(true);
	World->SpawnActor<AVolumetricCloud>(FVector::ZeroVector, FRotator::ZeroRotator);
	APostProcessVolume* Post = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator);
	Post->bUnbound = true;
	Post->Settings.bOverride_AutoExposureMinBrightness = true;
	Post->Settings.AutoExposureMinBrightness = 0.6f;
	Post->Settings.bOverride_AutoExposureMaxBrightness = true;
	Post->Settings.AutoExposureMaxBrightness = 2.f;

	const float StartZ = TestHeightAt(0.f, 0.f) * 100.f + 120.f;
	World->SpawnActor<APlayerStart>(FVector(0.f, 0.f, StartZ), FRotator(0.f, 0.f, 0.f));

	const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, TestMapPath);
	UE_LOG(LogAedirEditor, Display, TEXT("Test map saved: %d"), bSaved ? 1 : 0);
	World->DestroyWorld(false);
}
