#include "Dev/AedirDevTools.h"

#if !UE_BUILD_SHIPPING

#include "Aedir.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Creature/AedirCreatureTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemCatalog.h"
#include "Player/AedirPlayerCharacter.h"
#include "UObject/Class.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"
#include "Wounds/AedirBloodSubsystem.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	constexpr float DevToolsSpawnForwardCm = 160.f;
	constexpr float DevToolsSpawnUpCm = 80.f;
	constexpr float DevToolsEnemyDistanceCm = 450.f;
	constexpr float DevToolsEnemySpacingCm = 120.f;
	constexpr float DevToolsWoundEnergyJoules = 60.f;
	constexpr float DevToolsWeatherTransitionSeconds = 3.f;
	constexpr float DevToolsSkillDefaultAmount = 10.f;
	constexpr float DevToolsHurtDefaultAmount = 20.f;
	constexpr float DevToolsScreenMessageSeconds = 8.f;
	constexpr double DevToolsLethalDamage = 1.0e6;

	void DevToolsReport(const FString& Message)
	{
		UE_LOG(LogAedir, Display, TEXT("[Dev] %s"), *Message);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, DevToolsScreenMessageSeconds, FColor::Cyan, Message);
		}
	}

	TArray<FString> DevToolsTokens(const FString& Args)
	{
		TArray<FString> Tokens;
		Args.ParseIntoArrayWS(Tokens);
		return Tokens;
	}

	FString DevToolsEnumNames(const UEnum* Enum, int32 Count)
	{
		FString Names;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!Names.IsEmpty())
			{
				Names += TEXT(", ");
			}

			Names += Enum->GetNameStringByIndex(Index);
		}

		return Names;
	}

	FRotator DevToolsFlatYaw(const AAedirPlayerCharacter* Player)
	{
		return FRotator(0.f, Player->GetActorRotation().Yaw, 0.f);
	}

	bool DevToolsIsToken(const FString& Token, const TCHAR* Expected)
	{
		return Token.Equals(Expected, ESearchCase::IgnoreCase);
	}
}

void UAedirDevTools::Execute(AAedirPlayerCharacter* Player, const FString& Command, const FString& Args)
{
	if (!Player)
	{
		return;
	}

	if (Command == TEXT("SpawnItem"))
	{
		SpawnItem(Player, Args);
	}
	else if (Command == TEXT("ListItems"))
	{
		ListItems(Player, Args);
	}
	else if (Command == TEXT("SpawnEnemy"))
	{
		SpawnEnemy(Player, Args);
	}
	else if (Command == TEXT("KillAll"))
	{
		KillAll(Player, Args);
	}
	else if (Command == TEXT("SetWeatherType"))
	{
		SetWeather(Player, Args);
	}
	else if (Command == TEXT("Heal"))
	{
		Heal(Player, Args);
	}
	else if (Command == TEXT("Hurt"))
	{
		Hurt(Player, Args);
	}
	else if (Command == TEXT("God"))
	{
		God(Player, Args);
	}
	else if (Command == TEXT("GiveSkill"))
	{
		GiveSkill(Player, Args);
	}
	else if (Command == TEXT("Wound"))
	{
		Wound(Player, Args);
	}
	else if (Command == TEXT("BloodTest"))
	{
		BloodTest(Player, Args);
	}
	else if (Command == TEXT("DebugBody"))
	{
		DebugBody(Player, Args);
	}
	else if (Command == TEXT("DevHelp"))
	{
		Help(Player, Args);
	}
	else
	{
		DevToolsReport(FString::Printf(TEXT("Unknown dev command %s"), *Command));
	}
}

void UAedirDevTools::SpawnItem(AAedirPlayerCharacter* Player, const FString& Args)
{
	const TArray<FString> Tokens = DevToolsTokens(Args);
	if (Tokens.IsEmpty())
	{
		DevToolsReport(TEXT("Usage: SpawnItem <Id> [hold]. Run ListItems for the ids."));
		return;
	}

	const FName Id(*Tokens[0]);
	UWorld* World = Player->GetWorld();
	if (!World || !UAedirItemCatalog::Find(Id))
	{
		DevToolsReport(FString::Printf(TEXT("Unknown item %s. Run ListItems for the ids."), *Tokens[0]));
		return;
	}

	const FVector Location = Player->GetActorLocation() + Player->GetActorForwardVector() * DevToolsSpawnForwardCm + FVector(0.f, 0.f, DevToolsSpawnUpCm);
	AAedirCatalogItem* Item = AAedirCatalogItem::SpawnCatalogItem(World, Id, FTransform(DevToolsFlatYaw(Player), Location));
	if (!Item)
	{
		DevToolsReport(FString::Printf(TEXT("Could not spawn %s"), *Tokens[0]));
		return;
	}

	const bool bHold = Tokens.IsValidIndex(1) && DevToolsIsToken(Tokens[1], TEXT("hold"));
	if (!bHold)
	{
		DevToolsReport(FString::Printf(TEXT("Spawned %s in front of the player"), *Tokens[0]));
		return;
	}

	if (Player->Interaction && Player->Interaction->EquipImmediate(EAedirHand::Right, Item))
	{
		DevToolsReport(FString::Printf(TEXT("Spawned %s in the right hand"), *Tokens[0]));
	}
	else
	{
		DevToolsReport(FString::Printf(TEXT("Spawned %s, but the right hand could not take it"), *Tokens[0]));
	}
}

