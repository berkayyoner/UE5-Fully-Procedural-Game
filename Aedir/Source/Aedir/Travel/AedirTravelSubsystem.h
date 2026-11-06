#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "AedirTravelSubsystem.generated.h"

class AAedirCreature;
class SAedirNotifications;
class SAedirTravelOverlay;
class UWorld;

// Per local player: map discovery, top left notifications and fast travel with its road ambushes.
UCLASS()
class AEDIR_API UAedirTravelSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void ShowNotification(const FText& Message, float Seconds = 4.f);

	// Empty when the player may fast travel to the marker, otherwise the reason it is blocked.
	FText GetFastTravelBlockReason(FName MarkerId) const;

	// Fades out, moves the player and fades back in. Returns false when the travel is not allowed.
	bool BeginFastTravel(FName MarkerId);

	bool IsTravelling() const { return Phase != EPhase::Idle; }

	// Registers an undiscovered location in front of the pawn.
	void AddLocationAhead(const FString& Name);

private:
	enum class EPhase : uint8
	{
		Idle,
		FadeOut,
		Hold,
		FadeIn
	};

	bool TickDiscovery(float DeltaSeconds);
	bool TickTravel(float DeltaSeconds);
	void RegisterDemoLocations(UWorld* World, const FVector& Start);
	bool IsEnemyNearby(const AAedirCreature* Pawn) const;
	void Arrive();
	void FinishTravel();
	FVector SnapToGround(const AAedirCreature* Pawn, const FVector& Location, float MinZ, float MaxZ) const;
	AAedirCreature* GetPawn() const;
	void EnsureNotifications();

	TSharedPtr<SAedirNotifications> Notifications;
	TSharedPtr<SAedirTravelOverlay> Overlay;
	FTSTicker::FDelegateHandle DiscoveryHandle;
	FTSTicker::FDelegateHandle TravelHandle;
	TWeakObjectPtr<UWorld> DemoWorld;

	EPhase Phase = EPhase::Idle;
	float PhaseElapsed = 0.f;
	bool bPausedByTravel = false;
	bool bAmbush = false;
	float AmbushFraction = 0.5f;
	float TotalHours = 0.f;
	float ShownHours = 0.f;
	FVector Origin = FVector::ZeroVector;
	FVector Destination = FVector::ZeroVector;
};
