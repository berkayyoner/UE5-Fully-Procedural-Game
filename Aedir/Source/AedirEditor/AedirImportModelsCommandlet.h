#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AedirImportModelsCommandlet.generated.h"

// Takes models prepared outside the editor, SM_<Name>.fbx with T_<Name>_A, _N and _RMF beside it, into /Game/Megascans/3D_Assets/<Name>.
// -Source=<folder>
UCLASS()
class UAedirImportModelsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAedirImportModelsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
