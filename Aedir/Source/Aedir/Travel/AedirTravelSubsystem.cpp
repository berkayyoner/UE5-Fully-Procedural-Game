#include "Travel/AedirTravelSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Creature/AedirCreature.h"
#include "Creature/AedirStatsComponent.h"
#include "Dev/AedirDevTools.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HUD/AedirMarkerSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Travel/AedirTravelLogic.h"
#include "UI/AedirMenuSubsystem.h"
#include "UI/SAedirNotifications.h"
#include "UI/SAedirTravelOverlay.h"

namespace
{
	constexpr int32 TravelNotificationZOrder = 110;
	constexpr int32 TravelOverlayZOrder = 130;
	constexpr float TravelFadeOutSeconds = 0.8f;
	constexpr float TravelHoldSeconds = 2.2f;
	constexpr float TravelFadeInSeconds = 0.9f;
	constexpr float TravelArrivalOffsetCm = 300.f;
	constexpr float TravelGroundTraceCm = 20000.f;
	constexpr float TravelBanditDistanceCm = 600.f;
	constexpr float TravelAheadLocationCm = 8000.f;

	struct FTravelDemoLocation
	{
		const TCHAR* Name;
		float BearingDegrees;
		float DistanceCm;
	};

	// Spread around the start, 150 to 400 m away, so each needs a walk to be revealed.
	const FTravelDemoLocation TravelDemoLocations[] =
	{
		{ TEXT("Ashen Watchtower"), 35.f, 18000.f },
		{ TEXT("Willowmere Camp"), 160.f, 27000.f },
		{ TEXT("Stonefall Ruins"), 280.f, 38000.f }
	};
}

void UAedirTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DiscoveryHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAedirTravelSubsystem::TickDiscovery), AedirTravelLogic::DiscoveryIntervalSeconds);
}

void UAedirTravelSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(DiscoveryHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TravelHandle);

	if (GEngine && GEngine->GameViewport)
	{
		if (Notifications.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Notifications.ToSharedRef());
		}
		if (Overlay.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
	}
	Notifications.Reset();
	Overlay.Reset();

	if (bPausedByTravel && GetLocalPlayer() && GetLocalPlayer()->GetWorld())
	{
		UGameplayStatics::SetGamePaused(GetLocalPlayer()->GetWorld(), false);
	}
	bPausedByTravel = false;

	Super::Deinitialize();
}

AAedirCreature* UAedirTravelSubsystem::GetPawn() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const APlayerController* PlayerController = LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
	return PlayerController ? Cast<AAedirCreature>(PlayerController->GetPawn()) : nullptr;
}

void UAedirTravelSubsystem::EnsureNotifications()
{
	if (Notifications.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	Notifications = SNew(SAedirNotifications)
		.AvoidMenu_Lambda([this]()
		{
			const ULocalPlayer* LocalPlayer = GetLocalPlayer();
			const UAedirMenuSubsystem* Menus = LocalPlayer ? LocalPlayer->GetSubsystem<UAedirMenuSubsystem>() : nullptr;
			return Menus && Menus->IsListMenuOpen();
		});
	GEngine->GameViewport->AddViewportWidgetContent(Notifications.ToSharedRef(), TravelNotificationZOrder);
}

void UAedirTravelSubsystem::ShowNotification(const FText& Message, float Seconds)
{
	EnsureNotifications();
	if (Notifications.IsValid())
	{
		Notifications->AddNotification(Message, Seconds);
	}
}

bool UAedirTravelSubsystem::TickDiscovery(float DeltaSeconds)
{
	if (IsTravelling())
	{
		return true;
	}

	const AAedirCreature* Pawn = GetPawn();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UAedirMarkerSubsystem* Markers = World && World->IsGameWorld() ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers)
	{
		return true;
	}

#if !UE_BUILD_SHIPPING
	if (DemoWorld != World)
	{
		DemoWorld = World;
		RegisterDemoLocations(World, Pawn->GetActorLocation());
	}
#endif

	for (const FText& Label : Markers->UpdateDiscovery(Pawn->GetActorLocation()))
	{
		ShowNotification(FText::Format(NSLOCTEXT("Aedir", "LocationDiscovered", "{0} discovered"), Label));
	}
	return true;
}

