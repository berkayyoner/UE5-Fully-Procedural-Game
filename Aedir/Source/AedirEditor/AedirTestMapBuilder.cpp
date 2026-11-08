#include "AedirTestMapBuilder.h"

#include "AedirEditor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Game/AedirWorldDirector.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const TestMapPath = TEXT("/Game/Aedir/Maps/Lvl_AedirTest");
	const TCHAR* const TestMaterialPath = TEXT("/Game/Aedir/Materials/M_AedirLandscape");
	const TCHAR* const TestSurfaceRoot = TEXT("/Game/Materials/Landscape/Textures/");
	const TCHAR* const TestLayerInfoRoot = TEXT("/Game/Aedir/Materials/Landscape/");
	const TCHAR* const TestScannedInstanceRoot = TEXT("/Game/Aedir/Materials/Scanned/");
	const TCHAR* const TestFogClass = TEXT("/Game/FogArea/Blueprints/BP_FogArea.BP_FogArea_C");

	// The paint layers of the ground, in the order the material blends them, and the folder of scanned textures each one
	// starts with. A folder holds T_<Folder>_A (base colour), _N (normal), _R (roughness), _D (displacement), and may hold
	// _O or _AO (ambient occlusion) and _M (metallic).
	struct FTestLayer
	{
		const TCHAR* Name;
		const TCHAR* Folder;
		FLinearColor Tint;
	};
	const FTestLayer TestLayers[] = {
		{ TEXT("Grass"), TEXT("GrassyLayer"), FLinearColor(0.25f, 0.35f, 0.11f) },
		{ TEXT("DrySoil"), TEXT("DrySoil"), FLinearColor(0.50f, 0.41f, 0.30f) },
		{ TEXT("TrampledMud"), TEXT("TrampledMud"), FLinearColor(0.40f, 0.32f, 0.23f) },
		{ TEXT("Mud"), TEXT("Mud"), FLinearColor(0.30f, 0.23f, 0.16f) },
		{ TEXT("WetSoil"), TEXT("WetSoil"), FLinearColor(0.22f, 0.17f, 0.12f) },
		{ TEXT("Stone"), TEXT("StoneyGround"), FLinearColor(0.46f, 0.44f, 0.41f) },
	};
	constexpr int32 TestLayerCount = UE_ARRAY_COUNT(TestLayers);

	// The maps of a layer, the parameter each is set by, and the suffixes its texture may have.
	struct FTestMap
	{
		const TCHAR* Param;
		const TCHAR* Suffixes[2];
	};
	const FTestMap TestMaps[] = {
		{ TEXT("BaseColor"), { TEXT("A"), nullptr } },
		{ TEXT("Normal"), { TEXT("N"), nullptr } },
		{ TEXT("Roughness"), { TEXT("R"), nullptr } },
		{ TEXT("Displacement"), { TEXT("D"), nullptr } },
		{ TEXT("AmbientOcclusion"), { TEXT("O"), TEXT("AO") } },
		{ TEXT("Metallic"), { TEXT("M"), nullptr } },
	};
	constexpr int32 TestMapCount = UE_ARRAY_COUNT(TestMaps);
	const TCHAR* const TestMapShort[] = { TEXT("A"), TEXT("N"), TEXT("R"), TEXT("D"), TEXT("O"), TEXT("M") };

	// 16 by 16 components of 63 quads at a metre each: about a kilometre square.
	constexpr int32 TestQuadsPerComponent = 63;
	constexpr int32 TestComponents = 16;
	constexpr int32 TestVerts = TestQuadsPerComponent * TestComponents + 1;
	constexpr float TestScaleXY = 100.f;
	constexpr float TestScaleZ = 100.f;
	constexpr float TestClearingRadiusM = 55.f;
	constexpr float TestClearingEdgeM = 140.f;
	// How far the ground's scanned height pushes the surface in and out, in cm.
	constexpr float TestDisplacementCm = 25.f;

	template <typename TExpression>
	TExpression* TestNode(UMaterial* Material, int32 X, int32 Y)
	{
		return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass(), X, Y));
	}

	// A package already on disk would be saved again as it was, not as built here, so the old one goes first.
	UPackage* TestFreshPackage(const FString& PackagePath)
	{
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		if (IFileManager::Get().FileExists(*File))
		{
			IFileManager::Get().Delete(*File, false, true);
		}
		return CreatePackage(*PackagePath);
	}

	UTexture2D* TestFindTexture(const FTestLayer& Layer, const FTestMap& Map)
	{
		for (const TCHAR* Suffix : Map.Suffixes)
		{
			if (!Suffix)
			{
				continue;
			}
			const FString Name = FString::Printf(TEXT("T_%s_%s"), Layer.Folder, Suffix);
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s%s/%s.%s"), TestSurfaceRoot, Layer.Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Texture;
			}
		}
		return nullptr;
	}

	// A small flat texture standing in for a map a surface does not have: white for occlusion, black for metal, a flat
	// normal. Linear, so any scanned mask can take its place.
	UTexture2D* TestFlatTexture(const TCHAR* Name, FColor Color, bool bNormal, TArray<UPackage*>& ToSave)
	{
		UPackage* Package = TestFreshPackage(FString::Printf(TEXT("/Game/Aedir/Materials/Landscape/%s"), Name));
		UTexture2D* Texture = NewObject<UTexture2D>(Package, Name, RF_Public | RF_Standalone);
		constexpr int32 Size = 4;
		TArray<FColor> Pixels;
		Pixels.Init(Color, Size * Size);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = bNormal ? TC_Normalmap : TC_Masks;
		Texture->PostEditChange();
		ToSave.Add(Package);
		return Texture;
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
		Height -= 5.5f * FMath::Exp(-FMath::Square(AedirTestMap::TestStreamDistance(X, Y) / 9.f));
		// The clearing in the middle is nearly flat, easing into the hills around it.
		const float Wild = FMath::SmoothStep(TestClearingRadiusM, TestClearingEdgeM, Distance);
		return Height * Wild + 0.4f * Fbm(P / 12.0, 2) * (1.f - Wild);
	}
}

