#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirFootprintSubsystem.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;

// Keeps the footprints left in the ground and fades each one out slowly over its life, then takes it away; the oldest go
// first when there are too many.
UCLASS()
class AEDIR_API UAedirFootprintSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// A print just made, as strong as this, to fade out over this many seconds of play. Without a material of its own to
	// fade, it is only taken away at the end.
	void Add(UDecalComponent* Decal, UMaterialInstanceDynamic* Print, float Strength, float LifeSeconds, int32 MaxPrints);

	int32 GetCount() const { return Prints.Num(); }
	// How strong the oldest print still is, as a share of how it was made.
	float GetOldestShare() const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FPrint
	{
		TWeakObjectPtr<UDecalComponent> Decal;
		TWeakObjectPtr<UMaterialInstanceDynamic> Print;
		float Strength = 1.f;
		double BornSeconds = 0.0;
		float LifeSeconds = 1.f;
	};
	TArray<FPrint> Prints;
	// Where the slow walk through the prints, a few each frame, has got to.
	int32 NextToFade = 0;
	float LastShare(const FPrint& Print, double Now) const;
};