void UAedirDevTools::ListItems(AAedirPlayerCharacter* Player, const FString& Args)
{
	DevToolsReport(TEXT("Catalog items:"));
	for (const FAedirItemDef& Def : UAedirItemCatalog::GetAll())
	{
		DevToolsReport(FString::Printf(TEXT("  %s  (%s, %.2f kg)"), *Def.Id.ToString(), *Def.DisplayName.ToString(), Def.WeightKg));
	}
}

void UAedirDevTools::SpawnEnemy(AAedirPlayerCharacter* Player, const FString& Args)
{
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const int32 Count = FMath::Clamp(Tokens.IsValidIndex(0) ? FCString::Atoi(*Tokens[0]) : 1, 1, 6);
	const bool bArmed = Tokens.IsValidIndex(1) && FCString::Atoi(*Tokens[1]) != 0;

	UWorld* World = Player->GetWorld();
	if (!World)
	{
		return;
	}

	static const TCHAR* const DevToolsWeaponIds[] = { TEXT("ShortSword"), TEXT("Mace"), TEXT("BattleAxe"), TEXT("Longsword"), TEXT("Club") };
	const FVector PlayerLocation = Player->GetActorLocation();
	const FRotator Yaw = DevToolsFlatYaw(Player);
	const FVector Forward = Yaw.Vector();
	const FVector Right = Yaw.RotateVector(FVector::RightVector);
	const FVector Center = PlayerLocation + Forward * DevToolsEnemyDistanceCm;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float SideOffset = (static_cast<float>(Index) - static_cast<float>(Count - 1) * 0.5f) * DevToolsEnemySpacingCm;
		const FVector Location = Center + Right * SideOffset;
		const FRotator Facing(0.f, (PlayerLocation - Location).Rotation().Yaw, 0.f);

		AAedirCreature* Enemy = World->SpawnActor<AAedirCreature>(AAedirCreature::StaticClass(), Location, Facing, SpawnParameters);
		if (!Enemy)
		{
			continue;
		}

		++Spawned;
		Enemy->GroupId = FName(TEXT("Bandits"));
		Enemy->OnGroupIdChanged.Broadcast(Enemy->GroupId);

		const FTransform HandTransform(Facing, Location + FVector(0.f, 0.f, 100.f));
		if (bArmed && Enemy->Interaction)
		{
			const FName WeaponId(DevToolsWeaponIds[FMath::RandRange(0, UE_ARRAY_COUNT(DevToolsWeaponIds) - 1)]);
			if (AAedirCatalogItem* Weapon = AAedirCatalogItem::SpawnCatalogItem(World, WeaponId, HandTransform))
			{
				Enemy->Interaction->EquipImmediate(EAedirHand::Right, Weapon);
			}
		}

		if (FMath::RandBool())
		{
			if (AAedirCatalogItem* Armor = AAedirCatalogItem::SpawnCatalogItem(World, FName(TEXT("LeatherArmor")), HandTransform))
			{
				Armor->Use(Enemy);
			}
		}
	}

	DevToolsReport(FString::Printf(TEXT("Spawned %d enemies (%s)"), Spawned, bArmed ? TEXT("armed") : TEXT("unarmed")));
}

void UAedirDevTools::KillAll(AAedirPlayerCharacter* Player, const FString& Args)
{
	UWorld* World = Player->GetWorld();
	if (!World)
	{
		return;
	}

	int32 Targets = 0;
	for (TActorIterator<AAedirCreature> Iterator(World); Iterator; ++Iterator)
	{
		AAedirCreature* Creature = *Iterator;
		if (Creature == Player || !Creature->Stats || Creature->Stats->IsDead())
		{
			continue;
		}

		Creature->ReceiveCreatureDamage(static_cast<float>(DevToolsLethalDamage));
		++Targets;
	}

	DevToolsReport(FString::Printf(TEXT("Applied lethal damage to %d creatures"), Targets));
}

