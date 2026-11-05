#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirCreatureTypes.h"
#include "AedirGroupSubsystem.generated.h"

UCLASS()
class AEDIR_API UAedirGroupSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Aedir|Groups")
	EAedirRelation GetRelation(FName GroupA, FName GroupB) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Groups")
	bool AreEnemies(FName GroupA, FName GroupB) const;
};
