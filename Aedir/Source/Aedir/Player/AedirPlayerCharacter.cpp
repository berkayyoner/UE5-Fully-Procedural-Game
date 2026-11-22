#include "AedirPlayerCharacter.h"
#include "PhysicsEngine/BodySetup.h"
#include "Containers/Ticker.h"
#include "Aedir.h"
#include "Audio/AedirSfxSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/App.h"
#include "RenderTimer.h"
#include "RHI.h"
#include "Travel/AedirTravelSubsystem.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "LandscapeProxy.h"
#include "Components/DecalComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Npc/AedirTrainingDummy.h"
#include "Wounds/AedirWoundComponent.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Net/AedirSaveSubsystem.h"
#include "UI/AedirMenuSubsystem.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"

#include "Settings/AedirSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/AedirCombatComponent.h"
#include "Combat/AedirDustPuff.h"
#include "Fists/AedirFistArms.h"
#include "Animation/AedirGaitComponent.h"
#include "Creature/AedirStatsComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Dev/AedirDevTools.h"
#include "Surface/AedirFootprintSubsystem.h"
#include "Physics/AedirBodyControlComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Dev/AedirConsoleCommands.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMeshActor.h"
#include "Game/AedirEntities.h"
#include "Weather/AedirPrecipitationSubsystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Graphics/AedirFoliageSubsystem.h"
#include "UI/SAedirConsole.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AedirProceduralAnimInstance.h"
#include "Player/AedirCameraBoom.h"
#include "Items/AedirInteractionComponent.h"
#include "Physics/AedirBalanceComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/AedirGameMode.h"
#include "Items/AedirCatalogItem.h"
#include "Items/AedirItemActor.h"
#include "Components/BoxComponent.h"
#include "TimerManager.h"
#include "UI/AedirJournalSubsystem.h"
#include "UI/AedirMenuSubsystem.h"
#include "Weather/AedirWeatherEffectsComponent.h"
#include "Weather/AedirWeatherManager.h"
#include "GroomComponent.h"
#include "NiagaraComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"

static TAutoConsoleVariable<int32> CVarAedirAutoGetUp(TEXT("aedir.AutoGetUp"), 0, TEXT("1: the player's body gets up by itself whenever it can, for tests left to run on their own."));

namespace
{
}

namespace
{
	TAutoConsoleVariable<int32> CVarAedirNewFists(TEXT("aedir.NewFists"), 1, TEXT("Empty hands fight with the rebuilt fists (1) or the older fist code (0)."));
	// The photo camera: how slow time runs while it is up, and how far the wheel takes it in and out.
	constexpr float PhotoTimeDilation = 0.0001f;
	// The free camera: degrees it turns for a unit of mouse, how much faster Shift makes it, and the wheel's speed range.
	constexpr float FreeCameraDegreesPerInput = 2.5f;
	constexpr float FreeCameraFastScale = 4.f;
	constexpr float FreeCameraWheelScale = 1.2f;
	constexpr float FreeCameraMinSpeedCm = 25.f;
	constexpr float FreeCameraMaxSpeedCm = 3000.f;
	// How quickly the free camera turns after the mouse (the larger, the closer it follows).
	constexpr float FreeCameraTurnFollow = 6.f;
	// A press of the thrust key shorter than this is a tap that leaves the thrust on.
	constexpr double ThrustTapSeconds = 0.25;
	constexpr float AedirGuestStoreSeconds = 60.f;
	constexpr float AedirConsolePickRangeCm = 20000.f;
	constexpr float AedirConsolePickRadiusCm = 4.f;
	constexpr float AedirPointRangeCm = 8000.f;
	constexpr float AedirPointBehindCm = 40.f;
	constexpr float AedirPointItemWidthCm = 18.f;
	constexpr float AedirPointItemWidthShare = 0.012f;
	constexpr float AedirTalkRangeCm = 300.f;
	constexpr float PlayerCharacterCreationDelaySeconds = 0.8f;
	constexpr float PlayerTutorialTurnDegrees = 15.f;
	// The engine turns the view 2.5 degrees for each unit of mouse input; going round the body turns the camera the same.
	constexpr float PlayerOrbitDegreesPerInput = 2.5f;
	// Locking on, anyone not an enemy counts as this much further off the view (in radians) as an enemy.
	constexpr float PlayerLockOnNonEnemyPenalty = 10.f;
	// One frame turning a held item further than this is a tumble, not a swing.
	constexpr float BladeWatchFlipDegrees = 20.f;

	FName PlayerTutorialQuestId()
	{
		return FName(TEXT("Awakening"));
	}

	const FAedirQuest* PlayerFindQuest(const UAedirJournalSubsystem* Journal, FName QuestId)
	{
		return Journal->GetQuests().FindByPredicate([QuestId](const FAedirQuest& Quest)
		{
			return Quest.Id == QuestId;
		});
	}

	FAedirQuest PlayerBuildAwakeningQuest()
	{
		FAedirQuest Quest;
		Quest.Id = PlayerTutorialQuestId();
		Quest.Title = FText::FromString(TEXT("Awakening"));
		Quest.Description = FText::FromString(TEXT("You come to your senses on the training grounds of Aedir. Learn how to move, fight and take what you need."));

		const TCHAR* const ObjectiveTexts[] =
		{
			TEXT("Look around with the mouse"),
			TEXT("Pick up an item (Q or E)"),
			TEXT("Raise your fists (hold left or right mouse button)"),
			TEXT("Open your menu (Tab)")
		};
		for (const TCHAR* ObjectiveText : ObjectiveTexts)
		{
			FAedirQuestObjective Objective;
			Objective.Text = FText::FromString(ObjectiveText);
			Quest.Objectives.Add(Objective);
		}

		return Quest;
	}
}

AAedirPlayerCharacter::AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	CameraBoom = CreateDefaultSubobject<UAedirCameraBoom>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	CameraBoom->TargetArmLength = ThirdPersonArmLength;
	CameraBoom->SocketOffset = ThirdPersonShoulderOffset;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 14.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;


	WeatherEffects = CreateDefaultSubobject<UAedirWeatherEffectsComponent>(TEXT("WeatherEffects"));
	FistArms = CreateDefaultSubobject<UAedirFistArms>(TEXT("FistArms"));

	Hair = CreateDefaultSubobject<UGroomComponent>(TEXT("Hair"));
	Hair->SetupAttachment(Face ? Face.Get() : GetMesh());
	Hair->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AAedirPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseConsole();
	Super::EndPlay(EndPlayReason);
}

void AAedirPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	ApplyLook();

	// The hair grows on the face it was bound to; on another head (or none) it is left off rather than floating.
	UGroomAsset* Groom = Cast<UGroomAsset>(HairGroom.TryLoad());
	UGroomBindingAsset* Binding = Cast<UGroomBindingAsset>(HairBinding.TryLoad());
	const USkeletalMeshComponent* Head = Face && Face->GetSkeletalMeshAsset() ? Face.Get() : nullptr;
	if (Hair && Groom && Binding && Head && Binding->GetTargetSkeletalMesh() == Head->GetSkeletalMeshAsset())
	{
		// It swings under the physics set on the groom itself.
		Hair->SetGroomAsset(Groom, Binding);
		UMaterialInterface* HairLook = Cast<UMaterialInterface>(HairMaterial.TryLoad());
		for (int32 Slot = 0; Slot < Hair->GetNumMaterials(); ++Slot)
		{
			if (HairLook)
			{
				Hair->SetMaterial(Slot, HairLook);
			}
			if (UMaterialInstanceDynamic* Look = Hair->CreateDynamicMaterialInstance(Slot))
			{
				Look->SetScalarParameterValue(TEXT("hairMelanin"), HairMelanin);
				Look->SetScalarParameterValue(TEXT("hairRedness"), HairRedness);
			}
		}
		UE_LOG(LogAedir, Log, TEXT("[Hair] %s grows %s on %s, %d material slots, look %s"), *GetName(), *Groom->GetName(), *Head->GetSkeletalMeshAsset()->GetName(), Hair->GetNumMaterials(), *GetNameSafe(HairLook));
		GetWorldTimerManager().SetTimer(HairHelmetTimer, this, &AAedirPlayerCharacter::UpdateHairUnderHelmet, 0.25f, true);
	}
	else if (Hair)
	{
		Hair->SetVisibility(false);
		UE_LOG(LogAedir, Warning, TEXT("[Hair] %s has no hair: groom %d, binding %d, head %d"), *GetName(), Groom ? 1 : 0, Binding ? 1 : 0, Head ? 1 : 0);
	}

	// The camera settings of the code win over values stored on the boom in older blueprints.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UAedirSettingsSubsystem* Settings = GameInstance->GetSubsystem<UAedirSettingsSubsystem>())
		{
			Settings->ApplyWhenWorldReady();
		}
	}

	CameraZoomTargetCm = FMath::Clamp(ThirdPersonArmLength, CameraZoomMinCm, CameraZoomMaxCm);
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = CameraZoomTargetCm;
		CameraBoom->SocketOffset = ThirdPersonShoulderOffset;
	}

	if (HasAuthority() && GroupId.IsNone())
	{
		GroupId = FName(TEXT("Heroes"));
		OnGroupIdChanged.Broadcast(GroupId);
	}

	if (HasAuthority())
	{
		EnsureStarterQuest();
	}

	ApplyViewPitchLimits();

	SendGuestCharacter();

	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(CharacterCreationTimer, this, &AAedirPlayerCharacter::MaybeOpenCharacterCreation, PlayerCharacterCreationDelaySeconds, false);
	}

#if !UE_BUILD_SHIPPING
	// Test helper: "-AedirStartup=\"SpawnEnemy 1 1;SpawnItem Longsword hold\"" runs console commands once after the local player spawned.
	static bool bStartupCommandsRan = false;
	FString StartupCommands;
	if (!bStartupCommandsRan && IsLocallyControlled() && FParse::Value(FCommandLine::Get(), TEXT("AedirStartup="), StartupCommands))
	{
		bStartupCommandsRan = true;

		// "Wait 3" between the commands delays everything after it, so a whole test can be scripted from the command line.
		TArray<FString> Commands;
		StartupCommands.ParseIntoArray(Commands, TEXT(";"), true);
		float Delay = 2.f;
		for (const FString& RawCommand : Commands)
		{
			const FString Command = RawCommand.TrimStartAndEnd();
			if (Command.StartsWith(TEXT("Wait "), ESearchCase::IgnoreCase))
			{
				Delay += FCString::Atof(*Command.RightChop(5));
				continue;
			}

			FTimerHandle StartupTimer;
			GetWorldTimerManager().SetTimer(StartupTimer, FTimerDelegate::CreateWeakLambda(this, [this, Command]()
			{
				if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
				{
					PlayerController->ConsoleCommand(Command);
				}
			}), Delay, false);
		}
	}
	else if (!bStartupCommandsRan && bSpawnTestItemsOnStart && IsLocallyControlled() && HasAuthority())
	{
		// Grab and inventory tests need something to pick up, so a few catalog items drop in front of the player.
		FTimerHandle TestItemTimer;
		GetWorldTimerManager().SetTimer(TestItemTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			for (const TCHAR* ItemId : { TEXT("RustySword"), TEXT("IronSword"), TEXT("SteelSword"), TEXT("SteelLongsword"), TEXT("BloodySword"), TEXT("SilverShortsword"), TEXT("ElvenBattleaxe"), TEXT("Axe"), TEXT("RustyIronMace") })
			{
				UAedirDevTools::Execute(this, TEXT("SpawnItem"), ItemId);
			}
		}), 1.5f, false);
	}
#endif
}

void AAedirPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	ApplyViewPitchLimits();

	// BeginPlay usually runs before possession, so the local journal is filled here.
	if (IsLocallyControlled())
	{
		EnsureStarterQuest();
	}
}

void AAedirPlayerCharacter::ShakeView(float Degrees, float Seconds)
{
	// The shake still going is kept if it is the bigger.
	if (Degrees >= HitShakeStrength * HitShake * HitShake)
	{
		HitShake = 1.f;
		HitShakeStrength = Degrees;
		HitShakeSeconds = Seconds;
	}
	UE_LOG(LogAedir, Log, TEXT("[Shake] %s's view shakes %.2f degrees for %.2f s"), *GetName(), Degrees, Seconds);
}

void AAedirPlayerCharacter::ShakeViewFromHurt(float Damage)
{
	if (Damage >= ShakeMinDamage && IsLocallyControlled())
	{
		ShakeView(FMath::Lerp(HurtShakeDegreesMin, HurtShakeDegreesMax, FMath::Clamp(Damage / FMath::Max(HurtShakeFullDamage, 1.f), 0.f, 1.f)), HurtShakeSeconds);
	}
}

void AAedirPlayerCharacter::ShakeViewFromStrike(float Damage)
{
	if (Damage >= ShakeMinDamage && IsLocallyControlled())
	{
		ShakeView(FMath::Lerp(StrikeShakeDegreesMin, StrikeShakeDegreesMax, FMath::Clamp(Damage / FMath::Max(StrikeShakeFullDamage, 1.f), 0.f, 1.f)), StrikeShakeSeconds);
	}
}

namespace
{
	constexpr int32 HeartSampleRate = 48000;
	// One beat: the "lub", a low thump falling in pitch, and the "dub" after it, a little higher, softer and shorter.
	constexpr float HeartBeatSeconds = 0.6f;
	constexpr float HeartDubAfterSeconds = 0.26f;
	constexpr float HeartPulseSeconds = 0.35f;
}

void AAedirPlayerCharacter::BuildHeartbeat()
{
	const int32 Count = FMath::RoundToInt(HeartBeatSeconds * HeartSampleRate);
	HeartBeatPcm.SetNumZeroed(Count * 2);
	int16* Samples = reinterpret_cast<int16*>(HeartBeatPcm.GetData());
	double PhaseLub = 0.0;
	double PhaseDub = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float T = static_cast<float>(Index) / HeartSampleRate;
		// The lub: 58 Hz falling to 38, coming up in a few milliseconds and dying away over a twentieth of a second.
		const float LubPitch = FMath::Lerp(58.f, 38.f, FMath::Clamp(T / 0.14f, 0.f, 1.f));
		PhaseLub += UE_TWO_PI * LubPitch / HeartSampleRate;
		const float Lub = (1.f - FMath::Exp(-T / 0.006f)) * FMath::Exp(-T / 0.05f) * FMath::Sin(static_cast<float>(PhaseLub));
		// The dub, 70 Hz falling to 48.
		const float U = T - HeartDubAfterSeconds;
		float Dub = 0.f;
		if (U > 0.f)
		{
			const float DubPitch = FMath::Lerp(70.f, 48.f, FMath::Clamp(U / 0.11f, 0.f, 1.f));
			PhaseDub += UE_TWO_PI * DubPitch / HeartSampleRate;
			Dub = 0.7f * (1.f - FMath::Exp(-U / 0.005f)) * FMath::Exp(-U / 0.038f) * FMath::Sin(static_cast<float>(PhaseDub));
		}
		// Faded out at the very end, so beats laid one after another meet without a click.
		const float Tail = FMath::Clamp((HeartBeatSeconds - T) / 0.02f, 0.f, 1.f);
		Samples[Index] = static_cast<int16>(FMath::Clamp((Lub + Dub) * 0.85f * Tail, -1.f, 1.f) * 32767.f);
	}
	HeartWave = NewObject<USoundWaveProcedural>(this);
	HeartWave->SetSampleRate(HeartSampleRate);
	HeartWave->NumChannels = 1;
	HeartWave->Duration = INDEFINITELY_LOOPING_DURATION;
	HeartWave->SoundGroup = SOUNDGROUP_Default;
	HeartWave->bLooping = false;
	HeartAudio = NewObject<UAudioComponent>(this);
	HeartAudio->bAutoActivate = false;
	HeartAudio->bAllowSpatialization = false;
	HeartAudio->bIsUISound = true;
	HeartAudio->SetSound(HeartWave);
	HeartAudio->RegisterComponent();
}

float AAedirPlayerCharacter::GetHeartPulse() const
{
	const double Since = FPlatformTime::Seconds() - LastHeartBeatSeconds;
	return Since < 0.0 ? 0.f : FMath::Clamp(1.f - static_cast<float>(Since) / HeartPulseSeconds, 0.f, 1.f);
}

void AAedirPlayerCharacter::UpdateHurtFeel()
{
	const bool bDead = !Stats || Stats->IsDead();
	const float Health = Stats ? Stats->GetHealth() / FMath::Max(Stats->GetMaxHealth(), 1.f) : 1.f;
	HurtShare = bDead ? 0.f : FMath::Clamp((HurtFromHealthShare - Health) / FMath::Max(HurtFromHealthShare, 0.01f), 0.f, 1.f);
	if (!HeartWave && HurtShare > 0.f)
	{
		BuildHeartbeat();
	}
	const bool bBeat = HurtShare > 0.f && HeartAudio && HeartWave;
	if (bBeat != bHeartBeating)
	{
		bHeartBeating = bBeat;
		UE_LOG(LogAedir, Log, TEXT("[Heart] %s"), bBeat ? *FString::Printf(TEXT("starts to be heard, health %.0f%%"), Health * 100.f) : bDead ? TEXT("stops: dead") : TEXT("is no longer heard"));
		if (!bBeat && HeartAudio)
		{
			HeartAudio->Stop();
			if (HeartWave)
			{
				HeartWave->ResetAudio();
			}
		}
	}
	if (!bBeat)
	{
		return;
	}
	if (!HeartAudio->IsPlaying())
	{
		HeartAudio->Play();
	}
	const double Now = FPlatformTime::Seconds();
	HeartAudio->SetVolumeMultiplier(FMath::Lerp(static_cast<float>(HeartVolumeMinMax.X), static_cast<float>(HeartVolumeMinMax.Y), HurtShare));
	if (Now >= NextHeartBeatSeconds)
	{
		const float Bpm = FMath::Lerp(static_cast<float>(HeartBpmMinMax.X), static_cast<float>(HeartBpmMinMax.Y), HurtShare);
		HeartWave->QueueAudio(HeartBeatPcm.GetData(), HeartBeatPcm.Num());
		LastHeartBeatSeconds = Now;
		NextHeartBeatSeconds = Now + 60.0 / FMath::Max(Bpm, 20.f);
		static const IConsoleVariable* HitLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
		UE_CLOG(HitLog && HitLog->GetInt() != 0, LogAedir, Log, TEXT("[Heart] beat at %.0f a minute, volume %.2f, health %.0f%%"), Bpm, HeartAudio->VolumeMultiplier, Health * 100.f);
	}
}

void AAedirPlayerCharacter::HeartWav()
{
	if (!HeartBeatPcm.Num())
	{
		BuildHeartbeat();
	}
	// A plain 16-bit mono wave file.
	TArray<uint8> File;
	const auto Add32 = [&File](uint32 V) { File.Append(reinterpret_cast<uint8*>(&V), 4); };
	const auto Add16 = [&File](uint16 V) { File.Append(reinterpret_cast<uint8*>(&V), 2); };
	const auto AddTag = [&File](const char* Tag) { File.Append(reinterpret_cast<const uint8*>(Tag), 4); };
	AddTag("RIFF");
	Add32(36 + HeartBeatPcm.Num());
	AddTag("WAVE");
	AddTag("fmt ");
	Add32(16);
	Add16(1);
	Add16(1);
	Add32(HeartSampleRate);
	Add32(HeartSampleRate * 2);
	Add16(2);
	Add16(16);
	AddTag("data");
	Add32(HeartBeatPcm.Num());
	File.Append(HeartBeatPcm);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Heartbeat.wav");
	FFileHelper::SaveArrayToFile(File, *Path);
	UE_LOG(LogAedir, Display, TEXT("[Dev] One heartbeat written to %s"), *FPaths::ConvertRelativePathToFull(Path));
}

void AAedirPlayerCharacter::ApplyLook()
{
	if (!FollowCamera)
	{
		return;
	}
	FPostProcessSettings& Look = FollowCamera->PostProcessSettings;
	Look.bOverride_ColorSaturation = true;
	Look.ColorSaturation = FVector4(1.f, 1.f, 1.f, LookSaturation);
	Look.bOverride_ColorSaturationHighlights = true;
	Look.ColorSaturationHighlights = FVector4(1.f, 1.f, 1.f, LookHighlightSaturation);
	Look.bOverride_ColorContrast = true;
	Look.ColorContrast = FVector4(1.f, 1.f, 1.f, LookContrast);
	Look.bOverride_ColorOffsetShadows = true;
	Look.ColorOffsetShadows = FVector4(0.f, 0.f, 0.f, LookShadowLift);
	Look.bOverride_FilmToe = true;
	Look.FilmToe = LookFilmToe;
	Look.bOverride_LocalExposureShadowContrastScale = true;
	Look.LocalExposureShadowContrastScale = LookShadowContrastScale;
	Look.bOverride_LocalExposureHighlightContrastScale = true;
	Look.LocalExposureHighlightContrastScale = LookHighlightContrastScale;
	FollowCamera->PostProcessBlendWeight = 1.f;
}

static TAutoConsoleVariable<int32> CVarAedirLook(TEXT("aedir.Look"), 1, TEXT("1: the camera's own softer grading of the world (LookSaturation and on); 0: the engine's and the level's alone, to compare."));

FString AAedirPlayerCharacter::DevLookInfo() const
{
	const FPostProcessSettings* Look = FollowCamera ? &FollowCamera->PostProcessSettings : nullptr;
	return FString::Printf(TEXT("[Look] grading %s, saturation %.2f, darkness %.2f, time %.2f, knocked out %d, dead %d"), CVarAedirLook.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"),
		Look ? Look->ColorSaturation.W : 0.f, SenseDarkness, GetWorld() ? GetWorld()->GetWorldSettings()->TimeDilation : 1.f, Stats && Stats->IsKnockedOut() ? 1 : 0, Stats && Stats->IsDead() ? 1 : 0);
}

void AAedirPlayerCharacter::UpdateSenseView()
{
	// Read in real seconds: dying slows time itself.
	const float Real = static_cast<float>(FApp::GetDeltaTime());
	const bool bAlone = GetNetMode() == NM_Standalone;
	const bool bDead = Stats && Stats->IsDead();
	float Grey = 0.f;
	if (bDead)
	{
		DeathRealSeconds = DeathRealSeconds < 0.f ? 0.f : DeathRealSeconds + Real;
		const float Slow = FMath::SmoothStep(0.f, FMath::Max(DeathSlowSeconds, 0.01f), DeathRealSeconds);
		if (bAlone && !bPhotoCamera)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, FMath::Lerp(1.f, DeathTimeDilation, Slow));
		}
		Grey = FMath::SmoothStep(0.f, FMath::Max(DeathGreySeconds, 0.01f), DeathRealSeconds);
		SenseDarkness = DeathDarkness * FMath::SmoothStep(0.f, FMath::Max(DeathDarkSeconds, 0.01f), DeathRealSeconds);
	}
	else
	{
		// Back from the dead (brought round, or a new body): time runs as it did.
		if (DeathRealSeconds >= 0.f)
		{
			DeathRealSeconds = -1.f;
			if (bAlone && !bPhotoCamera)
			{
				UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
			}
		}
		const bool bOut = Stats && Stats->IsKnockedOut();
		const float In = Real / FMath::Max(KnockoutFadeSeconds, 0.01f);
		SenseOutShare = bOut ? FMath::Min(SenseOutShare + In, 1.f) : FMath::Max(SenseOutShare - Real / FMath::Max(WakeDarkSeconds, 0.01f), 0.f);
		SenseGreyShare = bOut ? FMath::Min(SenseGreyShare + In, 1.f) : FMath::Max(SenseGreyShare - Real / FMath::Max(WakeColourSeconds, 0.01f), 0.f);
		SenseDarkness = KnockoutDarkness * FMath::SmoothStep(0.f, 1.f, SenseOutShare);
		Grey = FMath::SmoothStep(0.f, 1.f, SenseGreyShare);
	}
	if (FollowCamera)
	{
		const bool bGraded = CVarAedirLook.GetValueOnGameThread() != 0;
		FollowCamera->PostProcessSettings.ColorSaturation = FVector4(1.f, 1.f, 1.f, (bGraded ? LookSaturation : 1.f) * (1.f - Grey));
		FollowCamera->PostProcessSettings.ColorSaturationHighlights = FVector4(1.f, 1.f, 1.f, (bGraded ? LookHighlightSaturation : 1.f) * (1.f - Grey));
		FollowCamera->PostProcessSettings.bOverride_ColorSaturationHighlights = bGraded;
		FollowCamera->PostProcessSettings.bOverride_ColorContrast = bGraded;
		FollowCamera->PostProcessSettings.bOverride_ColorOffsetShadows = bGraded;
		FollowCamera->PostProcessSettings.bOverride_FilmToe = bGraded;
		FollowCamera->PostProcessSettings.bOverride_LocalExposureShadowContrastScale = bGraded;
		FollowCamera->PostProcessSettings.bOverride_LocalExposureHighlightContrastScale = bGraded;
	}
}

void AAedirPlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocallyControlled())
	{
		return;
	}
	UpdateSenseView();
	UpdateHurtFeel();

	if (!IsMenuOpen() && !IsCharacterCreationOpen())
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const float ForwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::W) ? 1.f : 0.f;
		const float BackwardKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::S) ? 1.f : 0.f;
		const float RightKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::D) ? 1.f : 0.f;
		const float LeftKeyValue = PlayerController && PlayerController->IsInputKeyDown(EKeys::A) ? 1.f : 0.f;
		// The keys fly the free camera instead of walking the body while it is up.
		const FVector2D MoveInput = bPhotoCamera ? FVector2D::ZeroVector : FVector2D(RightKeyValue - LeftKeyValue, ForwardKeyValue - BackwardKeyValue) + DevAutoMove;
		if (bNoclip)
		{
			// Flying, forward is where the view looks, up or down too; Space rises and Ctrl sinks, Shift goes faster.
			const FRotationMatrix View(GetControlRotation());
			const float Rise = (PlayerController && PlayerController->IsInputKeyDown(EKeys::SpaceBar) ? 1.f : 0.f) - (PlayerController && PlayerController->IsInputKeyDown(EKeys::LeftControl) ? 1.f : 0.f);
			const FVector Way = View.GetUnitAxis(EAxis::X) * MoveInput.Y + View.GetUnitAxis(EAxis::Y) * MoveInput.X + FVector::UpVector * Rise;
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
			{
				Move->MaxFlySpeed = NoclipSpeed * (PlayerController && PlayerController->IsInputKeyDown(EKeys::LeftShift) ? NoclipFastShare : 1.f);
			}
			AddMovementInput(Way.GetClampedToMaxSize(1.0), 1.f);
		}
		else
		{
			MoveLocal(MoveInput.GetClampedToMaxSize(1.0));
		}
		// Down on the ground the walking keys get the body up: held, it lifts its hips up and over to the way they go, as seen
		// from the camera, and turns round to face it; let go, it sinks back.
		DevHeldKeysLeft = FMath::Max(DevHeldKeysLeft - DeltaSeconds, 0.f);
		const FVector2D Keys = DevHeldKeysLeft > 0.f ? FVector2D(DevHeldKeys.Y, DevHeldKeys.X) : FVector2D(MoveInput.X, MoveInput.Y);
		if (Balance && (Balance->IsRagdoll() || Balance->IsGettingUp()) && Keys.Size() > 0.5)
		{
			const FRotator CameraYaw(0.f, GetControlRotation().Yaw, 0.f);
			const FVector KeyWay = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X) * Keys.Y + FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y) * Keys.X;
			Balance->RequestGetUp(KeyWay.GetClampedToMaxSize(1.0));
		}
		// For tests left to run by themselves: the body gets up on its own whenever it can, the way it lies.
		static const IConsoleVariable* AutoGetUp = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.AutoGetUp"));
		if (AutoGetUp && AutoGetUp->GetInt() != 0 && Balance && (Balance->IsRagdoll() || Balance->IsGettingUp()))
		{
			Balance->RequestGetUp(FVector::ZeroVector);
		}
	}

	UpdateLockOn(DeltaSeconds);
	UpdateTutorialObjectives();
	UpdateHandUseHold(DeltaSeconds);
	UpdateSwingInput(DeltaSeconds);
	UpdatePhotoCamera();

	if (bDebugBody)
	{
		DrawDebugBody();
	}
}

void AAedirPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}


	PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &AAedirPlayerCharacter::HandleMouseX);
	PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &AAedirPlayerCharacter::HandleMouseY);
	PlayerInputComponent->BindAxisKey(EKeys::MouseWheelAxis, this, &AAedirPlayerCharacter::HandleMouseWheel);

	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AAedirPlayerCharacter::HandleRunPressed);
	PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AAedirPlayerCharacter::HandleRunReleased);

	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AAedirPlayerCharacter::HandleCrouchPressed);
	PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &AAedirPlayerCharacter::HandleCrouchReleased);

	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &AAedirPlayerCharacter::HandleViewTogglePressed);
	PlayerInputComponent->BindKey(EKeys::V, IE_Released, this, &AAedirPlayerCharacter::HandleViewToggleReleased);
	PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AAedirPlayerCharacter::HandleHeadCameraPressed);

	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AAedirPlayerCharacter::HandleMenuTogglePressed);
	PlayerInputComponent->BindKey(EKeys::J, IE_Pressed, this, &AAedirPlayerCharacter::HandleJournalPressed);
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAedirPlayerCharacter::HandleMapPressed);
	PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &AAedirPlayerCharacter::HandleSkillsPressed);
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAedirPlayerCharacter::HandlePausePressed);

	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AAedirPlayerCharacter::HandleJumpPressed);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &AAedirPlayerCharacter::HandleJumpReleased);

	// The left mouse button works the right hand and the right button the left hand.
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightGuardPressed);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleRightGuardReleased);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftGuardPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AAedirPlayerCharacter::HandleLeftGuardReleased);

	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftHandPressed);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &AAedirPlayerCharacter::HandleLeftHandReleased);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightHandPressed);
	PlayerInputComponent->BindKey(EKeys::E, IE_Released, this, &AAedirPlayerCharacter::HandleRightHandReleased);

	// A hand goes across the body to the belt, so T works the left side of the belt with the right hand and Y the right side with the left.
	// T uses what the right hand holds, Y what the left one holds.
	PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &AAedirPlayerCharacter::HandleRightUsePressed);
	PlayerInputComponent->BindKey(EKeys::T, IE_Released, this, &AAedirPlayerCharacter::HandleRightUseReleased);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AAedirPlayerCharacter::HandleLeftUsePressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Released, this, &AAedirPlayerCharacter::HandleLeftUseReleased);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AAedirPlayerCharacter::HandleTalkPressed);
	PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AAedirPlayerCharacter::HandleShovePressed);
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &AAedirPlayerCharacter::HandleChangeHandsPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Pressed, this, &AAedirPlayerCharacter::HandleThrustPressed);
	PlayerInputComponent->BindKey(EKeys::LeftAlt, IE_Released, this, &AAedirPlayerCharacter::HandleThrustReleased);
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AAedirPlayerCharacter::HandleTurnWeaponPressed);
	// The key under Esc types " on a Turkish keyboard, and is known by that.
	PlayerInputComponent->BindKey(EKeys::Tilde, IE_Pressed, this, &AAedirPlayerCharacter::ToggleConsole);
	PlayerInputComponent->BindKey(EKeys::Quote, IE_Pressed, this, &AAedirPlayerCharacter::ToggleConsole);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AAedirPlayerCharacter::HandleDynamicKickPressed);
	PlayerInputComponent->BindKey(EKeys::CapsLock, IE_Pressed, this, &AAedirPlayerCharacter::HandleLockOnTogglePressed);
}

void AAedirPlayerCharacter::HandleLockOnTogglePressed()
{
	bLockOnEnabled = !bLockOnEnabled;
	LockOnTarget.Reset();
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UAedirTravelSubsystem* Travel = PlayerController->GetLocalPlayer() ? PlayerController->GetLocalPlayer()->GetSubsystem<UAedirTravelSubsystem>() : nullptr)
		{
			Travel->ShowNotification(bLockOnEnabled ? NSLOCTEXT("Aedir", "LockOnOn", "Target lock on") : NSLOCTEXT("Aedir", "LockOnOff", "Target lock off"), 2.f);
		}
	}
}

AAedirCreature* AAedirPlayerCharacter::FindLockOnTarget(float ConeDegrees) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !GetWorld())
	{
		return nullptr;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Look = ViewRotation.Vector();
	const float MinCos = FMath::Cos(FMath::DegreesToRadians(ConeDegrees > 0.f ? ConeDegrees : LockOnConeDegrees));

	// The living creature nearest to where the view points, among those close enough; a little further away counts a little.
	AAedirCreature* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		AAedirCreature* Other = *It;
		// Never a friend; an enemy always before anyone else in view.
		if (!Other || Other == this || (Other->Stats && Other->Stats->IsDead()) || IsFriendOf(Other))
		{
			continue;
		}
		const FVector Chest = Other->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector To = Chest - ViewLocation;
		const float Distance = static_cast<float>((Other->GetActorLocation() - GetActorLocation()).Size());
		const float Cos = static_cast<float>(FVector::DotProduct(To.GetSafeNormal(), Look));
		if (Distance > LockOnRangeCm || Cos < MinCos)
		{
			continue;
		}
		const float Score = FMath::Acos(FMath::Clamp(Cos, -1.f, 1.f)) + Distance / LockOnRangeCm * 0.25f + (IsEnemyOf(Other) ? 0.f : PlayerLockOnNonEnemyPenalty);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

void AAedirPlayerCharacter::UpdateLockOn(float DeltaSeconds)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	// The older guard, or a fist's (or blade's) button held down.
	const bool bOldGuard = Combat && Combat->IsInFightStance();
	const bool bGuard = bOldGuard || (FistArms && FistArms->IsSteering());
	// Down, getting up, or walking after its fall: the view lets go until the feet are back under the body.
	const bool bDown = Balance && (Balance->IsRagdoll() || Balance->IsGettingUp() || Balance->IsStumbling());
	if (!bLockOnEnabled || !bGuard || bDown || !PlayerController || IsMenuOpen())
	{
		LockOnTarget.Reset();
		return;
	}

	AAedirCreature* Target = LockOnTarget.Get();
	// The one locked on to has fallen: the view goes on to whoever is nearest to where it looks, further round than a new lock.
	const bool bKilled = (Target && Target->Stats && Target->Stats->IsDead()) || LockOnTarget.IsStale();
	const bool bLost = !Target || (Target->Stats && Target->Stats->IsDead()) || FVector::Dist(Target->GetActorLocation(), GetActorLocation()) > LockOnRangeCm * 1.4f;
	// Held on someone who is no enemy, the view goes over to an enemy as soon as one comes into it.
	const AAedirCreature* Better = !bLost && !IsEnemyOf(Target) ? FindLockOnTarget() : nullptr;
	if (bLost || (Better && Better != Target && IsEnemyOf(Better)))
	{
		Target = FindLockOnTarget(bKilled ? LockOnSwitchConeDegrees : 0.f);
		LockOnTarget = Target;
		UE_CLOG(Target != nullptr, LogAedir, Log, TEXT("[LockOn] The view locks on %s."), *GetNameSafe(Target));
	}
	if (!Target)
	{
		if (!bOldGuard)
		{
			return;
		}
		// With nobody to look at, the guard still brings the view down over the shoulder.
		FRotator Control = PlayerController->GetControlRotation();
		const float Pitch = FMath::FInterpTo(static_cast<float>(FRotator::NormalizeAxis(Control.Pitch)), FightViewPitch, DeltaSeconds, FightViewPitchSpeed);
		PlayerController->SetControlRotation(FRotator(Pitch, Control.Yaw, Control.Roll));
		return;
	}

	// The view turns to a little under the target's head, or where its body lies, and keeps it there while the guard is held:
	// standing, that is straight across at him, not down at his belly.
	FVector Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	if (const USkeletalMeshComponent* TargetMesh = Target->GetMesh(); TargetMesh && TargetMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
	{
		Aim = TargetMesh->GetBoneLocation(TEXT("head")) - FVector(0.f, 0.f, LockOnAimBelowHeadCm);
	}
	// Measured from the player's own head, not from the camera: the camera rises and sinks with the view's tilt, and
	// measured from it the view would chase itself up or down.
	const FVector Pivot = GetMesh() && GetMesh()->GetBoneIndex(TEXT("head")) != INDEX_NONE ? GetMesh()->GetBoneLocation(TEXT("head")) : GetPawnViewLocation();
	// Close in, the height between the two counts as if seen from LockOnLevelDistanceCm away, or the view would tip down
	// steeply at a foe at arm's length.
	const FVector ToAim = Aim - Pivot;
	const double Across = FMath::Max(ToAim.Size2D(), static_cast<double>(LockOnLevelDistanceCm));
	FRotator Want = ToAim.Rotation();
	Want.Pitch = FMath::RadiansToDegrees(FMath::Atan2(ToAim.Z, Across));
	FRotator Control = PlayerController->GetControlRotation();
	// Down at one lying on the ground, but never steeper than this.
	const float WantPitch = FMath::Clamp(static_cast<float>(FRotator::NormalizeAxis(Want.Pitch)) + LockOnPitchLiftDegrees, LockOnSteepestPitch, LockOnHighestPitch);
	static const IConsoleVariable* LockLog = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.HitLog"));
	UE_CLOG(GFrameCounter % 60 == 0 && LockLog && LockLog->GetInt() != 0, LogAedir, Log, TEXT("[LockOn] Looking at %s: wanted pitch %.1f, the view at %.1f"), *GetNameSafe(Target), WantPitch, static_cast<float>(FRotator::NormalizeAxis(Control.Pitch)));
	const FRotator Turned = FMath::RInterpTo(Control, FRotator(FMath::Clamp(WantPitch, MinViewPitch, MaxViewPitch), Want.Yaw, Control.Roll), DeltaSeconds, LockOnTurnSpeed);
	PlayerController->SetControlRotation(FRotator(Turned.Pitch, Turned.Yaw, Control.Roll));
}

void AAedirPlayerCharacter::ApplyViewPitchLimits()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	PlayerController->PlayerCameraManager->ViewPitchMin = MinViewPitch;
	PlayerController->PlayerCameraManager->ViewPitchMax = MaxViewPitch;
}

void AAedirPlayerCharacter::HandleMouseX(float Value)
{
	// While a guard is held the mouse mostly moves the arm; the camera still follows a few percent of it.
	const bool bFighting = Combat && Combat->IsInFightStance();
	const FAedirGameplaySettings& Settings = UAedirSettingsSubsystem::GetValues();
	const float CameraValue = Value * Settings.MouseSensitivity;
	if (bPhotoCamera)
	{
		FreeCameraTargetRotation.Yaw += CameraValue * FreeCameraDegreesPerInput;
		return;
	}
	if (bOrbitHeld && CameraBoom)
	{
		// The camera alone goes round; the body, its view and the arms are left as they are.
		OrbitOffset.Yaw += CameraValue * PlayerOrbitDegreesPerInput;
		OrbitTurnedDegrees += FMath::Abs(CameraValue * PlayerOrbitDegreesPerInput);
		bOrbitMoved |= OrbitTurnedDegrees > ViewTapMaxTurnDegrees;
		return;
	}
	// While a fist's button is down the mouse works the fists; the view only follows a small share of it.
	if (FistArms && FistArms->IsSteering())
	{
		PendingMouseDelta.X += Value * Settings.WeaponSensitivity;
		AddControllerYawInput(CameraValue * FistArms->CameraShare);
		return;
	}
	// Steering a weapon the view is locked as in Half Sword: the mouse moves the weapon alone, and turns the view only once
	// the weapon is at the end of its travel that way.
	const bool bSteering = Combat && Combat->IsSteeringWeapon();
	AddControllerYawInput(bSteering ? (Combat->IsWeaponAimAtEdge(Value) ? CameraValue : 0.f) : (bFighting ? CameraValue * FightCameraMouseScale : CameraValue));
	if (!FMath::IsNearlyZero(Value))
	{
		LastLookInputSeconds = 0.f;
	}

	PendingMouseDelta.X += Value * Settings.WeaponSensitivity;
}

void AAedirPlayerCharacter::HandleMouseY(float Value)
{
	const bool bFighting = Combat && Combat->IsInFightStance();
	const FAedirGameplaySettings& Settings = UAedirSettingsSubsystem::GetValues();
	const float CameraValue = (Settings.bInvertMouseY ? Value : -Value) * Settings.MouseSensitivity;
	if (bPhotoCamera)
	{
		FreeCameraTargetRotation.Pitch = FMath::Clamp(FreeCameraTargetRotation.Pitch - CameraValue * FreeCameraDegreesPerInput, -89.0, 89.0);
		return;
	}
	if (bOrbitHeld && CameraBoom)
	{
		OrbitOffset.Pitch = FMath::Clamp(OrbitOffset.Pitch - CameraValue * PlayerOrbitDegreesPerInput, -70.0, 70.0);
		OrbitTurnedDegrees += FMath::Abs(CameraValue * PlayerOrbitDegreesPerInput);
		bOrbitMoved |= OrbitTurnedDegrees > ViewTapMaxTurnDegrees;
		return;
	}
	if (FistArms && FistArms->IsSteering())
	{
		PendingMouseDelta.Y += Value * Settings.WeaponSensitivity;
		AddControllerPitchInput(CameraValue * FistArms->CameraShare);
		return;
	}
	AddControllerPitchInput(Combat && Combat->IsSteeringWeapon() ? 0.f : (bFighting ? CameraValue * FightCameraMouseScale : CameraValue));
	if (!FMath::IsNearlyZero(Value))
	{
		LastLookInputSeconds = 0.f;
	}

	PendingMouseDelta.Y += Value * Settings.WeaponSensitivity;
}

void AAedirPlayerCharacter::HandleMouseWheel(float Value)
{
	if (bPhotoCamera)
	{
		FreeCameraSpeedCm = FMath::Clamp(FreeCameraSpeedCm * FMath::Pow(FreeCameraWheelScale, Value), FreeCameraMinSpeedCm, FreeCameraMaxSpeedCm);
		return;
	}
	if (!Combat || Value == 0.f)
	{
		return;
	}

	// A raised fist takes the wheel to set its reach; everything else zooms the camera.
	bool bFistRaised = false;
	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		if (Combat->IsGuardActive(Hand) && !(Interaction && Interaction->GetHeldItem(Hand)))
		{
			Combat->AddExtensionInput(Hand, Value);
			bFistRaised = true;
		}
	}

	if (!bFistRaised)
	{
		CameraZoomTargetCm = FMath::Clamp(CameraZoomTargetCm - Value * CameraZoomStepCm, CameraZoomMinCm, CameraZoomMaxCm);
	}
}

void AAedirPlayerCharacter::HandleRunPressed()
{
	SetWantsToRun(true);
}

void AAedirPlayerCharacter::HandleRunReleased()
{
	SetWantsToRun(false);
}

void AAedirPlayerCharacter::HandleCrouchPressed()
{
	if (bNoclip)
	{
		return;
	}
	SetWantsToCrouch(true);
}

void AAedirPlayerCharacter::HandleCrouchReleased()
{
	SetWantsToCrouch(false);
}

void AAedirPlayerCharacter::HandleJumpPressed()
{
	if (bNoclip)
	{
		return;
	}
	StartJump();
}

void AAedirPlayerCharacter::HandleJumpReleased()
{
	StopJump();
}

void AAedirPlayerCharacter::HandleLeftGuardPressed()
{
	// With a weapon held in both hands the right button means two things: clicked, the hand slides up onto the blade or
	// back; held down, the left hand lets go and is raised on its own. Anything else raises the left guard at once.
	bRightButtonDown = true;
	RightButtonHeldSeconds = 0.f;
	bRightButtonFreedHand = false;
	// A blade in the right hand with the left empty: whether the button is clicked or held down is known only when it comes
	// up or has been down a moment (UpdateRightButtonHold).
	if (UsesFistArms(EAedirHand::Left) && FistArms->CanTakeBothHands())
	{
		return;
	}
	if (UsesFistArms(EAedirHand::Left) && !(Combat && Combat->IsTwoHandGrip()))
	{
		FistArms->SetHeld(EAedirHand::Left, true, static_cast<float>(FRotator::NormalizeAxis(GetControlRotation().Pitch)));
		bRightButtonFreedHand = true;
		return;
	}
	if (Combat && !Combat->IsTwoHandGrip())
	{
		Combat->SetGuard(EAedirHand::Left, true);
		bRightButtonFreedHand = true;
	}
}

void AAedirPlayerCharacter::HandleLeftGuardReleased()
{
	bRightButtonDown = false;
	if (FistArms && FistArms->IsButtonHeld(EAedirHand::Left))
	{
		// Let go, a left hand that came off the grip goes back on it.
		FistArms->SetHeld(EAedirHand::Left, false);
		FistArms->SetOffHandOut(false);
		return;
	}
	// Clicked with a blade in the right hand: it is taken in both hands, or back in one.
	if (UsesFistArms(EAedirHand::Left) && FistArms->CanTakeBothHands() && !bRightButtonFreedHand)
	{
		FistArms->SetTwoHand(!FistArms->IsTwoHand());
		return;
	}
	if (!Combat)
	{
		return;
	}
	if (!bRightButtonFreedHand)
	{
		Combat->ToggleHalfSword();
		return;
	}
	Combat->SetGuard(EAedirHand::Left, false);
	Combat->SetOffHandFree(false);
}

void AAedirPlayerCharacter::UpdateRightButtonHold(float DeltaSeconds)
{
	if (!bRightButtonDown || bRightButtonFreedHand)
	{
		return;
	}
	RightButtonHeldSeconds += DeltaSeconds;
	// Held down with a blade in the right hand, the left hand comes off its grip, or up from the side, as a fist.
	if (UsesFistArms(EAedirHand::Left) && FistArms->CanTakeBothHands())
	{
		if (RightButtonHeldSeconds >= RightButtonHoldSeconds)
		{
			bRightButtonFreedHand = true;
			FistArms->SetOffHandOut(true);
			FistArms->SetHeld(EAedirHand::Left, true, static_cast<float>(FRotator::NormalizeAxis(GetControlRotation().Pitch)));
		}
		return;
	}
	if (!Combat)
	{
		return;
	}
	if (RightButtonHeldSeconds >= RightButtonHoldSeconds)
	{
		bRightButtonFreedHand = true;
		Combat->SetOffHandFree(true);
		Combat->SetGuard(EAedirHand::Left, true);
	}
}

bool AAedirPlayerCharacter::UsesFistArms(EAedirHand Hand) const
{
	// Empty hands and hands with a knife or a one-handed sword are moved by the fists; anything else by the older code.
	const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
	// Something that is not a weapon (a torch, a cup, a stone) is held in the fist, and the arm moves as a fist does.
	const bool bObject = Item && Item->ItemClass != EAedirItemClass::Weapon && !Item->IsShield() && !(Combat && Combat->IsTwoHandGrip());
	return FistArms && CVarAedirNewFists.GetValueOnGameThread() != 0 && (!Item || bObject || UAedirFistArms::IsOneHandBlade(Item));
}

