#include "Graphics/AedirScannedMaterial.h"

#include "Components/MeshComponent.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* const ScannedMasterPath = TEXT("/Game/Aedir/Materials/M_AedirScanned.M_AedirScanned");
	const TCHAR* const ScannedRoot = TEXT("/Game/Megascans/3D_Assets/");

	UTexture* ScannedTexture(const FString& Folder, const TCHAR* Suffix)
	{
		const FString Name = FString::Printf(TEXT("T_%s_%s"), *Folder, Suffix);
		return LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("%s%s/%s.%s"), ScannedRoot, *Folder, *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

UMaterialInterface* AedirScannedMaterial::Make(UObject* Outer, const FString& AssetFolder)
{
	// A model from elsewhere comes with its own materials.
	if (AssetFolder.IsEmpty())
	{
		return nullptr;
	}

	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, ScannedMasterPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UTexture* Base = ScannedTexture(AssetFolder, TEXT("A"));
	if (!Master || !Base)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Master, Outer);
	Material->SetTextureParameterValue(TEXT("BaseColor"), Base);
	if (UTexture* Normal = ScannedTexture(AssetFolder, TEXT("N")))
	{
		Material->SetTextureParameterValue(TEXT("Normal"), Normal);
	}
	UTexture* Masks = ScannedTexture(AssetFolder, TEXT("RMF"));
	if (!Masks)
	{
		Masks = ScannedTexture(AssetFolder, TEXT("R"));
	}
	if (Masks)
	{
		Material->SetTextureParameterValue(TEXT("Masks"), Masks);
	}
	return Material;
}

void AedirScannedMaterial::Apply(UMeshComponent* Mesh, const FString& AssetFolder)
{
	if (UMaterialInterface* Material = Mesh ? Make(Mesh, AssetFolder) : nullptr)
	{
		for (int32 Slot = 0; Slot < FMath::Max(Mesh->GetNumMaterials(), 1); ++Slot)
		{
			Mesh->SetMaterial(Slot, Material);
		}
	}
}

FString AedirScannedMaterial::FolderOfPath(const FString& AssetPath)
{
	FString Rest;
	if (!AssetPath.Split(ScannedRoot, nullptr, &Rest))
	{
		return FString();
	}
	FString Folder;
	Rest.Split(TEXT("/"), &Folder, nullptr);
	return Folder;
}
