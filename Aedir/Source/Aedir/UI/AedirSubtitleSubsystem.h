#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AedirSubtitleSubsystem.generated.h"

// One thing said aloud somewhere in the world.
struct FAedirSubtitle
{
	FText Speaker;
	FText Line;
	FVector Location = FVector::ZeroVector;
	double Until = 0.0;
};

// What NPCs say outside a conversation with the player: remarks to the player and talk among themselves. The HUD shows the
// lines spoken within earshot as Skyrim does, the speaker's name before the words, low in the middle of the screen.
UCLASS()
class AEDIR_API UAedirSubtitleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// How long a line of this length takes to say, at a calm pace.
	static float GetSpeakSeconds(const FText& Line);

	void Say(const FText& Speaker, const FText& Line, const FVector& Location, float Seconds);

	// The lines being spoken now within earshot of the point, newest last.
	void GetHeard(const FVector& Listener, TArray<FAedirSubtitle>& OutLines) const;

private:
	TArray<FAedirSubtitle> Lines;
};
