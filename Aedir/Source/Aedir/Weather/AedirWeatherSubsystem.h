#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirWeatherSubsystem.generated.h"

class AAedirWeatherManager;
class UWorld;

UCLASS()
class AEDIR_API UAedirWeatherSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintPure, Category="Aedir|Weather")
	AAedirWeatherManager* GetManager() const;

private:
	mutable TWeakObjectPtr<AAedirWeatherManager> CachedManager;
};
