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
	// A fist on the arms, the trunk or the legs, and a fist in the face; each take belongs to a share of the full speed of a
	// punch, and the one nearest the blow's speed is played.
	PunchBody,
	PunchFace,
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
	// kind at the same place is heard only once within a moment. A kind whose takes belong to speeds plays the take nearest
	// SpeedShare (0 the slowest blow, 1 the fastest); left below 0, or for any other kind, a take is chosen at random.
	static void Play(const UObject* WorldContext, EAedirSfx Kind, const FVector& Location, float Strength, float SpeedShare = -1.f);

	static const TCHAR* GetName(EAedirSfx Kind);

private:
	void PlayAt(EAedirSfx Kind, const FVector& Location, float Strength, float SpeedShare);
	const TArray<TObjectPtr<USoundBase>>& GetTakes(EAedirSfx Kind);

	// Every take of every kind, kept loaded; Takes holds them by kind.
	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> AllTakes;
	TArray<TObjectPtr<USoundBase>> Takes[static_cast<int32>(EAedirSfx::Count)];
	// The speed share each loaded take of a kind belongs to, for the kinds that have them.
	TArray<float> TakeSpeeds[static_cast<int32>(EAedirSfx::Count)];
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