float AedirTestMap::TestStreamDistance(float X, float Y)
{
	const float StreamY = 95.f + 40.f * FMath::Sin(X / 85.f);
	return FMath::Abs(Y - StreamY);
}

void AedirTestMap::BuildLandscapeMaterial(TArray<UPackage*>& ToSave)
{
	UPackage* Package = TestFreshPackage(TestMaterialPath);
	UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_AedirLandscape"), RF_Public | RF_Standalone | RF_Transactional);
	ToSave.Add(Package);

	UTexture2D* const Flats[] = {
		nullptr,
		TestFlatTexture(TEXT("T_AedirFlatNormal"), FColor(128, 128, 255), true, ToSave),
		TestFlatTexture(TEXT("T_AedirFlatRough"), FColor(200, 200, 200), false, ToSave),
		TestFlatTexture(TEXT("T_AedirFlatHeight"), FColor(128, 128, 128), false, ToSave),
		TestFlatTexture(TEXT("T_AedirWhite"), FColor::White, false, ToSave),
		TestFlatTexture(TEXT("T_AedirBlack"), FColor::Black, false, ToSave),
	};

	UMaterialExpressionCustom* Blend = TestNode<UMaterialExpressionCustom>(Material, -300, 0);
	Blend->Description = TEXT("AedirGroundLayers");
	Blend->OutputType = CMOT_Float3;
	Blend->Inputs.Reset();
	const auto AddInput = [&](const FString& Name, UMaterialExpression* From)
	{
		FCustomInput& Input = Blend->Inputs.AddDefaulted_GetRef();
		Input.InputName = FName(*Name);
		Input.Input.Connect(0, From);
	};
	const auto AddScalar = [&](const FString& Name, const FName Group, float Value, int32 Y, int32 Sort)
	{
		UMaterialExpressionScalarParameter* Param = TestNode<UMaterialExpressionScalarParameter>(Material, -1300, Y);
		Param->ParameterName = FName(*Name);
		Param->DefaultValue = Value;
		Param->Group = Group;
		Param->SortPriority = Sort;
		AddInput(Name, Param);
	};

	UMaterialExpressionWorldPosition* Position = TestNode<UMaterialExpressionWorldPosition>(Material, -1700, -400);
	AddInput(TEXT("WP"), Position);
	UMaterialExpressionNoise* Noise = TestNode<UMaterialExpressionNoise>(Material, -1500, -300);
	Noise->Scale = 0.0012f;
	Noise->Levels = 4;
	Noise->OutputMin = 0.f;
	Noise->OutputMax = 1.f;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Position, TEXT(""), Noise, TEXT("Position"));
	AddInput(TEXT("Patches"), Noise);
	AddScalar(TEXT("HeightBlend"), TEXT("Blend"), 0.6f, -200, 0);
	AddScalar(TEXT("BlendSoftness"), TEXT("Blend"), 0.22f, -150, 1);

	int32 Y = 0;
	for (int32 Layer = 0; Layer < TestLayerCount; ++Layer)
	{
		const FTestLayer& Def = TestLayers[Layer];
		const FName Group(Def.Name);
		UMaterialExpressionLandscapeLayerSample* Weight = TestNode<UMaterialExpressionLandscapeLayerSample>(Material, -1500, Y);
		Weight->ParameterName = FName(Def.Name);
		Weight->PreviewWeight = Layer == 0 ? 1.f : 0.f;
		AddInput(FString::Printf(TEXT("W%d"), Layer), Weight);
		for (int32 Map = 0; Map < TestMapCount; ++Map)
		{
			UTexture2D* Texture = TestFindTexture(Def, TestMaps[Map]);
			if (!Texture)
			{
				Texture = Map == 0 ? LoadObject<UTexture2D>(nullptr, TEXT("/Game/Materials/Masters/BaseTextures/T_Default_A.T_Default_A")) : Flats[Map];
			}
			UMaterialExpressionTextureObjectParameter* Param = TestNode<UMaterialExpressionTextureObjectParameter>(Material, -1100, Y);
			Y += 60;
			Param->ParameterName = FName(FString::Printf(TEXT("%s_%s"), Def.Name, TestMaps[Map].Param));
			Param->Group = Group;
			Param->SortPriority = Map;
			Param->Texture = Texture;
			Param->SamplerType = Texture && Texture->CompressionSettings == TC_Normalmap ? SAMPLERTYPE_Normal : Texture && Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
			AddInput(FString::Printf(TEXT("T%d%s"), Layer, TestMapShort[Map]), Param);
		}
		AddScalar(FString::Printf(TEXT("%s_TilingCm"), Def.Name), Group, 280.f, Y, 10);
		AddScalar(FString::Printf(TEXT("%s_TintAmount"), Def.Name), Group, 1.f, Y + 30, 11);
		AddScalar(FString::Printf(TEXT("%s_DisplacementScale"), Def.Name), Group, 1.f, Y + 60, 12);
		AddScalar(FString::Printf(TEXT("%s_RoughnessScale"), Def.Name), Group, 1.f, Y + 90, 13);
		UMaterialExpressionVectorParameter* Tint = TestNode<UMaterialExpressionVectorParameter>(Material, -1300, Y + 120);
		Tint->ParameterName = FName(FString::Printf(TEXT("%s_Tint"), Def.Name));
		Tint->DefaultValue = Def.Tint;
		Tint->Group = Group;
		Tint->SortPriority = 14;
		AddInput(FString::Printf(TEXT("L%dTint"), Layer), Tint);
		Y += 180;
	}

	// Each painted layer is read only where it is painted, twice: at its own tiling and at a turned, larger one, mixed by
	// patches of noise so no tile repeats. The layers meet by their own heights, so stones stand out of the mud rather than
	// fading into it. The scans come grey and are coloured by the layer's tint, as much as its tint amount says.
	FString Code = TEXT(R"(
float2x2 turn = float2x2(0.8, -0.6, 0.6, 0.8);
float mixB = saturate((Patches - 0.35) * 2.5) * 0.65;
float2 dx = ddx(WP.xy);
float2 dy = ddy(WP.xy);
float w[6] = { W0, W1, W2, W3, W4, W5 };
// Ground nobody has painted yet is grass.
if (W0 + W1 + W2 + W3 + W4 + W5 < 0.01) { w[0] = 1.0; }
float3 col[6]; float3 nrm[6]; float rough[6]; float h[6]; float ao[6]; float metal[6];
for (int z = 0; z < 6; z++) { col[z] = 0; nrm[z] = float3(0, 0, 1); rough[z] = 0.8; h[z] = 0.5; ao[z] = 1; metal[z] = 0; }
float2 uvA, uvB, dxA, dyA, dxB, dyB;
#define AEDIR_SMP(T) lerp(Texture2DSampleGrad(T, View.MaterialTextureBilinearWrapedSampler, uvA, dxA, dyA), Texture2DSampleGrad(T, View.MaterialTextureBilinearWrapedSampler, uvB, dxB, dyB), mixB)
#define AEDIR_LAYER(I, TA, TN, TR, TD, TO, TM, TIL, TAMT, DSC, RSC, TINT) \
	if (w[I] > 0.003) { \
		float til = max(TIL, 1.0); uvA = WP.xy / til; dxA = dx / til; dyA = dy / til; \
		uvB = mul(turn, WP.xy / (til * 2.7)) + float2(0.37, 0.71); dxB = mul(turn, dx / (til * 2.7)); dyB = mul(turn, dy / (til * 2.7)); \
		float3 d = AEDIR_SMP(TA).rgb; \
		col[I] = lerp(d, saturate(dot(d, float3(0.3, 0.59, 0.11)) * TINT * 2.4), saturate(TAMT)); \
		float2 nn = AEDIR_SMP(TN).rg * 2.0 - 1.0; nrm[I] = float3(nn, sqrt(saturate(1.0 - dot(nn, nn)))); \
		rough[I] = saturate(pow(saturate(AEDIR_SMP(TR).r), 0.4545) * RSC); \
		h[I] = saturate(0.5 + (pow(saturate(AEDIR_SMP(TD).r), 0.4545) - 0.5) * DSC); \
		ao[I] = AEDIR_SMP(TO).r; metal[I] = AEDIR_SMP(TM).r; }
)");
	for (int32 Layer = 0; Layer < TestLayerCount; ++Layer)
	{
		const TCHAR* Name = TestLayers[Layer].Name;
		Code += FString::Printf(TEXT("AEDIR_LAYER(%d, T%dA, T%dN, T%dR, T%dD, T%dO, T%dM, %s_TilingCm, %s_TintAmount, %s_DisplacementScale, %s_RoughnessScale, L%dTint)\n"),
			Layer, Layer, Layer, Layer, Layer, Layer, Layer, Name, Name, Name, Name, Layer);
	}
	Code += TEXT(R"(
float top = -10.0;
float b[6];
for (int i = 0; i < 6; i++) { b[i] = w[i] > 0.003 ? w[i] + h[i] * HeightBlend : -10.0; top = max(top, b[i]); }
float sum = 0.0;
for (int j = 0; j < 6; j++) { b[j] = max(b[j] - top + BlendSoftness, 0.0); sum += b[j]; }
float3 outColor = 0; float3 outNormal = 0; float outRough = 0; float outHeight = 0; float outAO = 0; float outMetal = 0;
for (int k = 0; k < 6; k++) { float s = b[k] / max(sum, 0.0001); outColor += col[k] * s; outNormal += nrm[k] * s; outRough += rough[k] * s; outHeight += h[k] * s; outAO += ao[k] * s; outMetal += metal[k] * s; }
if (sum <= 0.0001) { outColor = 0.2; outNormal = float3(0, 0, 1); outRough = 0.8; outHeight = 0.5; outAO = 1; }
outColor *= lerp(0.9, 1.06, Patches);
GroundNormal = normalize(outNormal);
GroundRoughness = saturate(outRough);
GroundHeight = outHeight;
GroundAO = saturate(outAO);
GroundMetallic = saturate(outMetal);
return outColor;
)");
	Blend->Code = Code;
	const auto AddOutput = [&](const TCHAR* Name, ECustomMaterialOutputType Type)
	{
		FCustomOutput& Out = Blend->AdditionalOutputs.AddDefaulted_GetRef();
		Out.OutputName = Name;
		Out.OutputType = Type;
	};
	AddOutput(TEXT("GroundNormal"), CMOT_Float3);
	AddOutput(TEXT("GroundRoughness"), CMOT_Float1);
	AddOutput(TEXT("GroundHeight"), CMOT_Float1);
	AddOutput(TEXT("GroundAO"), CMOT_Float1);
	AddOutput(TEXT("GroundMetallic"), CMOT_Float1);
	Blend->RebuildOutputs();

	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT(""), MP_BaseColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundNormal"), MP_Normal);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundRoughness"), MP_Roughness);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundAO"), MP_AmbientOcclusion);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundMetallic"), MP_Metallic);
	UMaterialEditingLibrary::ConnectMaterialProperty(Blend, TEXT("GroundHeight"), MP_Displacement);
	// Nanite tessellation lifts the ground by the scanned heights, so pebbles and clods stand out in the silhouette too.
	Material->bEnableTessellation = true;
	// Displacement is in the landscape's own space, which its actor scales up a hundredfold.
	Material->DisplacementScaling.Magnitude = TestDisplacementCm / TestScaleZ;
	Material->DisplacementScaling.Center = 0.5f;
	Material->bUsedWithNanite = true;
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();
	if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
	{
		Resource->FinishCompilation();
		for (const FString& Error : Resource->GetCompileErrors())
		{
			UE_LOG(LogAedirEditor, Error, TEXT("M_AedirLandscape: %s"), *Error);
		}
	}
	UE_LOG(LogAedirEditor, Display, TEXT("Built %s with %d expressions and %d paint layers."), *Material->GetPathName(), Material->GetExpressions().Num(), TestLayerCount);
}

