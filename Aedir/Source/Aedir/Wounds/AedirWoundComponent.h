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

	// The streams the blood has run down the skin in, each the way down the skin was while it ran, in the body's bind
	// pose (xyz) and how far it has got (w, 0 to 1 of a run's length): the body turned, fresh blood runs a new way from
	// the wound and the old stream stays where it ran.
	TArray<FVector4f, TInlineAllocator<4>> Streams;
	// A new way down the skin waits to have held this long before blood starts a stream down it (a body falling turns
	// through every way at once), with the blood shed meanwhile.
	FVector3f NewWay = FVector3f::ZeroVector;
	float NewWaySeconds = 0.f;
	float NewWayShed = 0.f;
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

	// How opaque a bruise is against the material's own measure: more, and less skin shows through it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0"))
	float BruiseOpacityScale = 1.5f;

	// How far the blood of a blow goes off the skin rather than along it, the way the blow drew over it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0", ClampMax="1"))
	float WoundSprayOutShare = 0.6f;

	// A blow's normal that lies less along the way off the body part than this is not believed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="-1", ClampMax="1"))
	float WoundOutwardLeastDot = 0.2f;

	// Blood on the skin is fresh, bright and wet when shed and darkens and dulls as it dries over this many game hours (a game
	// hour is three real minutes); while the wound still bleeds it stays at most BloodBleedingDryShare dried.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds")
	float BloodDryGameHours = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="1"))
	float BloodBleedingDryShare = 0.15f;

	// Blood runs from a wound down the skin the way gravity takes it as the body lies now: turned more than
	// BloodStreamTurnDegrees from the way it was running, fresh blood starts a new stream (at most BloodStreamsMax, the
	// shortest given up for a new one); turned less, the stream bends after it, BloodStreamFollowPerSecond of the way a
	// second. Where the skin is flatter to the ground than BloodStreamLeastSlope (a wound facing up or down) it does not run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="1", ClampMax="90"))
	float BloodStreamTurnDegrees = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="10"))
	float BloodStreamFollowPerSecond = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="1"))
	float BloodStreamLeastSlope = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="1", ClampMax="4"))
	int32 BloodStreamsMax = 4;

	// A new way down has to hold this long before fresh blood takes it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="5"))
	float BloodStreamSettleSeconds = 0.75f;

	// Blood from an open wound soaks into the clothes worn over it, the stain growing to ClothSoakShare of the wound's
	// severity (more the more it bleeds) and kept on the clothes; it washes off the body and out of the clothes in water,
	// WaterWashPerSecond of it each second, or all at once with WashBlood (a wash at a lavoir).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="1"))
	float ClothSoakShare = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds")
	float ClothStainRadiusShare = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds", meta=(ClampMin="0", ClampMax="1"))
	float WaterWashPerSecond = 0.35f;

	// Server only. Washes this share of the blood off the body and out of what it wears; blood still coming from a wound
	// covers it again.
	void WashBlood(float Share);

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

	// A bruise on the skin is this wide (its radius, in cm) from the lightest blow to the hardest.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt", meta=(ClampMin="0.5"))
	float BruiseRadiusMinCm = 4.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt", meta=(ClampMin="0.5"))
	float BruiseRadiusMaxCm = 10.f;

	// How far round a cut its blood is smeared on the skin, from the slightest to the worst (in cm), and round a stab.
	UPROPERTY(EditAnywhere, Category="Aedir|Wounds")
	FVector2D CutBloodRadiusMinMaxCm = FVector2D(1.5f, 4.5f);

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds", meta=(ClampMin="0.5"))
	float PunctureBloodRadiusCm = 1.5f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntBruiseJoules = 90.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntSplitJoules = 320.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Wounds|Blunt")
	float BluntWearHalfLifeSeconds = 120.f;

	// How much less blood (out of 100) a body has room for with a cut, a stab or a limb off, the worst of each, until it is
	// closed; all of them together no more than BloodRoomLostMax. Opened or opened again, the worst lets this much out at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0"))
	float CutBloodRoom = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0"))
	float PunctureBloodRoom = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0"))
	float SeverBloodRoom = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0", ClampMax="100"))
	float BloodRoomLostMax = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Wounds|Blood", meta=(ClampMin="0"))
	float WoundBloodAtOnce = 5.f;

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

	// Server only. A blow hard enough breaks a bone of a limb, and only what lies from there out goes limp: the hand or the
	// foot (1), from the forearm or the shin (2), the whole limb from the upper arm or the thigh (3). A hand that hangs lets
	// go of what it held; a leg broken above the ankle will not carry the body. A break further in counts over one further
	// out. Says whether it broke further in than it was.
	bool BreakLimb(EAedirLimb Limb, int32 Level = 3);
	// How far in the limb is broken: 0 whole, 1 the hand or foot, 2 the forearm or shin, 3 the upper arm or thigh.
	int32 GetBreakLevel(EAedirLimb Limb) const { return (BrokenLimbs >> (2 * static_cast<int32>(Limb))) & 3; }
	bool IsLimbBroken(EAedirLimb Limb) const { return GetBreakLevel(Limb) > 0; }
	// How far in a break on this bone of a limb reaches (1 to 3 as above), or 0 for a bone that is not of a limb.
	static int32 GetBoneBreakLevel(FName Bone);

	// Whether the arm on that side is of no use to raise, guard or strike with: gone, or broken from the forearm in.
	bool HasUselessArm(bool bLeft) const { return HasLostArm(bLeft) || GetBreakLevel(bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm) >= 2; }
	// Whether the hand on that side can hold nothing: gone, or broken anywhere from the hand in.
	bool HasUselessHand(bool bLeft) const { return HasLostArm(bLeft) || IsLimbBroken(bLeft ? EAedirLimb::LeftArm : EAedirLimb::RightArm); }

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

	// Every wound and bruise closed and every broken bone whole again, as if it had never been hurt; a limb cut off stays
	// off. Server only.
	void HealAll();

private:
	bool IsAuthoritative() const;
	bool IsDedicatedServer() const;
	USkeletalMeshComponent* GetBodyMesh() const;
	double GetWoundServerTime() const;
	float ComputeTotalBleedRate(double Now) const;
	float ComputeBloodRoomLost(double Now) const;
	uint8 AllocateWoundId();

	void UpdateBleeding(float DeltaTime);
	// Server only: blood soaking from the open wounds into the clothes over them, and washing off in water.
	void SoakClothes(float DeltaTime);
	float SoakTimer = 0.f;
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

	// How far in each limb is broken, two bits each in the order of EAedirLimb (see GetBreakLevel).
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
	void MulticastWoundSpray(FVector Location, FVector Direction, float Severity, EAedirWoundType Type, FVector Outward);

	// Which way is off the skin at a wound; At is brought onto the body.
	FVector FindWoundOutward(FName Bone, const FVector& Normal, const FVector& BlowWay, FVector& At) const;

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
	// Carries the blood shed since the last update down the skin the way it goes down now.
	void UpdateBloodStreams(FAedirWoundVisual& Visual, const FAedirWound& Wound, float Shed, float DeltaTime);
	void UpdateSkinBlood();

	uint8 NextWoundId = 0;
};
