#include "Dev/AedirConsoleCommands.h"

#include "Aedir.h"
#include "Components/PrimitiveComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "Dev/AedirDevTools.h"
#include "Engine/World.h"
#include "Game/AedirEntities.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemCatalog.h"
#include "Npc/AedirNpc.h"
#include "Npc/AedirTrainingDummy.h"
#include "Player/AedirPlayerCharacter.h"

namespace
{
	constexpr int32 ConsoleMaxAmount = 200;
	constexpr float ConsoleSpawnAheadCm = 160.f;
	constexpr float ConsoleSpawnUpCm = 60.f;
	constexpr float ConsoleLethalDamage = 1.0e6f;

	const TCHAR* const ConsoleVerbs[] = { TEXT("help"), TEXT("player.additem"), TEXT("additem"), TEXT("player.removeitem"), TEXT("removeitem"), TEXT("spawn"), TEXT("placeatme"), TEXT("player.placeatme"), TEXT("enable"), TEXT("disable"), TEXT("delete"), TEXT("markfordelete"), TEXT("prid"), TEXT("kill"), TEXT("moveto"), TEXT("player.moveto"), TEXT("getpos"), TEXT("setscale"), TEXT("player.setav"), TEXT("player.getav"), TEXT("tgm"), TEXT("sw"), TEXT("setweather"), TEXT("showinventory"), TEXT("player.showinventory") };
	const TCHAR* const ConsoleRefVerbs[] = { TEXT("enable"), TEXT("disable"), TEXT("delete"), TEXT("markfordelete"), TEXT("kill"), TEXT("moveto"), TEXT("player.moveto"), TEXT("getpos"), TEXT("setscale") };

	void ConsoleReport(const FString& Message)
	{
		UE_LOG(LogAedir, Display, TEXT("[Console] %s"), *Message);
		UAedirDevTools::OnReported().Broadcast(Message);
	}

	FString ConsoleDescribe(const AActor* Actor)
	{
		const FName Base = AedirEntities::BaseNameOfActor(Actor);
		return FString::Printf(TEXT("%s  Ref %08X  Base %08X (%s)"), *AedirEntities::DisplayNameOfActor(Actor), AedirEntities::RefIdOfActor(Actor), AedirEntities::BaseIdOfName(Base), *Base.ToString());
	}

	AActor* ConsoleFindRef(const AAedirPlayerCharacter* Player, const TArray<FString>& Words, int32 Index)
	{
		uint32 Ref = 0;
		const UAedirEntitySubsystem* Entities = Player->GetWorld()->GetSubsystem<UAedirEntitySubsystem>();
		AActor* Actor = Words.IsValidIndex(Index) && AedirEntities::ParseHex(Words[Index], Ref) && Entities ? Entities->FindByRef(Ref) : nullptr;
		if (!Actor)
		{
			ConsoleReport(Words.IsValidIndex(Index) ? FString::Printf(TEXT("Nothing has the reference %s."), *Words[Index]) : FString(TEXT("Click something first, or give its reference id.")));
		}
		return Actor;
	}

	int32 ConsoleAmount(const TArray<FString>& Words, int32 Index)
	{
		return FMath::Clamp(Words.IsValidIndex(Index) ? FCString::Atoi(*Words[Index]) : 1, 1, ConsoleMaxAmount);
	}

	void ConsoleForgetHeld(AAedirItemActor* Item)
	{
		AAedirCreature* Holder = Item->Holder.Get();
		if (Holder && Holder->Interaction)
		{
			Holder->Interaction->RemoveOwnedItem(Item);
		}
		else
		{
			Item->Destroy();
		}
	}

	void ConsoleAddItem(AAedirPlayerCharacter* Player, const TArray<FString>& Words)
	{
		const FName Base = Words.IsValidIndex(1) ? AedirEntities::FindBaseName(Words[1]) : NAME_None;
		if (!UAedirItemCatalog::Find(Base) || !Player->Interaction)
		{
			ConsoleReport(TEXT("Usage: player.additem <BaseID or name> [amount]. Only items go into an inventory."));
			return;
		}
		const int32 Amount = ConsoleAmount(Words, 2);
		for (int32 Index = 0; Index < Amount; ++Index)
		{
			if (AAedirCatalogItem* Item = AAedirCatalogItem::SpawnCatalogItem(Player->GetWorld(), Base, FTransform(Player->GetActorLocation())))
			{
				Player->Interaction->StowNewItem(Item);
			}
		}
		ConsoleReport(FString::Printf(TEXT("Added %d %s to the inventory."), Amount, *Base.ToString()));
	}