namespace
{
	// Where each layer lies when the land is first made, before anyone paints it: grass everywhere, dry soil in broad
	// patches and on banks, trodden mud in the camp, mud and wet soil down by the stream, stone on the steep slopes.
	void TestStartWeights(float X, float Y, float SlopeZ, float Height, float Out[TestLayerCount])
	{
		const float Broad = FMath::PerlinNoise2D(FVector2D(X, Y) / 120.0 + FVector2D(5.1, 2.7)) * 0.5f + 0.5f;
		const float Small = FMath::PerlinNoise2D(FVector2D(X, Y) / 25.0 + FVector2D(1.3, 8.2)) * 0.5f + 0.5f;
		const float Steep = 1.f - SlopeZ;
		const float FromStream = AedirTestMap::TestStreamDistance(X, Y);
		const float Camp = FMath::Clamp(1.f - (FVector2D(X - 7.f, Y + 1.f).Size() - 6.f) / 7.f, 0.f, 1.f);
		Out[0] = 1.f;
		Out[1] = FMath::Clamp((Broad - 0.6f) * 4.f, 0.f, 1.f) * 0.8f + FMath::Clamp((Steep - 0.12f) * 4.f, 0.f, 1.f) * 0.5f;
		Out[2] = FMath::Max(Camp * (0.6f + Small * 0.6f), FMath::Clamp((Small - 0.72f) * 5.f, 0.f, 1.f) * 0.6f);
		Out[3] = FMath::Clamp(1.f - (FromStream - 6.f) / 8.f, 0.f, 1.f);
		Out[4] = FMath::Clamp(1.f - (FromStream - 2.f) / 4.f, 0.f, 1.f);
		Out[5] = FMath::Clamp((Steep - 0.28f) * 5.f, 0.f, 1.f);
		(void)Height;
	}