void UAedirTravelSubsystem::RegisterDemoLocations(UWorld* World, const FVector& Start)
{
	UAedirMarkerSubsystem* Markers = World->GetSubsystem<UAedirMarkerSubsystem>();
	if (!Markers)
	{
		return;
	}

	for (const FTravelDemoLocation& Demo : TravelDemoLocations)
	{
		const FVector Direction = FRotator(0.f, Demo.BearingDegrees, 0.f).Vector();
		Markers->AddLocation(FText::FromString(Demo.Name), Start + Direction * Demo.DistanceCm);
	}
}

void UAedirTravelSubsystem::AddLocationAhead(const FString& Name)
{
	const AAedirCreature* Pawn = GetPawn();
	UAedirMarkerSubsystem* Markers = Pawn && Pawn->GetWorld() ? Pawn->GetWorld()->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	if (!Markers)
	{
		return;
	}

	static int32 LocationCounter = 0;
	const FString Label = Name.IsEmpty() ? FString::Printf(TEXT("Location %d"), ++LocationCounter) : Name;
	const FVector Forward = FVector(Pawn->GetActorForwardVector().X, Pawn->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	Markers->AddLocation(FText::FromString(Label), Pawn->GetActorLocation() + Forward * TravelAheadLocationCm);
	UAedirDevTools::OnReported().Broadcast(FString::Printf(TEXT("Added location %s 80 m ahead"), *Label));
}

bool UAedirTravelSubsystem::IsEnemyNearby(const AAedirCreature* Pawn) const
{
	UWorld* World = Pawn->GetWorld();
	const FVector Location = Pawn->GetActorLocation();
	for (TActorIterator<AAedirCreature> It(World); It; ++It)
	{
		const AAedirCreature* Other = *It;
		if (Other == Pawn || (Other->Stats && Other->Stats->IsDead()))
		{
			continue;
		}

		if (FVector::Dist2D(Other->GetActorLocation(), Location) < AedirTravelLogic::CombatRadiusCm && Pawn->IsEnemyOf(Other))
		{
			return true;
		}
	}
	return false;
}

FText UAedirTravelSubsystem::GetFastTravelBlockReason(FName MarkerId) const
{
	const AAedirCreature* Pawn = GetPawn();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	const UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr;
	const FAedirMapMarker* Marker = Markers ? Markers->FindMarker(MarkerId) : nullptr;
	if (!Marker || !AedirTravelLogic::CanFastTravelTo(Marker->Type, Marker->bDiscovered))
	{
		return NSLOCTEXT("Aedir", "TravelNotDiscovered", "Not discovered yet");
	}
	if (World->GetNetMode() == NM_Client)
	{
		return NSLOCTEXT("Aedir", "TravelClient", "Only the host can fast travel");
	}
	if (IsEnemyNearby(Pawn))
	{
		return NSLOCTEXT("Aedir", "TravelEnemies", "You cannot fast travel with enemies nearby");
	}
	return FText::GetEmpty();
}

bool UAedirTravelSubsystem::BeginFastTravel(FName MarkerId)
{
	AAedirCreature* Pawn = GetPawn();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (IsTravelling() || !GEngine || !GEngine->GameViewport)
	{
		return false;
	}

	const FText Reason = GetFastTravelBlockReason(MarkerId);
	if (!Reason.IsEmpty())
	{
		ShowNotification(Reason);
		return false;
	}

	Origin = Pawn->GetActorLocation();
	Destination = World->GetSubsystem<UAedirMarkerSubsystem>()->FindMarker(MarkerId)->Location;
	TotalHours = AedirTravelLogic::GetTravelHours(static_cast<float>(FVector::Dist2D(Origin, Destination)));
	bAmbush = AedirTravelLogic::ShouldEncounter(FMath::FRand());
	AmbushFraction = AedirTravelLogic::GetEncounterFraction(FMath::FRand());
	ShownHours = bAmbush ? TotalHours * AmbushFraction : TotalHours;

	// A paused standalone game keeps the player still while the screen is black.
	if (World->GetNetMode() == NM_Standalone && !UGameplayStatics::IsGamePaused(World))
	{
		UGameplayStatics::SetGamePaused(World, true);
		bPausedByTravel = true;
	}

	if (!Overlay.IsValid())
	{
		Overlay = SNew(SAedirTravelOverlay);
		GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), TravelOverlayZOrder);
	}
	Overlay->SetHours(0.f);
	Overlay->SetCover(0.f);

	Phase = EPhase::FadeOut;
	PhaseElapsed = 0.f;
	TravelHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAedirTravelSubsystem::TickTravel));
	return true;
}

