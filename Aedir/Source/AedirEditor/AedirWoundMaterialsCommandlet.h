#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AedirWoundMaterialsCommandlet.generated.h"

UCLASS()
class UAedirWoundMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAedirWoundMaterialsCommandlet();

	virtual int32 Main(const FString& Params) override;

	/** Builds and saves M_Wound_Bruise, M_Wound_Cut and M_Blood_Splat under /Game/Aedir/Materials. Existing assets are skipped unless bForce is set. Returns true when every asset was created, or skipped, and saved successfully. */
	static bool GenerateWoundMaterials(bool bForce);
};
