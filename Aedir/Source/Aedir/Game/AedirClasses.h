#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"

// The classes the game spawns its people and things from: the Blueprint made from each in /Game/Aedir/Blueprints when it is
// there, so what is changed in it shows in the game, and the C++ class itself when it is not.
namespace AedirClasses
{
	template <typename TNative>
	UClass* Find(const TCHAR* BlueprintName)
	{
		static TWeakObjectPtr<UClass> Cached;
		if (!Cached.IsValid())
		{
			const FString Path = FString::Printf(TEXT("/Game/Aedir/Blueprints/%s.%s_C"), BlueprintName, BlueprintName);
			UClass* Loaded = LoadClass<TNative>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			Cached = Loaded ? Loaded : TNative::StaticClass();
		}
		return Cached.Get();
	}
}
