#include "Dev/AedirConsoleCommands.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAedirConsoleSuggestionTest, "Aedir.Console.Suggestions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FAedirConsoleSuggestionTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Nothing typed suggests nothing"), AedirConsoleCommands::GetSuggestions(TEXT("")).IsEmpty());

	const TArray<FString> Stats = AedirConsoleCommands::GetSuggestions(TEXT("player.s"));
	TestTrue(TEXT("player.s offers player.setstats"), Stats.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("player.setstats")); }));
	TestFalse(TEXT("player.s does not offer spawn"), Stats.ContainsByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("spawn")); }));

	const TArray<FString> Names = AedirConsoleCommands::GetSuggestions(TEXT("player.addstats st"));
	TestTrue(TEXT("A stats command offers the stat names"), Names.Num() == 1 && Names[0].Contains(TEXT("strength")) && Names[0].Contains(TEXT("stamina")) && !Names[0].Contains(TEXT("health")));

	const TArray<FString> Usage = AedirConsoleCommands::GetSuggestions(TEXT("setweather "));
	TestTrue(TEXT("A finished command shows how it is written"), Usage.Num() == 1 && Usage[0].Contains(TEXT("Snowy")));

	TestTrue(TEXT("The new commands are known"), AedirConsoleCommands::IsCommand(TEXT("player.removestats health 5")));
	TestTrue(TEXT("Help lists every command"), AedirConsoleCommands::GetHelp().Num() >= 20);
	return true;
}
