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

	// Every command with how it is written and what it does, one line each.
	TArray<FString> GetHelp();

	// The commands that what has been typed so far could become, with how each is written; stat names after a stats command.
	TArray<FString> GetSuggestions(const FString& Typed);
}
