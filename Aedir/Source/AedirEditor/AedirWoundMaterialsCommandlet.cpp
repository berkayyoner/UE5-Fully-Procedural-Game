#include "AedirWoundMaterialsCommandlet.h"

#include "AedirEditor.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLength.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "RHIShaderPlatform.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* const WoundMatPackageFolder = TEXT("/Game/Aedir/Materials/");

	/** Thin builder over UMaterialEditingLibrary that tracks node creation and link failures. */
	struct FWoundMatBuilder
	{
		explicit FWoundMatBuilder(UMaterial* InMaterial)
			: Material(InMaterial)
		{
		}

		template <typename TExpression>
		TExpression* Make()
		{
			TExpression* Node = Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(Material, TExpression::StaticClass()));
			if (Node)
			{
				++NodeCount;
			}
			else
			{
				bOk = false;
				UE_LOG(LogAedirEditor, Error, TEXT("Failed to create material expression %s."), *TExpression::StaticClass()->GetName());
			}
			return Node;
		}

		void Link(UMaterialExpression* From, UMaterialExpression* To, const TCHAR* InputName)
		{
			bool bConnected = From && To && UMaterialEditingLibrary::ConnectMaterialExpressions(From, TEXT(""), To, InputName);
			// Single-input nodes keep an unnamed pin, so the first input is addressed with an empty name.
			if (!bConnected && From && To && (FCString::Strcmp(InputName, TEXT("Input")) == 0 || FCString::Strcmp(InputName, TEXT("Position")) == 0))
			{
				bConnected = UMaterialEditingLibrary::ConnectMaterialExpressions(From, TEXT(""), To, TEXT(""));
			}
			if (!bConnected)
			{
				bOk = false;
				UE_LOG(LogAedirEditor, Error, TEXT("Failed to connect material expression input %s."), InputName);
			}
		}

		void Output(UMaterialExpression* From, EMaterialProperty Property)
		{
			if (!From || !UMaterialEditingLibrary::ConnectMaterialProperty(From, TEXT(""), Property))
			{
				bOk = false;
				UE_LOG(LogAedirEditor, Error, TEXT("Failed to connect material property %d."), static_cast<int32>(Property));
			}
		}

		UMaterialExpression* Constant(float Value)
		{
			UMaterialExpressionConstant* Node = Make<UMaterialExpressionConstant>();
			if (Node)
			{
				Node->R = Value;
			}
			return Node;
		}

		UMaterialExpression* Constant2(float X, float Y)
		{
			UMaterialExpressionConstant2Vector* Node = Make<UMaterialExpressionConstant2Vector>();
			if (Node)
			{
				Node->R = X;
				Node->G = Y;
			}
			return Node;
		}

		UMaterialExpression* Constant3(const FLinearColor& Value)
		{
			UMaterialExpressionConstant3Vector* Node = Make<UMaterialExpressionConstant3Vector>();
			if (Node)
			{
				Node->Constant = Value;
			}
			return Node;
		}

		UMaterialExpression* Parameter(const TCHAR* Name, float DefaultValue)
		{
			UMaterialExpressionScalarParameter* Node = Make<UMaterialExpressionScalarParameter>();
			if (Node)
			{
				Node->ParameterName = Name;
				Node->DefaultValue = DefaultValue;
			}
			return Node;
		}

		template <typename TExpression>
		UMaterialExpression* Unary(UMaterialExpression* Input)
		{
			TExpression* Node = Make<TExpression>();
			Link(Input, Node, TEXT("Input"));
			return Node;
		}

		template <typename TExpression>
		UMaterialExpression* Binary(UMaterialExpression* A, UMaterialExpression* B)
		{
			TExpression* Node = Make<TExpression>();
			Link(A, Node, TEXT("A"));
			Link(B, Node, TEXT("B"));
			return Node;
		}

		UMaterialExpression* Lerp(UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha)
		{
			UMaterialExpressionLinearInterpolate* Node = Make<UMaterialExpressionLinearInterpolate>();
			Link(A, Node, TEXT("A"));
			Link(B, Node, TEXT("B"));
			Link(Alpha, Node, TEXT("Alpha"));
			return Node;
		}

		UMaterialExpression* SmoothStep(UMaterialExpression* Min, UMaterialExpression* Max, UMaterialExpression* Value)
		{
			UMaterialExpressionSmoothStep* Node = Make<UMaterialExpressionSmoothStep>();
			Link(Min, Node, TEXT("Min"));
			Link(Max, Node, TEXT("Max"));
			Link(Value, Node, TEXT("Value"));
			return Node;
		}

		UMaterialExpression* Noise(UMaterialExpression* Position, float OutputMin, float OutputMax)
		{
			UMaterialExpressionNoise* Node = Make<UMaterialExpressionNoise>();
			if (Node)
			{
				Node->Scale = 1.0f;
				Node->Quality = 1;
				Node->OutputMin = OutputMin;
				Node->OutputMax = OutputMax;
			}
			UMaterialExpressionAppendVector* Volume = Make<UMaterialExpressionAppendVector>();
			Link(Position, Volume, TEXT("A"));
			Link(Constant(0.f), Volume, TEXT("B"));
			Link(Volume, Node, TEXT("Position"));
			return Node;
		}

		UMaterial* Material = nullptr;
		int32 NodeCount = 0;
		bool bOk = true;
	};

	/** UV remapped from [0,1] to [-1,1] around the decal centre. */
	UMaterialExpression* WoundMatCenteredUV(FWoundMatBuilder& B, UMaterialExpression* Coord)
	{
		return B.Binary<UMaterialExpressionMultiply>(
			B.Binary<UMaterialExpressionSubtract>(Coord, B.Constant(0.5f)),
			B.Constant(2.0f));
	}

	/** Irregular radius: r * 0.9 plus a noise term. */
	UMaterialExpression* WoundMatNoisyRadius(FWoundMatBuilder& B, UMaterialExpression* Radius, UMaterialExpression* Noise)
	{
		return B.Binary<UMaterialExpressionAdd>(
			B.Binary<UMaterialExpressionMultiply>(Radius, B.Constant(0.9f)),
			Noise);
	}

	/** Soft edged mask: saturate((1 - r) * 3). */
	UMaterialExpression* WoundMatEdgeMask(FWoundMatBuilder& B, UMaterialExpression* Radius)
	{
		return B.Unary<UMaterialExpressionSaturate>(
			B.Binary<UMaterialExpressionMultiply>(
				B.Binary<UMaterialExpressionSubtract>(B.Constant(1.0f), Radius),
				B.Constant(3.0f)));
	}

	/** Linear ramp from Start over Width, clamped to 0..1. */
	UMaterialExpression* WoundMatRamp(FWoundMatBuilder& B, UMaterialExpression* Value, float Start, float Width)
	{
		return B.Unary<UMaterialExpressionSaturate>(
			B.Binary<UMaterialExpressionDivide>(
				B.Binary<UMaterialExpressionSubtract>(Value, B.Constant(Start)),
				B.Constant(Width)));
	}

	void WoundMatBuildBruise(FWoundMatBuilder& B)
	{
		UMaterialExpression* Coord = B.Make<UMaterialExpressionTextureCoordinate>();
		UMaterialExpression* Centered = WoundMatCenteredUV(B, Coord);
		UMaterialExpression* Noise = B.Noise(B.Binary<UMaterialExpressionMultiply>(Coord, B.Constant(6.0f)), -0.5f, 0.5f);
		UMaterialExpression* Radius = WoundMatNoisyRadius(B, B.Unary<UMaterialExpressionLength>(Centered), Noise);
		UMaterialExpression* Mask = WoundMatEdgeMask(B, Radius);

		UMaterialExpression* Severity = B.Parameter(TEXT("Severity"), 0.5f);
		UMaterialExpression* Age = B.Parameter(TEXT("Age01"), 0.0f);

		// Piecewise ramp: fresh -> dark violet -> violet -> yellow-green -> nearly nothing.
		UMaterialExpression* Stage1 = B.Lerp(
			B.Constant3(FLinearColor(0.32f, 0.09f, 0.14f)),
			B.Constant3(FLinearColor(0.18f, 0.12f, 0.30f)),
			WoundMatRamp(B, Age, 0.0f, 0.35f));
		UMaterialExpression* Stage2 = B.Lerp(
			Stage1,
			B.Constant3(FLinearColor(0.42f, 0.45f, 0.10f)),
			WoundMatRamp(B, Age, 0.35f, 0.40f));
		UMaterialExpression* Colour = B.Lerp(
			Stage2,
			B.Constant3(FLinearColor(0.10f, 0.10f, 0.05f)),
			WoundMatRamp(B, Age, 0.75f, 0.25f));

		UMaterialExpression* AgeFade = B.Binary<UMaterialExpressionSubtract>(
			B.Constant(1.0f),
			B.Binary<UMaterialExpressionMultiply>(Age, Age));
		UMaterialExpression* Opacity = B.Unary<UMaterialExpressionSaturate>(
			B.Binary<UMaterialExpressionMultiply>(
				B.Binary<UMaterialExpressionMultiply>(Mask, Severity),
				B.Binary<UMaterialExpressionMultiply>(AgeFade, B.Constant(0.42f))));

		B.Output(Colour, MP_BaseColor);
		B.Output(Opacity, MP_Opacity);
		B.Output(B.Constant(0.5f), MP_Roughness);
	}

	void WoundMatBuildCut(FWoundMatBuilder& B)
	{
		UMaterialExpression* Coord = B.Make<UMaterialExpressionTextureCoordinate>();
		UMaterialExpression* Centered = WoundMatCenteredUV(B, Coord);
		UMaterialExpression* Noise = B.Noise(B.Binary<UMaterialExpressionMultiply>(Coord, B.Constant(6.0f)), -0.5f, 0.5f);

		// Slit: Y divided by 0.22 gives a thin ellipse elongated along U.
		UMaterialExpression* SlitScaled = B.Binary<UMaterialExpressionMultiply>(Centered, B.Constant2(1.0f, 1.0f / 0.22f));
		UMaterialExpression* SlitMask = WoundMatEdgeMask(
			B,
			WoundMatNoisyRadius(B, B.Unary<UMaterialExpressionLength>(SlitScaled), Noise));

		// Bruised halo: same construction with a much rounder ellipse.
		UMaterialExpression* HaloScaled = B.Binary<UMaterialExpressionMultiply>(Centered, B.Constant2(1.0f, 2.0f));
		UMaterialExpression* HaloMask = WoundMatEdgeMask(
			B,
			WoundMatNoisyRadius(B, B.Unary<UMaterialExpressionLength>(HaloScaled), Noise));

		UMaterialExpression* Severity = B.Parameter(TEXT("Severity"), 0.5f);
		UMaterialExpression* Age = B.Parameter(TEXT("Age01"), 0.0f);

		// Wet blood darkens to dry scab between Age01 0.15 and 0.55.
		UMaterialExpression* ScabRamp = WoundMatRamp(B, Age, 0.15f, 0.40f);
		UMaterialExpression* SlitColour = B.Lerp(
			B.Constant3(FLinearColor(0.30f, 0.0f, 0.005f)),
			B.Constant3(FLinearColor(0.10f, 0.03f, 0.02f)),
			ScabRamp);
		UMaterialExpression* HaloColour = B.Constant3(FLinearColor(0.25f, 0.04f, 0.05f));

		UMaterialExpression* SlitFade = B.Lerp(
			B.Constant(0.9f),
			B.Constant(0.0f),
			B.SmoothStep(B.Constant(0.7f), B.Constant(1.0f), Age));
		UMaterialExpression* SlitOpacity = B.Binary<UMaterialExpressionMultiply>(SlitMask, SlitFade);
		UMaterialExpression* HaloOpacity = B.Binary<UMaterialExpressionMultiply>(
			HaloMask,
			B.Binary<UMaterialExpressionMultiply>(Severity, B.Constant(0.25f)));

		UMaterialExpression* Opacity = B.Unary<UMaterialExpressionSaturate>(
			B.Binary<UMaterialExpressionAdd>(SlitOpacity, HaloOpacity));

		// Blend the two colours by how much of the slit covers this pixel.
		UMaterialExpression* SlitWeight = B.Binary<UMaterialExpressionDivide>(
			SlitOpacity,
			B.Binary<UMaterialExpressionAdd>(
				B.Binary<UMaterialExpressionAdd>(SlitOpacity, HaloOpacity),
				B.Constant(0.0001f)));
		UMaterialExpression* Colour = B.Lerp(HaloColour, SlitColour, SlitWeight);

		UMaterialExpression* Roughness = B.Lerp(B.Constant(0.25f), B.Constant(0.8f), ScabRamp);

		B.Output(Colour, MP_BaseColor);
		B.Output(Opacity, MP_Opacity);
		B.Output(Roughness, MP_Roughness);
	}

	void WoundMatBuildBloodSplat(FWoundMatBuilder& B)
	{
		UMaterialExpression* Coord = B.Make<UMaterialExpressionTextureCoordinate>();
		UMaterialExpression* Centered = WoundMatCenteredUV(B, Coord);
		UMaterialExpression* Seed = B.Parameter(TEXT("Seed"), 0.0f);
		UMaterialExpression* Fade = B.Parameter(TEXT("Fade"), 1.0f);

		UMaterialExpression* LowNoise = B.Noise(
			B.Binary<UMaterialExpressionAdd>(B.Binary<UMaterialExpressionMultiply>(Coord, B.Constant(6.0f)), Seed),
			-0.5f,
			0.5f);
		UMaterialExpression* HighNoise = B.Noise(
			B.Binary<UMaterialExpressionAdd>(B.Binary<UMaterialExpressionMultiply>(Coord, B.Constant(24.0f)), Seed),
			-0.5f,
			0.5f);

		UMaterialExpression* Radius = B.Binary<UMaterialExpressionAdd>(
			WoundMatNoisyRadius(B, B.Unary<UMaterialExpressionLength>(Centered), LowNoise),
			B.Binary<UMaterialExpressionMultiply>(HighNoise, B.Constant(0.2f)));
		UMaterialExpression* Mask = WoundMatEdgeMask(B, Radius);

		UMaterialExpression* Colour = B.Lerp(
			B.Constant3(FLinearColor(0.22f, 0.0f, 0.0f)),
			B.Constant3(FLinearColor(0.12f, 0.01f, 0.01f)),
			B.Unary<UMaterialExpressionSaturate>(Radius));
		UMaterialExpression* Opacity = B.Unary<UMaterialExpressionSaturate>(
			B.Binary<UMaterialExpressionMultiply>(Mask, Fade));

		B.Output(Colour, MP_BaseColor);
		B.Output(Opacity, MP_Opacity);
		B.Output(B.Constant(0.2f), MP_Roughness);
	}

	typedef void (*FWoundMatBuildFunc)(FWoundMatBuilder&);

	struct FWoundMatSpec
	{
		const TCHAR* AssetName;
		FWoundMatBuildFunc Build;
	};

	/** Creates (or rebuilds when forced) one material and saves its package. Returns false on failure; a skipped asset counts as success. */
	bool WoundMatGenerateOne(const FWoundMatSpec& Spec, bool bForce, TArray<UPackage*>& OutPackagesToSave)
	{
		const FString PackageName = FString(WoundMatPackageFolder) + Spec.AssetName;

		if (FPackageName::DoesPackageExist(PackageName) && !bForce)
		{
			UE_LOG(LogAedirEditor, Display, TEXT("Skipped %s (already exists, use -force to rebuild)."), *PackageName);
			return true;
		}

		UPackage* Package = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			Package = CreatePackage(*PackageName);
		}
		if (!Package)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Failed to create package %s."), *PackageName);
			return false;
		}

		UMaterial* Material = FindObject<UMaterial>(Package, Spec.AssetName);
		if (Material)
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		}
		else
		{
			Material = NewObject<UMaterial>(Package, UMaterial::StaticClass(), FName(Spec.AssetName), RF_Public | RF_Standalone);
		}
		if (!Material)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Failed to create material %s."), *PackageName);
			return false;
		}

		Material->MaterialDomain = MD_DeferredDecal;
		Material->BlendMode = BLEND_Translucent;
		Material->DecalBlendMode = DBM_Translucent;

		FWoundMatBuilder Builder(Material);
		Spec.Build(Builder);
		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		UMaterialEditingLibrary::RecompileMaterial(Material);

		bool bOk = Builder.bOk;
		if (UMaterialEditingLibrary::GetNumMaterialExpressions(Material) < Builder.NodeCount)
		{
			UE_LOG(LogAedirEditor, Error, TEXT("%s: expression graph has fewer nodes than were created."), *PackageName);
			bOk = false;
		}
		if (!UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_BaseColor)
			|| !UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Opacity)
			|| !UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Roughness))
		{
			UE_LOG(LogAedirEditor, Error, TEXT("%s: BaseColor, Opacity or Roughness is not connected."), *PackageName);
			bOk = false;
		}

		if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
		{
			for (const FString& CompileError : Resource->GetCompileErrors())
			{
				UE_LOG(LogAedirEditor, Error, TEXT("%s compile error: %s"), *PackageName, *CompileError);
				bOk = false;
			}
		}

		if (!bOk)
		{
			return false;
		}

		Package->MarkPackageDirty();
		OutPackagesToSave.AddUnique(Package);
		UE_LOG(LogAedirEditor, Display, TEXT("Built %s."), *PackageName);
		return true;
	}

	const FWoundMatSpec WoundMatSpecs[] =
	{
		{ TEXT("M_Wound_Bruise"), &WoundMatBuildBruise },
		{ TEXT("M_Wound_Cut"), &WoundMatBuildCut },
		{ TEXT("M_Blood_Splat"), &WoundMatBuildBloodSplat },
	};
}

UAedirWoundMaterialsCommandlet::UAedirWoundMaterialsCommandlet()
{
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

bool UAedirWoundMaterialsCommandlet::GenerateWoundMaterials(bool bForce)
{
	bool bAllOk = true;
	TArray<UPackage*> PackagesToSave;

	for (const FWoundMatSpec& Spec : WoundMatSpecs)
	{
		if (!WoundMatGenerateOne(Spec, bForce, PackagesToSave))
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Failed to generate %s."), Spec.AssetName);
			bAllOk = false;
		}
	}

	if (PackagesToSave.Num() > 0)
	{
		if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false))
		{
			UE_LOG(LogAedirEditor, Error, TEXT("Saving %d wound material packages failed."), PackagesToSave.Num());
			return false;
		}

		UE_LOG(LogAedirEditor, Display, TEXT("Saved %d wound material packages."), PackagesToSave.Num());
	}

	return bAllOk;
}

int32 UAedirWoundMaterialsCommandlet::Main(const FString& Params)
{
	const bool bForce = FParse::Param(*Params, TEXT("force"));

	if (!GenerateWoundMaterials(bForce))
	{
		UE_LOG(LogAedirEditor, Error, TEXT("Wound material generation failed."));
		return 1;
	}

	return 0;
}