void AAedirPlayerCharacter::HandleRightGuardPressed()
{
	if (UsesFistArms(EAedirHand::Right))
	{
		FistArms->SetHeld(EAedirHand::Right, true, static_cast<float>(FRotator::NormalizeAxis(GetControlRotation().Pitch)));
		return;
	}
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Right, true);
	}
}

void AAedirPlayerCharacter::HandleRightGuardReleased()
{
	if (FistArms && FistArms->IsButtonHeld(EAedirHand::Right))
	{
		FistArms->SetHeld(EAedirHand::Right, false);
		return;
	}
	if (Combat)
	{
		Combat->SetGuard(EAedirHand::Right, false);
	}
	// Letting go of the guard goes back to swinging.
	EndLatchedThrust();
}

void AAedirPlayerCharacter::UpdateSwingInput(float DeltaSeconds)
{
	UpdateRightButtonHold(DeltaSeconds);
	if (CameraBoom)
	{
		// The fight stance draws the view in close and up over the shoulder; out of it the view goes back to where it was set.
		const float FightAlpha = FMath::SmoothStep(0.f, 1.f, FightCameraAlpha);
		// Into a fight, with a weapon or the fists up, the view draws back a little.
		const bool bFistsUp = FistArms && FMath::Max(FistArms->GetRaised(EAedirHand::Left), FistArms->GetRaised(EAedirHand::Right)) > 0.5f;
		const bool bInFight = (Combat && Combat->IsInFightStance()) || bFistsUp;
		FightPullBackAlpha = FMath::FInterpTo(FightPullBackAlpha, bInFight ? 1.f : 0.f, DeltaSeconds, FightCameraPullBackSpeed);
		const float PullBack = FightCameraPullBackCm * FMath::SmoothStep(0.f, 1.f, FightPullBackAlpha);
		const float WantedLength = bPhotoCamera ? PhotoArmLengthCm : (bHeadCamera ? HeadCameraArmLengthCm : FMath::Lerp(CameraZoomTargetCm, FightArmLengthCm, FightAlpha) + PullBack);
		CameraBoom->TargetArmLength = bPhotoCamera ? WantedLength : FMath::FInterpTo(CameraBoom->TargetArmLength, WantedLength, DeltaSeconds, 10.f);
		CameraBoom->SocketOffset = bHeadCamera ? HeadCameraOffset : FMath::Lerp(ThirdPersonShoulderOffset, FightShoulderOffset, FightAlpha);

		// Let go of V, the camera comes back round behind the body.
		if (bOrbitHeld)
		{
			OrbitHeldSeconds += DeltaSeconds;
		}
		else
		{
			OrbitOffset = FMath::RInterpTo(OrbitOffset, FRotator::ZeroRotator, DeltaSeconds, OrbitReturnSpeed);
		}
		// Turned round, the arm is set by hand where the view plus the turn has it; back behind, it follows the view again.
		const bool bOrbiting = bOrbitHeld || !OrbitOffset.IsNearlyZero(0.5);
		CameraBoom->bUsePawnControlRotation = !bOrbiting;
		if (bOrbiting)
		{
			const FRotator View = GetControlRotation();
			CameraBoom->SetWorldRotation(FRotator(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch + OrbitOffset.Pitch), -80.0, 80.0), View.Yaw + OrbitOffset.Yaw, 0.0));
		}
	}

	LastLookInputSeconds += DeltaSeconds;
	if (CameraBoom && Gait && GetCharacterMovement())
	{
		// The capsule changes size at once when crouching; the boom pivot is eased so the camera sinks with the pelvis.
		const float Drop = FMath::Max(GetDefaultHalfHeight() - GetCharacterMovement()->GetCrouchedHalfHeight(), 0.f);
		const float CapsuleDrop = bIsCrouched ? Drop : 0.f;
		FVector TargetRelative(0.f, 0.f, CameraBoomBaseZ + CapsuleDrop - Drop * Gait->GetCrouchAlpha());
		float FollowSpeed = CameraBodyFollowSpeed;

		if (USkeletalMeshComponent* BodyMesh = GetMesh())
		{
			const FName PelvisName = Balance ? Balance->PelvisBoneName : FName(TEXT("pelvis"));
			const FVector PelvisWorld = BodyMesh->GetBoneLocation(PelvisName, EBoneSpaces::WorldSpace);
			if (Balance && Balance->IsRagdoll())
			{
				// A fallen or dead body keeps the camera: the pivot follows the pelvis wherever the ragdoll goes.
				TargetRelative = GetActorTransform().InverseTransformPositionNoScale(PelvisWorld + FVector(0.f, 0.f, CameraRagdollRaiseCm));
				FollowSpeed = CameraRagdollFollowSpeed;
				// Down on the ground the view comes round to look along the body, unless the mouse is turning it: face down
				// from behind its feet towards its head, on its back from behind its head towards its feet, so the key
				// that gets it up, the one towards the feet, is S face down and W on the back. Getting up it stays where it is,
				// for the way of that key not to turn under the player as the body rises.
				const FVector HeadWorld = BodyMesh->GetBoneLocation(TEXT("head"), EBoneSpaces::WorldSpace);
				const FVector Along = (HeadWorld - PelvisWorld).GetSafeNormal2D() * (Balance->GetLyingStyle() == EAedirGetUpStyle::FromBack ? -1.f : 1.f);
				if (Balance->IsRagdoll() && !Balance->IsHoisting() && !Along.IsNearlyZero() && GetController() && LastLookInputSeconds > CameraRagdollFollowAfterSeconds)
				{
					const FRotator Current = GetControlRotation();
					const FRotator Wanted(CameraRagdollPitch, Along.Rotation().Yaw, 0.f);
					GetController()->SetControlRotation(FMath::RInterpTo(Current, Wanted, DeltaSeconds, CameraRagdollTurnSpeed));
				}
			}
			else if (bHeadCamera && BodyMesh->GetBoneIndex(TEXT("head")) != INDEX_NONE)
			{
				// As in Half Sword the camera rides with the head: every lean, duck and stagger of the body moves the view.
				TargetRelative = GetActorTransform().InverseTransformPositionNoScale(BodyMesh->GetBoneLocation(TEXT("head"), EBoneSpaces::WorldSpace));
				FollowSpeed = HeadCameraFollowSpeed;
			}
			else
			{
				// The pivot follows the pelvis height with a slight delay, so steps, jumps and landings reach the camera.
				// Horizontally too: sway, lean and step shifts of the pelvis move the pivot a little.
				const FVector PelvisLocal = GetActorTransform().InverseTransformPositionNoScale(PelvisWorld);
				CameraPelvisBaseline = bCameraStateInitialized ? FMath::VInterpTo(CameraPelvisBaseline, PelvisLocal, DeltaSeconds, 0.6f) : PelvisLocal;
				TargetRelative += (PelvisLocal - CameraPelvisBaseline) * CameraPelvisInfluence * UAedirSettingsSubsystem::GetValues().CameraBodyMotion;
			}
		}

		CameraRelative = bCameraStateInitialized ? FMath::VInterpTo(CameraRelative, TargetRelative, DeltaSeconds, FollowSpeed) : TargetRelative;
		bCameraStateInitialized = true;
		CameraBoom->SetRelativeLocation(CameraRelative);
	}

	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(UAedirSettingsSubsystem::GetValues().FieldOfView);

		// The fight camera is locked in aim but never dead still: breathing drift plus a lean into the mouse movement.
		const bool bFighting = Combat && Combat->IsInFightStance();
		FightCameraAlpha = FMath::FInterpTo(FightCameraAlpha, bFighting ? 1.f : 0.f, DeltaSeconds, 4.f);
		const float Time = GetWorld()->GetTimeSeconds();
		const FVector2D MouseLean = FVector2D(PendingMouseDelta.X, PendingMouseDelta.Y).GetClampedToMaxSize(30.f) * 0.03f;
		const FRotator Sway(
			(FMath::Sin(Time * 1.3f) * 0.12f - MouseLean.Y * 0.4f) * FightCameraAlpha,
			(FMath::Sin(Time * 0.9f + 1.f) * 0.15f + MouseLean.X * 0.2f) * FightCameraAlpha,
			(FMath::Sin(Time * 1.1f + 2.f) * 0.15f + MouseLean.X * 0.4f) * FightCameraAlpha);
		FightCameraSway = FMath::RInterpTo(FightCameraSway, Sway, DeltaSeconds, 6.f);
		// A blow taken jolts the view a little, quickly and fading, never so much that the fight is lost from sight.
		HitShake = FMath::Max(HitShake - DeltaSeconds / FMath::Max(HitShakeSeconds, 0.05f), 0.f);
		const float ShakeDegrees = HitShakeStrength * HitShake * HitShake * UAedirSettingsSubsystem::GetValues().CameraShake;
		const FRotator Shake(FMath::PerlinNoise1D(Time * 31.f) * ShakeDegrees, FMath::PerlinNoise1D(Time * 27.f + 7.f) * ShakeDegrees, FMath::PerlinNoise1D(Time * 23.f + 13.f) * ShakeDegrees * 0.6f);
		FollowCamera->SetRelativeRotation(FightCameraSway + Shake);
	}

	if (FistArms && FistArms->IsSteering())
	{
		FistArms->AddMouse(PendingMouseDelta);
	}
	if (Combat)
	{
		for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
		{
			if (Combat->IsGuardActive(Hand))
			{
				Combat->AddSwingInput(Hand, PendingMouseDelta, DeltaSeconds);
			}
		}
	}

	PendingMouseDelta = FVector2D::ZeroVector;
}

void AAedirPlayerCharacter::HandleLeftHandPressed()
{
	// Q and E take the free camera down and up while it is out.
	if (bPhotoCamera)
	{
		return;
	}
	HandleHandPressed(EAedirHand::Left);
}

void AAedirPlayerCharacter::HandleLeftHandReleased()
{
	HandleHandReleased(EAedirHand::Left);
}

void AAedirPlayerCharacter::HandleRightHandPressed()
{
	// Q and E take the free camera down and up while it is out.
	if (bPhotoCamera)
	{
		return;
	}
	HandleHandPressed(EAedirHand::Right);
}

void AAedirPlayerCharacter::HandleRightHandReleased()
{
	HandleHandReleased(EAedirHand::Right);
}

void AAedirPlayerCharacter::HandleLeftHolsterPressed()
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::HandleRightHolsterPressed()
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(EAedirHand::Right);
	}
}

void AAedirPlayerCharacter::SendGuestCharacter()
{
	UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr;
	if (!Saves || !Saves->IsGuest() || HasAuthority())
	{
		return;
	}

	// Controllers arrive a moment after the body on a guest's machine.
	FTimerHandle BringTimer;
	GetWorldTimerManager().SetTimer(BringTimer, FTimerDelegate::CreateWeakLambda(this, [this, Saves]()
	{
		if (!IsLocallyControlled())
		{
			return;
		}
		FAedirCreatureSave Character;
		bool bFound = false;
		Saves->GetCharacterOfSlot(Saves->GetCurrentSlot(), Character, bFound);
		if (bFound)
		{
			ServerBringCharacter(Character);
		}
		GetWorldTimerManager().SetTimer(GuestStoreTimer, this, &AAedirPlayerCharacter::StoreGuestCharacterNow, AedirGuestStoreSeconds, true);
	}), 1.f, false);
}

void AAedirPlayerCharacter::StoreGuestCharacterNow()
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr)
	{
		Saves->StoreGuestCharacter(Saves->GetCurrentSlot(), Cast<APlayerController>(GetController()));
	}
}

void AAedirPlayerCharacter::ServerBringCharacter_Implementation(const FAedirCreatureSave& Character)
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>() : nullptr)
	{
		Saves->ApplyCharacter(this, Character);
	}
}

AAedirCreature* AAedirPlayerCharacter::FindLookTarget(float MaxDistance) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		return nullptr;
	}

	FVector Eye;
	FRotator View;
	PlayerController->GetPlayerViewPoint(Eye, View);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirLookTarget), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	// The camera sits behind the shoulder, so the distance counts from the body.
	const float CameraOffset = static_cast<float>(FVector::Dist(Eye, GetActorLocation()));
	if (!World->SweepSingleByObjectType(Hit, Eye, Eye + View.Vector() * (MaxDistance + CameraOffset), FQuat::Identity, Objects, FCollisionShape::MakeSphere(22.f), Params))
	{
		return nullptr;
	}

	AAedirCreature* Target = Cast<AAedirCreature>(Hit.GetActor());
	return Target && Target->Stats && !Target->Stats->IsDead() ? Target : nullptr;
}

void AAedirPlayerCharacter::HandleTalkPressed()
{
	if (IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// A follower is waiting to be shown where to go: this press points it out.
	if (AAedirNpc* Pointing = PointingNpc.Get())
	{
		PointingNpc.Reset();
		FVector Where;
		AAedirItemActor* Item = nullptr;
		if (FindPointTarget(Where, Item))
		{
			ServerSendNpc(Pointing, Where, Item);
		}
		return;
	}

	AAedirNpc* Npc = Cast<AAedirNpc>(FindLookTarget(AedirTalkRangeCm));
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* Local = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UAedirMenuSubsystem* Menus = Local ? Local->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
	if (Npc && Menus && Npc->CanTalkTo(this))
	{
		Menus->OpenDialogue(Npc);
	}
}

bool AAedirPlayerCharacter::FindPointTarget(FVector& OutWhere, AAedirItemActor*& OutItem) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		return false;
	}

	FVector Eye;
	FRotator View;
	PlayerController->GetPlayerViewPoint(Eye, View);
	const FVector Direction = View.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirPointTarget), false, this);
	Params.AddIgnoredActor(PointingNpc.Get());
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Direction * AedirPointRangeCm, ECC_Visibility, Params);
	const double Limit = bHit ? Hit.Distance + AedirPointBehindCm : AedirPointRangeCm;

	// The item closest to the line of sight, and not behind whatever the line ends on. Big objects are nothing to go and fetch.
	OutItem = nullptr;
	double BestOff = UE_BIG_NUMBER;
	for (TActorIterator<AAedirItemActor> It(World); It; ++It)
	{
		const FVector ToItem = It->GetBoundsCenterWorld() - Eye;
		const double Along = FVector::DotProduct(ToItem, Direction);
		const double Off = (ToItem - Direction * Along).Size();
		if (Along > 0.0 && Along <= Limit && Off < AedirPointItemWidthCm + Along * AedirPointItemWidthShare && Off < BestOff && AAedirItemActor::IsPickable(*It) && It->ItemClass != EAedirItemClass::Object)
		{
			OutItem = *It;
			BestOff = Off;
		}
	}
	if (OutItem)
	{
		OutWhere = OutItem->GetBoundsCenterWorld();
		return true;
	}

	OutWhere = Hit.ImpactPoint;
	return bHit;
}

void AAedirPlayerCharacter::ServerSendNpc_Implementation(AAedirNpc* Npc, const FVector& Where, AAedirItemActor* Item)
{
	if (!Npc)
	{
		return;
	}
	if (Item)
	{
		Npc->OrderFetch(this, Item);
	}
	else
	{
		Npc->OrderGoTo(this, Where);
	}
}

void AAedirPlayerCharacter::RequestNpcAction(AAedirNpc* Npc, EAedirNpcAction Action)
{
	// Showing the way happens on this screen, the order goes out once a place is picked.
	if (Action == EAedirNpcAction::GoThere)
	{
		PointingNpc = Npc;
		return;
	}

	if (HasAuthority())
	{
		if (Npc)
		{
			Npc->PerformAction(this, Action);
		}
	}
	else
	{
		ServerNpcAction(Npc, Action);
	}
}

void AAedirPlayerCharacter::ServerNpcAction_Implementation(AAedirNpc* Npc, EAedirNpcAction Action)
{
	if (Npc && FVector::Dist(Npc->GetActorLocation(), GetActorLocation()) < AedirTalkRangeCm * 2.f)
	{
		Npc->PerformAction(this, Action);
	}
}

void AAedirPlayerCharacter::HandleShovePressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	Combat->StartShove();
}

void AAedirPlayerCharacter::HandleThrustPressed()
{
	if (Combat)
	{
		bThrustWasOn = bThrustLatched;
		ThrustPressedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Combat->SetThrustMode(true);
	}
}

void AAedirPlayerCharacter::HandleThrustReleased()
{
	if (!Combat)
	{
		return;
	}
	// A short tap turns the thrust on and leaves it, or off if it was on; a held key lets it go with the key.
	const bool bTap = GetWorld() && GetWorld()->GetTimeSeconds() - ThrustPressedAt < ThrustTapSeconds;
	bThrustLatched = bTap && !bThrustWasOn;
	Combat->SetThrustMode(bThrustLatched);
}

void AAedirPlayerCharacter::EndLatchedThrust()
{
	if (bThrustLatched && Combat)
	{
		bThrustLatched = false;
		Combat->SetThrustMode(false);
	}
}

void AAedirPlayerCharacter::HandleTurnWeaponPressed()
{
	if (!Interaction || !Combat)
	{
		return;
	}
	AAedirItemActor* Item = Interaction->GetHeldItem(EAedirHand::Right);
	if (!Item)
	{
		Item = Interaction->GetHeldItem(EAedirHand::Left);
	}
	if (Item)
	{
		const AAedirItemActor::EGripTurn Turn = Item->TurnInHand(this, Combat->IsHalfSwording());
		static const TCHAR* const Names[] = { TEXT("as made"), TEXT("turned on its haft"), TEXT("reversed"), TEXT("by the blade") };
		UE_LOG(LogAedir, Log, TEXT("[Grip] %s held %s"), *Item->GetName(), Names[static_cast<int32>(Turn)]);
	}
}

void AAedirPlayerCharacter::TurnWeapon()
{
	HandleTurnWeaponPressed();
}

void AAedirPlayerCharacter::HandleDynamicKickPressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// The kick stays where the body stands: the leg lifts slowly, the knee comes up to the belly and the foot snaps out.
	Combat->StartKick();
}

void AAedirPlayerCharacter::HandleChangeHandsPressed()
{
	if (!Combat || IsMenuOpen() || IsCharacterCreationOpen())
	{
		return;
	}

	// X only ever changes hands; the kick is on C. With a weapon held in both hands the upper hand and the lower hand change
	// places, and with one weapon and a free hand it is passed over.
	if (!Interaction)
	{
		return;
	}
	const AAedirItemActor* LeftItem = Interaction->GetHeldItem(EAedirHand::Left);
	if (LeftItem && LeftItem == Interaction->GetHeldItem(EAedirHand::Right))
	{
		Interaction->SwapGripOrder();
		return;
	}
	Interaction->PassToOtherHand();
}

void AAedirPlayerCharacter::HandleHandPressed(EAedirHand Hand)
{
	if (!Interaction)
	{
		return;
	}

	// A tap takes hold of what is in reach or lets go of what is held; holding the key down puts the held thing on the
	// belt, or with the hand empty takes the next one from it. Which it was is known once the key comes up or has been
	// held long enough.
	const int32 Index = static_cast<int32>(Hand);
	const EAedirHandState State = Interaction->GetHandState(Hand);
	if (State == EAedirHandState::Idle || State == EAedirHandState::Holding)
	{
		bHandKeyTracked[Index] = true;
		bHandUseTriggered[Index] = false;
		HandKeyHeldSeconds[Index] = 0.f;
	}
}

void AAedirPlayerCharacter::HandleHandReleased(EAedirHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	if (!bHandKeyTracked[Index])
	{
		return;
	}

	if (Interaction && Interaction->IsMouthHold(Hand))
	{
		Interaction->SetMouthHold(Hand, false);
	}

	const bool bHolstered = bHandUseTriggered[Index];

	bHandKeyTracked[Index] = false;
	bHandUseTriggered[Index] = false;
	HandKeyHeldSeconds[Index] = 0.f;

	if (!Interaction || bHolstered)
	{
		return;
	}
	switch (Interaction->GetHandState(Hand))
	{
	case EAedirHandState::Idle:
		Interaction->StartReach(Hand);
		break;
	case EAedirHandState::Holding:
		Interaction->DropHeld(Hand);
		break;
	default:
		break;
	}
}

void AAedirPlayerCharacter::UpdateHandUseHold(float DeltaSeconds)
{
	if (!Interaction)
	{
		return;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!bHandKeyTracked[Index])
		{
			continue;
		}

		HandKeyHeldSeconds[Index] += DeltaSeconds;
		if (bHandUseTriggered[Index] || HandKeyHeldSeconds[Index] < Interaction->UseHoldTime)
		{
			continue;
		}

		const EAedirHand Hand = static_cast<EAedirHand>(Index);
		// Held down on a dead body: the armour where the hand holds it comes off into the hand.
		if (Interaction->IsGrabbingBody(Hand))
		{
			Interaction->StripGrabbedArmor(Hand);
			bHandUseTriggered[Index] = true;
			continue;
		}
		const EAedirHandState State = Interaction->GetHandState(Hand);
		if (State != EAedirHandState::Holding && State != EAedirHandState::Idle)
		{
			continue;
		}
		// Held down: to the belt, or from it.
		Interaction->HolsterToWaist(Hand);
		bHandUseTriggered[Index] = true;
	}
}

void AAedirPlayerCharacter::HandleUsePressed(EAedirHand Hand)
{
	// Using what the hand holds: food and drink go to the mouth while the key is down, anything else is used at once.
	const AAedirItemActor* HeldItem = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
	if (!HeldItem || Interaction->GetHandState(Hand) != EAedirHandState::Holding)
	{
		return;
	}
	if (HeldItem->IsConsumable())
	{
		Interaction->SetMouthHold(Hand, true);
	}
	else if (HeldItem->IsUsable())
	{
		Interaction->UseHeld(Hand);
	}
}

void AAedirPlayerCharacter::HandleUseReleased(EAedirHand Hand)
{
	if (Interaction && Interaction->IsMouthHold(Hand))
	{
		Interaction->SetMouthHold(Hand, false);
	}
}

void AAedirPlayerCharacter::HandleViewTogglePressed()
{
	bOrbitHeld = true;
	bOrbitMoved = false;
	OrbitHeldSeconds = 0.f;
	OrbitTurnedDegrees = 0.f;
}

void AAedirPlayerCharacter::HandleViewToggleReleased()
{
	// Held, V takes the camera round the body; let go, it comes back. A tap the camera turned a little for is undone.
	if (bOrbitHeld && !bOrbitMoved && OrbitHeldSeconds < ViewTapSeconds)
	{
		OrbitOffset = FRotator::ZeroRotator;
	}
	bOrbitHeld = false;
}