	void ConsoleRemoveItem(AAedirPlayerCharacter* Player, const TArray<FString>& Words)
	{
		const FName Base = Words.IsValidIndex(1) ? AedirEntities::FindBaseName(Words[1]) : NAME_None;
		UAedirInteractionComponent* Hands = Player->Interaction.Get();
		if (Base.IsNone() || !Hands)
		{
			ConsoleReport(TEXT("Usage: player.removeitem <BaseID or name> [amount]."));
			return;
		}

		// From the inventory first, then whatever hangs on the belt, then the hands.
		TArray<AAedirItemActor*> Owned = Hands->GetBackpackItemsCopy();
		Owned.Append(Hands->GetWaistItemsCopy());
		Owned.Add(Hands->GetHeldItem(EAedirHand::Right));
		Owned.AddUnique(Hands->GetHeldItem(EAedirHand::Left));
		int32 Left = ConsoleAmount(Words, 2);
		int32 Removed = 0;
		for (AAedirItemActor* Item : Owned)
		{
			if (Left > 0 && AedirEntities::BaseNameOfActor(Item) == Base)
			{
				Hands->RemoveOwnedItem(Item);
				--Left;
				++Removed;
			}
		}
		ConsoleReport(FString::Printf(TEXT("Removed %d %s."), Removed, *Base.ToString()));
	}

