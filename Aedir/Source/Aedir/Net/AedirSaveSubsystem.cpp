#include "Net/AedirSaveSubsystem.h"

#include "Aedir.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Net/AedirSaveGame.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"

namespace
{
	constexpr int32 SaveSubsystemUserIndex = 0;
	constexpr float SaveSubsystemRestoreWeatherSeconds = 0.1f;

	FAedirCreatureSave SaveSubsystemMakeCreatureSave(const FString& PlayerKey, const AAedirCreature* Creature)
	{
		FAedirCreatureSave Result;
		Result.PlayerKey = PlayerKey;
		Result.Transform = Creature->GetActorTransform();
		Result.GroupId = Creature->GroupId;
		Result.RaceId = Creature->RaceId;

		if (const UAedirStatsComponent* Stats = Creature->Stats.Get())
		{
			Result.Health = Stats->GetHealth();
			Result.MaxHealth = Stats->GetMaxHealth();
			Result.Mana = Stats->GetMana();
			Result.MaxMana = Stats->GetMaxMana();
			Result.Stamina = Stats->GetStamina();
			Result.MaxStamina = Stats->GetMaxStamina();
			Result.Strength = Stats->Strength;
			Result.Athletism = Stats->Athletism;

			const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
			Result.Skills.SetNumZeroed(SkillCount);
			for (int32 Index = 0; Index < SkillCount; ++Index)
			{
				Result.Skills[Index] = Stats->GetSkill(static_cast<EAedirSkill>(Index));
			}
		}

		if (const UAedirInteractionComponent* Interaction = Creature->Interaction.Get())
		{
			const AAedirItemActor* LeftItem = Interaction->GetHeldItem(EAedirHand::Left);
			const AAedirItemActor* RightItem = Interaction->GetHeldItem(EAedirHand::Right);

			// Two-handed items are held by both hands; only the left entry is stored so they are restored once.
			Result.LeftHandItem = LeftItem ? TSoftClassPtr<AAedirItemActor>(LeftItem->GetClass()) : TSoftClassPtr<AAedirItemActor>();
			Result.RightHandItem = (RightItem && RightItem != LeftItem) ? TSoftClassPtr<AAedirItemActor>(RightItem->GetClass()) : TSoftClassPtr<AAedirItemActor>();

			const TArray<AAedirItemActor*> WaistItems = Interaction->GetWaistItemsCopy();
			Result.WaistItems.SetNum(WaistItems.Num());
			for (int32 Index = 0; Index < WaistItems.Num(); ++Index)
			{
				Result.WaistItems[Index] = WaistItems[Index] ? TSoftClassPtr<AAedirItemActor>(WaistItems[Index]->GetClass()) : TSoftClassPtr<AAedirItemActor>();
			}
		}

		return Result;
	}

	AAedirItemActor* SaveSubsystemSpawnItem(UWorld* World, const TSoftClassPtr<AAedirItemActor>& ItemClass, const FTransform& Transform)
	{
		UClass* Class = ItemClass.LoadSynchronous();
		if (!World || !Class || !Class->IsChildOf(AAedirItemActor::StaticClass()))
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AAedirItemActor>(Class, Transform, SpawnParameters);
	}
}

void UAedirSaveSubsystem::Deinitialize()
{
	StopAutosave();

	Super::Deinitialize();
}