void UAedirDevTools::SetWeather(AAedirPlayerCharacter* Player, const FString& Args)
{
	const UEnum* WeatherEnum = StaticEnum<EAedirWeather>();
	const TArray<FString> Tokens = DevToolsTokens(Args);
	if (Tokens.IsEmpty())
	{
		DevToolsReport(FString::Printf(TEXT("Usage: SetWeatherType <Name>. Names: %s"), *DevToolsEnumNames(WeatherEnum, WeatherEnum->NumEnums())));
		return;
	}

	const int64 Value = WeatherEnum->GetValueByNameString(Tokens[0]);
	if (Value == INDEX_NONE)
	{
		DevToolsReport(FString::Printf(TEXT("Unknown weather %s"), *Tokens[0]));
		return;
	}

	UWorld* World = Player->GetWorld();
	const UAedirWeatherSubsystem* WeatherSubsystem = World ? World->GetSubsystem<UAedirWeatherSubsystem>() : nullptr;
	AAedirWeatherManager* Manager = WeatherSubsystem ? WeatherSubsystem->GetManager() : nullptr;
	if (!Manager)
	{
		DevToolsReport(TEXT("No weather manager in this world"));
		return;
	}

	Manager->SetWeather(static_cast<EAedirWeather>(Value), DevToolsWeatherTransitionSeconds);
	DevToolsReport(FString::Printf(TEXT("Weather changing to %s"), *Tokens[0]));
}

void UAedirDevTools::Heal(AAedirPlayerCharacter* Player, const FString& Args)
{
	if (!Player->Stats)
	{
		return;
	}

	Player->Stats->RestoreAll();
	DevToolsReport(TEXT("Health, mana and stamina restored"));
}

void UAedirDevTools::Hurt(AAedirPlayerCharacter* Player, const FString& Args)
{
	if (!Player->Stats)
	{
		return;
	}

	const TArray<FString> Tokens = DevToolsTokens(Args);
	const float Amount = Tokens.IsEmpty() ? DevToolsHurtDefaultAmount : FCString::Atof(*Tokens[0]);
	const float Applied = Player->Stats->ApplyDamage(Amount);
	DevToolsReport(FString::Printf(TEXT("Applied %.1f damage, health %.1f"), Applied, Player->Stats->GetHealth()));
}

void UAedirDevTools::God(AAedirPlayerCharacter* Player, const FString& Args)
{
	if (!Player->Stats)
	{
		return;
	}

	Player->Stats->bInvulnerable = !Player->Stats->bInvulnerable;
	DevToolsReport(Player->Stats->bInvulnerable ? TEXT("God mode on") : TEXT("God mode off"));
}

void UAedirDevTools::GiveSkill(AAedirPlayerCharacter* Player, const FString& Args)
{
	const UEnum* SkillEnum = StaticEnum<EAedirSkill>();
	const int32 SkillCount = static_cast<int32>(EAedirSkill::Count);
	const TArray<FString> Tokens = DevToolsTokens(Args);
	if (Tokens.IsEmpty() || !Player->Stats)
	{
		DevToolsReport(FString::Printf(TEXT("Usage: GiveSkill <SkillName or index> [amount]. Skills: %s"), *DevToolsEnumNames(SkillEnum, SkillCount)));
		return;
	}

	int32 SkillIndex = INDEX_NONE;
	if (Tokens[0].IsNumeric())
	{
		SkillIndex = FCString::Atoi(*Tokens[0]);
	}
	else
	{
		SkillIndex = static_cast<int32>(SkillEnum->GetValueByNameString(Tokens[0]));
	}

	if (SkillIndex < 0 || SkillIndex >= SkillCount)
	{
		DevToolsReport(FString::Printf(TEXT("Unknown skill %s"), *Tokens[0]));
		return;
	}

	const float Amount = Tokens.IsValidIndex(1) ? FCString::Atof(*Tokens[1]) : DevToolsSkillDefaultAmount;
	Player->Stats->AddSkillExperience(static_cast<EAedirSkill>(SkillIndex), Amount);
	DevToolsReport(FString::Printf(TEXT("%s now %.1f"), *SkillEnum->GetNameStringByIndex(SkillIndex), Player->Stats->GetSkill(static_cast<EAedirSkill>(SkillIndex))));
}