	void ConsoleSpawn(AAedirPlayerCharacter* Player, const TArray<FString>& Words)
	{
		const FName Base = Words.IsValidIndex(1) ? AedirEntities::FindBaseName(Words[1]) : NAME_None;
		if (Base.IsNone() || Base == FName(TEXT("Player")))
		{
			ConsoleReport(TEXT("Usage: spawn <BaseID or name> [amount]. Type help for the list."));
			return;
		}

		UWorld* World = Player->GetWorld();
		const int32 Amount = ConsoleAmount(Words, 2);
		for (int32 Index = 0; Index < Amount; ++Index)
		{
			const FVector Where = Player->GetActorLocation() + Player->GetActorForwardVector() * (ConsoleSpawnAheadCm + Index * 40.f) + FVector(0.f, 0.f, ConsoleSpawnUpCm);
			const FRotator Facing(0.f, Player->GetActorRotation().Yaw + 180.f, 0.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			AActor* Spawned = nullptr;
			if (UAedirItemCatalog::Find(Base))
			{
				Spawned = AAedirCatalogItem::SpawnCatalogItem(World, Base, FTransform(Facing, Where));
			}
			else if (Base == FName(TEXT("TrainingDummy")))
			{
				Spawned = World->SpawnActor<AAedirTrainingDummy>(AAedirTrainingDummy::StaticClass(), Where, FRotator::ZeroRotator, Params);
			}
			else if (Base == FName(TEXT("Creature")))
			{
				Spawned = World->SpawnActor<AAedirCreature>(AAedirCreature::StaticClass(), Where, Facing, Params);
			}
			else if (AAedirNpc* Npc = World->SpawnActor<AAedirNpc>(AAedirNpc::StaticClass(), Where, Facing, Params))
			{
				const bool bEnemy = Base == FName(TEXT("NpcEnemy"));
				const bool bFollower = Base == FName(TEXT("NpcFollower"));
				Npc->InitNpc(bEnemy ? EAedirNpcRole::Enemy : bFollower ? EAedirNpcRole::Follower : EAedirNpcRole::Neutral, FText::FromString(bEnemy ? TEXT("Bandit") : bFollower ? TEXT("Sellsword") : TEXT("Villager")), FText::FromString(TEXT("Nobody you would have heard of.")));
				Spawned = Npc;
			}
			if (Spawned)
			{
				ConsoleReport(TEXT("Spawned ") + ConsoleDescribe(Spawned));
			}
		}
	}

	void ConsoleSetEnabled(AActor* Actor, bool bEnabled)
	{
		UAedirEntityIdComponent* Id = Actor->FindComponentByClass<UAedirEntityIdComponent>();
		if (!Id || Id->bDisabled != bEnabled)
		{
			ConsoleReport(FString::Printf(TEXT("Already %s."), bEnabled ? TEXT("enabled") : TEXT("disabled")));
			return;
		}

		Id->bDisabled = !bEnabled;
		UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
		if (!bEnabled)
		{
			Id->bWasSimulating = Root && Root->IsSimulatingPhysics();
			if (Id->bWasSimulating)
			{
				Root->SetSimulatePhysics(false);
			}
			Id->ComponentsStopped.Reset();
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component->IsComponentTickEnabled())
				{
					Component->SetComponentTickEnabled(false);
					Id->ComponentsStopped.Add(Component);
				}
			}
		}
		else
		{
			for (UActorComponent* Component : Id->ComponentsStopped)
			{
				if (IsValid(Component))
				{
					Component->SetComponentTickEnabled(true);
				}
			}
			Id->ComponentsStopped.Reset();
			if (Id->bWasSimulating && Root)
			{
				Root->SetSimulatePhysics(true);
			}
		}
		Actor->SetActorHiddenInGame(!bEnabled);
		Actor->SetActorEnableCollision(bEnabled);
		Actor->SetActorTickEnabled(bEnabled);
		ConsoleReport(FString::Printf(TEXT("%s %s"), bEnabled ? TEXT("Enabled") : TEXT("Disabled"), *ConsoleDescribe(Actor)));
	}

	void ConsoleDelete(AAedirPlayerCharacter* Player, AActor* Actor)
	{
		if (Cast<AAedirPlayerCharacter>(Actor))
		{
			ConsoleReport(TEXT("A player cannot be deleted."));
			return;
		}
		ConsoleReport(TEXT("Deleted ") + ConsoleDescribe(Actor));
		if (AAedirItemActor* Item = Cast<AAedirItemActor>(Actor))
		{
			ConsoleForgetHeld(Item);
			return;
		}
		if (AAedirNpc* Npc = Cast<AAedirNpc>(Actor))
		{
			Npc->RestoreGear(TArray<FName>());
		}
		Actor->Destroy();
	}

	void ConsoleHelp(const TArray<FString>& Words)
	{
		const FString Filter = Words.IsValidIndex(1) ? Words[1] : FString();
		if (Filter.IsEmpty())
		{
			ConsoleReport(TEXT("player.additem, player.removeitem, spawn, enable, disable, delete, prid, moveto, kill, getpos, setscale, player.setav, player.getav, tgm, setweather, showinventory. Click something to select it; a click elsewhere clears it. help <word> lists base ids."));
		}
		for (const FName& Name : AedirEntities::GetAllBaseNames())
		{
			if (!Filter.IsEmpty() && (Filter == TEXT("*") || Name.ToString().Contains(Filter)))
			{
				ConsoleReport(FString::Printf(TEXT("%08X  %s"), AedirEntities::BaseIdOfName(Name), *Name.ToString()));
			}
		}
	}

	void ConsoleShowInventory(const AAedirPlayerCharacter* Player)
	{
		if (!Player->Interaction)
		{
			return;
		}
		TMap<FName, int32> Counts;
		TArray<AAedirItemActor*> Owned = Player->Interaction->GetBackpackItemsCopy();
		Owned.Append(Player->Interaction->GetWaistItemsCopy());
		for (const AAedirItemActor* Item : Owned)
		{
			if (Item)
			{
				++Counts.FindOrAdd(AedirEntities::BaseNameOfActor(Item));
			}
		}
		for (const TPair<FName, int32>& Entry : Counts)
		{
			ConsoleReport(FString::Printf(TEXT("%08X  %s x%d"), AedirEntities::BaseIdOfName(Entry.Key), *Entry.Key.ToString(), Entry.Value));
		}
		ConsoleReport(FString::Printf(TEXT("%d kinds of thing carried."), Counts.Num()));
	}

	void ConsoleGetAv(const AAedirPlayerCharacter* Player, const TArray<FString>& Words)
	{
		const UAedirStatsComponent* Stats = Player->Stats.Get();
		const FString What = Words.IsValidIndex(1) ? Words[1].ToLower() : FString();
		if (Stats)
		{
			const float Value = What == TEXT("health") ? Stats->GetHealth() : What == TEXT("stamina") ? Stats->GetStamina() : What == TEXT("magicka") ? Stats->GetMana() : What == TEXT("strength") ? Stats->Strength : What == TEXT("athletics") ? Stats->Athletism : What == TEXT("hunger") ? Stats->GetHunger() : What == TEXT("thirst") ? Stats->GetThirst() : -1.f;
			ConsoleReport(Value >= 0.f ? FString::Printf(TEXT("%s = %.1f"), *What, Value) : FString(TEXT("Usage: player.getav health|stamina|magicka|strength|athletics|hunger|thirst")));
		}
	}

	void ConsoleSetAv(AAedirPlayerCharacter* Player, const TArray<FString>& Words)
	{
		UAedirStatsComponent* Stats = Player->Stats.Get();
		const FString What = Words.IsValidIndex(1) ? Words[1].ToLower() : FString();
		const float Value = Words.IsValidIndex(2) ? FCString::Atof(*Words[2]) : 0.f;
		if (!Stats || !Words.IsValidIndex(2))
		{
			ConsoleReport(TEXT("Usage: player.setav strength|athletics|hunger|thirst <value>"));
		}
		else if (What == TEXT("strength"))
		{
			Stats->SetStrength(Value);
		}
		else if (What == TEXT("athletics"))
		{
			Stats->SetAthletism(Value);
		}
		else if (What == TEXT("hunger"))
		{
			Stats->SetNeeds(Value, Stats->GetThirst());
		}
		else if (What == TEXT("thirst"))
		{
			Stats->SetNeeds(Stats->GetHunger(), Value);
		}
		else
		{
			ConsoleReport(TEXT("Usage: player.setav strength|athletics|hunger|thirst <value>"));
			return;
		}
		ConsoleReport(FString::Printf(TEXT("%s set to %.1f"), *What, Value));
	}
}

