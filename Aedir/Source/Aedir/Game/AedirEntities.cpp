#include "Game/AedirEntities.h"

#include "Creature/AedirCreature.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirItemCatalog.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "Npc/AedirNpc.h"
#include "Npc/AedirTrainingDummy.h"
#include "Player/AedirPlayerCharacter.h"

namespace
{
	const TCHAR* const EntityExtraBases[] = { TEXT("Player"), TEXT("NpcNeutral"), TEXT("NpcFollower"), TEXT("NpcEnemy"), TEXT("Creature"), TEXT("TrainingDummy") };

	bool EntityIsTracked(const AActor* Actor)
	{
		return Cast<AAedirItemActor>(Actor) || Cast<AAedirCreature>(Actor) || Cast<AAedirTrainingDummy>(Actor);
	}
}

UAedirEntityIdComponent::UAedirEntityIdComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UAedirEntityIdComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAedirEntityIdComponent, RefId);
	DOREPLIFETIME(UAedirEntityIdComponent, bDisabled);
}

void UAedirEntitySubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);

	if (World.GetNetMode() == NM_Client)
	{
		return;
	}
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		Register(*It);
	}
	SpawnHandle = World.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UAedirEntitySubsystem::Register));
}

void UAedirEntitySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
	}
	Super::Deinitialize();
}

void UAedirEntitySubsystem::Register(AActor* Actor)
{
	if (!EntityIsTracked(Actor) || Actor->FindComponentByClass<UAedirEntityIdComponent>())
	{
		return;
	}

	UAedirEntityIdComponent* Id = NewObject<UAedirEntityIdComponent>(Actor);
	Id->RefId = NextRef++;
	Id->RegisterComponent();
	ByRef.Add(Id->RefId, Actor);
}

AActor* UAedirEntitySubsystem::FindByRef(uint32 RefId) const
{
	const TWeakObjectPtr<AActor>* Found = ByRef.Find(RefId);
	return Found ? Found->Get() : nullptr;
}

uint32 AedirEntities::BaseIdOfName(FName BaseName)
{
	// The top byte stays clear, reference ids start with FF.
	return FCrc::StrCrc32(*BaseName.ToString().ToLower()) & 0x00FFFFFF;
}

TArray<FName> AedirEntities::GetAllBaseNames()
{
	TArray<FName> Names = UAedirItemCatalog::GetIds();
	for (const TCHAR* Extra : EntityExtraBases)
	{
		Names.Add(FName(Extra));
	}
	return Names;
}

FName AedirEntities::BaseNameOfActor(const AActor* Actor)
{
	if (const AAedirCatalogItem* Item = Cast<AAedirCatalogItem>(Actor))
	{
		return Item->CatalogId;
	}
	if (Cast<AAedirPlayerCharacter>(Actor))
	{
		return FName(TEXT("Player"));
	}
	if (const AAedirNpc* Npc = Cast<AAedirNpc>(Actor))
	{
		return FName(Npc->Role_Npc == EAedirNpcRole::Enemy ? TEXT("NpcEnemy") : Npc->Role_Npc == EAedirNpcRole::Follower ? TEXT("NpcFollower") : TEXT("NpcNeutral"));
	}
	if (Cast<AAedirTrainingDummy>(Actor))
	{
		return FName(TEXT("TrainingDummy"));
	}
	return Cast<AAedirCreature>(Actor) ? FName(TEXT("Creature")) : NAME_None;
}

bool AedirEntities::ParseHex(const FString& Token, uint32& OutValue)
{
	FString Text = Token;
	Text.RemoveFromStart(TEXT("0x"), ESearchCase::IgnoreCase);
	if (Text.IsEmpty() || Text.Len() > 8)
	{
		return false;
	}
	for (const TCHAR Letter : Text)
	{
		if (!FChar::IsHexDigit(Letter))
		{
			return false;
		}
	}
	OutValue = FParse::HexNumber(*Text);
	return true;
}

FName AedirEntities::FindBaseName(const FString& Token)
{
	uint32 Number = 0;
	const bool bHex = ParseHex(Token, Number);
	for (const FName& Name : GetAllBaseNames())
	{
		if (Name.ToString().Equals(Token, ESearchCase::IgnoreCase) || (bHex && BaseIdOfName(Name) == Number))
		{
			return Name;
		}
	}
	return NAME_None;
}

uint32 AedirEntities::RefIdOfActor(const AActor* Actor)
{
	const UAedirEntityIdComponent* Id = Actor ? Actor->FindComponentByClass<UAedirEntityIdComponent>() : nullptr;
	return Id ? Id->RefId : 0;
}

FString AedirEntities::DisplayNameOfActor(const AActor* Actor)
{
	if (const AAedirNpc* Npc = Cast<AAedirNpc>(Actor))
	{
		return Npc->NpcName.ToString();
	}
	if (const AAedirItemActor* Item = Cast<AAedirItemActor>(Actor))
	{
		return Item->ItemName.ToString();
	}
	if (Cast<AAedirTrainingDummy>(Actor))
	{
		return TEXT("Training Pell");
	}
	return Actor ? Actor->GetName() : FString();
}
