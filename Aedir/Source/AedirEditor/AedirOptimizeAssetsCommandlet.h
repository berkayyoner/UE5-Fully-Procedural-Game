#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AedirOptimizeAssetsCommandlet.generated.h"

UCLASS()
class UAedirOptimizeAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAedirOptimizeAssetsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
