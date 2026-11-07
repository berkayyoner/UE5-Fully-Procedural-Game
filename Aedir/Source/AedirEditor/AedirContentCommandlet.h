#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AedirContentCommandlet.generated.h"

// Builds what the game needs from the editor once: the material scanned models wear, footprint decals and the main menu map, and opens the body mesh up for slicing.
UCLASS()
class UAedirContentCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAedirContentCommandlet();

	virtual int32 Main(const FString& Params) override;
};