	// The placed scanned models wear a material made for them in the game; in the level each gets a saved instance of its
	// own instead, so it can be opened and changed in the editor.
	void TestSaveScannedMaterials(UWorld* World, TArray<UPackage*>& ToSave)
	{
		TMap<FString, UMaterialInstanceConstant*> Made;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UMeshComponent*> Meshes;
			It->GetComponents(Meshes);
			for (UMeshComponent* Mesh : Meshes)
			{
				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
				{
					UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot));
					if (!Dynamic || Mesh->IsEditorOnly() || Mesh->HasAnyFlags(RF_Transient))
					{
						continue;
					}
					UTexture* Base = nullptr;
					Dynamic->GetTextureParameterValue(FName(TEXT("BaseColor")), Base);
					const FString Key = Base ? Base->GetName().Replace(TEXT("T_"), TEXT("")).LeftChop(2) : FString();
					if (Key.IsEmpty())
					{
						continue;
					}
					UMaterialInstanceConstant*& Instance = Made.FindOrAdd(Key);
					if (!Instance)
					{
						const FString Name = FString::Printf(TEXT("MI_%s"), *Key);
						UPackage* Package = TestFreshPackage(FString(TestScannedInstanceRoot) + Name);
						Instance = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone);
						Instance->SetParentEditorOnly(Dynamic->Parent);
						for (const FTextureParameterValue& Value : Dynamic->TextureParameterValues)
						{
							Instance->SetTextureParameterValueEditorOnly(Value.ParameterInfo, Value.ParameterValue);
						}
						for (const FScalarParameterValue& Value : Dynamic->ScalarParameterValues)
						{
							Instance->SetScalarParameterValueEditorOnly(Value.ParameterInfo, Value.ParameterValue);
						}
						Instance->PostEditChange();
						ToSave.Add(Package);
					}
					Mesh->SetMaterial(Slot, Instance);
				}
			}
		}
		UE_LOG(LogAedirEditor, Display, TEXT("Saved %d material instances for the placed models."), Made.Num());
	}
}

