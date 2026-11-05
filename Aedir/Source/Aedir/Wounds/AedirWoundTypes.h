#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "AedirWoundTypes.generated.h"

UENUM(BlueprintType)
enum class EAedirWoundType : uint8
{
	Bruise,
	Cut,
	Puncture
};

// One procedural wound on a creature. Replicated, so keep it compact.
USTRUCT()
struct AEDIR_API FAedirWound
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Id = 0;

	UPROPERTY()
	FName Bone = NAME_None;

	// Bone-local position of the wound on the skin.
	UPROPERTY()
	FVector_NetQuantize Offset = FVector::ZeroVector;

	// Bone-local outward surface normal at the wound.
	UPROPERTY()
	FVector_NetQuantizeNormal Normal = FVector::UpVector;

	UPROPERTY()
	EAedirWoundType Type = EAedirWoundType::Bruise;

	UPROPERTY()
	uint8 Severity255 = 0;

	// Server world time in seconds when the wound was opened or last reopened.
	UPROPERTY()
	float StartServerTime = 0.f;

	float GetSeverity() const
	{
		return static_cast<float>(Severity255) / 255.f;
	}
};