bool UAedirSaveSubsystem::SaveWorld(const FString& Slot)
{
	UWorld* World = GetWorld();
	if (!World || Slot.IsEmpty() || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	UAedirSaveGame* Save = Cast<UAedirSaveGame>(UGameplayStatics::CreateSaveGameObject(UAedirSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}

	Save->SaveVersion = UAedirSaveGame::CurrentSaveVersion;
	Save->Timestamp = FDateTime::UtcNow();
	Save->MapName = UGameplayStatics::GetCurrentLevelName(World);
	Save->WorldSeconds = World->GetTimeSeconds();

	if (UAedirWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UAedirWeatherSubsystem>())
	{
		if (const AAedirWeatherManager* Manager = WeatherSubsystem->GetManager())
		{
			Save->Weather = Manager->GetWeather();
		}
	}

	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		const APlayerController* PlayerController = Iterator->Get();
		const AAedirCreature* Creature = PlayerController ? Cast<AAedirCreature>(PlayerController->GetPawn()) : nullptr;
		if (Creature)
		{
			Save->Players.Add(SaveSubsystemMakeCreatureSave(MakePlayerKey(PlayerController), Creature));
		}
	}

	for (TActorIterator<AAedirItemActor> Iterator(World); Iterator; ++Iterator)
	{
		const AAedirItemActor* Item = *Iterator;
		if (!Item || Item->IsHeld() || Item->IsActorBeingDestroyed())
		{
			continue;
		}

		FAedirItemSave ItemSave;
		ItemSave.ItemClass = TSoftClassPtr<AAedirItemActor>(Item->GetClass());
		ItemSave.Transform = Item->GetActorTransform();
		Save->LooseItems.Add(ItemSave);
	}

	if (!UGameplayStatics::SaveGameToSlot(Save, Slot, SaveSubsystemUserIndex))
	{
		UE_LOG(LogAedir, Warning, TEXT("Save to slot %s failed"), *Slot);
		return false;
	}

	UE_LOG(LogAedir, Log, TEXT("Saved world to slot %s: %d players, %d loose items"), *Slot, Save->Players.Num(), Save->LooseItems.Num());
	return true;
}

bool UAedirSaveSubsystem::LoadWorld(const FString& Slot)
{
	UWorld* World = GetWorld();
	if (!World || Slot.IsEmpty() || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (!UGameplayStatics::DoesSaveGameExist(Slot, SaveSubsystemUserIndex))
	{
		UE_LOG(LogAedir, Warning, TEXT("Load failed: slot %s does not exist"), *Slot);
		return false;
	}

	const UAedirSaveGame* Save = Cast<UAedirSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, SaveSubsystemUserIndex));
	if (!Save)
	{
		UE_LOG(LogAedir, Warning, TEXT("Load failed: slot %s is not an Aedir save"), *Slot);
		return false;
	}

	if (Save->SaveVersion != UAedirSaveGame::CurrentSaveVersion)
	{
		UE_LOG(LogAedir, Warning, TEXT("Load failed: slot %s has save version %d, expected %d"), *Slot, Save->SaveVersion, UAedirSaveGame::CurrentSaveVersion);
		return false;
	}

	const FString CurrentMapName = UGameplayStatics::GetCurrentLevelName(World);
	if (Save->MapName != CurrentMapName)
	{
		UE_LOG(LogAedir, Warning, TEXT("Load failed: slot %s was saved on map %s, current map is %s"), *Slot, *Save->MapName, *CurrentMapName);
		return false;
	}

	TArray<AAedirCreature*> Creatures;
	TArray<FString> Keys;
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		const APlayerController* PlayerController = Iterator->Get();
		AAedirCreature* Creature = PlayerController ? Cast<AAedirCreature>(PlayerController->GetPawn()) : nullptr;
		if (Creature)
		{
			Creatures.Add(Creature);
			Keys.Add(MakePlayerKey(PlayerController));
		}
	}

	// Matching keys win. Creatures without a match take the first unused saved entry in index order.
	TArray<int32> Assignment;
	Assignment.Init(INDEX_NONE, Creatures.Num());
	TArray<bool> SaveUsed;
	SaveUsed.Init(false, Save->Players.Num());

	for (int32 CreatureIndex = 0; CreatureIndex < Creatures.Num(); ++CreatureIndex)
	{
		for (int32 SaveIndex = 0; SaveIndex < Save->Players.Num(); ++SaveIndex)
		{
			if (!SaveUsed[SaveIndex] && Save->Players[SaveIndex].PlayerKey == Keys[CreatureIndex])
			{
				Assignment[CreatureIndex] = SaveIndex;
				SaveUsed[SaveIndex] = true;
				break;
			}
		}
	}

	int32 NextUnusedSave = 0;
	for (int32 CreatureIndex = 0; CreatureIndex < Creatures.Num(); ++CreatureIndex)
	{
		if (Assignment[CreatureIndex] != INDEX_NONE)
		{
			continue;
		}

		while (NextUnusedSave < Save->Players.Num() && SaveUsed[NextUnusedSave])
		{
			++NextUnusedSave;
		}

		if (NextUnusedSave < Save->Players.Num())
		{
			Assignment[CreatureIndex] = NextUnusedSave;
			SaveUsed[NextUnusedSave] = true;
		}
	}

	// Collect everything that is about to be replaced: the connected creatures' hand and waist items, then all loose items.
	TArray<AAedirItemActor*> ItemsToDestroy;
	for (AAedirCreature* Creature : Creatures)
	{
		UAedirInteractionComponent* Interaction = Creature->Interaction.Get();
		if (!Interaction)
		{
			continue;
		}

		for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
		{
			if (AAedirItemActor* HeldItem = Interaction->GetHeldItem(Hand))
			{
				ItemsToDestroy.AddUnique(HeldItem);
			}
		}

		for (AAedirItemActor* WaistItem : Interaction->GetWaistItemsCopy())
		{
			if (WaistItem)
			{
				ItemsToDestroy.AddUnique(WaistItem);
			}
		}

		for (AAedirItemActor* StowedItem : Interaction->GetBackpackItemsCopy())
		{
			if (StowedItem)
			{
				ItemsToDestroy.AddUnique(StowedItem);
			}
		}

		Interaction->ReleaseAllItems();
	}

	for (TActorIterator<AAedirItemActor> Iterator(World); Iterator; ++Iterator)
	{
		AAedirItemActor* Item = *Iterator;
		if (Item && !Item->IsHeld())
		{
			ItemsToDestroy.AddUnique(Item);
		}
	}

	for (AAedirItemActor* Item : ItemsToDestroy)
	{
		if (IsValid(Item))
		{
			Item->Destroy();
		}
	}

	if (UAedirWeatherSubsystem* WeatherSubsystem = World->GetSubsystem<UAedirWeatherSubsystem>())
	{
		if (AAedirWeatherManager* Manager = WeatherSubsystem->GetManager())
		{
			Manager->SetWeather(Save->Weather, SaveSubsystemRestoreWeatherSeconds);
		}
	}

	for (const FAedirItemSave& ItemSave : Save->LooseItems)
	{
		SaveSubsystemSpawnItem(World, ItemSave.ItemClass, ItemSave.Transform);
	}

	for (int32 CreatureIndex = 0; CreatureIndex < Creatures.Num(); ++CreatureIndex)
	{
		if (Assignment[CreatureIndex] == INDEX_NONE)
		{
			continue;
		}

		AAedirCreature* Creature = Creatures[CreatureIndex];
		const FAedirCreatureSave& CreatureSave = Save->Players[Assignment[CreatureIndex]];

		Creature->TeleportTo(CreatureSave.Transform.GetLocation(), CreatureSave.Transform.Rotator(), false, true);
		Creature->SetGroupId(CreatureSave.GroupId);
		if (!CreatureSave.RaceId.IsNone())
		{
			// The race sets the base stats first; the saved stats are applied on top of it below.
			Creature->SetRace(CreatureSave.RaceId);
		}

		if (UAedirStatsComponent* Stats = Creature->Stats.Get())
		{
			Stats->ApplySavedState(
				CreatureSave.Health,
				CreatureSave.MaxHealth,
				CreatureSave.Mana,
				CreatureSave.MaxMana,
				CreatureSave.Stamina,
				CreatureSave.MaxStamina,
				CreatureSave.Strength,
				CreatureSave.Athletism,
				CreatureSave.Skills);
		}

		UAedirInteractionComponent* Interaction = Creature->Interaction.Get();
		if (!Interaction)
		{
			continue;
		}

		const FTransform SpawnTransform(FQuat::Identity, Creature->GetActorLocation());

		if (AAedirItemActor* LeftItem = SaveSubsystemSpawnItem(World, CreatureSave.LeftHandItem, SpawnTransform))
		{
			if (!Interaction->EquipImmediate(EAedirHand::Left, LeftItem))
			{
				LeftItem->Destroy();
			}
		}

		if (AAedirItemActor* RightItem = SaveSubsystemSpawnItem(World, CreatureSave.RightHandItem, SpawnTransform))
		{
			if (!Interaction->EquipImmediate(EAedirHand::Right, RightItem))
			{
				RightItem->Destroy();
			}
		}

		for (int32 WaistIndex = 0; WaistIndex < CreatureSave.WaistItems.Num(); ++WaistIndex)
		{
			if (AAedirItemActor* WaistItem = SaveSubsystemSpawnItem(World, CreatureSave.WaistItems[WaistIndex], SpawnTransform))
			{
				if (!Interaction->HolsterImmediate(WaistIndex, WaistItem))
				{
					WaistItem->Destroy();
				}
			}
		}
	}

	UE_LOG(LogAedir, Log, TEXT("Loaded world from slot %s: %d saved players, %d loose items"), *Slot, Save->Players.Num(), Save->LooseItems.Num());
	return true;
}

