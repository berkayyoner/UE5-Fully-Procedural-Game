#include "Dev/AedirDevTools.h"
#include "Combat/AedirCombatTypes.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirCreatureTypes.h"
#include "Engine/World.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "UI/AedirJournalSubsystem.h"

FAedirDevReported& UAedirDevTools::OnReported()
{
	static FAedirDevReported Delegate;
	return Delegate;
}

int32 UAedirDevTools::SpawnBandits(UWorld* World, const FVector& Center, const FVector& FacePoint, int32 Count, bool bArmed)
{
	constexpr float BanditSpacingCm = 120.f;
	static const TCHAR* const BanditWeaponIds[] = { TEXT("ShortSword"), TEXT("Mace"), TEXT("BattleAxe"), TEXT("Sword"), TEXT("Club") };

	if (!World)
	{
		return 0;
	}

	const FVector ToFace = (FacePoint - Center).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ToFace);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float SideOffset = (static_cast<float>(Index) - static_cast<float>(Count - 1) * 0.5f) * BanditSpacingCm;
		const FVector Location = Center + Right * SideOffset;
		const FRotator Facing(0.f, (FacePoint - Location).Rotation().Yaw, 0.f);

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
			const FName WeaponId(BanditWeaponIds[FMath::RandRange(0, UE_ARRAY_COUNT(BanditWeaponIds) - 1)]);
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

	return Spawned;
}

#if !UE_BUILD_SHIPPING

#include "Aedir.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Creature/AedirCreatureTypes.h"
#include "Creature/AedirRaceCatalog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirInteractionComponent.h"
#include "Items/AedirItemCatalog.h"
#include "Animation/AedirGaitComponent.h"
#include "Combat/AedirCombatComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Physics/AedirBalanceComponent.h"
#include "TimerManager.h"
#include "Player/AedirPlayerCharacter.h"
#include "UObject/Class.h"
#include "Weather/AedirWeatherManager.h"
#include "Weather/AedirWeatherSubsystem.h"
#include "Wounds/AedirBloodSubsystem.h"
#include "Wounds/AedirWoundComponent.h"

namespace
{
	constexpr float DevToolsSpawnForwardCm = 90.f;
	constexpr float DevToolsSpawnUpCm = 80.f;
	constexpr float DevToolsSpawnSpreadCm = 45.f;
	constexpr float DevToolsEnemyDistanceCm = 450.f;
	constexpr float DevToolsWoundEnergyJoules = 60.f;
	constexpr float DevToolsWeatherTransitionSeconds = 3.f;
	constexpr float DevToolsSkillDefaultAmount = 10.f;
	constexpr float DevToolsHurtDefaultAmount = 20.f;
	constexpr float DevToolsScreenMessageSeconds = 8.f;
	constexpr double DevToolsLethalDamage = 1.0e6;

