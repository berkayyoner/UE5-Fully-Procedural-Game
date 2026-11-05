#include "AedirAssetPolicySubsystem.h"

#include "AedirEditor.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Subsystems/ImportSubsystem.h"

namespace
{
	constexpr int64 AedirAssetPolicyMaxTextureDimension = 4096;
	constexpr uint32 AedirAssetPolicyNaniteMinTriangles = 2000;
}

void UAedirAssetPolicySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UImportSubsystem>();

	UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
	if (!ImportSubsystem)
	{
		UE_LOG(LogAedirEditor, Warning, TEXT("UImportSubsystem is unavailable, asset policies will not run on import."));
		return;
	}

	ImportSubsystemWeak = ImportSubsystem;
	PostImportHandle = ImportSubsystem->OnAssetPostImport.AddUObject(this, &UAedirAssetPolicySubsystem::HandleAssetPostImport);
	ReimportHandle = ImportSubsystem->OnAssetReimport.AddUObject(this, &UAedirAssetPolicySubsystem::HandleAssetReimport);
}

void UAedirAssetPolicySubsystem::Deinitialize()
{
	if (UImportSubsystem* ImportSubsystem = ImportSubsystemWeak.Get())
	{
		ImportSubsystem->OnAssetPostImport.Remove(PostImportHandle);
		ImportSubsystem->OnAssetReimport.Remove(ReimportHandle);
	}

	ImportSubsystemWeak.Reset();
	PostImportHandle.Reset();
	ReimportHandle.Reset();

	Super::Deinitialize();
}

bool UAedirAssetPolicySubsystem::ApplyTexturePolicy(UTexture2D* Texture)
{
	if (!Texture || !Texture->Source.IsValid())
	{
		return false;
	}

	const int64 SizeX = Texture->Source.GetSizeX();
	const int64 SizeY = Texture->Source.GetSizeY();
	if (SizeX <= AedirAssetPolicyMaxTextureDimension && SizeY <= AedirAssetPolicyMaxTextureDimension)
	{
		return false;
	}

	if (Texture->MaxTextureSize > 0 && Texture->MaxTextureSize <= AedirAssetPolicyMaxTextureDimension)
	{
		return false;
	}

	Texture->Modify();
	Texture->MaxTextureSize = static_cast<int32>(AedirAssetPolicyMaxTextureDimension);
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	UE_LOG(LogAedirEditor, Log, TEXT("Texture %s source %lldx%lld capped to Max Texture Size %lld."),
		*Texture->GetPathName(), SizeX, SizeY, AedirAssetPolicyMaxTextureDimension);

	return true;
}

bool UAedirAssetPolicySubsystem::ApplyNanitePolicy(UStaticMesh* StaticMesh)
{
	if (!StaticMesh || StaticMesh->GetNaniteSettings().bEnabled)
	{
		return false;
	}

	const FString PathName = StaticMesh->GetPathName();
	if (PathName.Contains(TEXT("/UI/")) || PathName.Contains(TEXT("/HUD/")))
	{
		return false;
	}

	if (!StaticMesh->GetRenderData())
	{
		return false;
	}

	const int32 Lod0Triangles = StaticMesh->GetNumTriangles(0);
	if (Lod0Triangles < static_cast<int32>(AedirAssetPolicyNaniteMinTriangles))
	{
		return false;
	}

	StaticMesh->Modify();
	StaticMesh->GetNaniteSettings().bEnabled = true;
	StaticMesh->NotifyNaniteSettingsChanged();
	StaticMesh->MarkPackageDirty();

	UE_LOG(LogAedirEditor, Log, TEXT("Static mesh %s enabled for Nanite (LOD0 triangles %d)."), *PathName, Lod0Triangles);

	return true;
}

void UAedirAssetPolicySubsystem::HandleAssetPostImport(UFactory* Factory, UObject* ImportedObject)
{
	ApplyPoliciesToObject(ImportedObject);
}

void UAedirAssetPolicySubsystem::HandleAssetReimport(UObject* ImportedObject)
{
	ApplyPoliciesToObject(ImportedObject);
}

void UAedirAssetPolicySubsystem::ApplyPoliciesToObject(UObject* Object)
{
	if (UTexture2D* Texture = Cast<UTexture2D>(Object))
	{
		ApplyTexturePolicy(Texture);
	}
	else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
	{
		ApplyNanitePolicy(StaticMesh);
	}
}
