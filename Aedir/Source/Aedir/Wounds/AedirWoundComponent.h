#pragma once

#include "CoreMinimal.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/ActorComponent.h"
#include "Math/RandomStream.h"
#include "Wounds/AedirWoundTypes.h"
#include "AedirWoundComponent.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAedirWounded, EAedirWoundType, Type, FName, Bone, float, Severity);

// Client-side visual state of one wound. Never replicated.
USTRUCT()
struct FAedirWoundVisual
{
	GENERATED_BODY()

	uint8 Id = 0;
	FRandomStream Random;
	float DripTimer = 0.f;
	float DecalRadius = 0.f;

	UPROPERTY()
	TObjectPtr<UDecalComponent> Decal;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirWoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirWoundComponent();

	UPROPERTY(BlueprintAssignable, Category="Aedir|Wounds")
	FAedirWounded OnWounded;

	UPROPERTY(ReplicatedUsing=OnRep_Wounds, VisibleAnywhere, Category="Aedir|Wounds")
	TArray<FAedirWound> ActiveWounds;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Pure helpers used by the wound model and by tests.
	static EAedirWoundType ClassifyWound(EAedirDamageType DamageType);
	static bool ShouldCreateWound(EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection);
	static float ComputeSeverity(EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection);
	static float GetClotSeconds(EAedirWoundType Type, float Severity);
	static float GetHealSeconds(EAedirWoundType Type, float Severity);
	static float ComputeBleedRate(EAedirWoundType Type, float Severity, float AgeSeconds);
	static float ComputeAge01(EAedirWoundType Type, float Severity, float AgeSeconds);

	// Server only. Returns true when a wound was opened or merged into an existing one.
	bool AddWound(FName Bone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection, const FVector& SprayDirection);

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	int32 GetWoundCount() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	float GetTotalBleedRate() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	bool IsBleeding() const;

private:
	bool IsAuthoritative() const;
	bool IsDedicatedServer() const;
	USkeletalMeshComponent* GetBodyMesh() const;
	double GetWoundServerTime() const;
	float ComputeTotalBleedRate(double Now) const;
	uint8 AllocateWoundId();

	void UpdateBleeding(float DeltaTime);
	void EnsureWoundVisuals();
	void UpdateWoundVisuals(float DeltaTime);
	FAedirWoundVisual* FindVisual(uint8 Id);
	UDecalComponent* CreateWoundDecal(USkeletalMeshComponent* Mesh, const FAedirWound& Wound, UMaterialInstanceDynamic* Material, float Radius, float Roll);

	UFUNCTION()
	void OnRep_Wounds();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWoundSpray(FVector Location, FVector Direction, float Severity, EAedirWoundType Type);

	UPROPERTY(Transient)
	TArray<FAedirWoundVisual> Visuals;

	uint8 NextWoundId = 0;
};