bool UAedirTravelSubsystem::TickTravel(float DeltaSeconds)
{
	PhaseElapsed += DeltaSeconds;

	switch (Phase)
	{
	case EPhase::FadeOut:
		Overlay->SetCover(PhaseElapsed / TravelFadeOutSeconds);
		if (PhaseElapsed >= TravelFadeOutSeconds)
		{
			Phase = EPhase::Hold;
			PhaseElapsed = 0.f;
		}
		break;
	case EPhase::Hold:
		Overlay->SetHours(ShownHours * FMath::Clamp(PhaseElapsed / TravelHoldSeconds, 0.f, 1.f));
		if (PhaseElapsed >= TravelHoldSeconds)
		{
			Arrive();
		}
		break;
	case EPhase::FadeIn:
		Overlay->SetCover(1.f - PhaseElapsed / TravelFadeInSeconds);
		if (PhaseElapsed >= TravelFadeInSeconds)
		{
			FinishTravel();
			return false;
		}
		break;
	default:
		return false;
	}
	return true;
}

FVector UAedirTravelSubsystem::SnapToGround(const AAedirCreature* Pawn, const FVector& Location, float MinZ, float MaxZ) const
{
	const float HalfHeight = Pawn->GetCapsuleComponent() ? Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirTravelGround), false, Pawn);
	FHitResult Hit;
	const FVector Start(Location.X, Location.Y, MaxZ + TravelGroundTraceCm);
	const FVector End(Location.X, Location.Y, MinZ - TravelGroundTraceCm);
	if (Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return FVector(Location.X, Location.Y, Hit.ImpactPoint.Z + HalfHeight + 5.f);
	}
	return Location;
}

void UAedirTravelSubsystem::Arrive()
{
	Phase = EPhase::FadeIn;
	PhaseElapsed = 0.f;
	Overlay->SetHours(ShownHours);

	AAedirCreature* Pawn = GetPawn();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FVector Direction = (Destination - Origin).GetSafeNormal2D();
	const FVector Target = bAmbush
		? AedirTravelLogic::GetEncounterPoint(Origin, Destination, AmbushFraction)
		: Destination - Direction * TravelArrivalOffsetCm;
	const FVector Ground = SnapToGround(Pawn, Target, FMath::Min(Origin.Z, Destination.Z), FMath::Max(Origin.Z, Destination.Z));

	const FRotator Facing(0.f, Direction.Rotation().Yaw, 0.f);
	Pawn->SetActorLocation(Ground, false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->SetActorRotation(Facing);
	if (APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
	{
		PlayerController->SetControlRotation(Facing);
	}

	if (bAmbush)
	{
		ShowNotification(NSLOCTEXT("Aedir", "TravelAmbush", "Ambushed on the road!"));
		const int32 Count = FMath::RandRange(AedirTravelLogic::EncounterMinBandits, AedirTravelLogic::EncounterMaxBandits);
		UAedirDevTools::SpawnBandits(World, Ground + Direction * TravelBanditDistanceCm + FVector(0.f, 0.f, 50.f), Ground, Count, true);
	}
}

void UAedirTravelSubsystem::FinishTravel()
{
	Phase = EPhase::Idle;
	Overlay->SetCover(0.f);

	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	if (bPausedByTravel && World)
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
	bPausedByTravel = false;
}

#if !UE_BUILD_SHIPPING

namespace
{
	UAedirTravelSubsystem* TravelFindSubsystem(UWorld* World)
	{
		const ULocalPlayer* LocalPlayer = World ? World->GetFirstLocalPlayerFromController() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UAedirTravelSubsystem>() : nullptr;
	}

	void TravelRunAddLocation(const TArray<FString>& Args, UWorld* World)
	{
		if (UAedirTravelSubsystem* Travel = TravelFindSubsystem(World))
		{
			Travel->AddLocationAhead(FString::Join(Args, TEXT(" ")));
		}
	}

	void TravelRunDiscoverAll(const TArray<FString>& Args, UWorld* World)
	{
		if (UAedirMarkerSubsystem* Markers = World ? World->GetSubsystem<UAedirMarkerSubsystem>() : nullptr)
		{
			Markers->DiscoverAll();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs TravelAddLocationCommand(
		TEXT("AddLocation"),
		TEXT("AddLocation <Name>: registers an undiscovered map location 80 m in front of the player"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TravelRunAddLocation));

	FAutoConsoleCommandWithWorldAndArgs TravelDiscoverAllCommand(
		TEXT("DiscoverAll"),
		TEXT("DiscoverAll: reveals and discovers every map marker"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TravelRunDiscoverAll));
}

#endif
