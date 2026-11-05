#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "AedirAssetPolicySubsystem.generated.h"

class UFactory;
class UImportSubsystem;
class UStaticMesh;
class UTexture2D;

UCLASS()
class UAedirAssetPolicySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static bool ApplyTexturePolicy(UTexture2D* Texture);
	static bool ApplyNanitePolicy(UStaticMesh* StaticMesh);

private:
	void HandleAssetPostImport(UFactory* Factory, UObject* ImportedObject);
	void HandleAssetReimport(UObject* ImportedObject);
	static void ApplyPoliciesToObject(UObject* Object);

	TWeakObjectPtr<UImportSubsystem> ImportSubsystemWeak;
	FDelegateHandle PostImportHandle;
	FDelegateHandle ReimportHandle;
};
