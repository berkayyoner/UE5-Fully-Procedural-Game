#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/AedirDustPuff.h"
#include "AedirFootstepComponent.generated.h"

class AAedirCreature;
class UAedirPhysicalMaterial;
class UAedirSurfaceSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAedirFootstepSignature, FVector, Location, const UAedirPhysicalMaterial*, Material, float, Volume);

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirFootstepComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Surface")
	FAedirFootstepSignature OnFootstep;

	// Wetness drying rate used for deposited foot water when the surface has no physical material.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface", meta=(ClampMin=0))
	float DefaultWetnessDryRatePerSecond = 0.03f;

	// Dust or spray kicked up by a step, low about the foot: the puff of a walking step and of a running one, between which
	// the pace chooses, and the share of walking and running steps that throw any up (every one by default). A landing
	// always does, StepDustLandingShare stronger. The colour is the ground's: dirt, grass, snow or water.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FAedirDustLook StepDustWalk = MakeStepDust(0.45f, 4, 1.5f, 5.f, 25.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FAedirDustLook StepDustRun = MakeStepDust(0.55f, 6, 2.f, 8.f, 45.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects", meta=(ClampMin="1"))
	float StepDustLandingShare = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FLinearColor StepDustDirt = FLinearColor(0.32f, 0.29f, 0.24f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FLinearColor StepDustGrass = FLinearColor(0.27f, 0.27f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FLinearColor StepDustSnow = FLinearColor(0.86f, 0.88f, 0.92f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	FLinearColor StepDustWater = FLinearColor(0.62f, 0.66f, 0.7f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	float StepEffectWalkChance = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects")
	float StepEffectRunChance = 1.f;

	// A footprint fades out slowly over this many days of the game's clock, and no more than this many are kept in the world
	// (the oldest go first).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects", meta=(ClampMin=0))
	float FootprintFadeGameDays = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Surface|Effects", meta=(ClampMin=1))
	int32 MaxFootprints = 3000;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category="Aedir|Surface")
	float GetSurfaceSlipperiness() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Surface")
	float GetSurfaceSpeedMultiplier() const;

private:
	void ProcessFootPlant(int32 FootIndex, const FVector& FootWorld, float Speed2D, float VolumeMultiplier);
	// Every step tells: a mark in the ground, the sound of what is under it, dust at speed, and blood or water carried along for some steps.
	void LeaveStepMarks(int32 FootIndex, const FVector& Point, const FVector& Normal, float Speed2D, float Volume, float SurfaceWetness, const UPhysicalMaterial* Physical);
	float BloodOnFoot[2] = { 0.f, 0.f };
	float WaterOnFoot[2] = { 0.f, 0.f };
	void PlayFootEffects(const UAedirPhysicalMaterial* Material, const FVector& Location, const FVector& Normal, float Speed2D, float Volume, bool bWetFootstep, bool bLeaveDecal);
	void UpdateSurfaceQuery();
	UAedirSurfaceSubsystem* GetSurfaceSubsystem() const;
	UObject* ResolveAsset(const FSoftObjectPath& Path);

	UPROPERTY(Transient)
	TObjectPtr<AAedirCreature> CreatureOwner;

	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UObject>> LoadedAssets;

	float FootWetness[2] = { 0.f, 0.f };
	float PreviousLift[2] = { 0.f, 0.f };

	// What the step came down on, and the family of sounds it makes when it has no surface type of its own.
	TWeakObjectPtr<UPrimitiveComponent> StepGround;
	FString GuessStepFamily(const FVector& Point) const;

	// Whether the step being made is a landing from a fall or a jump.
	bool bStepIsLanding = false;

	// A footstep cue of the sound library by surface family and gait, and a footstep effect by name. Null when there is none.
	static class USoundBase* FindStepSound(const FString& Family, const TCHAR* Gait);
	static class UNiagaraSystem* FindStepEffect(const TCHAR* Name);
	static FAedirDustLook MakeStepDust(float LifeSeconds, int32 Specks, float StartSizeCm, float EndSizeCm, float ThrowSpeed)
	{
		FAedirDustLook Look;
		Look.LifeSeconds = LifeSeconds;
		Look.Specks = Specks;
		Look.StartSizeCm = StartSizeCm;
		Look.EndSizeCm = EndSizeCm;
		Look.ThrowSpeed = ThrowSpeed;
		Look.Drag = 7.f;
		Look.Opacity = 0.22f;
		return Look;
	}

	// How a step is heard: from where the foot is, fading with distance, so a far walker is barely heard and never inside the head.
	UPROPERTY(Transient)
	TObjectPtr<class USoundAttenuation> StepAttenuation;
	class USoundAttenuation* GetStepAttenuation();

	// Whether each foot has been lifted since it last came down.
	bool bFootRaised[2] = { false, false };
	// For the hit log: where the last print of each foot was laid, and when, to tell how far the foot then came to rest from it.
	FVector LastPrintAt[2] = { FVector::ZeroVector, FVector::ZeroVector };
	double LastPrintTime[2] = { -1.0, -1.0 };
	float SurfaceQueryTimer = 0.1f;
	float CachedSlipperiness = 0.f;
	float CachedSpeedMultiplier = 1.f;
	bool bWasFalling = false;
};