bool UAedirSaveSubsystem::DoesSaveExist(const FString& Slot) const
{
	return !Slot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(Slot, SaveSubsystemUserIndex);
}

bool UAedirSaveSubsystem::DeleteSave(const FString& Slot)
{
	return !Slot.IsEmpty() && UGameplayStatics::DeleteGameInSlot(Slot, SaveSubsystemUserIndex);
}

FDateTime UAedirSaveSubsystem::GetSaveTimestamp(const FString& Slot) const
{
	if (!DoesSaveExist(Slot))
	{
		return FDateTime(0);
	}

	const UAedirSaveGame* Save = Cast<UAedirSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, SaveSubsystemUserIndex));
	return Save ? Save->Timestamp : FDateTime(0);
}

void UAedirSaveSubsystem::StartAutosave(const FString& Slot, float IntervalSeconds)
{
	UWorld* World = GetWorld();
	if (!World || Slot.IsEmpty() || IntervalSeconds <= 0.f || World->GetNetMode() == NM_Client)
	{
		return;
	}

	StopAutosave();

	AutosaveSlot = Slot;
	World->GetTimerManager().SetTimer(AutosaveTimerHandle, FTimerDelegate::CreateUObject(this, &UAedirSaveSubsystem::AutosaveTick), IntervalSeconds, true);
}

void UAedirSaveSubsystem::StopAutosave()
{
	AutosaveSlot.Reset();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutosaveTimerHandle);
	}
}

FString UAedirSaveSubsystem::MakePlayerKey(const APlayerController* PlayerController)
{
	const APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState.Get() : nullptr;
	if (!PlayerState)
	{
		return FString();
	}

	const FString PlayerName = PlayerState->GetPlayerName();
	const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
	if (!UniqueId.IsValid())
	{
		return PlayerName;
	}

	return FString::Printf(TEXT("%s|%s"), *PlayerName, *UniqueId.ToString());
}

void UAedirSaveSubsystem::AutosaveTick()
{
	if (!AutosaveSlot.IsEmpty())
	{
		SaveWorld(AutosaveSlot);
	}
}