void AedirTestMap::BuildLevel()
{
	TArray<UPackage*> ToSave;
	UPackage* Package = CreatePackage(TestMapPath);
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("Lvl_AedirTest"), Package);
	if (!World)
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Could not create the test world."));
		return;
	}
	World->SetFlags(RF_Public | RF_Standalone);

	// The land, and where each layer starts out painted on it.
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(TestVerts * TestVerts);
	TArray<float> Metres;
	Metres.SetNumUninitialized(TestVerts * TestVerts);
	const float Half = (TestVerts - 1) * 0.5f;
	for (int32 Row = 0; Row < TestVerts; ++Row)
	{
		for (int32 Col = 0; Col < TestVerts; ++Col)
		{
			const float H = TestHeightAt((Col - Half) * TestScaleXY / 100.f, (Row - Half) * TestScaleXY / 100.f);
			Metres[Row * TestVerts + Col] = H;
			const float Units = H * 100.f / (TestScaleZ / 128.f);
			Heights[Row * TestVerts + Col] = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(32768.f + Units), 0, 65535));
		}
	}

	TArray<FLandscapeImportLayerInfo> Layers;
	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	for (int32 Layer = 0; Layer < TestLayerCount; ++Layer)
	{
		const FString Name = FString::Printf(TEXT("LI_Aedir%s"), TestLayers[Layer].Name);
		UPackage* LayerPackage = TestFreshPackage(FString(TestLayerInfoRoot) + Name);
		ULandscapeLayerInfoObject* Info = NewObject<ULandscapeLayerInfoObject>(LayerPackage, *Name, RF_Public | RF_Standalone | RF_Transactional);
		Info->SetLayerName(FName(TestLayers[Layer].Name), false);
		LayerInfos.Add(Info);
		ToSave.Add(LayerPackage);
		FLandscapeImportLayerInfo& Import = Layers.Emplace_GetRef(FName(TestLayers[Layer].Name));
		Import.LayerInfo = Info;
		Import.LayerData.SetNumZeroed(TestVerts * TestVerts);
	}
	UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
	for (int32 Row = 0; Row < TestVerts; ++Row)
	{
		for (int32 Col = 0; Col < TestVerts; ++Col)
		{
			const int32 Index = Row * TestVerts + Col;
			const float Dx = Metres[Row * TestVerts + FMath::Min(Col + 1, TestVerts - 1)] - Metres[Row * TestVerts + FMath::Max(Col - 1, 0)];
			const float Dy = Metres[FMath::Min(Row + 1, TestVerts - 1) * TestVerts + Col] - Metres[FMath::Max(Row - 1, 0) * TestVerts + Col];
			const float SlopeZ = static_cast<float>(FVector(-Dx, -Dy, 2.f).GetSafeNormal().Z);
			float Weights[TestLayerCount];
			TestStartWeights((Col - Half) * TestScaleXY / 100.f, (Row - Half) * TestScaleXY / 100.f, SlopeZ, Metres[Index], Weights);
			// Each layer covers what is under it in turn, and the weights of a point always add up to the whole.
			float Left = 1.f;
			float Share[TestLayerCount];
			for (int32 Layer = TestLayerCount - 1; Layer >= 0; --Layer)
			{
				Share[Layer] = FMath::Clamp(Weights[Layer], 0.f, 1.f) * Left;
				Left -= Share[Layer];
			}
			Share[0] += Left;
			int32 Total = 0;
			for (int32 Layer = 1; Layer < TestLayerCount; ++Layer)
			{
				const uint8 Value = static_cast<uint8>(FMath::RoundToInt(Share[Layer] * 255.f));
				Layers[Layer].LayerData[Index] = Value;
				Total += Value;
			}
			Layers[0].LayerData[Index] = static_cast<uint8>(FMath::Max(255 - Total, 0));
		}
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-Half * TestScaleXY, -Half * TestScaleXY, 0.f), FRotator::ZeroRotator);
	Landscape->SetActorScale3D(FVector(TestScaleXY, TestScaleXY, TestScaleZ));
	Landscape->LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.M_AedirLandscape"), TestMaterialPath));
	TMap<FGuid, TArray<uint16>> HeightData;
	HeightData.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> LayerData;
	LayerData.Add(FGuid(), Layers);
	Landscape->Import(FGuid::NewGuid(), 0, 0, TestVerts - 1, TestVerts - 1, 1, TestQuadsPerComponent, HeightData, nullptr, LayerData, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
	Landscape->SetActorLabel(TEXT("AedirLand"));
	for (int32 Layer = 0; Layer < TestLayerCount; ++Layer)
	{
		if (!Landscape->HasTargetLayer(LayerInfos[Layer]))
		{
			Landscape->AddTargetLayer(FName(TestLayers[Layer].Name), FLandscapeTargetLayerSettings(LayerInfos[Layer]), false);
		}
	}
	// Set the way the editor sets it, so every component gets its material instance.
	if (FProperty* MaterialProperty = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)))
	{
		Landscape->PreEditChange(MaterialProperty);
		FPropertyChangedEvent Changed(MaterialProperty);
		Landscape->PostEditChangeProperty(Changed);
	}
	Landscape->CreateLandscapeInfo();
	// The painted layers live in the landscape's edit layer; the weights the game reads are merged from it on the GPU, which
	// the editor does as it ticks and a commandlet has to ask for.
	Landscape->ForceLayersFullUpdate();
	FlushRenderingCommands();
	Landscape->UpdateAllComponentMaterialInstances(true);
	// Drawn as Nanite, which the displacement of the ground needs.
	if (FBoolProperty* NaniteProperty = FindFProperty<FBoolProperty>(ALandscapeProxy::StaticClass(), TEXT("bEnableNanite")))
	{
		NaniteProperty->SetPropertyValue_InContainer(Landscape, true);
	}
	Landscape->UpdateNaniteRepresentation(nullptr);
	Landscape->Tags.Add(AedirWorldDirector::LaidOutTag);
	UE_LOG(LogAedirEditor, Display, TEXT("Landscape material %s on %d components, Nanite %d."), *GetNameSafe(Landscape->GetLandscapeMaterial()), Landscape->LandscapeComponents.Num(), Landscape->HasNaniteComponents() ? 1 : 0);

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

	// Everything that stands on the land, placed now and saved with the level so it can be moved, changed or removed in
	// the editor: the camp, its people, the meadow and forest, the rocks and the ruin.
	World->UpdateWorldComponents(true, false);
	FHitResult Probe;
	const bool bGround = World->LineTraceSingleByChannel(Probe, FVector(0.f, 0.f, 50000.f), FVector(0.f, 0.f, -50000.f), ECC_Visibility);
	UE_LOG(LogAedirEditor, Display, TEXT("Ground under the start: %d at %.0f"), bGround ? 1 : 0, Probe.ImpactPoint.Z);
	AedirWorldDirector::LayOutNatureMap(World);
	TestSaveScannedMaterials(World, ToSave);

	// Ground mist in the stream bed and the hollows, from the fog pack.
	if (UClass* FogClass = LoadClass<AActor>(nullptr, TestFogClass, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const FVector2D Spots[] = { { -160.f, 0.f }, { -60.f, 0.f }, { 40.f, 0.f }, { 140.f, 0.f } };
		for (const FVector2D& Spot : Spots)
		{
			const float X = Spot.X;
			const float StreamY = 95.f + 40.f * FMath::Sin(X / 85.f);
			const FVector Where(X * 100.f, StreamY * 100.f, TestHeightAt(X, StreamY) * 100.f + 150.f);
			if (AActor* Mist = World->SpawnActor<AActor>(FogClass, Where, FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan(40.f / 85.f * FMath::Cos(X / 85.f))), 0.f)))
			{
				Mist->SetActorScale3D(FVector(25.f, 12.f, 4.f));
				Mist->SetActorLabel(TEXT("StreamMist"));
			}
		}
	}
	else
	{
		UE_LOG(LogAedirEditor, Warning, TEXT("The fog pack is not in the project: %s"), TestFogClass);
	}

	UEditorLoadingAndSavingUtils::SavePackages(ToSave, false);
	const bool bSaved = UEditorLoadingAndSavingUtils::SaveMap(World, TestMapPath);
	UE_LOG(LogAedirEditor, Display, TEXT("Test map saved: %d"), bSaved ? 1 : 0);
	World->DestroyWorld(false);
}
