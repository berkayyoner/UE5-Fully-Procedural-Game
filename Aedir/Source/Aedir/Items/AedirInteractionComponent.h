#pragma once

#include "CoreMinimal.h"
#include "Combat/AedirCombatTypes.h"
#include "Components/ActorComponent.h"
#include "Items/AedirItemTypes.h"
#include "AedirInteractionComponent.generated.h"

class AAedirCreature;
class AAedirItemActor;

USTRUCT(BlueprintType)
struct AEDIR_API FAedirHandSlot
{
	GENERATED_BODY()

	// Reaching: Item is the reach target. Holding: Item is the held item (shared by both hands for two-handed items).
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	EAedirHandState State = EAedirHandState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	TObjectPtr<AAedirItemActor> Item;

	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachElapsed = 0.f;

	// Simulated hand position moving from the hand bone toward the reach target at ReachSpeed.
	UPROPERTY(BlueprintReadOnly, Category="Aedir|Interaction")
	FVector ReachHandWorld = FVector::ZeroVector;
};

// How far the body bends down for something low.
struct FAedirReachPosture
{
	float LowerCm = 0.f;
	float LeanDegrees = 0.f;
	float HipForwardCm = 0.f;
	// Sideways shift of the pelvis and twist of the spine towards an item off to the side. Positive is to the right.
	float HipSideCm = 0.f;
	float TwistDegrees = 0.f;
};

class UPrimitiveComponent;

// A hand closed on a body or a piece of one, held to it by a spring.
struct FAedirBodyGrab
{
	TWeakObjectPtr<class UPhysicsConstraintComponent> Joint;
	TWeakObjectPtr<UPrimitiveComponent> Target;
	FName Bone = NAME_None;
	FVector LocalPoint = FVector::ZeroVector;
	float TearSeconds = 0.f;
	// Reaching for it, not yet holding it.
	bool bReaching = false;
	float ReachSeconds = 0.f;
	// Holding it, the arm carries what it holds by this joint to the holder's own body, to Where (in the holder's frame, not
	// scaled), as hard as the arm can; how far it is behind that, how hard it is pulled (N), and whether the hand lifts.
	TWeakObjectPtr<class UPhysicsConstraintComponent> Carry;
	FVector Where = FVector::ZeroVector;
	FVector WhereWorld = FVector::ZeroVector;
	// How far past the arm's own reach it was taken hold of (the middle of a trunk is further in than the hand gets).
	float BeyondReachCm = 0.f;
	float LagCm = 0.f;
	float PullN = 0.f;
	// How much of what it holds the hand bears just now (kg).
	float BorneKilos = 0.f;
	bool bLifting = false;
	float LiftSeconds = 0.f;
	float NextLog = 0.f;
	// Where this hand is from the middle of the hands that lift, about the way they are taken (ahead, right, up).
	FVector LiftOffset = FVector::ZeroVector;
	// How fast the place it is carried to goes (cm/s).
	FVector WhereVelocity = FVector::ZeroVector;
};

UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// For the dev tools: where the hand is being taken by a reach, a draw or a stow, and how far that has taken over.
	FVector DevGetReachTarget(EAedirHand Hand) const { return ReachTargetCache[Hand == EAedirHand::Left ? 0 : 1]; }
	float DevGetReachBlend(EAedirHand Hand) const { return ReachBlend[Hand == EAedirHand::Left ? 0 : 1]; }
	UAedirInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float PickupRange = 150.f;

	// Distance in cm from the hand bone to where it reaches for on the item that counts as a grab.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float GrabDistance = 22.f;

	// What is not a weapon (or a shield) has no handle: the hand goes to the nearest point of the thing's surface and takes
	// hold of it there, as it lies. The hand bone is the wrist, so it stops this far off the surface for the palm to lie on
	// it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float SurfaceGripWristCm = 7.f;

	// Whether the hand takes hold of the item anywhere on its surface rather than by a handle.
	bool UsesSurfaceGrip(const AAedirItemActor* Item) const;

	// Speed in cm/s of the hand moving toward the reach target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachSpeed = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ReachTimeout = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float UseHoldTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	float ThrowSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName LeftHandBone = FName(TEXT("hand_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName RightHandBone = FName(TEXT("hand_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FName WaistBone = FName(TEXT("pelvis"));

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReach(EAedirHand Hand);

	// A weapon held in both hands goes at once into the other hand, the one already on its grip, leaving FromHand empty.
	bool HandOverNow(EAedirHand FromHand);

	// A free hand takes hold of the nearest body or loose part of one in front of it, living, dead or cut off. False when there is none.
	bool TryGrabBody(EAedirHand Hand, const FVector* AimedPoint = nullptr);

	// Where the eyes meet a body or a piece of one within reach, if they do.
	bool FindAimedBody(FVector& OutPoint) const;
	void ReleaseBodyGrab(EAedirHand Hand);
	bool IsGrabbingBody(EAedirHand Hand) const;
	// Its hand closed on it, not only on its way there.
	bool IsHoldingBody(EAedirHand Hand) const;

	// What a hand can pull, hold and carry of somebody else's body (N): so much, and so much more for each point of the
	// holder's strength, a share of it with no breath left. Lifting (the hand's mouse button held) it strains to the second
	// pair and pays breath for it, the more the harder it pulls. A grown man weighs some 800 N: both hands of an ordinary
	// man lift one, hard; one hand of a strong man does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyHoldForceN = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyHoldForcePerStrengthN = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyLiftForceN = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyLiftForcePerStrengthN = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0", ClampMax="1"))
	float BodyForceTiredShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyLiftStaminaPerSecond = 16.f;

	// What a body held weighs on the holder: this share of what its hands bear counts as carried, slowing it as a load does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0", ClampMax="1"))
	float BodyBorneLoadShare = 0.6f;

	// The arm pulls with all it has once what it holds is this far behind where it would have it (cm), and is damped by this
	// share of that stiffness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="1"))
	float BodyHoldFullCm = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyHoldDampingShare = 0.12f;

	// A body that does not stand is drawn to the hand as a weight on a spring of this stiffness (1/s2), as if never further
	// behind than this (cm); its whole weight is borne once it is half BodyPullBearCm below where the hand would have it. No
	// single part of it is given more speed in a step than BodyPullPartSpeedCm (cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyPullPerSecondSq = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyPullFullCm = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="1"))
	float BodyPullBearCm = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyPullPartSpeedCm = 150.f;

	// The share of the pull that reaches the whole body at once (the rest goes to the part held and what it hangs on).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0", ClampMax="1"))
	float BodyPullSpreadShare = 0.5f;

	// Not lifting, a hand bears no more than this share of the weight of what lies: it drags it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0", ClampMax="1"))
	float BodyDragBearShare = 0.4f;

	// Lifting, the mouse takes what is held up, down and to the sides, about the shoulders, at the arms' length: so many mouse
	// units from the middle to the furthest across and up or down, and this far round to each side, up and down (degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="1"))
	float LiftMouseAcross = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="1"))
	float LiftMouseUpDown = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="150"))
	float LiftSideDegrees = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="89"))
	float LiftUpDegrees = 65.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="89"))
	float LiftDownDegrees = 72.f;

	// At the end of its way across, the mouse turns the holder round on, with what is held (degrees of view for each unit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftEdgeTurnShare = 0.6f;

	// How far up the mouse's way it starts at the least (-1 the lowest, 1 the highest), and how fast what is held is taken to
	// where the mouse has it (cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="-1", ClampMax="1"))
	float LiftStartLeast = -0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftSpeedCm = 560.f;

	// What is lifted goes the slower the heavier it is for each hand: at LiftSpeedCm for nothing, at half of it for about
	// three times this weight (kg).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="1", Units="kg"))
	float LiftSpeedKilos = 14.f;

	// Lifting, the arms bear this share of their strength with what they hold right over the shoulder, and this share with it
	// held straight out before it (in between, the less the higher and the further out).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0.1", ClampMax="1"))
	float LiftOverheadShare = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0.1", ClampMax="1"))
	float LiftHeldOutShare = 0.75f;

	// Swung, the arms and the back put this much more into it than they could hold up at rest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="1"))
	float LiftSwingForceShare = 1.15f;

	// Lifting, the weight is drawn by a stiffer spring (1/s2), as if never further behind than this (cm), and goes along with
	// this share of the speed of the place it is taken to: what lets it be swung and flung.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftPullPerSecondSq = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftPullFullCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="1"))
	float LiftFollowShare = 0.85f;

	// The share of that pull that reaches the trunk all at once; the rest goes through the part held, and the limbs hang.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="1"))
	float LiftSpreadShare = 0.3f;

	// Where the shoulders are, standing: this share of the capsule's height up and this far ahead (cm). What is held is at most
	// this far past the wrists (cm); all the way down the arms reach this much further, the body bending, no lower than the last (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0", ClampMax="1"))
	float LiftShoulderShare = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift")
	float LiftShoulderAheadCm = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftBeyondMostCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftLowBendCm = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float LiftLowestCm = 12.f;

	// The two hands are kept no further than this each from the middle between them (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies|Lift", meta=(ClampMin="0"))
	float BodyLiftHandsApartCm = 25.f;

	// The mouse moved while a hand lifts (mouse axis units, up positive).
	void AddLiftMouse(const FVector2D& Delta);
	bool IsLiftingBody() const { return BodyGrabs[0].bLifting || BodyGrabs[1].bLifting; }

	// Let go of a body that does not stand, the holder's own body stays passable to it this long (seconds).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyThroughSeconds = 1.f;

	// Carried or lifted, a body is kept out of the holder's own: its parts come no nearer to the holder's trunk, head and legs
	// than this past where they touch (cm), and are pushed back out at up to this speed (cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyKeptOutCm = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyKeptOutSpeedCm = 250.f;

	// Let hang, a hand brings what it holds of a body lying down up to this far before it, out to its side and over the feet
	// (cm, a body of the usual size), at this speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies")
	FVector BodyCarryPlaceCm = FVector(40.f, 18.f, 78.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyCarrySpeedCm = 70.f;

	// What is held is torn out of the hand once it is this much further from the shoulder than the arm is long, for this long.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyHoldTearCm = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyHoldTearSeconds = 0.3f;

	// Somebody standing is lifted off their feet once the hands that lift have pulled up at them past this (cm) for this
	// long with more than their weight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyLiftOffCm = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction|Bodies", meta=(ClampMin="0"))
	float BodyLiftOffSeconds = 0.35f;

	// A hand holding a dead body takes off the armour worn where it holds it and keeps it in that hand. False when there is
	// none there, or the body is not dead.
	bool StripGrabbedArmor(EAedirHand Hand);
	// A hand closing on the body strips it once it is this near the place (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Interaction")
	float StripReachCm = 35.f;
	// Who the hand holds, if it holds a body, and by which bone.
	AAedirCreature* GetGrabbedCreature(EAedirHand Hand, FName* OutBone = nullptr) const;
	// The worn piece covering the part of the body this bone is in, if any.
	AAedirItemActor* FindWornOn(FName Bone) const;
	// Takes a worn piece off the body as it is, to be handed to someone: it lies loose, nowhere stowed.
	void TakeOffWornLoose(AAedirItemActor* Item);
	bool GetBodyGrabPoint(EAedirHand Hand, FVector& OutWorld) const;

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void StartReachForItem(EAedirHand Hand, AAedirItemActor* Item);


	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void CancelReach(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropHeld(EAedirHand Hand);

	// Swaps which hand grips a two-handed weapon higher: the upper hand goes to the pommel and the lower one moves up.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void SwapGripOrder();

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void UseHeld(EAedirHand Hand);

	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void HolsterToWaist(EAedirHand Hand);

	// Takes the item of one belt slot in the given hand, whichever side of the belt it hangs on. At once while the game is paused.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DrawWaistSlot(EAedirHand Hand, int32 SlotIndex);

	// The belt slot of the weapon to draw first, the one put away last if it is still there. INDEX_NONE without a weapon on the belt.
	int32 FindWaistWeaponSlot() const;

	// True if that hand can take the item of that belt slot now.
	bool CanDrawWaistSlot(EAedirHand Hand, int32 SlotIndex) const;

	// Takes a stowed item: the hand goes to the belt as for a drawn weapon, and the item is in it when it comes back.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DrawFromBackpack(int32 Index, EAedirHand Hand);

	// Brings food or drink in that hand to the mouth while held. It is eaten or drunk there, a bite at a time.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void SetMouthHold(EAedirHand Hand, bool bHold);

	bool IsMouthHold(EAedirHand Hand) const { return (MouthHands & (1 << static_cast<int32>(Hand))) != 0; }

	// How the wrist holds food or drink, at rest or at the mouth.
	bool GetConsumableAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const;

	// Server only. A new item straight into the backpack.
	void StowNewItem(AAedirItemActor* Item);

	// Server only. Takes an item out of the hands, belt or backpack and destroys it.
	void RemoveOwnedItem(AAedirItemActor* Item);

	// A one-handed weapon goes to the empty other hand, which reaches for it first. False if there is nothing to pass.
	bool PassToOtherHand();

	// True while a hand is on its way to the belt.
	bool IsHolsterMoving() const;

	// How the wrist turns an item on its way to the belt.
	bool GetHolsterAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutShare) const;

	// On its way to take an item off the belt: how that item hangs there, and how far the hand has turned to it.
	bool GetDrawAim(EAedirHand Hand, FVector& OutDirectionWorld, float& OutAlpha) const;

	// True while the other hand is coming to take the weapon this hand holds.
	bool IsPassingHeld(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	EAedirHandState GetHandState(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	AAedirItemActor* GetHeldItem(EAedirHand Hand) const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetCarriedWeight() const;

	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	AAedirItemActor* GetHighlightedItem() const;

	// True while the hand is reaching or still blending out after a grab or cancel.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	bool GetReachPose(EAedirHand Hand, FVector& OutTargetWorld, float& OutAlpha) const;

	// Positive number of cm the pelvis should go down, smoothed.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetPelvisLowerZ() const;

	// Forward bend of the spine in degrees while reaching low, smoothed.
	float GetReachLeanDegrees() const { return ReachLeanDegrees; }

	// Forward shift of the pelvis in unscaled cm while reaching low, negative when it sits back. Smoothed.
	float GetReachHipForward() const { return ReachHipForward; }

	// Sideways shift of the pelvis in unscaled cm and twist of the spine in degrees while reaching low. Smoothed.
	float GetReachHipSide() const { return ReachHipSide; }
	float GetReachTwistDegrees() const { return ReachTwistDegrees; }

	// The body posture that brings the shoulder within arm's reach of a point. Forward and lateral are relative to the shoulder's
	// side of the body, the height is above the floor. All in unscaled cm.
	static FAedirReachPosture ComputeReachPosture(float ForwardCm, float LateralCm, float ItemHeightCm);

	// Server only. Equips a freshly spawned item into a hand without a reach phase.
	bool EquipImmediate(EAedirHand Hand, AAedirItemActor* Item);

	// Server only. Stores a freshly spawned item in waist slot 0-5 without a reach phase.
	bool HolsterImmediate(int32 SlotIndex, AAedirItemActor* Item);

	// Server only. Clears both hands and every waist slot. The items themselves are left untouched.
	void ReleaseAllItems();

	TArray<AAedirItemActor*> GetWaistItemsCopy() const;

	// Server only. Takes an armor item off the hand or waist, attaches it to its bone and adds it to the worn list.
	// FromHand is the hand the item is taken from. Returns false when the item cannot be worn.
	bool WearItem(AAedirItemActor* Item, EAedirHand FromHand);

	// Stows a held or holstered item in the backpack. Clients forward the request to the server.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void AddToBackpack(AAedirItemActor* Item);

	// Takes the backpack entry into a free hand.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void TakeFromBackpack(int32 Index, EAedirHand Hand);

	// Drops the backpack entry on the ground in front of the creature.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropFromBackpack(int32 Index);

	// Drops the holstered item of waist slot 0-5.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void DropFromWaist(int32 SlotIndex);

	// Takes off a worn piece of armour, by its place among the worn items, and puts it in the backpack.
	UFUNCTION(BlueprintCallable, Category="Aedir|Interaction")
	void TakeOffWorn(int32 WornIndex);

	TArray<AAedirItemActor*> GetWornItemsCopy() const;

	// Entries keep their backpack index, destroyed items show up as null.
	TArray<AAedirItemActor*> GetBackpackItemsCopy() const;

	// Highest protection of the worn items covering the zone, range 0..0.9.
	UFUNCTION(BlueprintPure, Category="Aedir|Interaction")
	float GetWornArmorProtection(EAedirBodyZone Zone) const;

	// What the worn armour does to a blow of this kind landing on this bone at this point: the piece that takes the most of it,
	// or nothing where the body is bare.
	FAedirArmorHit GetArmorAt(FName Bone, const FVector& Point, EAedirDamageType Type) const;
	bool HasWornArmor() const { return !WornItems.IsEmpty(); }

	// A blow knocks a worn piece off the body: it flies off with this speed and lies where it falls.
	void KnockOffWorn(AAedirItemActor* Item, const FVector& Velocity);

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot LeftSlot;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	FAedirHandSlot RightSlot;

	// Left side entries are indices 0-2, right side entries are indices 3-5.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> WaistItems;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> WornItems;

	// Stowed items: hidden, without collision, counted in the carried weight.
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Interaction")
	TArray<TObjectPtr<AAedirItemActor>> Backpack;

private:
	void UpdateBodyGrabs(float DeltaTime);
	void CloseHandOnBody(EAedirHand Hand);
	// Where the hand would have what it holds, in the world, no further from its shoulder than the arm reaches.
	FVector UpdateBodyCarryPlace(EAedirHand Hand, const FVector& Point, float DeltaTime);
	void UpdateBodyLiftOff();
	// Whether what the hand holds is somebody alive and on their feet.
	bool IsStandingHeld(const FAedirBodyGrab& Grab) const;
	// Pulls a body that does not stand, or a piece of one, the way the hand would have it; gives back how hard (N).
	float PullBody(const FAedirBodyGrab& Grab, const FVector& Point, const FVector& Lag, float MostN, float& OutBorneKilos) const;
	// Pushes the parts of a body held out of the holder's own trunk, head and legs.
	void KeepHeldOutOfHolder(const FAedirBodyGrab& Grab) const;
	// Where the mouse has what the hands lift: across (right positive) and up, each -1 to 1.
	FVector2D LiftAim = FVector2D::ZeroVector;

	FAedirBodyGrab BodyGrabs[2];

	void BeginReach(EAedirHand Hand, AAedirItemActor* Item);
	void GrabItem(EAedirHand Hand, AAedirItemActor* Item);
	// Moves an item so the spot on it the hand went for is where the hand is now.
	void BringIntoHand(EAedirHand Hand, AAedirItemActor* Item, const FVector& SpotWorld) const;
	void UpdateReach(EAedirHand Hand, float DeltaTime);
	void UpdateReachBlend(float DeltaTime);
	void UpdatePelvisLower(float DeltaTime);
	void UpdateHighlight();
	void UpdateCarriedWeight();
	void StoreHeldToWaist(EAedirHand Hand, AAedirCreature* Creature);
	void DrawFromWaist(EAedirHand Hand, AAedirCreature* Creature, int32 SourceIndex);
	void ReleaseHeldItem(const AAedirItemActor* Item);
	void ClearSlotIfHolding(FAedirHandSlot& Slot, const AAedirItemActor* Item);

	FAedirHandSlot& GetSlot(EAedirHand Hand);
	const FAedirHandSlot& GetSlot(EAedirHand Hand) const;
	AAedirItemActor* FindBestItem() const;
	AAedirCreature* GetCreature() const;
	FVector GetHandWorld(EAedirHand Hand) const;
	bool IsAuthoritative() const;

	UFUNCTION(Server, Reliable)
	void ServerStartReachForItem(EAedirHand Hand, AAedirItemActor* Item);

	UFUNCTION(Server, Reliable)
	void ServerCancelReach(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDropHeld(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerSwapGripOrder();

	UFUNCTION(Server, Reliable)
	void ServerUseHeld(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerHolsterToWaist(EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDrawWaistSlot(EAedirHand Hand, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerPassToOtherHand();

	UFUNCTION(Server, Reliable)
	void ServerHandOverNow(EAedirHand FromHand);

	UFUNCTION(Server, Reliable)
	void ServerDrawFromBackpack(int32 Index, EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerSetMouthHold(EAedirHand Hand, bool bHold);

	void UpdateMouth(EAedirHand Hand, float DeltaTime);
	FVector GetConsumableHandTarget(EAedirHand Hand, const AAedirItemActor* Item) const;
	FVector GetMouthWorld() const;
	float GetMouthShare(EAedirHand Hand) const;

	UPROPERTY(Replicated)
	uint8 MouthHands = 0;

	float MouthElapsed[2] = { 0.f, 0.f };

	// A piece of armour is put on by the hand that holds it: carried to where it is worn, and fastened there once it arrives.
	UPROPERTY(Replicated)
	uint8 DonHands = 0;
	float DonElapsed[2] = { 0.f, 0.f };
	bool IsDonning(EAedirHand Hand) const { return (DonHands & (1 << static_cast<int32>(Hand))) != 0; }
	void UpdateDon(EAedirHand Hand, float DeltaTime);
	FVector GetDonHandTarget(EAedirHand Hand, const AAedirItemActor* Item) const;
	float BiteElapsed[2] = { 0.f, 0.f };

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> BackpackDrawItem[2];

	UFUNCTION(Server, Reliable)
	void ServerAddToBackpack(AAedirItemActor* Item);

	UFUNCTION(Server, Reliable)
	void ServerTakeFromBackpack(int32 Index, EAedirHand Hand);

	UFUNCTION(Server, Reliable)
	void ServerDropFromBackpack(int32 Index);

	UFUNCTION(Server, Reliable)
	void ServerDropFromWaist(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerTakeOffWorn(int32 WornIndex);

	UFUNCTION(Server, Reliable)
	void ServerStripGrabbedArmor(EAedirHand Hand);

	UPROPERTY(Transient)
	TObjectPtr<AAedirItemActor> HighlightedItem;

	float HighlightTimer = 0.f;
	float ReachBlend[2] = { 0.f, 0.f };
	// How close each reaching hand has come to its target, and how long it has gone without coming closer.
	float ReachBestGap[2] = { 0.f, 0.f };
	float ReachStallSeconds[2] = { 0.f, 0.f };
	FVector ReachTargetCache[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float PelvisLowerZ = 0.f;
	float ReachLeanDegrees = 0.f;
	float ReachHipForward = 0.f;
	float ReachHipSide = 0.f;
	float ReachTwistDegrees = 0.f;

	// The hand whose key started a reach. It takes the upper grip on a weapon held in both hands.
	EAedirHand ReachLeadHand = EAedirHand::Right;

	FVector GetReachTargetWorld(EAedirHand Hand, const AAedirItemActor* Item) const;
	bool CanLift(const AAedirItemActor* Item) const;
	// A hand whose arm is broken or gone takes and holds nothing.
	bool IsHandUseless(EAedirHand Hand) const;
	void UpdateObjectStrain(float DeltaTime);

	// The hand goes to the belt first, and the item changes place when it gets there.
	void BeginHolsterMotion(EAedirHand Hand);
	void StartHolsterPath(EAedirHand Hand);
	void UpdateHolsterMotion(EAedirHand Hand, float DeltaTime);
	int32 FindFreeWaistIndex(EAedirHand Hand) const;
	int32 FindDrawWaistIndex(EAedirHand Hand) const;
	void GetWaistSlot(int32 WaistIndex, const AAedirItemActor* Item, FVector& OutSlotOnBone, FQuat& OutRotationOnBone, float& OutPivotCm) const;
	FVector GetWaistSlotWorld(int32 WaistIndex, const AAedirItemActor* Item) const;
	FVector GetTossVelocity(const AAedirItemActor* Item, float SpeedShare) const;

	enum class EAedirHolsterMotion : uint8 { None, Store, Draw };
	EAedirHolsterMotion HolsterMotion[2] = { EAedirHolsterMotion::None, EAedirHolsterMotion::None };
	float HolsterElapsed[2] = { 0.f, 0.f };
	float HolsterDrawSettle[2] = { 0.f, 0.f };
	FVector HolsterDrawFrom[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector HolsterStartLocal[2] = { FVector::ZeroVector, FVector::ZeroVector };
	int32 HolsterWaistIndex[2] = { INDEX_NONE, INDEX_NONE };

	// The hand is reaching for a weapon in the other hand, to take it over.
	bool bReachingToTakeOver[2] = { false, false };
	float HoldStrainSeconds = 0.f;
	int32 LastStoredWaistIndex[2] = { INDEX_NONE, INDEX_NONE };
};
