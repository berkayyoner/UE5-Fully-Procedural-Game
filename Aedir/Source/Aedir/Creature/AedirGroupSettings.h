#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AedirCreatureTypes.h"
#include "AedirGroupSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Aedir Groups"))
class AEDIR_API UAedirGroupSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAedirGroupSettings();

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Groups")
	TArray<FName> Groups;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Groups")
	EAedirRelation DefaultRelation = EAedirRelation::Neutral;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Groups")
	EAedirRelation SameGroupRelation = EAedirRelation::Ally;

	UPROPERTY(Config, EditAnywhere, Category="Aedir|Groups")
	TArray<FAedirGroupRelationEntry> Relations;
};