void AAedirPlayerCharacter::HandleHeadCameraPressed()
{
	// The photo camera, as Half Sword's C: the camera leaves the body and flies free from where the view was.
	bPhotoCamera = !bPhotoCamera;
	// Only playing alone may time stop; with others in the world it would stop it for them too. Loose bodies are held to
	// what they were doing, or they would come out of it flung into the air.
	if (GetNetMode() == NM_Standalone)
	{
		for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
		{
			if (It->Balance)
			{
				It->Balance->HoldBodiesStill(bPhotoCamera);
			}
		}
		UGameplayStatics::SetGlobalTimeDilation(this, bPhotoCamera ? PhotoTimeDilation : 1.f);
		// Time all but stopped is not stopped for the solver: it still pushes bodies apart and out of the ground every frame,
		// and limbs would creep open for as long as the camera is out. The physics is held still altogether instead.
		GetWorld()->bShouldSimulatePhysics = !bPhotoCamera;
		// The hair's own simulation is let go of while the camera is out: stepped by time all but stopped, it pulled the
		// strands out long, and only paused, the strands still shimmered from one frame to the next. The hair is drawn as it
		// is styled. Let go again, it starts afresh from how the hair hangs: carried on from where it was held, its first
		// step took up all the head had moved meanwhile at once and flung the strands out long for a moment.
		// Hair gone with a head cut off lies still on it.
		if (Hair && Hair->GetAttachParent() == Face)
		{
			Hair->SetEnableSimulation(!bPhotoCamera);
			if (!bPhotoCamera)
			{
				Hair->ResetSimulation();
			}
		}
	}
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (bPhotoCamera)
	{
		const APlayerCameraManager* View = PlayerController ? PlayerController->PlayerCameraManager.Get() : nullptr;
		FreeCameraLocation = View ? View->GetCameraLocation() : GetActorLocation();
		FreeCameraRotation = View ? View->GetCameraRotation() : GetActorRotation();
		FreeCameraRotation.Roll = 0.f;
		FreeCameraTargetRotation = FreeCameraRotation;
	}
	if (FollowCamera)
	{
		FollowCamera->SetUsingAbsoluteLocation(bPhotoCamera);
		FollowCamera->SetUsingAbsoluteRotation(bPhotoCamera);
		if (bPhotoCamera)
		{
			FollowCamera->SetWorldLocationAndRotation(FreeCameraLocation, FreeCameraRotation);
		}
		else
		{
			FollowCamera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		}
	}
	if (UAedirTravelSubsystem* Travel = PlayerController && PlayerController->GetLocalPlayer() ? PlayerController->GetLocalPlayer()->GetSubsystem<UAedirTravelSubsystem>() : nullptr)
	{
		Travel->ShowNotification(bPhotoCamera ? NSLOCTEXT("Aedir", "PhotoCameraOn", "Free camera: mouse to look, WASD to move, E up, Q down, Shift faster, wheel for speed, Z to go back") : NSLOCTEXT("Aedir", "PhotoCameraOff", "Free camera off"), 2.f);
	}
}

void AAedirPlayerCharacter::UpdatePhotoCamera()
{
	if (!bPhotoCamera || !FollowCamera)
	{
		return;
	}
	// Time may all but stand still, so the camera moves by the real time of the frame.
	const float RealSeconds = static_cast<float>(FApp::GetDeltaTime());
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	// The dev AutoMove flies it the same way, for tests.
	FVector Move(DevAutoMove.Y, DevAutoMove.X, 0.0);
	if (PlayerController && !IsMenuOpen())
	{
		const auto Key = [PlayerController](const FKey& Each) { return PlayerController->IsInputKeyDown(Each) ? 1.f : 0.f; };
		Move += FVector(Key(EKeys::W) - Key(EKeys::S), Key(EKeys::D) - Key(EKeys::A), Key(EKeys::E) - Key(EKeys::Q));
		Move *= Key(EKeys::LeftShift) > 0.f ? FreeCameraFastScale : 1.f;
	}
	// The view turns after the mouse over a moment, not at once. Yaw goes the short way round as the target may be far past 180.
	FRotator Turn = FreeCameraTargetRotation - FreeCameraRotation;
	Turn.Normalize();
	FreeCameraRotation += Turn * FMath::Min(FreeCameraTurnFollow * RealSeconds, 1.f);
	const FRotationMatrix Look(FreeCameraRotation);
	FreeCameraLocation += (Look.GetUnitAxis(EAxis::X) * Move.X + Look.GetUnitAxis(EAxis::Y) * Move.Y + FVector::UpVector * Move.Z) * FreeCameraSpeedCm * RealSeconds;
	FollowCamera->SetWorldLocationAndRotation(FreeCameraLocation, FreeCameraRotation);
}

void AAedirPlayerCharacter::HandleMenuTogglePressed()
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->ToggleMenu();
	}
}

void AAedirPlayerCharacter::HandleJournalPressed()
{
	OpenMenuCategory(4);
}

void AAedirPlayerCharacter::HandleMapPressed()
{
	OpenMenuCategory(3);
}

void AAedirPlayerCharacter::HandleSkillsPressed()
{
	OpenMenuCategory(2);
}

void AAedirPlayerCharacter::HandlePausePressed()
{
	if (PointingNpc.IsValid())
	{
		PointingNpc.Reset();
		return;
	}

	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->TogglePauseMenu();
	}
}

void AAedirPlayerCharacter::OpenMenuCategory(int32 Category)
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->OpenMenu(Category);
	}
}

UAedirMenuSubsystem* AAedirPlayerCharacter::GetMenuSubsystem()
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
}

bool AAedirPlayerCharacter::IsMenuOpen()
{
	const UAedirMenuSubsystem* Menu = GetMenuSubsystem();
	return Menu && Menu->IsMenuOpen();
}

bool AAedirPlayerCharacter::IsCharacterCreationOpen()
{
	const UAedirMenuSubsystem* Menu = GetMenuSubsystem();
	return Menu && Menu->IsCharacterCreationOpen();
}

void AAedirPlayerCharacter::MaybeOpenCharacterCreation()
{
	const AAedirGameMode* GameModeDefaults = GetDefault<AAedirGameMode>();
	if (GameModeDefaults && GameModeDefaults->bShowCharacterCreationOnStart && IsLocallyControlled())
	{
		CharacterCreation();
	}
}

void AAedirPlayerCharacter::ServerChooseRace_Implementation(FName ChosenRaceId)
{
	AAedirCreature::SetRace(ChosenRaceId);
	EnsureStarterQuest();
}

void AAedirPlayerCharacter::CharacterCreation()
{
	if (UAedirMenuSubsystem* Menu = GetMenuSubsystem())
	{
		Menu->OpenCharacterCreation();
	}
}

void AAedirPlayerCharacter::EnsureStarterQuest()
{
	UWorld* World = GetWorld();
	UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	if (!Journal || PlayerFindQuest(Journal, PlayerTutorialQuestId()))
	{
		return;
	}

	Journal->AddQuest(PlayerBuildAwakeningQuest());
}

void AAedirPlayerCharacter::UpdateTutorialObjectives()
{
	UWorld* World = GetWorld();
	UAedirJournalSubsystem* Journal = World ? World->GetSubsystem<UAedirJournalSubsystem>() : nullptr;
	const FAedirQuest* Quest = Journal ? PlayerFindQuest(Journal, PlayerTutorialQuestId()) : nullptr;
	if (!Quest || !Quest->bActive || Quest->bCompleted)
	{
		return;
	}

	if (!bTutorialRotationCaptured)
	{
		TutorialStartRotation = GetControlRotation();
		bTutorialRotationCaptured = true;
	}

	const FRotator Turn = (GetControlRotation() - TutorialStartRotation).GetNormalized();
	const bool bLookedAround = FMath::Abs(Turn.Yaw) > PlayerTutorialTurnDegrees || FMath::Abs(Turn.Pitch) > PlayerTutorialTurnDegrees;
	const bool bHoldingItem = Interaction && (IsValid(Interaction->GetHeldItem(EAedirHand::Left)) || IsValid(Interaction->GetHeldItem(EAedirHand::Right)));
	const bool bInFightStance = Combat && Combat->IsInFightStance();
	const bool bMenuOpened = IsMenuOpen();

	// Indices match the objective order in PlayerBuildAwakeningQuest.
	const bool bObjectiveMet[] = { bLookedAround, bHoldingItem, bInFightStance, bMenuOpened };
	const FName QuestId = PlayerTutorialQuestId();
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(bObjectiveMet)); ++Index)
	{
		if (bObjectiveMet[Index] && Quest->Objectives.IsValidIndex(Index) && !Quest->Objectives[Index].bDone)
		{
			Journal->CompleteObjective(QuestId, Index);
		}
	}
}


void AAedirPlayerCharacter::SpawnItem(const FString& ItemId, const FString& Mode)
{
	RunDevCommand(TEXT("SpawnItem"), FString::Printf(TEXT("%s %s"), *ItemId, *Mode), true);
}

void AAedirPlayerCharacter::ListItems()
{
	RunDevCommand(TEXT("ListItems"), FString(), false);
}

void AAedirPlayerCharacter::SpawnEnemy(int32 Count, int32 Armed, float DistanceCm)
{
	RunDevCommand(TEXT("SpawnEnemy"), DistanceCm > 0.f ? FString::Printf(TEXT("%d %d %f"), Count, Armed, DistanceCm) : FString::Printf(TEXT("%d %d"), Count, Armed), true);
}

void AAedirPlayerCharacter::KillAll()
{
	RunDevCommand(TEXT("KillAll"), FString(), true);
}

void AAedirPlayerCharacter::SetWeatherType(const FString& WeatherName)
{
	RunDevCommand(TEXT("SetWeatherType"), WeatherName, true);
}

void AAedirPlayerCharacter::Heal()
{
	RunDevCommand(TEXT("Heal"), FString(), true);
}

void AAedirPlayerCharacter::Hurt(float Amount)
{
	RunDevCommand(TEXT("Hurt"), FString::Printf(TEXT("%f"), Amount), true);
}

void AAedirPlayerCharacter::God()
{
	RunDevCommand(TEXT("God"), FString(), true);
}

void AAedirPlayerCharacter::GiveSkill(const FString& SkillName, float Amount)
{
	const FString Args = SkillName.IsEmpty() ? FString() : FString::Printf(TEXT("%s %f"), *SkillName, Amount);
	RunDevCommand(TEXT("GiveSkill"), Args, true);
}

void AAedirPlayerCharacter::ChooseRace(const FString& RaceName)
{
	RunDevCommand(TEXT("SetRace"), RaceName, true);
}

void AAedirPlayerCharacter::Wound(const FString& WoundType, const FString& Bone)
{
	RunDevCommand(TEXT("Wound"), FString::Printf(TEXT("%s %s"), *WoundType, *Bone), true);
}

void AAedirPlayerCharacter::BloodTest(const FString& BloodArgs)
{
	RunDevCommand(TEXT("BloodTest"), BloodArgs, false);
}

void AAedirPlayerCharacter::DebugBody()
{
	RunDevCommand(TEXT("DebugBody"), FString(), false);
}

void AAedirPlayerCharacter::DevHelp()
{
	RunDevCommand(TEXT("DevHelp"), FString(), false);
}

void AAedirPlayerCharacter::SetStat(const FString& StatName, float Value)
{
	RunDevCommand(TEXT("SetStat"), FString::Printf(TEXT("%s %f"), *StatName, Value), true);
}

void AAedirPlayerCharacter::ShowStats()
{
	RunDevCommand(TEXT("Stats"), FString(), false);
}

void AAedirPlayerCharacter::Push(float Strength)
{
	RunDevCommand(TEXT("Push"), FString::Printf(TEXT("%f"), Strength), true);
}

void AAedirPlayerCharacter::Knockdown(const FString& Args)
{
	RunDevCommand(TEXT("Knockdown"), Args, true);
}

void AAedirPlayerCharacter::HeadBlow(const FString& Args)
{
	RunDevCommand(TEXT("HeadBlow"), Args, true);
}

void AAedirPlayerCharacter::DieNow()
{
	RunDevCommand(TEXT("DieNow"), FString(), true);
}

void AAedirPlayerCharacter::LookInfo()
{
	RunDevCommand(TEXT("LookInfo"), FString(), true);
}

void AAedirPlayerCharacter::BoneAxes(const FString& Bones)
{
	RunDevCommand(TEXT("BoneAxes"), Bones, false);
}

void AAedirPlayerCharacter::CloudFlash()
{
	RunDevCommand(TEXT("CloudFlash"), FString(), false);
}

void AAedirPlayerCharacter::Holster(const FString& HandArgs)
{
	RunDevCommand(TEXT("Holster"), HandArgs, false);
}

void AAedirPlayerCharacter::WaistInfo()
{
	RunDevCommand(TEXT("WaistInfo"), FString(), false);
}

void AAedirPlayerCharacter::LayDecal(const FString& Args)
{
	RunDevCommand(TEXT("LayDecal"), Args, false);
}

void AAedirPlayerCharacter::Dent(const FString& Args)
{
	RunDevCommand(TEXT("Dent"), Args, false);
}

void AAedirPlayerCharacter::ArmourInfo()
{
	RunDevCommand(TEXT("ArmourInfo"), FString(), false);
}

void AAedirPlayerCharacter::ClearItems()
{
	RunDevCommand(TEXT("ClearItems"), FString(), false);
}

void AAedirPlayerCharacter::GaitInfo(float Seconds, float Interval)
{
	RunDevCommand(TEXT("GaitInfo"), Interval > 0.f ? FString::Printf(TEXT("%f %f"), Seconds, Interval) : FString::Printf(TEXT("%f"), Seconds), false);
}

void AAedirPlayerCharacter::AutoMove(float Forward, float Right, int32 Run, float TurnDegrees)
{
	RunDevCommand(TEXT("AutoMove"), FString::Printf(TEXT("%f %f %d %f"), Forward, Right, Run, TurnDegrees), false);
}

namespace
{
	// A blow on the chest from straight ahead, or on the back from straight behind, as a weapon of that mass and speed would land it.
	void StrikeCreature(AAedirCreature* Victim, float EnergyJoules, float MassKg, float SpeedCmPerSec, bool bFromBehind)
	{
		if (!Victim || !Victim->Balance || !Victim->GetMesh())
		{
			return;
		}
		const FVector Toward = Victim->GetActorForwardVector() * (bFromBehind ? -1.f : 1.f);
		FAedirImpactInfo Impact;
		Impact.Bone = TEXT("spine_03");
		Impact.Zone = EAedirBodyZone::Torso;
		Impact.EnergyJoules = EnergyJoules;
		Impact.Direction = -Toward;
		Impact.Location = Victim->GetMesh()->GetBoneLocation(TEXT("spine_03")) + Toward * 15.f;
		Impact.MassKg = MassKg;
		Impact.RelativeSpeed = SpeedCmPerSec;
		Victim->Balance->ApplyHitReaction(Impact);
		UE_LOG(LogTemp, Display, TEXT("[Dev] %s struck with %.0f J from %s, balance %.2f"), *Victim->GetName(), EnergyJoules, bFromBehind ? TEXT("behind") : TEXT("the front"), Victim->Balance->GetBalance());
	}
}

void AAedirPlayerCharacter::StrikeSelf(float EnergyJoules, float MassKg, float SpeedCmPerSec, int32 FromBehind)
{
	StrikeCreature(this, EnergyJoules, MassKg, SpeedCmPerSec, FromBehind != 0);
}

void AAedirPlayerCharacter::GrabDead(const FString& Bone, int32 Strip)
{
	if (!Interaction)
	{
		return;
	}
	if (Strip != 0)
	{
		FName HeldBone;
		const AAedirCreature* HeldBody = Interaction->GetGrabbedCreature(EAedirHand::Right, &HeldBone);
		UE_LOG(LogTemp, Display, TEXT("[Dev] GrabDead: grabbing %d, holds %s by %s"), Interaction->IsGrabbingBody(EAedirHand::Right) ? 1 : 0, *GetNameSafe(HeldBody), *HeldBone.ToString());
		const bool bTaken = Interaction->StripGrabbedArmor(EAedirHand::Right);
		const AAedirItemActor* Held = Interaction->GetHeldItem(EAedirHand::Right);
		UE_LOG(LogTemp, Display, TEXT("[Dev] GrabDead strip: %s, the right hand holds %s"), bTaken ? TEXT("taken") : TEXT("nothing taken"), *GetNameSafe(Held));
		return;
	}
	AAedirCreature* Dead = nullptr;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->Stats && It->Stats->IsDead() && (!Dead || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Dead->GetActorLocation(), GetActorLocation())))
		{
			Dead = *It;
		}
	}
	if (!Dead || !Dead->GetMesh())
	{
		UE_LOG(LogTemp, Display, TEXT("[Dev] GrabDead: no dead body"));
		return;
	}
	const FVector Point = Dead->GetMesh()->GetBoneLocation(FName(*Bone));
	// Stepped up beside it, at its own height, swept so as not to land inside anything.
	const FVector Beside = Point + (GetActorLocation() - Point).GetSafeNormal2D() * 55.f;
	SetActorLocation(FVector(Beside.X, Beside.Y, GetActorLocation().Z), true);
	const bool bHeld = Interaction->TryGrabBody(EAedirHand::Right, &Point);
	UE_LOG(LogTemp, Display, TEXT("[Dev] GrabDead: %s %s's %s"), bHeld ? TEXT("holds") : TEXT("could not hold"), *Dead->GetName(), *Bone);
}

void AAedirPlayerCharacter::StrikePart(const FString& Bone, float EnergyJoules, float SideCm, int32 Nearest, float AheadCm)
{
	AAedirCreature* Victim = this;
	if (Nearest != 0)
	{
		Victim = nullptr;
		for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
		{
			if (*It != this && (!Victim || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Victim->GetActorLocation(), GetActorLocation())))
			{
				Victim = *It;
			}
		}
	}
	if (!Victim || !Victim->Balance || !Victim->GetMesh() || Victim->GetMesh()->GetBoneIndex(FName(*Bone)) == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Dev] StrikePart: no body or no bone %s"), *Bone);
		return;
	}
	const FVector Ahead = Victim->GetActorForwardVector();
	const FVector Place = Victim->GetMesh()->GetBoneLocation(FName(*Bone)) + Victim->GetActorRightVector() * SideCm;
	FAedirImpactInfo Impact;
	Impact.Bone = FName(*Bone);
	Impact.Zone = Bone.StartsWith(TEXT("head")) || Bone.StartsWith(TEXT("neck")) ? EAedirBodyZone::Head : EAedirBodyZone::Torso;
	Impact.EnergyJoules = EnergyJoules;
	// From in front it pushes back; from behind (a negative AheadCm) it pushes forward.
	Impact.Direction = AheadCm >= 0.f ? -Ahead : Ahead;
	Impact.Location = Place + Ahead * AheadCm;
	Impact.MassKg = 3.f;
	Impact.RelativeSpeed = 900.f;
	Victim->Balance->ApplyHitReaction(Impact);
	UE_LOG(LogTemp, Display, TEXT("[Dev] %s struck on %s, %.0f cm to the right, with %.0f J"), *Victim->GetName(), *Bone, SideCm, EnergyJoules);
}

