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

	// The blood that runs down the skin from an open wound: how far it has got (0 to 1 of its length) and how wet it still is.
	UPROPERTY()
	TObjectPtr<UDecalComponent> Run;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RunMaterial;

	float RunLength = 0.f;
	float RunWet = 1.f;
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

	// The most wounds a body keeps at once, all drawn on the skin; the oldest goes only past this (no more than 250).
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	int32 MaxWounds = 250;

	// Blunt blows wear a place before they mark it: each adds its energy, all of it from BluntFastSpeed up and none of it at
	// BluntSlowSpeed or below (cm/s), less what armour takes; the face takes FaceBluntShare times as much. The place bruises
	// once its wear reaches BluntBruiseJoules and the skin splits and bleeds at BluntSplitJoules, worse the more it takes
	// after; the wear wanes by half every BluntWearHalfLifeSeconds.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntSlowSpeed = 250.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntFastSpeed = 800.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float FaceBluntShare = 2.2f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntBruiseJoules = 90.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntSplitJoules = 320.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntWearHalfLifeSeconds = 120.f;

	// A parted limb's stump runs with blood all the time, this many drops a second, and goes on for this long after death,
	// thinning out; the piece cut off runs from its cut end this many a second for this long.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpDropsPerSecond = 16.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpDeadBleedSeconds = 90.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float PieceDropsPerSecond = 8.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float PieceBleedSeconds = 45.f;

	// How fast the running blood leaves the stump (cm/s, a range) and how much it scatters.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpDropSpeedMin = 25.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpDropSpeedMax = 80.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpDropScatter = 18.f;

	// With a limb off, the wounds tick this often (seconds) so the running blood is smooth.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	float StumpTickSeconds = 0.04f;

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

	// Server only. Returns true when a wound was opened or merged into an existing one. A blunt blow only wears the place:
	// see the blunt settings. SpeedCmPerSec is how fast it came, below 0 when not known (then as fast as a hard blow).
	bool AddWound(FName Bone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirDamageType DamageType, float EnergyJoules, float ArmorProtection, const FVector& SprayDirection, float SpeedCmPerSec = -1.f);

	// How much a place has taken from blunt blows lately, in joules.
	float GetBluntWear(FName Bone) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	int32 GetWoundCount() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	float GetTotalBleedRate() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Wounds")
	bool IsBleeding() const;

	// Server only. A cut hard enough parts the limb at the joint it landed on. Says whether it did.
	bool TrySever(FName HitBone, const FVector& PlanePoint, const FVector& PlaneNormal, const FVector& Push, float EnergyJoules, float ArmorProtection);
	bool IsSevered(FName Bone) const { return SeveredBones.Contains(Bone); }

	// Server only. A blow hard enough breaks the bones of a limb: a broken arm hangs limp and lets go of what it held, a
	// broken leg will not carry the body. Says whether it was whole until now.
	bool BreakLimb(EAedirLimb Limb);
	bool IsLimbBroken(EAedirLimb Limb) const { return (BrokenLimbs & (1 << static_cast<int32>(Limb))) != 0; }

	// Whether the arm on that side is of no use: gone, or broken.
	bool HasUselessArm(bool bLeft) const { return HasLostArm(bLeft) || IsLimbBroken(bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm); }

	// Whether the arm on that side is gone, at the shoulder, the elbow or the wrist.
	bool HasLostArm(bool bLeft) const
	{
		const TCHAR* Side = bLeft ? TEXT("_l") : TEXT("_r");
		for (const FName& Bone : SeveredBones)
		{
			const FString Name = Bone.ToString();
			if (Name.EndsWith(Side) && (Name.StartsWith(TEXT("upperarm")) || Name.StartsWith(TEXT("lowerarm")) || Name.StartsWith(TEXT("hand"))))
			{
				return true;
			}
		}
		return false;
	}

	// A bandage: the worst of the open wounds stop running. Server only.
	void StopBleedingSome();

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
	UDecalComponent* CreateRunDecal(USkeletalMeshComponent* Mesh, const FAedirWound& Wound, UMaterialInstanceDynamic* Material);

	UFUNCTION()
	void OnRep_Wounds();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSever(FName Bone, FVector PlanePoint, FVector PlaneNormal, FVector Push);

	UPROPERTY()
	TSet<FName> SeveredBones;

	// Which limbs have broken bones, one bit each in the order of EAedirLimb.
	UPROPERTY(Replicated)
	uint8 BrokenLimbs = 0;

	// How much cutting each joint has taken so far, towards parting it.
	TMap<FName, float> CutWear;

	// How much each place has taken from blunt blows, and when it was last worked out (server time).
	struct FAedirBluntWear
	{
		float Joules = 0.f;
		double Time = 0.0;
	};
	TMap<FName, FAedirBluntWear> BluntWear;

	// Opens a wound of this kind and badness, or makes one already there that bad (bAdd: that much worse).
	void OpenWound(FName ResolvedBone, const FVector& WorldLocation, const FVector& WorldNormal, EAedirWoundType Type, float Severity, bool bAdd, const FVector& SprayDirection);

	// Blood running from an open artery comes in beats.
	float SeverBleedPerSecond = 0.f;
	float PulseTimer = 0.f;
	void UpdateArterialBleeding(float DeltaTime);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWoundSpray(FVector Location, FVector Direction, float Severity, EAedirWoundType Type);

	UPROPERTY(Transient)
	TArray<FAedirWoundVisual> Visuals;

	// Blood on the skin: drawn over the body by one material that knows where each open wound is on the body as it stands in
	// its bind pose, so the blood moves with the skin instead of sliding over it like a picture laid on.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkinBlood;
	// Every wound for the skin's material, one texel across each: where it is (bind pose, radius) on the first row, how it is
	// on the second.
	UPROPERTY(Transient)
	TObjectPtr<class UTexture2D> SkinBloodData;
	bool bSkinBloodTried = false;

	// Pieces cut off, where their cut end is in their own frame and which way it faces, and how long they have run.
	struct FAedirCutPiece
	{
		TWeakObjectPtr<AActor> Piece;
		FVector LocalEnd = FVector::ZeroVector;
		FVector LocalOut = FVector::ZeroVector;
		float Seconds = 0.f;
		float Debt = 0.f;
	};
	TArray<FAedirCutPiece> CutPieces;
	float StumpDebt = 0.f;
	float DeadSeconds = 0.f;
	bool UsesSkinBlood();
	void UpdateSkinBlood();

	uint8 NextWoundId = 0;
};
