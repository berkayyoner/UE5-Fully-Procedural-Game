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

	// How much of the blood round it has been washed off, 0 to 255; fresh blood from it brings it back.
	UPROPERTY()
	uint8 Washed255 = 0;

	float GetSeverity() const
	{
		return static_cast<float>(Severity255) / 255.f;
	}
};

// Blood soaked into a piece of clothing, kept on the item wherever it goes: where it is in the bind pose of the body that
// wears it (the clothes are skinned to that pose), how far it spread, how much of it there is (0 to 1) and when blood last
// came into it, from which it dries.
USTRUCT()
struct AEDIR_API FAedirBloodStain
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Where = FVector::ZeroVector;

	UPROPERTY()
	float Radius = 0.f;

	UPROPERTY()
	float Amount = 0.f;

	// World time blood last came into it; saved as seconds before the save.
	UPROPERTY()
	float ShedTime = 0.f;

	// The wound it came from, while worn by the one bleeding, so the same wound soaks the same stain.
	UPROPERTY()
	uint8 WoundId = 0;

	UPROPERTY()
	uint8 Seed = 0;
};