void AAedirPlayerCharacter::StrikeNearest(float EnergyJoules, int32 FromBehind)
{
	AAedirCreature* Nearest = nullptr;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (*It != this && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	StrikeCreature(Nearest, EnergyJoules, 4.f, 900.f, FromBehind != 0);
}

void AAedirPlayerCharacter::WarpTo(float XMetres, float YMetres, float Yaw)
{
	const FVector Spot(XMetres * 100.f, YMetres * 100.f, 0.f);
	// The ground itself, under whatever mist or tree stands over it.
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirTeleport), false, this);
	GetWorld()->LineTraceMultiByObjectType(Hits, Spot + FVector(0.f, 0.f, 100000.f), Spot - FVector(0.f, 0.f, 100000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	const FHitResult* Ground = Hits.FindByPredicate([](const FHitResult& One) { return One.GetActor() && One.GetActor()->IsA<ALandscapeProxy>(); });
	if (Ground)
	{
		const FHitResult& Hit = *Ground;
		SetActorLocationAndRotation(Hit.ImpactPoint + FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* Mind = GetController())
		{
			Mind->SetControlRotation(FRotator(0.f, Yaw, 0.f));
		}
	}
}

void AAedirPlayerCharacter::ShotCam(const FString& CameraArgs)
{
	RunDevCommand(TEXT("ShotCam"), CameraArgs, false);
}

void AAedirPlayerCharacter::RecordFrames(int32 On)
{
	// Below zero it goes on until set back; above zero it counts down a frame at a time.
	GIsDumpingMovie = On != 0 ? -1 : 0;
	UE_LOG(LogAedir, Display, TEXT("[Dev] RecordFrames %d at frame %llu"), GIsDumpingMovie, GFrameCounter);
}

void AAedirPlayerCharacter::LightningNow(float AheadCm)
{
	const FVector Ahead = GetActorLocation() + GetControlRotation().Vector().GetSafeNormal2D() * AheadCm;
	for (TActorIterator<AAedirWeatherManager> It(GetWorld()); It; ++It)
	{
		if (AheadCm > 0.f)
		{
			It->MulticastLightning(FVector(Ahead.X, Ahead.Y, GetActorLocation().Z - 90.f), FMath::FRand());
		}
		else
		{
			It->StrikeLightningNow();
		}
	}
}

void AAedirPlayerCharacter::ShapeInfo()
{
	RunDevCommand(TEXT("ShapeInfo"), FString(), false);
}

void AAedirPlayerCharacter::HolsterTest(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->HolsterToWaist(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::PoseArm(const FString& PoseArgs)
{
	RunDevCommand(TEXT("PoseArm"), PoseArgs, true);
}

void AAedirPlayerCharacter::UpdateHairUnderHelmet()
{
	if (!Hair || !Interaction || !Face || Hair->GetAttachParent() != Face)
	{
		return;
	}
	const FAedirArmorHit OnHead = Interaction->GetArmorAt(TEXT("head"), Face->GetBoneLocation(TEXT("head")), EAedirDamageType::Blunt);
	const bool bShown = !OnHead.bCovered;
	if (Hair->IsVisible() != bShown)
	{
		Hair->SetVisibility(bShown);
		UE_LOG(LogAedir, Log, TEXT("[Hair] %s's hair %s"), *GetName(), bShown ? TEXT("shows again") : TEXT("is under a helmet"));
	}
}

void AAedirPlayerCharacter::ClothOn(int32 bOn)
{
	AAedirCatalogItem::SetClothOn(GetWorld(), bOn != 0);
	UE_LOG(LogAedir, Display, TEXT("[Dev] Cloth %s"), AAedirCatalogItem::IsClothOn() ? TEXT("on") : TEXT("off"));
}

void AAedirPlayerCharacter::FrameProbe(float Seconds, const FString& Label)
{
	struct FProbe
	{
		double Elapsed = 0.0, Worst = 0.0, Game = 0.0, Draw = 0.0, Gpu = 0.0;
		int32 Frames = 0;
	};
	TSharedRef<FProbe> Probe = MakeShared<FProbe>();
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	const FString Name = Label;
	FTimerManagerTimerParameters Params;
	Params.bLoop = true;
	Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Probe, Timer, Seconds, Name]()
	{
		const double Delta = FApp::GetDeltaTime();
		Probe->Elapsed += Delta;
		Probe->Worst = FMath::Max(Probe->Worst, Delta);
		Probe->Game += FPlatformTime::ToMilliseconds(GGameThreadTime);
		Probe->Draw += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		Probe->Gpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		++Probe->Frames;
		if (Probe->Elapsed >= Seconds && Probe->Frames > 0)
		{
			const double N = Probe->Frames;
			UE_LOG(LogAedir, Display, TEXT("[FrameProbe] %s: %.1f fps (%.2f ms a frame, worst %.1f ms), game %.2f ms, drawing %.2f ms, graphics card %.2f ms, over %d frames"), *Name,
				N / Probe->Elapsed, Probe->Elapsed * 1000.0 / N, Probe->Worst * 1000.0, Probe->Game / N, Probe->Draw / N, Probe->Gpu / N, Probe->Frames);
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), 0.001f, Params);
}

void AAedirPlayerCharacter::OffHandFree(const FString& FreeArgs)
{
	RunDevCommand(TEXT("OffHandFree"), FreeArgs, true);
}

void AAedirPlayerCharacter::MenuRow(const FString& RowName)
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	UAedirMenuSubsystem* Menus = PC && PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
	if (!Menus)
	{
		return;
	}
	if (RowName == TEXT("pause"))
	{
		// Opens the pause menu, for stepping through its pages from the console.
		Menus->TogglePauseMenu(0);
		return;
	}
	if (RowName == TEXT("dump"))
	{
		// Every graphics line, its value and whether it can be changed now.
		if (const UAedirSettingsSubsystem* Settings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirSettingsSubsystem>() : nullptr)
		{
			for (const int32 Index : Settings->GetPageOptions(EAedirSettingsPage::Graphics))
			{
				const FAedirSettingOption* Option = Settings->GetOption(Index);
				UE_LOG(LogAedir, Display, TEXT("[Settings] %s = %s%s"), *Option->Name, *Settings->GetValueText(Index), Option->Enabled && !Option->Enabled() ? TEXT(" (locked)") : TEXT(""));
			}
		}
		return;
	}
	if (RowName.IsEmpty())
	{
		Menus->OpenMainMenuSettings();
		return;
	}
	Menus->ChooseMainMenuRow(RowName);
}

void AAedirPlayerCharacter::ElbowCheck(const FString& CheckArgs)
{
	RunDevCommand(TEXT("ElbowCheck"), CheckArgs, false);
}

void AAedirPlayerCharacter::HitMe(const FString& HitArgs)
{
	RunDevCommand(TEXT("HitMe"), HitArgs, false);
}

void AAedirPlayerCharacter::SeverMe(const FString& Bone)
{
	RunDevCommand(TEXT("SeverMe"), Bone, false);
}

void AAedirPlayerCharacter::BloodCount()
{
	RunDevCommand(TEXT("BloodCount"), FString(), false);
}

void AAedirPlayerCharacter::GripInfo()
{
	RunDevCommand(TEXT("GripInfo"), FString(), false);
}

void AAedirPlayerCharacter::HipProbe(const FString& ProbeArgs)
{
	RunDevCommand(TEXT("HipProbe"), ProbeArgs, false);
}

void AAedirPlayerCharacter::JitterProbe(const FString& ProbeArgs)
{
	RunDevCommand(TEXT("JitterProbe"), ProbeArgs, false);
}

void AAedirPlayerCharacter::MouseDrive(const FString& DriveArgs)
{
	RunDevCommand(TEXT("MouseDrive"), DriveArgs, true);
}

void AAedirPlayerCharacter::DropHand(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->DropHeld(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::KickNow(float HoldShare)
{
	if (Combat)
	{
		Combat->DevHoldKick(HoldShare);
		Combat->StartKick();
	}
}

void AAedirPlayerCharacter::HandKey(int32 RightHand)
{
	HandleHandPressed(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
}

void AAedirPlayerCharacter::PlaceBlock(float Forward, float Right, float Up, float SizeCm)
{
	// A fixed block in front of the body, for trying what a held item does against something that does not give way.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (AStaticMeshActor* Block = Cube ? GetWorld()->SpawnActor<AStaticMeshActor>(GetActorTransform().TransformPositionNoScale(FVector(Forward, Right, Up)), GetActorRotation()) : nullptr)
	{
		Block->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->SetActorScale3D(FVector(SizeCm / 100.f));
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	}
}

void AAedirPlayerCharacter::HandWatch(int32 RightHand, float Seconds)
{
	// How much the hand turns against the body over a stretch of time, and how fast at the worst moment.
	const FName HandBone = RightHand != 0 ? TEXT("hand_r") : TEXT("hand_l");
	struct FWatch
	{
		FTimerHandle Timer;
		FQuat Last = FQuat::Identity;
		bool bStarted = false;
		float Elapsed = 0.f;
		float PeakAt = 0.f;
		float Total = 0.f;
		float Peak = 0.f;
	};
	TSharedRef<FWatch> Watch = MakeShared<FWatch>();
	const float Step = 1.f / 60.f;
	GetWorldTimerManager().SetTimer(Watch->Timer, FTimerDelegate::CreateWeakLambda(this, [this, Watch, HandBone, Seconds, Step]()
	{
		const FQuat Now = GetActorQuat().Inverse() * GetMesh()->GetBoneQuaternion(HandBone);
		Watch->Elapsed += Step;
		if (Watch->bStarted)
		{
			const float Degrees = FMath::RadiansToDegrees(static_cast<float>(Watch->Last.AngularDistance(Now)));
			Watch->Total += Degrees;
			if (Degrees > Watch->Peak)
			{
				Watch->Peak = Degrees;
				Watch->PeakAt = Watch->Elapsed;
			}
		}
		Watch->Last = Now;
		Watch->bStarted = true;
		if (Watch->Elapsed >= Seconds)
		{
			UE_LOG(LogTemp, Display, TEXT("HandWatch total %.0f deg, worst step %.1f deg at %.2f s, spine twist %.1f"), Watch->Total, Watch->Peak, Watch->PeakAt, Combat ? Combat->GetSwingTwistDegrees() : 0.f);
			GetWorldTimerManager().ClearTimer(Watch->Timer);
		}
	}), Step, true);
}

void AAedirPlayerCharacter::HeadCamera()
{
	HandleHeadCameraPressed();
}

void AAedirPlayerCharacter::PhotoToggle(float RealSeconds)
{
	HandleHeadCameraPressed();
	if (RealSeconds <= 0.f)
	{
		return;
	}
	TWeakObjectPtr<AAedirPlayerCharacter> Self(this);
	const double Until = FPlatformTime::Seconds() + RealSeconds;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Until](float)
	{
		if (!Self.IsValid())
		{
			return false;
		}
		if (FPlatformTime::Seconds() < Until)
		{
			return true;
		}
		Self->HandleHeadCameraPressed();
		UE_LOG(LogAedir, Display, TEXT("[Dev] PhotoToggle: the free camera is put away"));
		return false;
	}));
}

void AAedirPlayerCharacter::PhotoShots(int32 Count, float RealSeconds)
{
	if (!bPhotoCamera)
	{
		HandleHeadCameraPressed();
	}
	TWeakObjectPtr<AAedirPlayerCharacter> Self(this);
	double Next = FPlatformTime::Seconds() + RealSeconds;
	int32 Left = Count;
	// Where every body part of every creature was as the time stopped, to see how far each has gone since.
	TArray<TPair<TWeakObjectPtr<USkeletalMeshComponent>, TArray<FVector>>> Start;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (USkeletalMeshComponent* Body = It->GetMesh())
		{
			TArray<FVector>& Places = Start.Emplace_GetRef(Body, TArray<FVector>()).Value;
			for (const FBodyInstance* Part : Body->Bodies)
			{
				Places.Add(Part ? Part->GetUnrealWorldTransform().GetLocation() : FVector::ZeroVector);
			}
		}
	}
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Next, Left, RealSeconds, Start](float) mutable
	{
		APlayerController* PlayerController = Self.IsValid() ? Cast<APlayerController>(Self->GetController()) : nullptr;
		if (!PlayerController || Left <= 0)
		{
			return false;
		}
		if (FPlatformTime::Seconds() >= Next)
		{
			PlayerController->ConsoleCommand(TEXT("HighResShot 1"));
			UE_LOG(LogAedir, Display, TEXT("[Dev] PhotoShots: picture %d"), Left);
			for (const TPair<TWeakObjectPtr<USkeletalMeshComponent>, TArray<FVector>>& Was : Start)
			{
				const USkeletalMeshComponent* Body = Was.Key.Get();
				float Furthest = 0.f;
				FName FurthestPart;
				for (int32 Index = 0; Body && Index < Body->Bodies.Num() && Index < Was.Value.Num(); ++Index)
				{
					const FBodyInstance* Part = Body->Bodies[Index];
					const float Gone = Part ? static_cast<float>(FVector::Dist(Part->GetUnrealWorldTransform().GetLocation(), Was.Value[Index])) : 0.f;
					if (Gone > Furthest)
					{
						Furthest = Gone;
						FurthestPart = Part->BodySetup.IsValid() ? Part->BodySetup->BoneName : NAME_None;
					}
				}
				UE_LOG(LogAedir, Display, TEXT("[Dev] PhotoShots: %s has moved up to %.2f cm (%s) since the time stopped"), *GetNameSafe(Body ? Body->GetOwner() : nullptr), Furthest, *FurthestPart.ToString());
			}
			Next += RealSeconds;
			--Left;
		}
		return Left > 0;
	}));
}

void AAedirPlayerCharacter::MouseInput(float ValueX, float ValueY, float Seconds)
{
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	TSharedRef<float> Left = MakeShared<float>(Seconds);
	TSharedRef<uint64> LastFrame = MakeShared<uint64>(0);
	FTimerManagerTimerParameters Params;
	Params.bLoop = true;
	Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Timer, Left, LastFrame, ValueX, ValueY]()
	{
		if (*LastFrame == GFrameCounter)
		{
			return;
		}
		*LastFrame = GFrameCounter;
		// The values are a second's worth, as the mouse axes give them spread over the frames.
		const float Dt = GetWorld()->GetDeltaSeconds();
		HandleMouseX(ValueX * Dt);
		HandleMouseY(ValueY * Dt);
		*Left -= Dt;
		if (*Left <= 0.f)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), 1.f / 240.f, Params);
}

void AAedirPlayerCharacter::PoseMeasure(const FString& Label)
{
	if (Combat)
	{
		UAedirProceduralAnimInstance* Pose = GetMesh() ? Cast<UAedirProceduralAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		const FVector PoseHand = Pose ? GetActorTransform().InverseTransformPosition(Pose->GetPoseArmWorld(1, 2).GetLocation()) : FVector::ZeroVector;
		UE_LOG(LogAedir, Display, TEXT("[PoseMeasure] %s right %s | anim hand %.0f %.0f %.0f"), *Label, *Combat->DevMeasurePose(EAedirHand::Right), PoseHand.X, PoseHand.Y, PoseHand.Z);
	}
}

void AAedirPlayerCharacter::ArmBones(const FString& Label)
{
	if (!GetMesh())
	{
		return;
	}
	FString Line;
	for (const TCHAR* Bone : { TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"), TEXT("pelvis") })
	{
		const FVector Local = GetActorTransform().InverseTransformPositionNoScale(GetMesh()->GetBoneLocation(Bone));
		Line += FString::Printf(TEXT(" %s %.1f %.1f %.1f"), Bone, Local.X, Local.Y, Local.Z);
	}
	if (UAedirProceduralAnimInstance* Pose = Cast<UAedirProceduralAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		Line += TEXT(" | ") + Pose->DevWrists();
		const FRotator Chest = GetActorTransform().InverseTransformRotation(GetMesh()->GetBoneQuaternion(TEXT("spine_05"))).Rotator();
		Line += FString::Printf(TEXT(" | lead %d grip %d chest yaw %.0f pitch %.0f roll %.0f"), Combat && Combat->IsLeadFootRight() ? 1 : 0, Combat && Combat->IsTwoHandGrip() ? 1 : 0, Chest.Yaw, Chest.Pitch, Chest.Roll);
	}
	UE_LOG(LogAedir, Display, TEXT("[ArmBones] %s%s"), *Label, *Line);
}

void AAedirPlayerCharacter::Wash()
{
	if (Wounds)
	{
		Wounds->WashBlood(1.f);
	}
	int32 Stained = 0;
	for (const AAedirItemActor* Item : Interaction ? Interaction->GetWornItemsCopy() : TArray<AAedirItemActor*>())
	{
		Stained += Item ? Item->Stains.Num() : 0;
	}
	const FString Message = FString::Printf(TEXT("Washed: the blood is off the body and out of the clothes (%d stains left)."), Stained);
	UE_LOG(LogAedir, Display, TEXT("[Dev] %s"), *Message);
	UAedirDevTools::OnReported().Broadcast(Message);
}

void AAedirPlayerCharacter::ClothInfo()
{
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist(It->GetActorLocation(), GetActorLocation()) > 1500.f)
		{
			continue;
		}
		TArray<USkeletalMeshComponent*> Parts;
		It->GetComponents<USkeletalMeshComponent>(Parts, true);
		TArray<AActor*> Held;
		It->GetAttachedActors(Held, true, true);
		for (AActor* Thing : Held)
		{
			TArray<USkeletalMeshComponent*> More;
			Thing->GetComponents<USkeletalMeshComponent>(More, true);
			Parts.Append(More);
		}
		for (const USkeletalMeshComponent* Part : Parts)
		{
			const USkeletalMesh* Model = Part ? Part->GetSkeletalMeshAsset() : nullptr;
			if (!Model || Model->GetMeshClothingAssets().IsEmpty())
			{
				continue;
			}
			const TMap<int32, FClothSimulData>& Sim = Part->GetCurrentClothingData_GameThread();
			int32 Points = 0;
			float Furthest = 0.f;
			FVector Mean = FVector::ZeroVector;
			for (const TPair<int32, FClothSimulData>& Entry : Sim)
			{
				Points += Entry.Value.Positions.Num();
				for (const FVector3f& At : Entry.Value.Positions)
				{
					Mean += (FVector)At;
				}
			}
			Mean /= FMath::Max(Points, 1);
			UE_LOG(LogAedir, Display, TEXT("[Cloth] %s %s on %s: simulation %d, suspended %d, disabled %d, leader %s, visible %d, %d cloth points, their middle %s"), *It->GetName(), *Model->GetName(), *GetNameSafe(Part->GetOwner()),
				Part->GetClothingSimulation() ? 1 : 0, Part->IsClothingSimulationSuspended() ? 1 : 0, Part->bDisableClothSimulation ? 1 : 0, *GetNameSafe(Part->LeaderPoseComponent.Get()), Part->IsVisible() ? 1 : 0, Points, *Mean.ToCompactString());
		}
	}
}

void AAedirPlayerCharacter::CamInfoLength()
{
	UE_LOG(LogAedir, Display, TEXT("[Dev] Camera arm %.0f cm (fight pull-back %.2f)"), CameraBoom ? CameraBoom->TargetArmLength : 0.f, FightPullBackAlpha);
}

void AAedirPlayerCharacter::HoldKeys(float Forward, float Right, float Seconds)
{
	DevHeldKeys = FVector2D(Forward, Right);
	DevHeldKeysLeft = Seconds;
	UE_LOG(LogAedir, Display, TEXT("[Dev] Holding the keys forward %.1f right %.1f for %.1f s, camera yaw %.0f"), Forward, Right, Seconds, GetControlRotation().Yaw);
}

void AAedirPlayerCharacter::FistButton(int32 LeftButton, int32 Down)
{
	if (LeftButton != 0)
	{
		Down != 0 ? HandleRightGuardPressed() : HandleRightGuardReleased();
	}
	else
	{
		Down != 0 ? HandleLeftGuardPressed() : HandleLeftGuardReleased();
	}
}

void AAedirPlayerCharacter::TwoHand(int32 On)
{
	if (FistArms)
	{
		FistArms->SetTwoHand(On != 0);
		UE_LOG(LogAedir, Display, TEXT("[TwoHand] both hands %d (could %d)"), FistArms->IsTwoHand() ? 1 : 0, FistArms->CanTakeBothHands() ? 1 : 0);
	}
}

void AAedirPlayerCharacter::Physics(int32 On)
{
	IConsoleVariable* Posed = IConsoleManager::Get().FindConsoleVariable(TEXT("aedir.DirectPose"));
	if (!Posed)
	{
		return;
	}
	const bool bPhysics = On < 0 ? Posed->GetInt() != 0 : On != 0;
	Posed->Set(bPhysics ? 0 : 1, ECVF_SetByConsole);
	// With the physics on, a held weapon is part of the hand's body and its arm is left to its muscles, so the weapon is
	// stopped by what it meets; it is solid to every body and weapon, its holder's own trunk and limbs too, and passes only
	// its holder's own hands, which close on it. Off, all three are as the game runs without physics.
	const TPair<const TCHAR*, int32> Settings[] = { { TEXT("aedir.WeldHeld"), bPhysics ? 1 : 0 }, { TEXT("aedir.PosedArms"), bPhysics ? 0 : 1 },
		{ TEXT("aedir.HeldPassesOwnBody"), bPhysics ? 0 : 1 } };
	for (const TPair<const TCHAR*, int32>& Setting : Settings)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Setting.Key))
		{
			Variable->Set(Setting.Value, ECVF_SetByConsole);
		}
	}
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		It->ApplyBodyMask();
	}
	// The clothes' cloth goes with the bodies' physics.
	AAedirCatalogItem::SetClothOn(GetWorld(), bPhysics);
	// The hands' bodies go over to the physics on the next frames; a weapon is welded to a hand only once it simulates.
	FTimerHandle Refresh;
	GetWorldTimerManager().SetTimer(Refresh, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		for (TActorIterator<AAedirItemActor> It(GetWorld()); It; ++It)
		{
			It->RefreshHeldPhysics();
		}
	}), 0.25f, false);
	const FString Message = bPhysics ? TEXT("Physics on: the bodies are held to their poses by their muscles, held weapons are solid, clothes are cloth.") : TEXT("Physics off: the bodies are posed directly, clothes only follow the bones.");
	UE_LOG(LogAedir, Display, TEXT("[Physics] %s"), *Message);
	UAedirDevTools::OnReported().Broadcast(Message);
}

void AAedirPlayerCharacter::Noclip()
{
	SetNoclip(!bNoclip);
	if (!HasAuthority())
	{
		ServerSetNoclip(bNoclip);
	}
	const FString Message = bNoclip ? TEXT("Noclip on: fly with WASD, Space up, Ctrl down, Shift faster.") : FString::Printf(TEXT("Noclip off at %s."), *GetActorLocation().ToCompactString());
	UE_LOG(LogAedir, Display, TEXT("[Noclip] %s"), *Message);
	UAedirDevTools::OnReported().Broadcast(Message);
}

void AAedirPlayerCharacter::Tcl()
{
	Noclip();
}

void AAedirPlayerCharacter::BodyHeights(const FString& Label)
{
	AAedirCreature* Nearest = nullptr;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (*It != this && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	for (const AAedirCreature* Body : { static_cast<const AAedirCreature*>(this), static_cast<const AAedirCreature*>(Nearest) })
	{
		const USkeletalMeshComponent* Skin = Body ? Body->GetMesh() : nullptr;
		if (!Skin)
		{
			continue;
		}
		const auto At = [Skin](const TCHAR* Bone) { return Skin->GetBoneLocation(FName(Bone)); };
		const float Sole = static_cast<float>(FMath::Min(At(TEXT("foot_l")).Z, At(TEXT("foot_r")).Z));
		UE_LOG(LogAedir, Display, TEXT("[BodyHeights] %s %s: head %.0f neck %.0f chest %.0f pelvis %.0f knee %.0f above the feet | pelvis to head %.0f, thigh %.0f, shin %.0f, upper arm %.0f, forearm %.0f, mesh scale %.2f"),
			*Label, *Body->GetName(), At(TEXT("head")).Z - Sole, At(TEXT("neck_01")).Z - Sole, At(TEXT("spine_03")).Z - Sole, At(TEXT("pelvis")).Z - Sole, At(TEXT("calf_l")).Z - Sole,
			FVector::Dist(At(TEXT("pelvis")), At(TEXT("head"))), FVector::Dist(At(TEXT("thigh_l")), At(TEXT("calf_l"))), FVector::Dist(At(TEXT("calf_l")), At(TEXT("foot_l"))),
			FVector::Dist(At(TEXT("upperarm_l")), At(TEXT("lowerarm_l"))), FVector::Dist(At(TEXT("lowerarm_l")), At(TEXT("hand_l"))), Skin->GetComponentScale().Z);
	}
}

void AAedirPlayerCharacter::JointCheck()
{
	const USkeletalMeshComponent* Skin = GetMesh();
	for (const FConstraintInstance* Joint : Skin->Constraints)
	{
		const FName Child = Joint->ConstraintBone1;
		const FName Parent = Joint->ConstraintBone2;
		const FTransform ChildBone = Skin->GetSocketTransform(Child);
		const FTransform ParentBone = Skin->GetSocketTransform(Parent);
		const FVector OnParent = ParentBone.TransformPosition(Joint->GetRefFrame(EConstraintFrame::Frame2).GetLocation());
		const FVector OnChild = ChildBone.TransformPosition(Joint->GetRefFrame(EConstraintFrame::Frame1).GetLocation());
		UE_LOG(LogAedir, Display, TEXT("[JointCheck] %s (%s on %s): parent's anchor %.1f cm off the child bone, child's %.1f, bone length %.1f, parent anchor along %.2f of it, scale %.2f"),
			*Joint->JointName.ToString(), *Child.ToString(), *Parent.ToString(), FVector::Dist(OnParent, ChildBone.GetLocation()), FVector::Dist(OnChild, ChildBone.GetLocation()),
			FVector::Dist(ChildBone.GetLocation(), ParentBone.GetLocation()),
			FVector::DotProduct(OnParent - ParentBone.GetLocation(), (ChildBone.GetLocation() - ParentBone.GetLocation()).GetSafeNormal()) / FMath::Max(FVector::Dist(ChildBone.GetLocation(), ParentBone.GetLocation()), 0.01),
			ParentBone.GetScale3D().X);
	}
}

void AAedirPlayerCharacter::BladeNear(float Seconds)
{
	BladeWatchLeft = Seconds;
	BladeWatchClosest = TNumericLimits<float>::Max();
	GetWorldTimerManager().SetTimer(BladeWatchTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const float Step = GetWorld()->GetDeltaSeconds();
		BladeWatchLeft -= Step;
		const AAedirItemActor* Blade = Interaction ? Interaction->GetHeldItem(EAedirHand::Right) : nullptr;
		const AAedirCreature* Nearest = nullptr;
		for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
		{
			if (*It != this && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
			{
				Nearest = *It;
			}
		}
		if (Blade && Nearest && Nearest->GetMesh())
		{
			FVector Lo, Hi;
			Blade->GetActorBounds(true, Lo, Hi);
			const float Half = static_cast<float>((Hi - Lo).Size() * 0.5);
			const FVector Middle = (Lo + Hi) * 0.5;
			const FVector Along = Blade->GetActorForwardVector().GetSafeNormal();
			const FVector Chest = Nearest->GetMesh()->GetBoneLocation(TEXT("spine_03"));
			const float Distance = static_cast<float>(FMath::PointDistToSegment(Chest, Middle - Along * Half, Middle + Along * Half));
			BladeWatchClosest = FMath::Min(BladeWatchClosest, Distance);
		}
		if (BladeWatchLeft <= 0.f)
		{
			GetWorldTimerManager().ClearTimer(BladeWatchTimer);
			UE_LOG(LogAedir, Display, TEXT("[BladeWatch] the blade came within %.1f cm of the chest bone of the nearest body"), BladeWatchClosest);
		}
	}), 0.001f, true);
}