void UAedirDevTools::Wound(AAedirPlayerCharacter* Player, const FString& Args)
{
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const FString TypeToken = Tokens.IsEmpty() ? TEXT("Cut") : Tokens[0];

	EAedirDamageType DamageType = EAedirDamageType::Slash;
	if (DevToolsIsToken(TypeToken, TEXT("Bruise")))
	{
		DamageType = EAedirDamageType::Blunt;
	}
	else if (DevToolsIsToken(TypeToken, TEXT("Puncture")))
	{
		DamageType = EAedirDamageType::Pierce;
	}
	else if (!DevToolsIsToken(TypeToken, TEXT("Cut")))
	{
		DevToolsReport(TEXT("Usage: Wound <Bruise|Cut|Puncture> [bone=upperarm_l]"));
		return;
	}

	FName Bone(TEXT("upperarm_l"));
	for (int32 Index = 1; Index < Tokens.Num(); ++Index)
	{
		FString BoneName = Tokens[Index];
		if (BoneName.StartsWith(TEXT("bone=")))
		{
			BoneName.RightChopInline(5);
		}

		Bone = FName(*BoneName);
	}

	const USkeletalMeshComponent* BodyMesh = Player->GetMesh();
	if (!BodyMesh || !Player->Wounds || BodyMesh->GetBoneIndex(Bone) == INDEX_NONE)
	{
		DevToolsReport(FString::Printf(TEXT("No bone %s on the player"), *Bone.ToString()));
		return;
	}

	const FVector Normal = Player->GetActorForwardVector();
	const FVector Location = BodyMesh->GetBoneLocation(Bone);
	const bool bOpened = Player->Wounds->AddWound(Bone, Location, Normal, DamageType, DevToolsWoundEnergyJoules, 0.f, Normal);
	DevToolsReport(bOpened ? FString::Printf(TEXT("%s wound on %s"), *TypeToken, *Bone.ToString()) : TEXT("The wound was not created"));
}

void UAedirDevTools::BloodTest(AAedirPlayerCharacter* Player, const FString& Args)
{
	UWorld* World = Player->GetWorld();
	UAedirBloodSubsystem* Blood = World ? World->GetSubsystem<UAedirBloodSubsystem>() : nullptr;
	if (!Blood)
	{
		DevToolsReport(TEXT("No blood subsystem in this world"));
		return;
	}

	const FVector Forward = Player->GetActorForwardVector();
	const FVector Location = Player->GetActorLocation() + Forward * 60.f + FVector(0.f, 0.f, 40.f);
	Blood->SpawnSpray(Location, Forward, 0.9f, EAedirWoundType::Cut);
	DevToolsReport(TEXT("Blood spray spawned in front of the player"));
}

void UAedirDevTools::DebugBody(AAedirPlayerCharacter* Player, const FString& Args)
{
	const bool bEnabled = !Player->IsDebugBodyEnabled();
	Player->SetDebugBodyEnabled(bEnabled);
	DevToolsReport(bEnabled ? TEXT("Body debug drawing on") : TEXT("Body debug drawing off"));
}

void UAedirDevTools::Help(AAedirPlayerCharacter* Player, const FString& Args)
{
	DevToolsReport(TEXT("Dev commands:"));
	DevToolsReport(TEXT("  SpawnItem <Id> [hold]: spawn a catalog item in front, or hold it in the right hand"));
	DevToolsReport(TEXT("  ListItems: print the catalog ids"));
	DevToolsReport(TEXT("  SpawnEnemy <count 1-6> <armed 0|1>: bandits facing the player"));
	DevToolsReport(TEXT("  KillAll: lethal damage to every other creature"));
	DevToolsReport(TEXT("  SetWeatherType <Sunny|ExtraSunny|Cloudy|Rainy|Stormy|Thunderstorm|Snowy>"));
	DevToolsReport(TEXT("  Heal: refill health, mana and stamina"));
	DevToolsReport(TEXT("  Hurt <amount>: damage through the stats component"));
	DevToolsReport(TEXT("  God: toggle invulnerability"));
	DevToolsReport(TEXT("  GiveSkill <SkillName or index> <amount>"));
	DevToolsReport(TEXT("  Wound <Bruise|Cut|Puncture> [bone=upperarm_l]"));
	DevToolsReport(TEXT("  BloodTest: blood spray in front of the player"));
	DevToolsReport(TEXT("  DebugBody: toggle centre of mass, support margin and hand drawing"));
	DevToolsReport(TEXT("  DevHelp: this list"));
}

#endif
