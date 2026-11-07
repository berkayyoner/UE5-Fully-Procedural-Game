#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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
	float SurfaceQueryTimer = 0.1f;
	float CachedSlipperiness = 0.f;
	float CachedSpeedMultiplier = 1.f;
	bool bWasFalling = false;
};