void AAedirPlayerCharacter::Wobble(float Seconds)
{
	WobbleLeft = Seconds;
	WobbleFrames = 0;
	WobbleTurns = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		WobbleSum[Index] = 0.f;
		WobbleMost[Index] = 0.f;
	}
	WobbleLastOff = FVector::ZeroVector;
	WobbleLastStep = FVector::ZeroVector;
	WobbleHeadSum = FVector::ZeroVector;
	WobbleHeadSquares = FVector::ZeroVector;
	WobbleLastHead = FVector::ZeroVector;
	WobbleLastHeadStep = FVector::ZeroVector;
	WobbleHeadTurns = 0;
	GetWorldTimerManager().SetTimer(WobbleTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		WobbleLeft -= GetWorld()->GetDeltaSeconds();
		const UAedirBodyControlComponent* Muscles = FindComponentByClass<UAedirBodyControlComponent>();
		const TCHAR* const Watched[4] = { TEXT("head"), TEXT("spine_03"), TEXT("hand_l"), TEXT("hand_r") };
		// The head as the body carries it, in the frame of the capsule: a body that bobs on its hips moves it about there.
		const FVector Head = GetActorTransform().InverseTransformPositionNoScale(GetMesh()->GetBoneLocation(TEXT("head")));
		WobbleHeadSum += Head;
		WobbleHeadSquares += Head * Head;
		const FVector HeadStep = Head - WobbleLastHead;
		if (WobbleFrames > 1 && HeadStep.Size() > 0.05 && FVector::DotProduct(HeadStep, WobbleLastHeadStep) < 0.0)
		{
			++WobbleHeadTurns;
		}
		if (HeadStep.Size() > 0.05)
		{
			WobbleLastHeadStep = HeadStep;
		}
		WobbleLastHead = Head;
		if (Muscles)
		{
			++WobbleFrames;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FVector Off = GetMesh()->GetBoneLocation(FName(Watched[Index])) - Muscles->GetPoseLocation(FName(Watched[Index]));
				WobbleSum[Index] += static_cast<float>(Off.Size());
				WobbleMost[Index] = FMath::Max(WobbleMost[Index], static_cast<float>(Off.Size()));
				if (Index == 0)
				{
					const FVector Step = Off - WobbleLastOff;
					if (Step.Size() > 0.05 && FVector::DotProduct(Step, WobbleLastStep) < 0.0)
					{
						++WobbleTurns;
					}
					if (Step.Size() > 0.05)
					{
						WobbleLastStep = Step;
					}
					WobbleLastOff = Off;
				}
			}
		}
		if (WobbleLeft <= 0.f)
		{
			GetWorldTimerManager().ClearTimer(WobbleTimer);
			const float Frames = static_cast<float>(FMath::Max(WobbleFrames, 1));
			UE_LOG(LogAedir, Display, TEXT("[Wobble] off the pose (mean / most cm): head %.1f / %.1f, chest %.1f / %.1f, left hand %.1f / %.1f, right hand %.1f / %.1f; the head turned back %d times in %d frames"),
				WobbleSum[0] / Frames, WobbleMost[0], WobbleSum[1] / Frames, WobbleMost[1], WobbleSum[2] / Frames, WobbleMost[2], WobbleSum[3] / Frames, WobbleMost[3], WobbleTurns, WobbleFrames);
			const FVector Mean = WobbleHeadSum / Frames;
			const FVector Spread = (WobbleHeadSquares / Frames - Mean * Mean).ComponentMax(FVector::ZeroVector);
			UE_LOG(LogAedir, Display, TEXT("[Wobble] the head on the capsule moved about by %.1f cm forward, %.1f sideways, %.1f up (standard deviation), and turned back %d times"),
				FMath::Sqrt(Spread.X), FMath::Sqrt(Spread.Y), FMath::Sqrt(Spread.Z), WobbleHeadTurns);
		}
	}), 0.001f, true);
}

void AAedirPlayerCharacter::BreakLimb(int32 Limb, int32 Near)
{
	AAedirCreature* Target = this;
	if (Near != 0)
	{
		Target = nullptr;
		for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
		{
			if (*It != this && (!Target || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Target->GetActorLocation(), GetActorLocation())))
			{
				Target = *It;
			}
		}
	}
	const bool bBroken = Target && Target->Wounds && Limb >= 0 && Limb < 4 && Target->Wounds->BreakLimb(static_cast<EAedirLimb>(Limb));
	if (bBroken)
	{
		// Heard as a bone broken in a fight is.
		UAedirSfxSubsystem::Play(this, EAedirSfx::Bone, Target->GetActorLocation(), 1.f);
	}
	UE_LOG(LogAedir, Display, TEXT("[Dev] Limb %d of %s %s"), Limb, *GetNameSafe(Target), bBroken ? TEXT("broken") : TEXT("not broken"));
}

void AAedirPlayerCharacter::ArmourFit(const FString& Label)
{
	USkeletalMeshComponent* Skin = GetMesh();
	USkeletalMesh* SkinAsset = Skin ? Skin->GetSkeletalMeshAsset() : nullptr;
	FSkeletalMeshRenderData* Render = SkinAsset ? SkinAsset->GetResourceForRendering() : nullptr;
	if (!Render || Render->LODRenderData.IsEmpty())
	{
		return;
	}
	const FVector Head = Skin->GetBoneLocation(TEXT("head"));
	const FQuat Frame = GetActorQuat();
	const auto InFrame = [&](const FVector& World) { return Frame.UnrotateVector(World - Head); };
	// The skin of the head: what lies above the bottom of the neck bone's child, near the head bone.
	TArray<FMatrix44f> RefToLocals;
	Skin->CacheRefToLocalMatrices(RefToLocals);
	TArray<FVector3f> Points;
	USkeletalMeshComponent::ComputeSkinnedPositions(Skin, Points, RefToLocals, Render->LODRenderData[0], Render->LODRenderData[0].SkinWeightVertexBuffer);
	FBox Skull(ForceInit);
	for (const FVector3f& Point : Points)
	{
		const FVector World = Skin->GetComponentTransform().TransformPosition(FVector(Point));
		const FVector Local = InFrame(World);
		if (Local.Size() < 30.0 && Local.Z > -3.0)
		{
			Skull += Local;
		}
	}
	UE_LOG(LogAedir, Display, TEXT("[ArmourFit] %s skull from the head bone: min %s max %s centre %s"), *Label, *Skull.Min.ToCompactString(), *Skull.Max.ToCompactString(), *Skull.GetCenter().ToCompactString());
	// The head bone's own axes in the body's frame, to know which way an offset on it goes.
	{
		const FQuat Bone = Skin->GetBoneQuaternion(TEXT("head"));
		UE_LOG(LogAedir, Display, TEXT("[ArmourFit] %s head bone axes: X %s Y %s Z %s"), *Label, *Frame.UnrotateVector(Bone.GetAxisX()).ToCompactString(),
			*Frame.UnrotateVector(Bone.GetAxisY()).ToCompactString(), *Frame.UnrotateVector(Bone.GetAxisZ()).ToCompactString());
	}
	// And the head's physical body, the shape the skull is given for blows.
	if (const FBodyInstance* HeadBody = Skin->GetBodyInstance(TEXT("head")))
	{
		const FBox Bounds = HeadBody->GetBodyBounds();
		FBox Shape(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			Shape += InFrame(FVector((Corner & 1) ? Bounds.Max.X : Bounds.Min.X, (Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y, (Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z));
		}
		UE_LOG(LogAedir, Display, TEXT("[ArmourFit] %s head body from the head bone: min %s max %s centre %s"), *Label, *Shape.Min.ToCompactString(), *Shape.Max.ToCompactString(), *Shape.GetCenter().ToCompactString());
	}
	for (TActorIterator<AAedirItemActor> It(GetWorld()); It; ++It)
	{
		if (It->Holder != this || !It->bWorn)
		{
			continue;
		}
		UE_LOG(LogAedir, Display, TEXT("[ArmourFit] %s %s hangs on %s's %s at %s, scale %s, the bone at %s"), *Label, *It->GetName(), *GetNameSafe(It->GetAttachParentActor()), *It->GetAttachParentSocketName().ToString(),
			*It->GetRootComponent()->GetRelativeLocation().ToCompactString(), *It->GetActorScale3D().ToCompactString(), *InFrame(Skin->GetBoneLocation(It->GetAttachParentSocketName())).ToCompactString());
		TArray<UStaticMeshComponent*> Parts;
		It->GetComponents(Parts);
		// Pieces worn apart hang from the body itself.
		for (USceneComponent* Attached : Skin->GetAttachChildren())
		{
			if (UStaticMeshComponent* Part = Cast<UStaticMeshComponent>(Attached))
			{
				if (Part->GetOwner() == *It)
				{
					Parts.AddUnique(Part);
				}
			}
		}
		for (const UStaticMeshComponent* Part : Parts)
		{
			if (!Part->IsVisible() || !Part->GetStaticMesh())
			{
				continue;
			}
			const FBox Box = Part->GetStaticMesh()->GetBoundingBox();
			FBox Placed(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector At((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
				Placed += InFrame(Part->GetComponentTransform().TransformPosition(At));
			}
			UE_LOG(LogAedir, Display, TEXT("[ArmourFit] %s %s %s on %s: min %s max %s centre %s"), *Label, *It->GetName(), *Part->GetStaticMesh()->GetName(),
				*Part->GetAttachSocketName().ToString(), *Placed.Min.ToCompactString(), *Placed.Max.ToCompactString(), *Placed.GetCenter().ToCompactString());
		}
	}
}

void AAedirPlayerCharacter::FeetInfo(const FString& Label)
{
	AAedirCreature* Nearest = nullptr;
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (*It != this && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	for (AAedirCreature* Body : { static_cast<AAedirCreature*>(this), Nearest })
	{
		const USkeletalMeshComponent* Skin = Body ? Body->GetMesh() : nullptr;
		if (!Skin)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirFeetInfo), false, Body);
		const auto Above = [&](const TCHAR* Bone)
		{
			const FVector At = Skin->GetBoneLocation(FName(Bone));
			FHitResult Hit;
			return GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 50.f), At - FVector(0.f, 0.f, 150.f), ECC_Visibility, Params) ? static_cast<float>(At.Z - Hit.ImpactPoint.Z) : -999.f;
		};
		const float CapsuleBottom = static_cast<float>(Body->GetActorLocation().Z - Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		FHitResult Under;
		const bool bGround = GetWorld()->LineTraceSingleByChannel(Under, Body->GetActorLocation(), Body->GetActorLocation() - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params);
		// The soles themselves: the lowest point of the skin under each ankle, as it is drawn now.
		float Sole[2] = { -999.f, -999.f };
		USkeletalMesh* SkinAsset = Skin->GetSkeletalMeshAsset();
		FSkeletalMeshRenderData* Render = SkinAsset ? SkinAsset->GetResourceForRendering() : nullptr;
		if (Render && !Render->LODRenderData.IsEmpty())
		{
			TArray<FMatrix44f> RefToLocals;
			const_cast<USkeletalMeshComponent*>(Skin)->CacheRefToLocalMatrices(RefToLocals);
			TArray<FVector3f> Points;
			USkeletalMeshComponent::ComputeSkinnedPositions(const_cast<USkeletalMeshComponent*>(Skin), Points, RefToLocals, Render->LODRenderData[0], Render->LODRenderData[0].SkinWeightVertexBuffer);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector Ankle = Skin->GetBoneLocation(Side == 0 ? TEXT("foot_l") : TEXT("foot_r"));
				double Lowest = TNumericLimits<double>::Max();
				FVector LowestAt = Ankle;
				for (const FVector3f& Point : Points)
				{
					const FVector World = Skin->GetComponentTransform().TransformPosition(FVector(Point));
					if (FVector::Dist2D(World, Ankle) < 20.0 && World.Z < Ankle.Z && World.Z < Lowest)
					{
						Lowest = World.Z;
						LowestAt = World;
					}
				}
				FHitResult Ground;
				if (Lowest < TNumericLimits<double>::Max() && GetWorld()->LineTraceSingleByChannel(Ground, LowestAt + FVector(0.f, 0.f, 50.f), LowestAt - FVector(0.f, 0.f, 150.f), ECC_Visibility, Params))
				{
					Sole[Side] = static_cast<float>(Lowest - Ground.ImpactPoint.Z);
				}
			}
		}
		UE_LOG(LogAedir, Display, TEXT("[FeetInfo] %s %s: the lowest point of the left sole %.1f, of the right %.1f above the ground under it"), *Label, *Body->GetName(), Sole[0], Sole[1]);
		UE_LOG(LogAedir, Display, TEXT("[FeetInfo] %s %s: above the ground, left ankle %.1f ball %.1f toe %.1f, right ankle %.1f ball %.1f toe %.1f; capsule bottom %.1f above the ground under it"),
			*Label, *Body->GetName(), Above(TEXT("foot_l")), Above(TEXT("ball_l")), Above(TEXT("ball_l")), Above(TEXT("foot_r")), Above(TEXT("ball_r")), Above(TEXT("ball_r")),
			bGround ? CapsuleBottom - static_cast<float>(Under.ImpactPoint.Z) : -999.f);
	}
}

void AAedirPlayerCharacter::LyingInfo(const FString& Label)
{
	if (Balance)
	{
		UE_LOG(LogAedir, Display, TEXT("[LyingInfo] %s own trunk bent by blows %s, head %s"), *Label, *Balance->GetHitSway().ToCompactString(), *Balance->GetHeadSway().ToCompactString());
	}
	// Every loose thing within three metres: how far its lowest point is above the ground under it (below zero, sunk in).
	for (TActorIterator<AAedirItemActor> It(GetWorld()); It; ++It)
	{
		const AAedirItemActor* Item = *It;
		if (Item->IsHeld() || FVector::Dist(Item->GetActorLocation(), GetActorLocation()) > 300.0)
		{
			continue;
		}
		const FBox Box = Item->GetComponentsBoundingBox(true);
		FCollisionQueryParams ItemParams(SCENE_QUERY_STAT(AedirLyingItem), false, Item);
		FHitResult Ground;
		const FVector Middle = Box.GetCenter();
		const bool bGround = GetWorld()->LineTraceSingleByChannel(Ground, Middle + FVector(0.f, 0.f, 50.f), Middle - FVector(0.f, 0.f, 300.f), ECC_WorldStatic, ItemParams);
		UE_LOG(LogAedir, Display, TEXT("[LyingInfo] %s item %s lowest %.1f cm above the ground, %.1f cm thick, awake %d, rest box %s welded %d solid %d"), *Label, *Item->ItemName.ToString(),
			bGround ? Box.Min.Z - Ground.ImpactPoint.Z : -999.0, Box.GetSize().Z, Item->GetRootComponent() && Cast<UPrimitiveComponent>(Item->GetRootComponent()) && Cast<UPrimitiveComponent>(Item->GetRootComponent())->RigidBodyIsAwake() ? 1 : 0,
			Item->RestBox ? *Item->RestBox->GetScaledBoxExtent().ToCompactString() : TEXT("none"), Item->RestBox && Item->RestBox->IsWelded() ? 1 : 0, Item->RestBox ? static_cast<int32>(Item->RestBox->GetCollisionEnabled()) : -1);
	}
	// Every other body within ten metres.
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		const AAedirCreature* Other = *It;
		// The own body too, when the label asks for it ("self" in it).
		const USkeletalMeshComponent* Skin = Other != this || Label.Contains(TEXT("self")) ? Other->GetMesh() : nullptr;
		if (!Skin || FVector::Dist(Other->GetActorLocation(), GetActorLocation()) > 1000.0)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirLyingInfo), false, Other);
		FString Line;
		float Highest = 0.f;
		for (const TCHAR* Bone : { TEXT("hand_l"), TEXT("hand_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("head"), TEXT("calf_l"), TEXT("calf_r") })
		{
			const FVector At = Skin->GetBoneLocation(FName(Bone));
			FHitResult Hit;
			const float Height = GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 100.f), At - FVector(0.f, 0.f, 200.f), ECC_Visibility, Params) ? static_cast<float>(At.Z - Hit.ImpactPoint.Z) : -1.f;
			Highest = FMath::Max(Highest, Height);
			Line += FString::Printf(TEXT(" %s %.0f"), Bone, Height);
		}
		// What may hold it up: bodies asleep, the fastest part, the joints' own drives and the bodies' damping.
		int32 Asleep = 0;
		int32 Simulating = 0;
		float Fastest = 0.f;
		float AngularDamping = 0.f;
		for (const FBodyInstance* Body : Skin->Bodies)
		{
			if (Body && Body->IsInstanceSimulatingPhysics())
			{
				++Simulating;
				Asleep += Body->IsInstanceAwake() ? 0 : 1;
				Fastest = FMath::Max(Fastest, static_cast<float>(Body->GetUnrealWorldVelocity().Size()));
				AngularDamping = FMath::Max(AngularDamping, Body->AngularDamping);
			}
		}
		float Drive = 0.f;
		for (const FConstraintInstance* Joint : Skin->Constraints)
		{
			const FAngularDriveConstraint& Angular = Joint->ProfileInstance.AngularDrive;
			Drive = FMath::Max(Drive, FMath::Max(Angular.SlerpDrive.Stiffness * (Angular.SlerpDrive.bEnablePositionDrive ? 1.f : 0.f),
				FMath::Max(Angular.SwingDrive.Stiffness * (Angular.SwingDrive.bEnablePositionDrive ? 1.f : 0.f), Angular.TwistDrive.Stiffness * (Angular.TwistDrive.bEnablePositionDrive ? 1.f : 0.f))));
			if (Label.StartsWith(TEXT("joints")))
			{
				UE_LOG(LogAedir, Display, TEXT("[LyingInfo] joint %s: mode %d, slerp %d/%d %.0f/%.0f, swing %d/%d %.0f/%.0f, twist %d/%d %.0f/%.0f, accel %d"), *Joint->ConstraintBone1.ToString(), static_cast<int32>(Angular.AngularDriveMode.GetValue()),
					Angular.SlerpDrive.bEnablePositionDrive, Angular.SlerpDrive.bEnableVelocityDrive, Angular.SlerpDrive.Stiffness, Angular.SlerpDrive.Damping,
					Angular.SwingDrive.bEnablePositionDrive, Angular.SwingDrive.bEnableVelocityDrive, Angular.SwingDrive.Stiffness, Angular.SwingDrive.Damping,
					Angular.TwistDrive.bEnablePositionDrive, Angular.TwistDrive.bEnableVelocityDrive, Angular.TwistDrive.Stiffness, Angular.TwistDrive.Damping, Angular.bAccelerationMode ? 1 : 0);
			}
		}
		UE_LOG(LogAedir, Display, TEXT("[LyingInfo] %s %s (%s, %.0f cm away) above the ground:%s; the highest %.0f cm; %d of %d bodies asleep, fastest %.0f cm/s, joint drive %.0f, angular damping %.2f, trunk bent by blows %s"), *Label, *Other->GetName(), Other->Stats && Other->Stats->IsDead() ? TEXT("dead") : TEXT("alive"),
			FVector::Dist(Other->GetActorLocation(), GetActorLocation()), *Line, Highest, Asleep, Simulating, Fastest, Drive, AngularDamping, Other->Balance ? *Other->Balance->GetHitSway().ToCompactString() : TEXT("-"));
	}
}

void AAedirPlayerCharacter::FootprintInfo()
{
	if (const UAedirFootprintSubsystem* Footprints = GetWorld()->GetSubsystem<UAedirFootprintSubsystem>())
	{
		UE_LOG(LogAedir, Display, TEXT("[Footprints] %d in the world, the oldest at %.3f of its strength"), Footprints->GetCount(), Footprints->GetOldestShare());
	}
}

void AAedirPlayerCharacter::ServerSetNoclip_Implementation(bool bOn)
{
	SetNoclip(bOn);
}

void AAedirPlayerCharacter::SetNoclip(bool bOn)
{
	bNoclip = bOn;
	SetActorEnableCollision(!bOn);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxFlySpeed = NoclipSpeed;
		Move->BrakingDecelerationFlying = NoclipSpeed * 4.f;
		Move->SetMovementMode(bOn ? MOVE_Flying : MOVE_Falling);
	}
}

void AAedirPlayerCharacter::FistPose(const FString& Args)
{
	TArray<FString> T;
	Args.ParseIntoArrayWS(T);
	if (FistArms)
	{
		if (T.Num() >= 3)
		{
			FistArms->DevSetPose(FVector2D(FCString::Atof(*T[0]), FCString::Atof(*T[1])), FCString::Atof(*T[2]), true);
		}
		else
		{
			FistArms->DevSetPose(FistArms->GetAim(), FistArms->GetPunch(), false);
		}
	}
}

