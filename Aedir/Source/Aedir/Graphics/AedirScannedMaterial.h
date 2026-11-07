#pragma once

#include "CoreMinimal.h"

class UMeshComponent;
class UMaterialInterface;
class UObject;

// Dresses a scanned model in its own maps, found by the naming of the Megascans folders.
namespace AedirScannedMaterial
{
	// Returns a material for the asset folder under /Game/Megascans/3D_Assets, or null when it has no maps.
	AEDIR_API UMaterialInterface* Make(UObject* Outer, const FString& AssetFolder);
	AEDIR_API void Apply(UMeshComponent* Mesh, const FString& AssetFolder);
	// "/Game/Megascans/3D_Assets/Axe/SM_Axe.SM_Axe" gives "Axe".
	AEDIR_API FString FolderOfPath(const FString& AssetPath);
}