	void DevToolsReport(const FString& Message)
	{
		UE_LOG(LogAedir, Display, TEXT("[Dev] %s"), *Message);
		UAedirDevTools::OnReported().Broadcast(Message);

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

	FString DevToolsRaceIds()
	{
		FString Names;
		for (const FName& Id : UAedirRaceCatalog::GetIds())
		{
			if (!Names.IsEmpty())
			{
				Names += TEXT(", ");
			}

			Names += Id.ToString();
		}

		return Names;
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
	else if (Command == TEXT("SetRace"))
	{
		SetRace(Player, Args);
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
	else if (Command == TEXT("SetStat"))
	{
		SetStat(Player, Args);
	}
	else if (Command == TEXT("Stats"))
	{
		PrintStats(Player, Args);
	}
	else if (Command == TEXT("Push"))
	{
		Push(Player, Args);
	}
	else if (Command == TEXT("Knockdown"))
	{
		Knockdown(Player, Args);
	}
	else if (Command == TEXT("FallTest"))
	{
		FallTest(Player, Args);
	}
	else if (Command == TEXT("GaitInfo"))
	{
		GaitInfo(Player, Args);
	}
	else if (Command == TEXT("ReachTest"))
	{
		ReachTest(Player, Args);
	}
	else if (Command == TEXT("CrouchTest"))
	{
		CrouchTest(Player, Args);
	}
	else if (Command == TEXT("AutoMove"))
	{
		AutoMove(Player, Args);
	}
	else if (Command == TEXT("ShotCam"))
	{
		ShotCam(Player, Args);
	}
	else if (Command == TEXT("PlaceItem"))
	{
		PlaceItem(Player, Args);
	}
	else if (Command == TEXT("PoseArm"))
	{
		PoseArm(Player, Args);
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

	// Items spawned one after the other are spread sideways, so they do not land inside each other.
	static int32 SpawnCounter = 0;
	const float SideOffset = static_cast<float>((SpawnCounter++ % 5) - 2) * DevToolsSpawnSpreadCm;
	const FVector Location = Player->GetActorLocation() + Player->GetActorForwardVector() * DevToolsSpawnForwardCm
		+ Player->GetActorRightVector() * SideOffset + FVector(0.f, 0.f, DevToolsSpawnUpCm);
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

	const FRotator Yaw = DevToolsFlatYaw(Player);
	const FVector PlayerLocation = Player->GetActorLocation();
	const int32 Spawned = SpawnBandits(World, PlayerLocation + Yaw.Vector() * DevToolsEnemyDistanceCm, PlayerLocation, Count, bArmed);

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
		if (SkillIndex < 0)
		{
			// Also accept the menu names ("One-Handed", "Archery", "Smithing" ...), ignoring case, spaces and hyphens.
			const auto Normalize = [](FString Text)
			{
				Text = Text.Replace(TEXT(" "), TEXT("")).Replace(TEXT("-"), TEXT(""));
				return Text.ToLower();
			};
			const FString Wanted = Normalize(Tokens[0]);
			for (int32 Index = 0; Index < SkillCount; ++Index)
			{
				if (Normalize(UAedirJournalSubsystem::GetSkillDisplayName(static_cast<EAedirSkill>(Index))) == Wanted)
				{
					SkillIndex = Index;
					break;
				}
			}
		}
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

void UAedirDevTools::SetRace(AAedirPlayerCharacter* Player, const FString& Args)
{
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const FAedirRaceDef* Race = Tokens.IsEmpty() ? nullptr : UAedirRaceCatalog::Find(FName(*Tokens[0]));
	if (!Race)
	{
		DevToolsReport(FString::Printf(TEXT("Usage: SetRace <%s>"), *DevToolsRaceIds()));
		return;
	}

	// The derived Exec SetRace(FString) hides the base SetRace(FName), so the base version is called explicitly.
	Player->AAedirCreature::SetRace(Race->Id);
	DevToolsReport(FString::Printf(TEXT("Race set to %s"), *Race->DisplayName.ToString()));
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
	DevToolsReport(TEXT("  ChooseRace <Human|Elf|DarkElf|Dwarf>: change the player's race and starting stats"));
	DevToolsReport(TEXT("  CharacterCreation: open the race picker (console command on the player)"));
	DevToolsReport(TEXT("  Wound <Bruise|Cut|Puncture> [bone=upperarm_l]"));
	DevToolsReport(TEXT("  BloodTest: blood spray in front of the player"));
	DevToolsReport(TEXT("  DebugBody: toggle centre of mass, support margin and hand drawing"));
	DevToolsReport(TEXT("  SetStat <Strength|Athletism|Health|Stamina|Mana> <value>: change a stat to see how the body reacts"));
	DevToolsReport(TEXT("  Stats: print the player's stats"));
	DevToolsReport(TEXT("  Push <strength 0-1>: shove the player's balance forward, 1 is a knockdown push"));
	DevToolsReport(TEXT("  Knockdown: drop the player to the ground"));
	DevToolsReport(TEXT("  FallTest <height cm>: lift the player and let them fall"));
	DevToolsReport(TEXT("  GaitInfo <seconds> [interval]: log foot heights, foot positions and reach to the output log"));
	DevToolsReport(TEXT("  ReachTest <delay>, CrouchTest, AutoMove <forward> <right> [run], ShotCam <yaw> [dist] [height]|off, PlaceItem <Id> <forward> <right>"));
	DevToolsReport(TEXT("  DevHelp: this list"));
}

void UAedirDevTools::SetStat(AAedirPlayerCharacter* Player, const FString& Args)
{
	const TArray<FString> Tokens = DevToolsTokens(Args);
	UAedirStatsComponent* Stats = Player->Stats.Get();
	if (Tokens.Num() < 2 || !Stats)
	{
		DevToolsReport(TEXT("Usage: SetStat <Strength|Athletism|Health|Stamina|Mana> <value>"));
		return;
	}

	const float Value = FCString::Atof(*Tokens[1]);
	if (DevToolsIsToken(Tokens[0], TEXT("Strength")))
	{
		Stats->SetStrength(Value);
	}
	else if (DevToolsIsToken(Tokens[0], TEXT("Athletism")))
	{
		Stats->SetAthletism(Value);
	}
	else if (DevToolsIsToken(Tokens[0], TEXT("Health")))
	{
		const float Delta = Value - Stats->GetHealth();
		if (Delta > 0.f)
		{
			Stats->Heal(Delta);
		}
		else
		{
			Stats->ApplyDamage(-Delta);
		}
	}
	else if (DevToolsIsToken(Tokens[0], TEXT("Stamina")))
	{
		const float Delta = Stats->GetStamina() - Value;
		if (Delta > 0.f)
		{
			Stats->ConsumeStamina(Delta);
		}
	}
	else if (DevToolsIsToken(Tokens[0], TEXT("Mana")))
	{
		DevToolsReport(TEXT("Mana can only be refilled with Heal."));
		return;
	}
	else
	{
		DevToolsReport(FString::Printf(TEXT("Unknown stat %s"), *Tokens[0]));
		return;
	}

	PrintStats(Player, FString());
}

void UAedirDevTools::PrintStats(AAedirPlayerCharacter* Player, const FString& Args)
{
	const UAedirStatsComponent* Stats = Player->Stats.Get();
	if (!Stats)
	{
		return;
	}

	DevToolsReport(FString::Printf(TEXT("Strength %.0f  Athletism %.0f  Health %.0f/%.0f  Stamina %.0f/%.0f  Magicka %.0f/%.0f"),
		Stats->Strength, Stats->Athletism, Stats->GetHealth(), Stats->GetMaxHealth(), Stats->GetStamina(), Stats->GetMaxStamina(), Stats->GetMana(), Stats->GetMaxMana()));
}

void UAedirDevTools::Push(AAedirPlayerCharacter* Player, const FString& Args)
{
	const float Strength = Args.IsEmpty() ? 0.5f : FMath::Clamp(FCString::Atof(*Args), 0.f, 1.f);
	if (Player->Balance)
	{
		Player->Balance->ApplyBalanceImpact(Player->GetActorForwardVector(), Strength);
		DevToolsReport(FString::Printf(TEXT("Pushed with %.2f. Balance %.2f"), Strength, Player->Balance->GetBalance()));
	}
}

void UAedirDevTools::Knockdown(AAedirPlayerCharacter* Player, const FString& Args)
{
	if (Player->Balance)
	{
		Player->Balance->ApplyBalanceImpact(Player->GetActorForwardVector(), 2.f);
		DevToolsReport(TEXT("Knocked down"));
	}
}

void UAedirDevTools::GaitInfo(AAedirPlayerCharacter* Player, const FString& Args)
{
	// Logs what the body really does for a number of seconds: heights of the foot bones above the floor, how far each ankle is
	// in front of the hip, and how far the right hand is from what it reaches for.
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const float Seconds = Tokens.IsValidIndex(0) ? FMath::Clamp(FCString::Atof(*Tokens[0]), 0.1f, 60.f) : 5.f;
	const float Interval = Tokens.IsValidIndex(1) ? FMath::Clamp(FCString::Atof(*Tokens[1]), 0.02f, 5.f) : 0.5f;
	TSharedRef<float> Elapsed = MakeShared<float>(0.f);
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	Player->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Player, [Player, Elapsed, Handle, Seconds, Interval]()
	{
		const FAedirGaitState& State = Player->Gait->GetGaitState();
		const USkeletalMeshComponent* Mesh = Player->GetMesh();
		const FVector ActorLocation = Player->GetActorLocation();
		const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
		const float FloorZ = static_cast<float>(ActorLocation.Z) - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2.15f;

		// Height above the real ground under the bone.
		const auto Height = [&](const TCHAR* Bone)
		{
			const FVector BoneWorld = Mesh->GetBoneLocation(FName(Bone));
			FHitResult Hit;
			const FCollisionQueryParams Params(FName(TEXT("AedirGaitInfo")), false, Player);
			const bool bHit = Player->GetWorld()->LineTraceSingleByChannel(Hit, BoneWorld + FVector(0.f, 0.f, 80.f), BoneWorld - FVector(0.f, 0.f, 150.f), ECC_Visibility, Params);
			return static_cast<float>(BoneWorld.Z) - (bHit ? static_cast<float>(Hit.ImpactPoint.Z) : FloorZ);
		};
		const auto Ahead = [&](const TCHAR* Bone) { return static_cast<float>(FVector::DotProduct(Mesh->GetBoneLocation(FName(Bone)) - ActorLocation, Forward)); };

		float HandToItem = -1.f;
		const UAedirInteractionComponent* Interaction = Player->Interaction.Get();
		FVector ReachTarget;
		float ReachAlpha = 0.f;
		if (Interaction && Interaction->GetReachPose(EAedirHand::Right, ReachTarget, ReachAlpha))
		{
			HandToItem = static_cast<float>(FVector::Dist(Mesh->GetBoneLocation(FName(TEXT("hand_r"))), ReachTarget));
		}

		UE_LOG(LogAedir, Display, TEXT("[GaitInfo] t=%.2f speed=%.0f air=%d crouch=%d bal=%.2f margin=%.1f step=%d lift %.2f %.2f ankleZ %.1f %.1f ballZ %.1f %.1f ahead %.1f %.1f hip=%.1f spine=%.1f lower=%.1f lean=%.1f hipFwd=%.1f handToItem=%.1f held=%d/%d"),
			*Elapsed, Player->GetVelocity().Size2D(), Player->GetCharacterMovement()->IsFalling() ? 1 : 0, Player->bIsCrouched ? 1 : 0,
			Player->Balance ? Player->Balance->GetBalance() : -1.f, Player->Balance ? Player->Balance->GetSupportMargin() : 0.f,
			State.bStepping ? 1 : 0, State.LeftFootLift, State.RightFootLift,
			Height(TEXT("foot_l")), Height(TEXT("foot_r")), Height(TEXT("ball_l")), Height(TEXT("ball_r")), Ahead(TEXT("foot_l")), Ahead(TEXT("foot_r")),
			State.HipOffsetZ, State.SpineLeanPitch,
			Interaction ? Interaction->GetPelvisLowerZ() : 0.f, Interaction ? Interaction->GetReachLeanDegrees() : 0.f, Interaction ? Interaction->GetReachHipForward() : 0.f,
			HandToItem, Interaction && Interaction->GetHeldItem(EAedirHand::Left) ? 1 : 0, Interaction && Interaction->GetHeldItem(EAedirHand::Right) ? 1 : 0);

		*Elapsed += Interval;
		if (*Elapsed >= Seconds)
		{
			Player->GetWorldTimerManager().ClearTimer(*Handle);
		}
	}), Interval, true);
}

void UAedirDevTools::AutoMove(AAedirPlayerCharacter* Player, const FString& Args)
{
	// Walks the body with fixed input, so the gait can be measured and filmed without a keyboard.
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const float Forward = Tokens.IsValidIndex(0) ? FCString::Atof(*Tokens[0]) : 0.f;
	const float Right = Tokens.IsValidIndex(1) ? FCString::Atof(*Tokens[1]) : 0.f;
	const bool bRun = Tokens.IsValidIndex(2) && FCString::Atoi(*Tokens[2]) != 0;
	const float TurnDegrees = Tokens.IsValidIndex(3) ? FCString::Atof(*Tokens[3]) : 0.f;
	if (AController* Controller = Player->GetController(); Controller && !FMath::IsNearlyZero(TurnDegrees))
	{
		Controller->SetControlRotation(Controller->GetControlRotation() + FRotator(0.f, TurnDegrees, 0.f));
	}
	Player->SetDevAutoMove(FVector2D(Right, Forward));
	Player->SetWantsToRun(bRun);
	DevToolsReport(FString::Printf(TEXT("AutoMove forward %.1f right %.1f run %d"), Forward, Right, bRun ? 1 : 0));
}

void UAedirDevTools::ShotCam(AAedirPlayerCharacter* Player, const FString& Args)
{
	// A camera fixed to the body at a yaw angle around it: 90 looks from the right side, 180 from the front. "ShotCam off" gives the view back.
	APlayerController* Controller = Cast<APlayerController>(Player->GetController());
	if (!Controller)
	{
		return;
	}

	const TArray<FString> Tokens = DevToolsTokens(Args);
	if (Tokens.IsValidIndex(0) && DevToolsIsToken(Tokens[0], TEXT("off")))
	{
		Controller->SetViewTarget(Player);
		return;
	}

	const float YawDegrees = Tokens.IsValidIndex(0) ? FCString::Atof(*Tokens[0]) : 90.f;
	const float Distance = Tokens.IsValidIndex(1) ? FCString::Atof(*Tokens[1]) : 260.f;
	const float Height = Tokens.IsValidIndex(2) ? FCString::Atof(*Tokens[2]) : -20.f;
	const float LookHeight = Tokens.IsValidIndex(3) ? FCString::Atof(*Tokens[3]) : -30.f;

	const FRotator Yaw(0.f, YawDegrees, 0.f);
	const FVector RelativeLocation = Yaw.RotateVector(FVector(Distance, 0.f, 0.f)) + FVector(0.f, 0.f, Height);
	const FVector LookAt(0.f, 0.f, LookHeight);

	ACameraActor* Camera = Player->GetWorld()->SpawnActor<ACameraActor>();
	if (!Camera)
	{
		return;
	}
	Camera->AttachToActor(Player, FAttachmentTransformRules::KeepRelativeTransform);
	Camera->SetActorRelativeLocation(RelativeLocation);
	Camera->SetActorRelativeRotation((LookAt - RelativeLocation).Rotation());
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	Controller->SetViewTarget(Camera);
}

void UAedirDevTools::PoseArm(AAedirPlayerCharacter* Player, const FString& Args)
{
	// PoseArm <hand 0|1> <guard 0|1> <aim yaw> <aim pitch> <extension> <thrust 0|1>: holds a stance without a mouse.
	const TArray<FString> Tokens = DevToolsTokens(Args);
	if (Tokens.Num() < 6 || !Player->Combat)
	{
		return;
	}

	const EAedirHand Hand = FCString::Atoi(*Tokens[0]) == 0 ? EAedirHand::Left : EAedirHand::Right;
	Player->Combat->SetGuard(Hand, FCString::Atoi(*Tokens[1]) != 0);
	Player->Combat->DevSetArmAim(Hand, FVector2D(FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3])), FCString::Atof(*Tokens[4]));
	Player->Combat->SetThrustMode(FCString::Atoi(*Tokens[5]) != 0);
}

void UAedirDevTools::PlaceItem(AAedirPlayerCharacter* Player, const FString& Args)
{
	// Lays a catalog item on the floor at an offset from the feet, to test a grab at an exact distance.
	const TArray<FString> Tokens = DevToolsTokens(Args);
	UWorld* World = Player->GetWorld();
	if (Tokens.Num() < 3 || !World || !UAedirItemCatalog::Find(FName(*Tokens[0])))
	{
		DevToolsReport(TEXT("Usage: PlaceItem <Id> <forward cm> <right cm>"));
		return;
	}

	const FVector Location = Player->GetActorLocation()
		+ Player->GetActorForwardVector() * FCString::Atof(*Tokens[1])
		+ Player->GetActorRightVector() * FCString::Atof(*Tokens[2])
		- FVector(0.f, 0.f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 20.f);
	if (AAedirCatalogItem::SpawnCatalogItem(World, FName(*Tokens[0]), FTransform(DevToolsFlatYaw(Player), Location)))
	{
		DevToolsReport(FString::Printf(TEXT("Placed %s"), *Tokens[0]));
	}
}

void UAedirDevTools::ReachTest(AAedirPlayerCharacter* Player, const FString& Args)
{
	// A delay in seconds lets a freshly spawned item land first.
	const TArray<FString> Tokens = DevToolsTokens(Args);
	const float Delay = Tokens.IsValidIndex(0) ? FCString::Atof(*Tokens[0]) : 0.f;
	const bool bBothHands = Tokens.IsValidIndex(1) && FCString::Atoi(*Tokens[1]) != 0;
	FTimerHandle ReachTimer;
	Player->GetWorldTimerManager().SetTimer(ReachTimer, FTimerDelegate::CreateWeakLambda(Player, [Player, bBothHands]()
	{
		if (Player->Interaction)
		{
			Player->Interaction->StartReach(EAedirHand::Right);
			if (bBothHands)
			{
				Player->Interaction->StartReach(EAedirHand::Left);
			}
			DevToolsReport(TEXT("Right hand reaching"));
		}
	}), FMath::Max(Delay, 0.01f), false);
}

void UAedirDevTools::CrouchTest(AAedirPlayerCharacter* Player, const FString& Args)
{
	if (Player->bIsCrouched)
	{
		Player->UnCrouch();
	}
	else
	{
		Player->Crouch();
	}
	DevToolsReport(Player->bIsCrouched ? TEXT("Crouching") : TEXT("Standing"));
}

void UAedirDevTools::FallTest(AAedirPlayerCharacter* Player, const FString& Args)
{
	const float Height = Args.IsEmpty() ? 300.f : FMath::Clamp(FCString::Atof(*Args), 20.f, 5000.f);
	Player->SetActorLocation(Player->GetActorLocation() + FVector(0.f, 0.f, Height), false, nullptr, ETeleportType::TeleportPhysics);
	DevToolsReport(FString::Printf(TEXT("Dropping from %.0f cm"), Height));
}

#endif