void AAedirPlayerCharacter::FistInfo(const FString& Label)
{
	if (FistArms)
	{
		FVector Fist[2], Elbow[2];
		float Alpha[2];
		FistArms->GetArm(EAedirHand::Left, Fist[0], Elbow[0], Alpha[0]);
		FistArms->GetArm(EAedirHand::Right, Fist[1], Elbow[1], Alpha[1]);
		const FTransform& Body = GetActorTransform();
		FVector2D PeakAim;
		const float Peak = FistArms->TakePeakPunch(PeakAim);
		const float StraightLeft = FistArms->TakePeakStraight(EAedirHand::Left);
		const float StraightRight = FistArms->TakePeakStraight(EAedirHand::Right);
		const float AheadLeft = FistArms->TakePeakAhead(EAedirHand::Left);
		const float AheadRight = FistArms->TakePeakAhead(EAedirHand::Right);
		UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s straightest since last: left %.3f right %.3f; furthest ahead of the chest: left %.0f cm right %.0f cm"), *Label, StraightLeft, StraightRight, AheadLeft, AheadRight);
		UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s aim %.2f %.2f punch %.2f peak %.2f at %.2f one turn %.2f | L %.2f fist %s | R %.2f fist %s"), *Label, FistArms->GetAim().X, FistArms->GetAim().Y, FistArms->GetPunch(), Peak, PeakAim.X, FistArms->GetOneTurn(),
			Alpha[0], *Body.InverseTransformPositionNoScale(Fist[0]).ToCompactString(), Alpha[1], *Body.InverseTransformPositionNoScale(Fist[1]).ToCompactString());
		if (const USkeletalMeshComponent* ArmBody = GetMesh())
		{
			// The left arm against the body: how high the elbow and the wrist are over the shoulder, how far out at its side.
			const FTransform Frame = GetActorTransform();
			const FVector Shoulder = Frame.InverseTransformPositionNoScale(ArmBody->GetBoneLocation(TEXT("upperarm_l")));
			const FVector ElbowAt = Frame.InverseTransformPositionNoScale(ArmBody->GetBoneLocation(TEXT("lowerarm_l")));
			const FVector WristAt = Frame.InverseTransformPositionNoScale(ArmBody->GetBoneLocation(TEXT("hand_l")));
			UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s left elbow %.0f cm over the shoulder, %.0f out, %.0f ahead; left wrist %.0f over it"), *Label,
				ElbowAt.Z - Shoulder.Z, Shoulder.Y - ElbowAt.Y, ElbowAt.X - Shoulder.X, WristAt.Z - Shoulder.Z);
		}
		// How low the knuckles come: their height above the ground under them.
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirFistInfo), false, this);
			const auto Above = [&](const TCHAR* Bone)
			{
				const FVector At = GetMesh()->GetBoneLocation(FName(Bone));
				FHitResult Hit;
				return GetWorld()->LineTraceSingleByChannel(Hit, At, At - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params) ? static_cast<float>(At.Z - Hit.ImpactPoint.Z) : -1.f;
			};
			const FVector Trunk = GetActorTransform().InverseTransformVectorNoScale(GetMesh()->GetBoneLocation(TEXT("neck_01")) - GetMesh()->GetBoneLocation(TEXT("pelvis"))).GetSafeNormal();
			UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s knuckles above the ground: left %.0f cm, right %.0f cm; the trunk leans %.0f deg forward, the hips %.0f cm above the ground"), *Label, Above(TEXT("middle_01_l")), Above(TEXT("middle_01_r")),
				FMath::RadiansToDegrees(FMath::Atan2(Trunk.X, Trunk.Z)), Above(TEXT("pelvis")));
		}
		// Which way the blade in the right hand points, and the hand's knuckle row (little finger to first) and thumb, in the
		// body's frame (forward, right, up).
		if (const AAedirItemActor* Blade = Interaction ? Interaction->GetHeldItem(EAedirHand::Right) : nullptr)
		{
			const auto Way = [&](const FVector& V) { return Body.InverseTransformVectorNoScale(V.GetSafeNormal()).ToCompactString(); };
			const FVector Row = GetMesh()->GetBoneLocation(TEXT("index_01_r")) - GetMesh()->GetBoneLocation(TEXT("pinky_01_r"));
			const FVector Thumb = GetMesh()->GetBoneLocation(TEXT("thumb_02_r")) - GetMesh()->GetBoneLocation(TEXT("hand_r"));
			const FVector Forearm = GetMesh()->GetBoneLocation(TEXT("hand_r")) - GetMesh()->GetBoneLocation(TEXT("lowerarm_r"));
			const FVector UpperArm = GetMesh()->GetBoneLocation(TEXT("lowerarm_r")) - GetMesh()->GetBoneLocation(TEXT("upperarm_r"));
			const float ElbowDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(static_cast<float>(UpperArm.GetSafeNormal().Z), -1.f, 1.f)));
			const float ElbowBend = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(UpperArm.GetSafeNormal(), Forearm.GetSafeNormal())), -1.f, 1.f)));
			const FVector HandAboveHead = GetActorTransform().InverseTransformVectorNoScale(GetMesh()->GetBoneLocation(TEXT("hand_r")) - GetMesh()->GetBoneLocation(TEXT("head")));
			// How far the lowest point of the blade is above the soles, to see whether it reaches the ground.
			const float Feet = static_cast<float>(GetActorLocation().Z) - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const float LowestAboveFeet = static_cast<float>(Blade->GetComponentsBoundingBox(true).Min.Z) - Feet;
			UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s blade %s row %s thumb %s forearm %s upper arm %.0f deg over level, elbow bent %.0f deg, hand from head %s, blade low point %.0f cm above the soles, wrist up %.0f aim %.2f, blade to forearm %.0f deg"), *Label, *Way(Blade->GetActorForwardVector()), *Way(Row), *Way(Thumb), *Way(Forearm), ElbowDegrees, ElbowBend, *HandAboveHead.ToCompactString(), LowestAboveFeet, FistArms->GetBladeWristUp(EAedirHand::Right), FistArms->GetBladeAim(EAedirHand::Right), FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Blade->GetActorForwardVector().GetSafeNormal(), Forearm.GetSafeNormal())), -1.f, 1.f))));
			// Where the hollow of each palm is: how far off the blade's line, and how far along it from the item's root (the
			// handle runs from the grip point on up towards the point, the pommel lies below it).
			const FVector Root = Blade->GetActorLocation();
			const FVector Along = Blade->GetActorForwardVector().GetSafeNormal();
			const auto Hollow = [&](const TCHAR* Suffix)
			{
				const auto At = [&](const TCHAR* Bone) { return GetMesh()->GetBoneLocation(FName(*FString::Printf(TEXT("%s%s"), Bone, Suffix))); };
				const FVector Hand = At(TEXT("hand"));
				FVector Normal = FVector::CrossProduct(At(TEXT("index_01")) - At(TEXT("pinky_01")), At(TEXT("middle_01")) - Hand).GetSafeNormal();
				if (FVector::DotProduct(Normal, At(TEXT("thumb_01")) - Hand) < 0.0)
				{
					Normal = -Normal;
				}
				const FVector Palm = Hand + (At(TEXT("middle_01")) - Hand) * AAedirItemActor::HandGripPalmShare + Normal * AAedirItemActor::HandGripPalmDepthCm;
				const FVector From = Palm - Root;
				// And whether the first finger is towards the point (1) or the pommel (-1), the palm's normal, and how far the wrist
				// is bent off the line of the forearm.
				const float WristBend = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct((At(TEXT("middle_01")) - Hand).GetSafeNormal(), (Hand - At(TEXT("lowerarm"))).GetSafeNormal())), -1.f, 1.f)));
				return FString::Printf(TEXT("off %.1f along %.1f first finger to point %.2f palm %s wrist bent %.0f deg"), FVector::VectorPlaneProject(From, Along).Size(), FVector::DotProduct(From, Along),
					FVector::DotProduct((At(TEXT("index_01")) - At(TEXT("pinky_01"))).GetSafeNormal(), Along), *Body.InverseTransformVectorNoScale(Normal).ToCompactString(), WristBend);
			};
			UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s palms: right %s, left %s | grip at %.1f span %.1f, both hands %d"), *Label, *Hollow(TEXT("_r")), *Hollow(TEXT("_l")), Blade->GripLocalCm + Blade->HeldGripShiftCm, Blade->GripSpanCm, FistArms->IsTwoHand() ? 1 : 0);
			// How near the blade comes to the head: the head bone's distance from the blade's line, from the grip to the point.
			{
				const FBox Box = Blade->GetComponentsBoundingBox(true);
				double Reach = 0.0;
				for (int32 Corner = 0; Corner < 8; ++Corner)
				{
					const FVector At((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
					Reach = FMath::Max(Reach, FVector::DotProduct(At - Root, Along));
				}
				const FVector Head = GetMesh()->GetBoneLocation(TEXT("head"));
				// The skull itself: the middle of the head's body, well above the bone at the top of the neck.
				const FBodyInstance* Skull = GetMesh()->GetBodyInstance(TEXT("head"));
				const FVector SkullMiddle = Skull ? Skull->GetBodyBounds().GetCenter() : Head;
				UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s blade from the head %.0f cm, from the skull %.0f cm, its point %s"), *Label, FMath::PointDistToSegment(Head, Root, Root + Along * Reach),
					FMath::PointDistToSegment(SkullMiddle, Root, Root + Along * Reach), *Body.InverseTransformPositionNoScale(Root + Along * Reach).ToCompactString());
			}
			// Where the elbows are in the body's frame (forward, right, up from the middle of the capsule).
			const FVector LeftUpper = GetMesh()->GetBoneLocation(TEXT("lowerarm_l")) - GetMesh()->GetBoneLocation(TEXT("upperarm_l"));
			const FVector LeftFore = GetMesh()->GetBoneLocation(TEXT("hand_l")) - GetMesh()->GetBoneLocation(TEXT("lowerarm_l"));
			UE_LOG(LogAedir, Display, TEXT("[FistInfo] %s elbows: left %s right %s, the left bent %.0f deg"), *Label, *Body.InverseTransformPositionNoScale(GetMesh()->GetBoneLocation(TEXT("lowerarm_l"))).ToCompactString(), *Body.InverseTransformPositionNoScale(GetMesh()->GetBoneLocation(TEXT("lowerarm_r"))).ToCompactString(),
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(LeftUpper.GetSafeNormal(), LeftFore.GetSafeNormal())), -1.f, 1.f))));
		}
	}
}

void AAedirPlayerCharacter::StanceCell(int32 Row, int32 Col, float Forward, float Right, float Up, float Pitch, float Yaw)
{
	UAedirCombatComponent::DevSetStanceCell(Row, Col, FVector(Forward, Right, Up), Pitch, Yaw);
}

void AAedirPlayerCharacter::PoseLog(float Seconds)
{
	GetWorldTimerManager().ClearTimer(PoseLogTimer);
	if (Seconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(PoseLogTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const FAedirArmDynamics* Arm = Combat ? &Combat->DevGetArm(EAedirHand::Right) : nullptr;
			PoseMeasure(Arm ? FString::Printf(TEXT("live raw %.0f %.0f ext %.2f punch %.2f mspeed %.0f"), Arm->AimOffsetDeg.X, Arm->AimOffsetDeg.Y, Arm->Extension, Arm->PunchBlend, Arm->MouseSpeed) : FString(TEXT("live")));
		}), Seconds, true);
	}
}

void AAedirPlayerCharacter::ArmInfo()
{
	if (Combat)
	{
		const FVector Right = GetMesh() ? GetActorTransform().InverseTransformPosition(GetMesh()->GetBoneLocation(TEXT("hand_r"))) : FVector::ZeroVector;
		const FVector Left = GetMesh() ? GetActorTransform().InverseTransformPosition(GetMesh()->GetBoneLocation(TEXT("hand_l"))) : FVector::ZeroVector;
		const FVector Head = GetMesh() ? GetActorTransform().InverseTransformPosition(GetMesh()->GetBoneLocation(TEXT("head"))) : FVector::ZeroVector;
		const FVector RightReach = Interaction ? GetActorTransform().InverseTransformPosition(Interaction->DevGetReachTarget(EAedirHand::Right)) : FVector::ZeroVector;
		UAedirProceduralAnimInstance* Pose = GetMesh() ? Cast<UAedirProceduralAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		const FVector RightPose = Pose ? GetActorTransform().InverseTransformPosition(Pose->GetPoseArmWorld(1, 2).GetLocation()) : FVector::ZeroVector;
		const FBodyInstance* RightBody = GetMesh() ? GetMesh()->GetBodyInstance(TEXT("hand_r")) : nullptr;
		UE_LOG(LogAedir, Display, TEXT("[ArmInfo] right hand body %s simulating %d"), RightBody ? *GetActorTransform().InverseTransformPosition(RightBody->GetUnrealWorldTransform().GetLocation()).ToCompactString() : TEXT("-"), RightBody && RightBody->IsInstanceSimulatingPhysics() ? 1 : 0);
		UE_LOG(LogAedir, Display, TEXT("[ArmInfo] right %s, bone %s, reach %s at %.2f, pose %s"), *Combat->DevDescribeArm(EAedirHand::Right), *Right.ToCompactString(), *RightReach.ToCompactString(), Interaction ? Interaction->DevGetReachBlend(EAedirHand::Right) : 0.f, *RightPose.ToCompactString());
		UE_LOG(LogAedir, Display, TEXT("[ArmInfo] left %s, bone %s, head %s"), *Combat->DevDescribeArm(EAedirHand::Left), *Left.ToCompactString(), *Head.ToCompactString());
	}
}

void AAedirPlayerCharacter::CamInfo()
{
	const FVector Head = GetMesh() ? GetMesh()->GetBoneLocation(TEXT("head")) : GetActorLocation();
	const FVector Camera = FollowCamera ? FollowCamera->GetComponentLocation() : FVector::ZeroVector;
	UE_LOG(LogAedir, Display, TEXT("[CamInfo] view pitch %.1f, camera %.0f cm above the head and %.0f cm from it, lock on %d, fight %d"),
		FRotator::NormalizeAxis(GetControlRotation().Pitch), Camera.Z - Head.Z, FVector::Dist2D(Camera, Head), bLockOnEnabled ? 1 : 0, Combat && Combat->IsInFightStance() ? 1 : 0);
}

void AAedirPlayerCharacter::OrbitCamera(float YawDegrees, float PitchDegrees)
{
	// Held round there as if V were held: let go with OrbitCamera 0 0.
	OrbitOffset = FRotator(PitchDegrees, YawDegrees, 0.f);
	bOrbitHeld = YawDegrees != 0.f || PitchDegrees != 0.f;
	bOrbitMoved = bOrbitHeld;
}

void AAedirPlayerCharacter::BladeWatch(int32 RightHand, float Seconds)
{
	// How the item held in a hand turns, frame by frame: every step of more than a set angle is logged with what the arm was
	// doing then, so a blade that tumbles over in the hand shows when and why.
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	struct FWatch
	{
		FTimerHandle Timer;
		FQuat Last = FQuat::Identity;
		FQuat LastHand = FQuat::Identity;
		FQuat LastInHand = FQuat::Identity;
		TWeakObjectPtr<UPrimitiveComponent> Body;
		bool bStarted = false;
		float Elapsed = 0.f;
		float Total = 0.f;
		float Peak = 0.f;
		int32 Flips = 0;
		int32 Frames = 0;
		uint64 LastFrame = 0;
	};
	TSharedRef<FWatch> Watch = MakeShared<FWatch>();
	FTimerManagerTimerParameters Params;
	Params.bLoop = true;
	Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(Watch->Timer, FTimerDelegate::CreateWeakLambda(this, [this, Watch, Hand, Seconds]()
	{
		if (Watch->LastFrame == GFrameCounter)
		{
			return;
		}
		Watch->LastFrame = GFrameCounter;
		const float Dt = GetWorld()->GetDeltaSeconds();
		Watch->Elapsed += Dt;
		AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
		UPrimitiveComponent* Body = Item ? Cast<UPrimitiveComponent>(Item->GetRootComponent()) : nullptr;
		if (Body)
		{
			const FQuat Now = Body->GetComponentQuat();
			const FQuat HandNow = GetMesh() ? GetMesh()->GetBoneQuaternion(Hand == EAedirHand::Right ? TEXT("hand_r") : TEXT("hand_l"), EBoneSpaces::WorldSpace) : FQuat::Identity;
			const FQuat InHandNow = HandNow.Inverse() * Now;
			const float HandStep = Watch->bStarted ? FMath::RadiansToDegrees(static_cast<float>(HandNow.AngularDistance(Watch->LastHand))) : 0.f;
			const float InHandStep = Watch->bStarted ? FMath::RadiansToDegrees(static_cast<float>(InHandNow.AngularDistance(Watch->LastInHand))) : 0.f;
			Watch->LastHand = HandNow;
			Watch->LastInHand = InHandNow;
			if (Watch->bStarted && Watch->Body.Get() == Body)
			{
				// A weapon welded to a hand moved by the pose keeps a stale transform on its own body: the hand turns it too.
				const float Step = FMath::Max(FMath::RadiansToDegrees(static_cast<float>(Now.AngularDistance(Watch->Last))), HandStep);
				Watch->Total += Step;
				Watch->Frames++;
				if (Step > Watch->Peak)
				{
					Watch->Peak = Step;
				}
				if (Step > BladeWatchFlipDegrees)
				{
					Watch->Flips++;
					const FVector Axis = Now.GetForwardVector();
					const FAedirArmDynamics* Arm = Combat ? &Combat->DevGetArm(Hand) : nullptr;
					UE_LOG(LogAedir, Display, TEXT("[BladeWatch] %.2f s turned %.0f deg in one frame (%.3f s), the hand %.0f, in the hand %.0f; guard %d, two hands %d, aim %s, blade %s, wanted %s, held %s"),
						Watch->Elapsed, Step, Dt, HandStep, InHandStep, Combat && Combat->IsGuardActive(Hand) ? 1 : 0, Combat && Combat->IsTwoHandGrip() ? 1 : 0,
						Arm ? *Arm->PoseAimDeg.ToString() : TEXT("-"), *GetActorTransform().InverseTransformVectorNoScale(Axis).ToCompactString(),
						Arm ? *GetActorTransform().InverseTransformVectorNoScale(Arm->WeaponDir).ToCompactString() : TEXT("-"), *Item->GetName());
				}
			}
			Watch->Last = Now;
			Watch->Body = Body;
			Watch->bStarted = true;
		}
		else
		{
			Watch->bStarted = false;
		}
		if (Watch->Elapsed >= Seconds)
		{
			UE_LOG(LogAedir, Display, TEXT("[BladeWatch] done: %d frames, %d tumbles over %.0f deg, worst step %.0f deg, mean %.1f deg/frame"),
				Watch->Frames, Watch->Flips, BladeWatchFlipDegrees, Watch->Peak, Watch->Frames > 0 ? Watch->Total / Watch->Frames : 0.f);
			GetWorldTimerManager().ClearTimer(Watch->Timer);
		}
	}), 1.f / 120.f, Params);
}

void AAedirPlayerCharacter::MouseFlick(int32 RightHand, float DeltaX, float DeltaY, float Seconds)
{
	// Feeds an arm the same input a moving mouse gives it, for a look at throws and swings without one.
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	const float Step = 1.f / 60.f;
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	TSharedRef<float> Left = MakeShared<float>(Seconds);
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Timer, Left, Hand, DeltaX, DeltaY, Step]()
	{
		if (Combat)
		{
			Combat->AddSwingInput(Hand, FVector2D(DeltaX, DeltaY), Step);
		}
		if (FistArms && FistArms->IsSteering())
		{
			FistArms->AddMouse(FVector2D(DeltaX, DeltaY));
		}
		*Left -= Step;
		if (*Left <= 0.f)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), Step, true);
}

void AAedirPlayerCharacter::SwingTrace(int32 RightHand, float Seconds)
{
	// One log line per frame with where the hand, the blade and the feet really are against the body, for a look at how evenly they move.
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	const FName HandBone = RightHand != 0 ? TEXT("hand_r") : TEXT("hand_l");
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	TSharedRef<float> Elapsed = MakeShared<float>(0.f);
	// The item in that hand at the start is followed wherever it goes, onto the belt and back.
	const TWeakObjectPtr<AAedirItemActor> Watched = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
	FTimerManagerTimerParameters Params;
	Params.bLoop = true;
	Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Timer, Elapsed, Hand, HandBone, Seconds, Watched]()
	{
		const float Delta = GetWorld()->GetDeltaSeconds();
		*Elapsed += Delta;
		const FTransform& Body = GetActorTransform();
		const USkeletalMeshComponent* BodyMesh = GetMesh();
		const FVector HandLocal = Body.InverseTransformPositionNoScale(BodyMesh->GetBoneLocation(HandBone));
		const AAedirItemActor* Item = Interaction ? Interaction->GetHeldItem(Hand) : nullptr;
		const FVector Blade = Item ? Body.InverseTransformVectorNoScale(Item->GetActorForwardVector()) : FVector::ZeroVector;
		const FVector ItemLocal = Watched.IsValid() ? Body.InverseTransformPositionNoScale(Watched->GetActorLocation()) : FVector::ZeroVector;
		const FVector2D Pose = Combat ? Combat->DevGetPoseAim(Hand) : FVector2D::ZeroVector;
		const auto FootLine = [&](const TCHAR* Foot, const TCHAR* Ball) { return Body.InverseTransformVectorNoScale(BodyMesh->GetBoneLocation(Ball) - BodyMesh->GetBoneLocation(Foot)).GetSafeNormal(); };
		const FVector FootL = FootLine(TEXT("foot_l"), TEXT("ball_l"));
		const FVector FootR = FootLine(TEXT("foot_r"), TEXT("ball_r"));
		FVector Meant = FVector::ZeroVector;
		float MeantAlpha = 0.f;
		if (Combat && Combat->GetWeaponAim(Hand, Meant, MeantAlpha))
		{
			Meant = Body.InverseTransformVectorNoScale(Meant);
		}
		UE_LOG(LogTemp, Display, TEXT("[Swing] %.3f %.4f %.1f %.1f %.2f %.2f %.2f %.4f %.4f %.4f %.3f %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f %.2f %.2f %.2f"), *Elapsed, Delta, Pose.X, Pose.Y, HandLocal.X, HandLocal.Y, HandLocal.Z, Blade.X, Blade.Y, Blade.Z, FootL.X, FootL.Y, FootL.Z, FootR.X, FootR.Y, FootR.Z, Meant.X, Meant.Y, Meant.Z, ItemLocal.X, ItemLocal.Y, ItemLocal.Z);
		if (*Elapsed >= Seconds)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
		}
	}), 0.001f, Params);
}

void AAedirPlayerCharacter::PointTest(float Yaw, float Pitch)
{
	if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FRotator(Pitch, GetActorRotation().Yaw + Yaw, 0.f));
	}
	FTimerHandle PressTimer;
	GetWorldTimerManager().SetTimer(PressTimer, this, &AAedirPlayerCharacter::HandleTalkPressed, 0.3f, false);
}

void AAedirPlayerCharacter::AimForTest(TWeakObjectPtr<AActor> Target, bool bPress)
{
	// The camera swings round the body as the view turns, so the aim is taken a few times over.
	const bool bPressShot = bPressThenShot;
	TSharedRef<int32> Left = MakeShared<int32>(6);
	TSharedRef<FTimerHandle> Timer = MakeShared<FTimerHandle>();
	GetWorldTimerManager().SetTimer(*Timer, FTimerDelegate::CreateWeakLambda(this, [this, Target, bPress, bPressShot, Left, Timer]()
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const AAedirItemActor* Item = Cast<AAedirItemActor>(Target.Get());
		if (PlayerController && Target.IsValid())
		{
			FVector Eye;
			FRotator View;
			PlayerController->GetPlayerViewPoint(Eye, View);
			GetController()->SetControlRotation(((Item ? Item->GetBoundsCenterWorld() : Target->GetActorLocation()) - Eye).Rotation());
		}
		if (--*Left <= 0)
		{
			GetWorldTimerManager().ClearTimer(*Timer);
			if (bPress)
			{
				HandleTalkPressed();
				if (bPressShot)
				{
					GetController<APlayerController>()->ConsoleCommand(TEXT("shot showui"));
				}
			}
		}
	}), 0.15f, true);
}

void AAedirPlayerCharacter::PointItemTest(const FString& Id)
{
	for (TActorIterator<AAedirCatalogItem> It(GetWorld()); It; ++It)
	{
		if (It->CatalogId == FName(*Id) && AAedirItemActor::IsPickable(*It))
		{
			AimForTest(*It, true);
			return;
		}
	}
}

void AAedirPlayerCharacter::AimOnlyItem(const FString& Id)
{
	for (TActorIterator<AAedirCatalogItem> It(GetWorld()); It; ++It)
	{
		if (It->CatalogId == FName(*Id) && AAedirItemActor::IsPickable(*It))
		{
			AimForTest(*It, false);
			return;
		}
	}
}

void AAedirPlayerCharacter::LookNpcTest(const FString& Name, int32 Press)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			bPressThenShot = Press == 2;
			AimForTest(*It, Press != 0);
			return;
		}
	}
}

void AAedirPlayerCharacter::NpcDropTest(const FString& Name, const FString& Id, float Forward)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			FHitResult Hit;
			const FVector Over = It->GetActorLocation() + It->GetActorForwardVector() * Forward;
			const bool bGround = GetWorld()->LineTraceSingleByChannel(Hit, Over + FVector(0.0, 0.0, 300.0), Over - FVector(0.0, 0.0, 1000.0), ECC_Visibility);
			AAedirCatalogItem::SpawnCatalogItem(GetWorld(), FName(*Id), FTransform((bGround ? Hit.ImpactPoint : Over) + FVector(0.0, 0.0, 30.0)));
			return;
		}
	}
}

void AAedirPlayerCharacter::NpcRemoveTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->Destroy();
		}
	}
}

void AAedirPlayerCharacter::NpcDisarmTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->RestoreGear(TArray<FName>());
		}
	}
}

void AAedirPlayerCharacter::NpcBringTest(const FString& Name, float Forward)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->NpcName.ToString().Contains(Name))
		{
			It->TeleportTo(GetActorLocation() + GetActorForwardVector() * Forward, GetActorRotation() + FRotator(0.f, 180.f, 0.f), false, false);
		}
	}
}

void AAedirPlayerCharacter::GearInfo()
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (!It->Interaction)
		{
			continue;
		}
		const auto NameOf = [](const AAedirItemActor* Item) { return Item ? Item->ItemName.ToString() : FString(TEXT("-")); };
		FString Belt;
		for (const AAedirItemActor* Item : It->Interaction->GetWaistItemsCopy())
		{
			if (Item)
			{
				Belt += Item->ItemName.ToString() + TEXT(" ");
			}
		}
		FString Worn;
		for (const TObjectPtr<AAedirItemActor>& Item : It->Interaction->WornItems)
		{
			Worn += NameOf(Item.Get()) + TEXT(" ");
		}
		for (TActorIterator<AAedirCatalogItem> Loose(GetWorld()); Loose; ++Loose)
		{
			if (AAedirItemActor::IsPickable(*Loose) && FVector::Dist(Loose->GetActorLocation(), It->GetActorLocation()) < 2000.0)
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirGearInfo), false, *It);
				Params.AddIgnoredActor(*Loose);
				GetWorld()->LineTraceSingleByChannel(Hit, It->GetPawnViewLocation(), Loose->GetBoundsCenterWorld(), ECC_Visibility, Params);
				Belt += FString::Printf(TEXT("(near %s %.0f at %s blocked by %s) "), *Loose->CatalogId.ToString(), FVector::Dist(Loose->GetActorLocation(), It->GetActorLocation()), *Loose->GetBoundsCenterWorld().ToCompactString(), *GetNameSafe(Hit.GetActor()));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[Gear] %s order %d at %s: right %s left %s belt [%s] worn [%s] target %d"), *It->NpcName.ToString(), static_cast<int32>(It->Order), *It->GetActorLocation().ToCompactString(), *NameOf(It->Interaction->GetHeldItem(EAedirHand::Right)), *NameOf(It->Interaction->GetHeldItem(EAedirHand::Left)), *Belt, *Worn, It->Combat && It->Combat->HasAITarget() ? 1 : 0);
	}
}