bool AedirConsoleCommands::IsCommand(const FString& Line)
{
	FString Verb;
	Line.Split(TEXT(" "), &Verb, nullptr);
	Verb = (Verb.IsEmpty() ? Line : Verb).ToLower();
	for (const TCHAR* Known : ConsoleVerbs)
	{
		if (Verb == Known)
		{
			return true;
		}
	}
	return false;
}

bool AedirConsoleCommands::TakesReference(const FString& Line)
{
	FString Verb;
	Line.Split(TEXT(" "), &Verb, nullptr);
	Verb = (Verb.IsEmpty() ? Line : Verb).ToLower();
	for (const TCHAR* Known : ConsoleRefVerbs)
	{
		if (Verb == Known)
		{
			return !Line.TrimEnd().Contains(TEXT(" "));
		}
	}
	return false;
}

void AedirConsoleCommands::Run(AAedirPlayerCharacter* Player, const FString& Line)
{
	TArray<FString> Words;
	Line.ParseIntoArrayWS(Words);
	if (!Player || !Player->HasAuthority() || Words.IsEmpty())
	{
		return;
	}

	const FString Verb = Words[0].ToLower();
	if (Verb == TEXT("help"))
	{
		ConsoleHelp(Words);
	}
	else if (Verb == TEXT("player.additem") || Verb == TEXT("additem"))
	{
		ConsoleAddItem(Player, Words);
	}
	else if (Verb == TEXT("player.removeitem") || Verb == TEXT("removeitem"))
	{
		ConsoleRemoveItem(Player, Words);
	}
	else if (Verb == TEXT("spawn") || Verb == TEXT("placeatme") || Verb == TEXT("player.placeatme"))
	{
		ConsoleSpawn(Player, Words);
	}
	else if (Verb == TEXT("prid"))
	{
		if (const AActor* Actor = ConsoleFindRef(Player, Words, 1))
		{
			ConsoleReport(ConsoleDescribe(Actor));
		}
	}
	else if (Verb == TEXT("enable") || Verb == TEXT("disable"))
	{
		if (AActor* Actor = ConsoleFindRef(Player, Words, 1))
		{
			ConsoleSetEnabled(Actor, Verb == TEXT("enable"));
		}
	}
	else if (Verb == TEXT("delete") || Verb == TEXT("markfordelete"))
	{
		if (AActor* Actor = ConsoleFindRef(Player, Words, 1))
		{
			ConsoleDelete(Player, Actor);
		}
	}
	else if (Verb == TEXT("kill"))
	{
		if (AAedirCreature* Creature = Cast<AAedirCreature>(ConsoleFindRef(Player, Words, 1)))
		{
			Creature->ReceiveCreatureDamage(ConsoleLethalDamage);
		}
	}
	else if (Verb == TEXT("moveto") || Verb == TEXT("player.moveto"))
	{
		if (const AActor* Actor = ConsoleFindRef(Player, Words, 1))
		{
			Player->TeleportTo(Actor->GetActorLocation() + Actor->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 60.f), Player->GetActorRotation());
		}
	}
	else if (Verb == TEXT("getpos"))
	{
		if (const AActor* Actor = ConsoleFindRef(Player, Words, 1))
		{
			ConsoleReport(Actor->GetActorLocation().ToCompactString());
		}
	}
	else if (Verb == TEXT("setscale"))
	{
		AActor* Actor = ConsoleFindRef(Player, Words, 1);
		if (Actor && Words.IsValidIndex(2))
		{
			Actor->SetActorScale3D(FVector(FCString::Atof(*Words[2])));
		}
	}
	else if (Verb == TEXT("player.getav"))
	{
		ConsoleGetAv(Player, Words);
	}
	else if (Verb == TEXT("player.setav"))
	{
		ConsoleSetAv(Player, Words);
	}
	else if (Verb == TEXT("tgm"))
	{
		UAedirDevTools::God(Player, FString());
	}
	else if (Verb == TEXT("sw") || Verb == TEXT("setweather"))
	{
		UAedirDevTools::SetWeather(Player, Words.IsValidIndex(1) ? Words[1] : FString());
	}
	else if (Verb == TEXT("showinventory") || Verb == TEXT("player.showinventory"))
	{
		ConsoleShowInventory(Player);
	}
}
