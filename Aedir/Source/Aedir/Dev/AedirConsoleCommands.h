#pragma once

#include "CoreMinimal.h"

class AAedirPlayerCharacter;

// The commands of the in-game console, spelled as in Skyrim. Those that change the world run on the server.
namespace AedirConsoleCommands
{
	// True if the first word of the line is one of these commands.
	bool IsCommand(const FString& Line);

	// True if the command acts on a thing and, like enable, takes the selected one when no id is given.
	bool TakesReference(const FString& Line);

	void Run(AAedirPlayerCharacter* Player, const FString& Line);
}