void AAedirPlayerCharacter::TalkTest(const FString& Topic)
{
	// Walks up to the nearest person, opens the talk and says what was asked for, without a keyboard.
	AAedirNpc* Nearest = nullptr;
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (It->Role_Npc != EAedirNpcRole::Enemy && (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation())) && (!Topic.StartsWith(TEXT("Follow")) && !Topic.StartsWith(TEXT("Stay")) && !Topic.StartsWith(TEXT("part")) && !Topic.StartsWith(TEXT("Go")) || It->Role_Npc == EAedirNpcRole::Follower))
		{
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		return;
	}
	TArray<FAedirDialogueTopic> Topics;
	Nearest->BuildTopics(this, Topics);
	FString List;
	for (const FAedirDialogueTopic& One : Topics)
	{
		List += One.Label.ToString() + TEXT(" | ");
		if (!Topic.IsEmpty() && One.Label.ToString().Contains(Topic))
		{
			UE_LOG(LogTemp, Display, TEXT("[Talk] %s says: %s"), *Nearest->NpcName.ToString(), *One.Response.ToString());
			RequestNpcAction(Nearest, One.Action);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[Talk] %s (%s), order %d, %s: %s"), *Nearest->NpcName.ToString(), *Nearest->GetStandingText(UAedirSaveSubsystem::MakePlayerKey(Cast<APlayerController>(GetController()))).ToString(), static_cast<int32>(Nearest->Order), *Nearest->GetActorLocation().ToCompactString(), *List);
}

void AAedirPlayerCharacter::AssetCheck(const FString& Paths)
{
	TArray<FString> List;
	Paths.ParseIntoArray(List, TEXT(","));
	for (const FString& Path : List)
	{
		const UObject* Object = LoadObject<UObject>(nullptr, *Path);
		const UStaticMesh* Static = Cast<UStaticMesh>(Object);
		const UMaterialInterface* Material = Static ? Static->GetMaterial(0) : Cast<UMaterialInterface>(Object);
		const UMaterial* Root = Material ? Material->GetMaterial() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("[Asset] %s: %s, material %s, root %s"), *Path, Object ? *Object->GetClass()->GetName() : TEXT("MISSING"), *GetNameSafe(Material), *GetPathNameSafe(Root));
		if (const UMaterialInterface* Master = Cast<UMaterialInterface>(Object))
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Master->GetAllTextureParameterInfo(Infos, Ids);
			FString Names;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				Names += Info.Name.ToString() + TEXT(" ");
			}
			UE_LOG(LogTemp, Display, TEXT("[Asset] params: %s domain %d"), *Names, static_cast<int32>(Master->GetMaterial()->MaterialDomain));
		}
		if (const UTexture* Texture = Cast<UTexture>(Object))
		{
			UE_LOG(LogTemp, Display, TEXT("[Asset] texture virtual %d srgb %d compression %d lodgroup %d"), Texture->VirtualTextureStreaming ? 1 : 0, Texture->SRGB ? 1 : 0, static_cast<int32>(Texture->CompressionSettings), static_cast<int32>(Texture->LODGroup));
		}
	}
}

void AAedirPlayerCharacter::CutTest(const FString& Bone)
{
	AAedirNpc* Nearest = nullptr;
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		if (!Nearest || FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), GetActorLocation()))
		{
			Nearest = *It;
		}
	}
	if (Nearest && Nearest->Wounds)
	{
		const FVector Where = Nearest->GetMesh()->GetBoneLocation(FName(*Bone));
		const bool bCut = Nearest->Wounds->TrySever(FName(*Bone), Where, FVector(0.3, 0.2, 1.0).GetSafeNormal(), Nearest->GetActorRightVector() * 500.0, 500.f, 0.f);
		UE_LOG(LogTemp, Display, TEXT("[Cut] %s %s: %d, %s stands at %s"), *Nearest->NpcName.ToString(), *Bone, bCut ? 1 : 0, *Nearest->NpcName.ToString(), *Nearest->GetActorLocation().ToCompactString());
	}
}

void AAedirPlayerCharacter::DummyCutTest(const FString& PartName)
{
	for (TActorIterator<AAedirTrainingDummy> It(GetWorld()); It; ++It)
	{
		TArray<UPrimitiveComponent*> Parts;
		It->GetComponents<UPrimitiveComponent>(Parts);
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part->GetName() == PartName)
			{
				It->ReceiveStrike(Part, Part->Bounds.Origin, FVector(0.0, 500.0, 0.0), FVector(1.0, 0.0, 0.2).GetSafeNormal(), 200.f, true);
				UE_LOG(LogTemp, Display, TEXT("[Cut] dummy part %s at %s"), *PartName, *Part->Bounds.Origin.ToCompactString());
			}
		}
	}
}

void AAedirPlayerCharacter::SaveNow(const FString& Slot)
{
	if (UAedirSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Save] %s: %d"), *Slot, Saves->SaveWorld(Slot) ? 1 : 0);
	}
}

void AAedirPlayerCharacter::GuestSaveNow()
{
	StoreGuestCharacterNow();
	UAedirSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UAedirSaveSubsystem>();
	UE_LOG(LogTemp, Display, TEXT("[Save] guest %d into %s, strength %.1f"), Saves && Saves->IsGuest() ? 1 : 0, Saves ? *Saves->GetCurrentSlot() : TEXT(""), Stats ? Stats->Strength : 0.f);
}

void AAedirPlayerCharacter::DecalTest()
{
	FHitResult Hit;
	const FVector From = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirDecalTest), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, 300.0), ECC_Visibility, Params))
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aedir/Materials/M_Footprint.M_Footprint"));
		UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(this, Base, FVector(30.f, 50.f, 90.f), Hit.ImpactPoint + GetActorForwardVector() * 120.f, FRotationMatrix::MakeFromXZ(-Hit.ImpactNormal, GetActorForwardVector()).Rotator(), 60.f);
		UE_LOG(LogTemp, Display, TEXT("[Decal] on %s receives %d, decal %d material %s"), *GetNameSafe(Hit.GetComponent()), Hit.GetComponent() && Hit.GetComponent()->bReceivesDecals ? 1 : 0, Decal ? 1 : 0, *GetNameSafe(Base));
		if (UMaterialInstanceDynamic* Print = Decal ? Decal->CreateDynamicMaterialInstance() : nullptr)
		{
			Print->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.3f, 0.f, 0.f));
			Print->SetScalarParameterValue(TEXT("Strength"), 1.f);
		}
	}
}

void AAedirPlayerCharacter::NeedsInfo()
{
	for (TActorIterator<AAedirCreature> It(GetWorld()); It; ++It)
	{
		if (It->Stats)
		{
			const AAedirNpc* Npc = Cast<AAedirNpc>(*It);
			UE_LOG(LogTemp, Display, TEXT("[Needs] %s at %s yaw %.0f: health %.0f hunger %.1f thirst %.1f target %d"), Npc ? *Npc->NpcName.ToString() : *It->GetName(), *It->GetActorLocation().ToCompactString(), It->GetActorRotation().Yaw, It->Stats->GetHealth(), It->Stats->GetHunger(), It->Stats->GetThirst(), It->Combat && It->Combat->HasAITarget() ? 1 : 0);
		}
	}
}

void AAedirPlayerCharacter::PassHand()
{
	if (Interaction)
	{
		Interaction->PassToOtherHand();
	}
}

void AAedirPlayerCharacter::DrawSlot(int32 RightHand, int32 Slot)
{
	if (Interaction)
	{
		Interaction->DrawWaistSlot(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left, Slot);
	}
}

void AAedirPlayerCharacter::PlaceItem(const FString& ItemArgs)
{
	RunDevCommand(TEXT("PlaceItem"), ItemArgs, true);
}

void AAedirPlayerCharacter::ReachTest(float DelaySeconds, int32 BothHands)
{
	RunDevCommand(TEXT("ReachTest"), FString::Printf(TEXT("%f %d"), DelaySeconds, BothHands), true);
}

void AAedirPlayerCharacter::CrouchTest()
{
	RunDevCommand(TEXT("CrouchTest"), FString(), false);
}

void AAedirPlayerCharacter::FallTest(float HeightCm)
{
	RunDevCommand(TEXT("FallTest"), FString::Printf(TEXT("%f"), HeightCm), true);
}

void AAedirPlayerCharacter::ToggleConsole()
{
	if (ConsoleWidget.IsValid())
	{
		CloseConsole();
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !IsLocallyControlled() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	ConsoleWidget = SNew(SAedirConsole)
		.OnCommand(FAedirConsoleCommand::CreateUObject(this, &AAedirPlayerCharacter::RunConsoleLine))
		.OnPick(FAedirConsolePick::CreateUObject(this, &AAedirPlayerCharacter::HandleConsolePick))
		.OnClose(FSimpleDelegate::CreateUObject(this, &AAedirPlayerCharacter::CloseConsole));
	GEngine->GameViewport->AddViewportWidgetContent(ConsoleWidget.ToSharedRef(), 90);

	ConsoleReportHandle = UAedirDevTools::OnReported().AddLambda([this](const FString& Message)
	{
		if (ConsoleWidget.IsValid())
		{
			ConsoleWidget->AddLogLine(Message, FLinearColor(0.6f, 1.f, 0.7f, 1.f));
		}
	});

	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(ConsoleWidget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
	ConsoleWidget->FocusInput();
	RefreshConsoleSelection();

	// Alone in the world, everything waits. With others there it goes on.
	bConsolePaused = GetWorld()->GetNetMode() == NM_Standalone && PlayerController->SetPause(true);
}

void AAedirPlayerCharacter::RunConsoleLine(const FString& Line)
{
	if (AedirConsoleCommands::IsCommand(Line))
	{
		const AActor* Selected = SelectedEntity.Get();
		const FString Full = AedirConsoleCommands::TakesReference(Line) && Selected ? FString::Printf(TEXT("%s %08X"), *Line.TrimEnd(), AedirEntities::RefIdOfActor(Selected)) : Line;
		if (HasAuthority())
		{
			AedirConsoleCommands::Run(this, Full);
		}
		else
		{
			ServerConsoleLine(Full);
		}
		if (Selected && !IsValid(SelectedEntity.Get()))
		{
			SelectedEntity.Reset();
		}
		RefreshConsoleSelection();
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		const FString Output = PlayerController->ConsoleCommand(Line, false);
		if (!Output.IsEmpty() && ConsoleWidget.IsValid())
		{
			ConsoleWidget->AddLogLine(Output);
		}
	}
}

void AAedirPlayerCharacter::ServerConsoleLine_Implementation(const FString& Line)
{
	AedirConsoleCommands::Run(this, Line);
}

void AAedirPlayerCharacter::HandleConsolePick(const FVector2D& ViewportPixel)
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	FVector Origin;
	FVector Direction;
	AActor* Picked = nullptr;
	if (PlayerController && PlayerController->DeprojectScreenPositionToWorld(ViewportPixel.X, ViewportPixel.Y, Origin, Direction))
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirConsolePick), true, this);
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, Origin, Origin + Direction * AedirConsolePickRangeCm, FQuat::Identity, Objects, FCollisionShape::MakeSphere(AedirConsolePickRadiusCm), Params);
		for (const FHitResult& Hit : Hits)
		{
			AActor* Actor = Hit.GetActor();
			if (AedirEntities::RefIdOfActor(Actor) != 0)
			{
				Picked = Actor;
				break;
			}
			// A wall in the way ends the search.
			if (Hit.bBlockingHit && Hit.Component.IsValid() && Hit.Component->GetCollisionObjectType() == ECC_WorldStatic)
			{
				break;
			}
		}
	}

	// A second click anywhere lets go of what was picked, on something else it takes that instead.
	SelectedEntity = Picked != SelectedEntity.Get() ? Picked : nullptr;
	RefreshConsoleSelection();
}

void AAedirPlayerCharacter::RefreshConsoleSelection()
{
	if (ConsoleWidget.IsValid())
	{
		const AActor* Selected = SelectedEntity.Get();
		const FName Base = AedirEntities::BaseNameOfActor(Selected);
		ConsoleWidget->SetSelection(Selected ? FString::Printf(TEXT("%s    Ref %08X    Base %08X (%s)"), *AedirEntities::DisplayNameOfActor(Selected), AedirEntities::RefIdOfActor(Selected), AedirEntities::BaseIdOfName(Base), *Base.ToString()) : FString());
	}
}

void AAedirPlayerCharacter::Con(const FString& Line)
{
	RunConsoleLine(Line);
}

void AAedirPlayerCharacter::TalkOpenTest(const FString& Name)
{
	for (TActorIterator<AAedirNpc> It(GetWorld()); It; ++It)
	{
		const APlayerController* PlayerController = Cast<APlayerController>(GetController());
		const ULocalPlayer* Local = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
		UAedirMenuSubsystem* Menus = Local ? Local->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
		if (Menus && It->NpcName.ToString().Contains(Name))
		{
			Menus->OpenDialogue(*It);
			return;
		}
	}
}

void AAedirPlayerCharacter::LandInfo()
{
	// Which material each piece of the land draws with, by its class.
	int32 Shown = 0;
	for (TActorIterator<AActor> It(GetWorld()); It && Shown < 6; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("Landscape")))
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Parts;
		It->GetComponents<UPrimitiveComponent>(Parts);
		for (UPrimitiveComponent* Part : Parts)
		{
			UMaterialInterface* Material = Part->GetMaterial(0);
			UE_LOG(LogTemp, Display, TEXT("[Land] %s %s %s material %s parent %s"), *It->GetClass()->GetName(), *Part->GetClass()->GetName(), *Part->GetName(), *GetNameSafe(Material), *GetNameSafe(Material ? Material->GetMaterial() : nullptr));
			TArray<UMaterialInterface*> Used;
			Part->GetUsedMaterials(Used);
			for (UMaterialInterface* Each : Used)
			{
				const FMaterialResource* Resource = Each ? Each->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
				const UMaterialInstance* Instance = Cast<UMaterialInstance>(Each);
				UE_LOG(LogTemp, Display, TEXT("[Land]   uses %s (%s) finished %d errors %d permutation %d landscape %d shadermap %d"), *GetNameSafe(Each), Each ? *Each->GetClass()->GetName() : TEXT("-"), Resource ? Resource->IsCompilationFinished() : -1, Resource ? Resource->GetCompileErrors().Num() : -1,
					Instance ? (int32)Instance->bHasStaticPermutationResource : -1, Resource ? (int32)Resource->IsUsedWithLandscape() : -1, Resource ? (int32)(Resource->GetGameThreadShaderMap() != nullptr) : -1);
				if (Resource)
				{
					for (const FString& Error : Resource->GetCompileErrors())
					{
						UE_LOG(LogTemp, Display, TEXT("[Land]     %s"), *Error.Left(300));
					}
				}
			}
			if (++Shown >= 6)
			{
				break;
			}
		}
	}
}

void AAedirPlayerCharacter::FoliageInfo()
{
	if (const UAedirFoliageSubsystem* Foliage = GetWorld()->GetSubsystem<UAedirFoliageSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Foliage] %d planted, %d crushed"), Foliage->GetInstanceCount(), Foliage->GetCrushedCount());
	}
}

void AAedirPlayerCharacter::FxTest(const FString& Path, float Scale)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	// The fist's own dust puff, at that strength.
	if (Path == TEXT("dust"))
	{
		AAedirDustPuff::Spawn(GetWorld(), GetActorLocation() + GetActorForwardVector() * 60.f + FVector(0.f, 0.f, 50.f), -GetActorForwardVector(), Scale);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[Fx] %s: %d"), *Path, System ? 1 : 0);
	if (System)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, GetActorLocation() + GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 40.f), GetActorForwardVector().Rotation(), FVector(Scale));
	}
}

void AAedirPlayerCharacter::MeshSheet(const FString& Folder)
{
	TArray<FAssetData> Assets;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({ Folder }, true);
	Registry.GetAssetsByPath(FName(*Folder), Assets, true);
	FVector Where = GetActorLocation() + GetActorForwardVector() * 300.f;
	const FVector Step = GetActorRightVector() * 45.f;
	Where -= Step * (Assets.Num() / 2);
	for (const FAssetData& Asset : Assets)
	{
		UStaticMesh* Model = Cast<UStaticMesh>(Asset.GetAsset());
		if (!Model)
		{
			continue;
		}
		const FBox Box = Model->GetBoundingBox();
		AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Where - Box.GetCenter(), FRotator::ZeroRotator);
		Actor->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Model);
		UE_LOG(LogTemp, Display, TEXT("[Model] %s size %s center %s tris %d material %s"), *Model->GetName(), *Box.GetSize().ToCompactString(), *Box.GetCenter().ToCompactString(), Model->GetNumTriangles(0), *GetNameSafe(Model->GetMaterial(0)));
		Where += Step;
	}
}

void AAedirPlayerCharacter::LookTo(float Yaw, float Pitch)
{
	if (AController* MyController = GetController())
	{
		MyController->SetControlRotation(FRotator(Pitch, GetActorRotation().Yaw + Yaw, 0.f));
	}
}

void AAedirPlayerCharacter::GuardKey(int32 RightHand, int32 Down)
{
	if (Combat)
	{
		Combat->SetGuard(RightHand != 0 ? EAedirHand::Right : EAedirHand::Left, Down != 0);
	}
}

void AAedirPlayerCharacter::PrecipInfo()
{
	if (const UAedirPrecipitationSubsystem* Precip = GetWorld()->GetSubsystem<UAedirPrecipitationSubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[Precip] rain %d snow %d"), Precip->GetActiveRain(), Precip->GetActiveSnow());
	}
}

void AAedirPlayerCharacter::ConOpen()
{
	ToggleConsole();
}

void AAedirPlayerCharacter::ConPick(float ShareX, float ShareY)
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->GetViewportSize(SizeX, SizeY);
	}
	HandleConsolePick(FVector2D(SizeX * ShareX, SizeY * ShareY));
	const AActor* Selected = SelectedEntity.Get();
	UE_LOG(LogTemp, Display, TEXT("[Console] selected %s ref %08X"), *AedirEntities::DisplayNameOfActor(Selected), AedirEntities::RefIdOfActor(Selected));
}

void AAedirPlayerCharacter::MouthTest(int32 RightHand, int32 Hold)
{
	const EAedirHand Hand = RightHand != 0 ? EAedirHand::Right : EAedirHand::Left;
	if (Hold != 0)
	{
		HandleHandPressed(Hand);
	}
	else
	{
		HandleHandReleased(Hand);
	}
}

void AAedirPlayerCharacter::PackDrawTest(int32 RightHand)
{
	if (Interaction)
	{
		Interaction->DrawFromBackpack(0, RightHand != 0 ? EAedirHand::Right : EAedirHand::Left);
	}
}

void AAedirPlayerCharacter::NeedInfo()
{
	if (Stats && Interaction)
	{
		const AAedirItemActor* Right = Interaction->GetHeldItem(EAedirHand::Right);
		const AAedirItemActor* Left = Interaction->GetHeldItem(EAedirHand::Left);
		const USkeletalMeshComponent* BodyMesh = GetMesh();
		const FVector Head = BodyMesh->GetBoneLocation(FName(TEXT("head")));
		UE_LOG(LogTemp, Display, TEXT("[Need] hunger %.1f thirst %.1f stamina %.1f/%.1f regen %.2f athletics %.0f pack %d right %s left %s item to head %.1f up %.2f"), Stats->GetHunger(), Stats->GetThirst(), Stats->GetStamina(), Stats->GetMaxStamina(), Stats->GetStaminaRegenRate(), Stats->Athletism, Interaction->GetBackpackItemsCopy().Num(), Right ? *Right->ItemName.ToString() : TEXT("-"), Left ? *Left->ItemName.ToString() : TEXT("-"), Right ? FVector::Dist(Right->GetBoundsCenterWorld(), Head) : 0.f, Right ? Right->GetActorForwardVector().Z : 0.f);
	}
}

void AAedirPlayerCharacter::CloseConsole()
{
	if (!ConsoleWidget.IsValid())
	{
		return;
	}

	UAedirDevTools::OnReported().Remove(ConsoleReportHandle);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ConsoleWidget.ToSharedRef());
	}
	ConsoleWidget.Reset();

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->SetShowMouseCursor(false);
		PlayerController->SetInputMode(FInputModeGameOnly());
		if (bConsolePaused)
		{
			PlayerController->SetPause(false);
		}
	}
	bConsolePaused = false;
	SelectedEntity.Reset();
}

void AAedirPlayerCharacter::RunDevCommand(const FString& Command, const FString& Args, bool bRunOnServer)
{
	if (bRunOnServer && !HasAuthority())
	{
		ServerDevCommand(Command, Args);
		return;
	}

	UAedirDevTools::Execute(this, Command, Args);
}

void AAedirPlayerCharacter::ServerDevCommand_Implementation(const FString& Command, const FString& Args)
{
	UAedirDevTools::Execute(this, Command, Args);
}

void AAedirPlayerCharacter::DrawDebugBody()
{
	const UWorld* World = GetWorld();
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!World || !BodyMesh || !Balance || !Combat || !Interaction)
	{
		return;
	}

	const FVector CenterOfMass = Balance->GetCenterOfMassWorld();
	DrawDebugSphere(World, CenterOfMass, 7.f, 12, FColor::Yellow, false, 0.f);

	const FVector HeadLocation = GetActorLocation() + FVector(0.f, 0.f, 110.f);
	DrawDebugString(World, HeadLocation, FString::Printf(TEXT("Margin %.1f cm  Balance %.2f"), Balance->GetSupportMargin(), Balance->GetBalance()), nullptr, FColor::White, 0.f, true);

	for (const EAedirHand Hand : { EAedirHand::Left, EAedirHand::Right })
	{
		const FColor HandColor = Hand == EAedirHand::Left ? FColor::Cyan : FColor::Green;
		const FName HandBone = Hand == EAedirHand::Left ? Interaction->LeftHandBone : Interaction->RightHandBone;
		const FVector HandLocation = BodyMesh->GetBoneLocation(HandBone);
		DrawDebugSphere(World, HandLocation, 4.f, 8, HandColor, false, 0.f);

		FVector SwingTarget = FVector::ZeroVector;
		float SwingAlpha = 0.f;
		if (Combat->GetSwingPose(Hand, SwingTarget, SwingAlpha))
		{
			DrawDebugSphere(World, SwingTarget, 4.f, 8, FColor::Magenta, false, 0.f);
		}

		const FVector Tip = Combat->GetTipWorld(Hand);
		DrawDebugLine(World, HandLocation, Tip, HandColor, false, 0.f, 0, 1.f);
		DrawDebugSphere(World, Tip, 3.f, 8, FColor::Red, false, 0.f);
	}
}


