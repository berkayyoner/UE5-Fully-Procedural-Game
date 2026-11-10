#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirSfxSubsystem.generated.h"

class USoundBase;
class USoundAttenuation;

// The sounds of a fight: steel on steel, steel and wood on the world, blows on flesh, cuts, stabs, bones, armour, swings
// through the air and bodies into walls. Each kind has a few takes, chosen at random and pitched a little each time.
UENUM()
enum class EAedirSfx : uint8
{
	Clash,
	MetalHit,
	WoodHit,
	WoodCut,
	Punch,
	BodyImpact,
	Cut,
	Stab,
	Bone,
	Armour,
	Whoosh,
	Count UMETA(Hidden)
};

UCLASS()
class AEDIR_API UAedirSfxSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Plays a sound of this kind at a place, as loud and as low as the blow behind it (0 a touch, 1 a full blow). The same
	// kind at the same place is heard only once within a moment.
	static void Play(const UObject* WorldContext, EAedirSfx Kind, const FVector& Location, float Strength);

	static const TCHAR* GetName(EAedirSfx Kind);

private:
	void PlayAt(EAedirSfx Kind, const FVector& Location, float Strength);
	const TArray<TObjectPtr<USoundBase>>& GetTakes(EAedirSfx Kind);

	// Every take of every kind, kept loaded; Takes holds them by kind.
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> AllTakes;
	TArray<TObjectPtr<USoundBase>> Takes[static_cast<int32>(EAedirSfx::Count)];
	bool bLoaded[static_cast<int32>(EAedirSfx::Count)] = {};

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	struct FRecent
	{
		EAedirSfx Kind;
		FVector Location;
		double Time;
	};
	TArray<FRecent> Recent;
};
