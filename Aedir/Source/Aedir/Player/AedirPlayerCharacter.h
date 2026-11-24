#pragma once

#include "CoreMinimal.h"
#include "Creature/AedirCreature.h"
#include "Net/AedirSaveGame.h"
#include "Npc/AedirNpc.h"
#include "Engine/TimerHandle.h"
#include "Items/AedirItemTypes.h"
#include "AedirPlayerCharacter.generated.h"

class UAedirWeatherEffectsComponent;
class UCameraComponent;
class USpringArmComponent;
class UAedirMenuSubsystem;

UCLASS(Blueprintable)
class AEDIR_API AAedirPlayerCharacter : public AAedirCreature
{
	GENERATED_BODY()

public:
	AAedirPlayerCharacter(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float ThirdPersonArmLength = 150.f;

	// The mouse wheel moves the camera between these distances.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomMinCm = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomMaxCm = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraZoomStepCm = 22.f;

	float CameraZoomTargetCm = 150.f;

	// Height of the camera boom pivot above the capsule centre while standing.
	float CameraBoomBaseZ = 60.f;

	// Drops a few catalog items in front of the player at the start, in non-shipping builds, so grab and inventory can be tested.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dev")
	bool bSpawnTestItemsOnStart = true;

	// How quickly the camera pivot follows the body, how much of the pelvis height motion it takes over, and the ragdoll follow settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraBodyFollowSpeed = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraPelvisInfluence = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollFollowSpeed = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollRaiseCm = 45.f;

	// Lying on the ground the view swings round behind the body, at this pitch and speed, once the mouse has been still this long.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollPitch = -28.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollTurnSpeed = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraRagdollFollowAfterSeconds = 0.8f;

	float LastLookInputSeconds = 100.f;

	// Whether holding a guard locks the view on a target; Caps Lock turns it on and off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	bool bLockOnEnabled = true;

	// How far, and how far off the line of sight, a creature can be to be locked on to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnRangeCm = 1100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnConeDegrees = 35.f;

	// When the one locked on to dies, the lock goes over to the creature nearest to where the view looks, this far round.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnSwitchConeDegrees = 90.f;

	// How quickly the view turns to the locked target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnTurnSpeed = 7.f;

	TWeakObjectPtr<class AAedirCreature> LockOnTarget;
	// The place looked at on a target lying down, followed smoothly, and the one it belongs to.
	FVector LockOnDownAim = FVector::ZeroVector;
	TWeakObjectPtr<class AAedirCreature> LockOnDownAimOf;

	// The capsule put somewhere at once, further than it moved by its own speed and this much more (as when it is set
	// under a body that has got up), leaves the view where it was in the world and eases it over from there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float CameraTeleportCm = 10.f;

	FVector CameraRelative = FVector::ZeroVector;
	FVector CameraPelvisBaseline = FVector::ZeroVector;
	FTransform CameraActorLast = FTransform::Identity;
	bool bCameraStateInitialized = false;

	// Share of the mouse movement that still turns the camera while a guard is held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightCameraMouseScale = 0.015f;

	// Blend of the subtle fight-stance camera drift, 0 outside the stance.
	float FightCameraAlpha = 0.f;
	// How far the view has drawn back for the fight, 0 to 1.
	float FightPullBackAlpha = 0.f;
	FRotator FightCameraSway = FRotator::ZeroRotator;

	// The view's kick: a turn away (pitch, yaw, roll) on a soft spring that sways back through still once or twice and
	// settles, and how fast it is turning.
	FVector ViewKick = FVector::ZeroVector;
	FVector ViewKickSpeed = FVector::ZeroVector;

	// The kick's spring: how often it would sway back and forth, and how quickly that dies away (1 never past still).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake", meta=(ClampMin="0.3", Units="Hz"))
	float ViewKickHz = 2.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake", meta=(ClampMin="0.05", ClampMax="1"))
	float ViewKickDamping = 0.42f;

	// Hurt by a blow the view shakes, the more the more harm it did: Min degrees for the least, Max from HurtShakeFullDamage on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float HurtShakeDegreesMin = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float HurtShakeDegreesMax = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float HurtShakeFullDamage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float HurtShakeSeconds = 0.45f;

	// A blow that does less harm than this, taken or landed, does not shake the view at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float ShakeMinDamage = 0.5f;

	// Landing a blow of one's own with a fist, a foot or a weapon in hand shakes it a little too, for the weight of the hit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float StrikeShakeDegreesMin = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float StrikeShakeDegreesMax = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float StrikeShakeFullDamage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera|Shake")
	float StrikeShakeSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector ThirdPersonShoulderOffset = FVector(0.f, 26.f, 10.f);

	// In the fight stance the view comes in close over the right shoulder at about head height and looks at the enemy across,
	// not down on him: a view looking down makes the hand pull the mouse up to see.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightArmLengthCm = 155.f;

	// Going into a fight, a weapon's stance or the fists raised, the view draws back this far, eased in at
	// FightCameraPullBackSpeed, so more of the fight is seen and it feels quicker.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightCameraPullBackCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightCameraPullBackSpeed = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector FightShoulderOffset = FVector(0.f, 42.f, 2.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightViewPitch = -10.f;

	// Locked on, the view looks from the player's head at this much below the enemy's head, tipped by
	// LockOnPitchLiftDegrees (down when negative), never steeper down than LockOnSteepestPitch nor higher up than
	// LockOnHighestPitch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnAimBelowHeadCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnPitchLiftDegrees = -4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnHighestPitch = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnLevelDistanceCm = 220.f;

	// Locked on to one lying down, the view turns to the middle of the body, not to the head thrown about with it, and turns
	// from the player's own feet; nearer than this across the ground it keeps its way and only tips (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnStillYawCm = 80.f;

	// How quickly the place looked at on a body lying down follows it (per second).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnDownAimFollowSpeed = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float LockOnSteepestPitch = -35.f;

	// Z: the camera as in Half Sword, close behind the head and moving with it, looking level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float HeadCameraArmLengthCm = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	FVector HeadCameraOffset = FVector(0.f, 18.f, 12.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float HeadCameraFollowSpeed = 14.f;

	// V held: the camera goes round the body as the mouse moves it, the body, its view and the arms left as they are; let go,
	// it comes back. A tap shorter than this turns the camera back straight at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float ViewTapSeconds = 0.4f;

	// A tap may still move the mouse a little; only a turn of the camera further than this, in degrees, makes it a hold.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float ViewTapMaxTurnDegrees = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float OrbitReturnSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Camera")
	float FightViewPitchSpeed = 3.f;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Weather")
	TObjectPtr<UAedirWeatherEffectsComponent> WeatherEffects;

	// The player's hair: strands grown on the face's head, bound to it (HairBinding), swinging under their own physics.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Look")
	TObjectPtr<class UGroomComponent> Hair;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	FSoftObjectPath HairGroom = FSoftObjectPath(TEXT("/Game/Grooms/SC_Hairstyle_Male_11/SC_Hairstyle_Male_11/SC_Hairstyle_Male_11.SC_Hairstyle_Male_11"));

	// Its colour: how dark (0 fair, 1 black) and how red.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look", meta=(ClampMin="0", ClampMax="1"))
	float HairMelanin = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look", meta=(ClampMin="0", ClampMax="1"))
	float HairRedness = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	FSoftObjectPath HairBinding = FSoftObjectPath(TEXT("/Game/Grooms/SC_Hairstyle_Male_11/GB_AedirMan_Hairstyle11.GB_AedirMan_Hairstyle11"));

	// The look of the hair's strands, coloured by HairMelanin and HairRedness; it takes the place of the groom's own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	FSoftObjectPath HairMaterial = FSoftObjectPath(TEXT("/Game/Grooms/Male_ClassicCrewCut/Male_ClassicCrewCut/Materials/MI_Hair.MI_Hair"));

	// Under a helmet the hair is not drawn: it would stand out through the metal.
	void UpdateHairUnderHelmet();
	FTimerHandle HairHelmetTimer;

	// Bare-handed fighting, rebuilt step by step (see UAedirFistArms).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aedir|Fists")
	TObjectPtr<class UAedirFistArms> FistArms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Player")
	float MinViewPitch = -80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Player")
	float MaxViewPitch = 80.f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Server only. Applies the chosen race and posts the starter quest.
	void HandleTalkPressed();
	void SendGuestCharacter();
	void StoreGuestCharacterNow();
	FTimerHandle GuestStoreTimer;

	UFUNCTION(Server, Reliable)
	void ServerBringCharacter(const FAedirCreatureSave& Character);

	UFUNCTION(Server, Reliable)
	void ServerNpcAction(AAedirNpc* Npc, EAedirNpcAction Action);

	UFUNCTION(Server, Reliable)
	void ServerChooseRace(FName ChosenRaceId);

	// Opens the race picker on the owning client when the game mode asks for it.
	void MaybeOpenCharacterCreation();

	// Console commands. Commands that change the world are sent to the server in co-op; ListItems, DebugBody, BloodTest and DevHelp run locally.
	UFUNCTION(Exec)
	void SpawnItem(const FString& ItemId, const FString& Mode);

	UFUNCTION(Exec)
	void ListItems();

	UFUNCTION(Exec)
	void SpawnEnemy(int32 Count, int32 Armed, float DistanceCm = 0.f);

	UFUNCTION(Exec)
	void KillAll();

	UFUNCTION(Exec)
	void SetWeatherType(const FString& WeatherName);

	UFUNCTION(Exec)
	void Heal();

	UFUNCTION(Exec)
	void Hurt(float Amount);

	UFUNCTION(Exec)
	void God();

	UFUNCTION(Exec)
	void GiveSkill(const FString& SkillName, float Amount);

	UFUNCTION(Exec)
	void CharacterCreation();

	// Ids are case-insensitive: Human, Elf, DarkElf, Dwarf.
	UFUNCTION(Exec)
	void ChooseRace(const FString& RaceName);

	UFUNCTION(Exec)
	void Wound(const FString& WoundType, const FString& Bone);

	UFUNCTION(Exec)
	void BloodTest(const FString& BloodArgs = TEXT(""));

	UFUNCTION(Exec)
	void DebugBody();

	UFUNCTION(Exec)
	void DevHelp();

	// SetStat Strength 80: change a stat and watch how the body reacts.
	UFUNCTION(Exec)
	void SetStat(const FString& StatName, float Value);

	UFUNCTION(Exec)
	void ShowStats();

	// Push 0.5: shove the balance forward, 1 and above knocks the body down.
	UFUNCTION(Exec)
	void Push(float Strength);

	UFUNCTION(Exec)
	void Knockdown(const FString& Args);

	// HeadBlow <share>: a blow to the head taking that share of the full health. DieNow: the player dies at once. LookInfo:
	// how the view is graded and how dark and grey it is now.
	UFUNCTION(Exec)
	void HeadBlow(const FString& Args);

	UFUNCTION(Exec)
	void DieNow();

	UFUNCTION(Exec)
	void LookInfo();

	// Diagnostics: logs gait, balance and reach state for the given number of seconds.
	UFUNCTION(Exec)
	void GaitInfo(float Seconds, float Interval);

	// Diagnostics: where the named bones' axes point on the standing body.
	UFUNCTION(Exec)
	void BoneAxes(const FString& Bones);

	// Diagnostics: a flash in the clouds ahead.
	UFUNCTION(Exec)
	void CloudFlash();

	UFUNCTION(Exec)
	void AutoMove(float Forward, float Right, int32 Run, float TurnDegrees);

	UFUNCTION(Exec)
	void ShotCam(const FString& CameraArgs);

	// Every rendered frame is written to the screenshots folder while on (1), for a video of what the body does.
	UFUNCTION(Exec)
	void RecordFrames(int32 On);

	// A lightning bolt strikes nearby now; given a distance, that far straight ahead of the view instead.
	UFUNCTION(Exec)
	void LightningNow(float AheadCm = 0.f);

	// Logs the collision filter of every physical shape of the hands, welded weapons included.
	UFUNCTION(Exec)
	void ShapeInfo();

	// Takes a blow on the chest from the front, or on the back from behind, to see how the body answers it.
	UFUNCTION(Exec)
	void StrikeSelf(float EnergyJoules, float MassKg, float SpeedCmPerSec, int32 FromBehind = 0);

	// A blow on one bone, this far to the body's right (cm, left negative) and this far in front of it (cm; behind, negative,
	// it comes from behind): to oneself, or with Nearest 1 to the nearest other body.
	UFUNCTION(Exec)
	void StrikePart(const FString& Bone, float EnergyJoules, float SideCm = 0.f, int32 Nearest = 0, float AheadCm = 12.f);

	// The right hand takes hold of the nearest dead body by this bone (it has to lie within reach), and with Strip 1 takes off what is worn there into it,
	// as holding the hand's key down does.
	UFUNCTION(Exec)
	void GrabDead(const FString& Bone, int32 Strip = 0);

	// The same blow on the nearest other creature, from the front or from behind.
	UFUNCTION(Exec)
	void StrikeNearest(float EnergyJoules, int32 FromBehind = 0);

	// Stands the body on the ground at a point given in metres from the middle of the map, facing a yaw.
	UFUNCTION(Exec)
	void WarpTo(float XMetres, float YMetres, float Yaw);

	UFUNCTION(Exec)
	void PlaceItem(const FString& ItemArgs);

	UFUNCTION(Exec)
	void PoseArm(const FString& PoseArgs);

	// Logs the mean and the worst frame over that many seconds, and how long the game thread, the drawing thread and the
	// graphics card took on average: for a look at what a setting costs.
	UFUNCTION(Exec)
	void FrameProbe(float Seconds, const FString& Label);

	// Worn clothes swing as cloth (1) or only follow the bones (0), as the Cloth physics setting does.
	UFUNCTION(Exec)
	void ClothOn(int32 bOn);

	UFUNCTION(Exec)
	void OffHandFree(const FString& FreeArgs);

	UFUNCTION(Exec)
	void HitMe(const FString& HitArgs);

	// SeverMe <bone>: cuts off the player's limb at that bone at once.
	UFUNCTION(Exec)
	void SeverMe(const FString& Bone);

	// BloodCount: logs how many drops of blood are in the world.
	UFUNCTION(Exec)
	void BloodCount();

	UFUNCTION(Exec)
	void ElbowCheck(const FString& CheckArgs);

	// MenuRow <name>: opens the pause menu if it is closed and picks the row starting with that name, for screenshots.
	UFUNCTION(Exec)
	void MenuRow(const FString& RowName);

	UFUNCTION(Exec)
	void GripInfo();

	// A blow taken, or landed, shakes the view: the shake starts full and fades, the further the more harm the blow did. A
	// bigger shake takes over from a smaller one still going.
	void ShakeViewFromHurt(float Damage);
	void ShakeViewFromStrike(float Damage);
	void ShakeView(float Degrees, float Seconds);

	UFUNCTION(Exec)
	void JitterProbe(const FString& ProbeArgs);
	UFUNCTION(Exec)
	void HipProbe(const FString& ProbeArgs);
	UFUNCTION(Exec)
	void ItemProbe(const FString& ProbeArgs);
	UFUNCTION(Exec)
	void HoldGap();
	UFUNCTION(Exec)
	void GrabPiece();
	UFUNCTION(Exec)
	void ClothGap(const FString& GapArgs);
	UFUNCTION(Exec)
	void GripProbe(const FString& ProbeArgs);
	// JumpNow: a tap of the jump key.
	UFUNCTION(Exec)
	void JumpNow();

	UFUNCTION(Exec)
	void MouseDrive(const FString& DriveArgs);

	UFUNCTION(Exec)
	void Holster(const FString& HandArgs);

	UFUNCTION(Exec)
	void ClearItems();

	UFUNCTION(Exec)
	void WaistInfo();

	// What the worn armour takes of an edge, a point and a blunt blow on each part of the body.
	UFUNCTION(Exec)
	void ArmourInfo();

	// A blow of so many joules on the armour over a bone: Dent <bone> <joules>.
	UFUNCTION(Exec)
	void Dent(const FString& Args);

	// Turns the weapon in the hand to its next way of holding, as the R key does.
	UFUNCTION(Exec)
	void TurnWeapon();

	// A decal material laid flat in front of the feet, to look at on its own.
	UFUNCTION(Exec)
	void LayDecal(const FString& Args);

	UFUNCTION(Exec)
	void DropHand(int32 RightHand);

	UFUNCTION(Exec)
	void HandWatch(int32 RightHand, float Seconds);

	UFUNCTION(Exec)
	void MouseFlick(int32 RightHand, float DeltaX, float DeltaY, float Seconds);

	// The camera behind the head (as Z does), and the camera turned round the body (as moving the mouse with V held does).
	UFUNCTION(Exec)
	void HeadCamera();

	// PhotoToggle [real seconds]: the photo camera on or off, as Z; given seconds, it goes off again after that many real
	// seconds (time all but stands still with it up, so nothing waiting on the game's time would).
	UFUNCTION(Exec)
	void PhotoToggle(float RealSeconds);

	// Puts the free camera up and takes this many pictures this many real seconds apart while its time all but stands still.
	UFUNCTION(Exec)
	void PhotoShots(int32 Count, float RealSeconds);

	// The mouse as the player moves it, through the same handlers: this much axis value a second, for this long.
	UFUNCTION(Exec)
	void MouseInput(float ValueX, float ValueY, float Seconds);

	// Where the view looks and where the camera stands against the head.
	UFUNCTION(Exec)
	void CamInfo();

	// Logs every frame in which the capsule, the mesh, the head, the camera or the view jumps, for that many seconds.
	UFUNCTION(Exec)
	void JumpWatch(float Seconds, float LimitCmPerSec = 0.f);

	// Logs how far the body turned as a whole on the ground, how far the head looked off the chest, and how much the right hand moved.
	UFUNCTION(Exec)
	void SpinWatch(float Seconds, int32 Nearest = 0);

	// Logs what the face can be moved with.
	UFUNCTION(Exec)
	void FaceInfo();

	// Where each arm aims, where its hand is and where it is wanted, against the body.
	UFUNCTION(Exec)
	void ArmInfo();

	// What the pose asks of the right hand and its weapon, where they really are, and how far off.
	UFUNCTION(Exec)
	void PoseMeasure(const FString& Label);

	// PoseMeasure logged every given seconds (0 stops), for a run driven by the real mouse.
	UFUNCTION(Exec)
	void PoseLog(float Seconds);

	// Sets one cell of the sword stance table: row col forward right up pitch yaw.
	// Logs both arms' shoulder, elbow and wrist in the body's frame (forward, right, up), for comparing with a filmed pose.
	UFUNCTION(Exec)
	void ArmBones(const FString& Label);

	// Holds the walking keys for this many seconds as the player would, forward and right each -1 to 1 (W is 1 0, D is 0 1).
	UFUNCTION(Exec)
	void HoldKeys(float Forward, float Right, float Seconds);

	// Logs how long the camera arm is now and how far it has drawn back for a fight.
	UFUNCTION(Exec)
	void CamInfoLength();

	// Dev: whether the clothes worn by the player and those near are simulated as cloth, and how far the loose cloth stands
	// off where the bones alone would put it.
	UFUNCTION(Exec)
	void ClothInfo();

	// Washes the blood off the body and out of the clothes worn, as a wash at a lavoir will.
	UFUNCTION(Exec)
	void Wash();
	FVector2D DevHeldKeys = FVector2D::ZeroVector;
	float DevHeldKeysLeft = 0.f;

	// Presses (1) or lets go (0) a mouse button for the fists as the player would: left button 1, right button 0.
	UFUNCTION(Exec)
	void FistButton(int32 LeftButton, int32 Down);

	// Takes the one-handed blade in the right hand in both hands (1) or back in one (0).
	UFUNCTION(Exec)
	void TwoHand(int32 On);

	// The bodies' physics on (1) or off (0, the posed bodies the game runs with for now); with nothing given, the other way.
	UFUNCTION(Exec)
	void Physics(int32 On = -1);

	// Flies through everything: no collision, no gravity, the view's way with WASD, Space up and Ctrl down. Again to land.
	UFUNCTION(Exec)
	void Noclip();

	UFUNCTION(Exec)
	void Tcl();

	// Logs how many footprints lie in the world and how strong the oldest still is.
	UFUNCTION(Exec)
	void FootprintInfo();

	// Logs how high the main bones of the player and of the nearest other body stand above the soles, and the lengths of
	// the trunk and legs, to see a body squash or stretch.
	UFUNCTION(Exec)
	void BodyHeights(const FString& Label);

	// Logs, for each joint of the player's physical body, how far its two anchors (on the parent bone and on the child bone)
	// lie from the child bone's own origin, where the joint should be.
	UFUNCTION(Exec)
	void JointCheck();

	// For this many seconds, logs how close the blade in the right hand comes to the chest of the nearest other body: the
	// least it came in that time, to see a blade stopped by the body or going through it.
	UFUNCTION(Exec)
	void BladeNear(float Seconds);

	// For this many seconds, logs how far the physical body strays from its pose (the mean and the most, for the head, the
	// chest and the hands) and how often the head's stray turns back, to see a body that wobbles like jelly.
	UFUNCTION(Exec)
	void Wobble(float Seconds);

	// Logs how far above the ground under them the player's and the nearest other body's ankles, balls of the feet and toes
	// are, to see feet that float or sink.
	UFUNCTION(Exec)
	void FeetInfo(const FString& Label);

	// WorldJitter <seconds> <label> <bone> [bone...]: how much each bone shakes where it is in the world, on the body played
	// and on the nearest other one: the root mean square of its frame-to-frame change of speed, and how often it turns back
	// while all but still (a planted foot, a hand on the ground).
	UFUNCTION(Exec)
	void WorldJitter(const FString& Args);

	// FaceNearest: turns the view and the body to the nearest other body's hips, so a walk forward goes into it.
	UFUNCTION(Exec)
	void FaceNearest();

	// NearestBody <seconds>: how far the nearest other body's hips go, and how fast at most, over the next seconds.
	UFUNCTION(Exec)
	void NearestBody(float Seconds);

	// TwoHandInfo <label>: with a weapon in the right hand, where both hands are along its grip and across it (the bodies as
	// they are, and where the pose wants them).
	UFUNCTION(Exec)
	void TwoHandInfo(const FString& Label);

	// Logs how the worn armour sits: the box of the skin of the head as it is drawn now and the box of each worn piece, in the
	// body's frame (forward, right, up) from the head bone, and the bone each piece hangs from.
	UFUNCTION(Exec)
	void ArmourFit(const FString& Label);

	// Breaks a limb's bone (0 left arm, 1 right arm, 2 left leg, 3 right leg), the player's own or, with Near 1, the nearest
	// other body's.
	UFUNCTION(Exec)
	void BreakLimb(int32 Limb, int32 Near = 0);

	// Logs how high above the ground the hands, elbows, head and knees of the nearest other body are, to see a fallen body
	// lie slack or stay stiff.
	UFUNCTION(Exec)
	void LyingInfo(const FString& Label);

	// How fast the player flies in noclip, and how many times that with Shift held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dev")
	float NoclipSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dev")
	float NoclipFastShare = 3.f;

	bool IsNoclip() const { return bNoclip; }

	// Logs where the fists' mouse stands and how far the punch is out.
	UFUNCTION(Exec)
	void FistInfo(const FString& Label);

	// Holds the fists' mouse at x y (-1..1) and the punch out (0..1) for a picture; FistPose off lets it go.
	UFUNCTION(Exec)
	void FistPose(const FString& Args);

	UFUNCTION(Exec)
	void StanceCell(int32 Row, int32 Col, float Forward, float Right, float Up, float Pitch, float Yaw);
	FTimerHandle PoseLogTimer;

	UFUNCTION(Exec)
	void OrbitCamera(float YawDegrees, float PitchDegrees);

	// Logs every frame the held item turns further than a tumble, and a summary at the end.
	UFUNCTION(Exec)
	void BladeWatch(int32 RightHand, float Seconds);

	UFUNCTION(Exec)
	void SwingTrace(int32 RightHand, float Seconds);

	UFUNCTION(Exec)
	void TalkTest(const FString& Topic);

	UFUNCTION(Exec)
	void NeedsInfo();

	UFUNCTION(Exec)
	void DecalTest();

	UFUNCTION(Exec)
	void SaveNow(const FString& Slot);

	UFUNCTION(Exec)
	void GuestSaveNow();

	// Parts a limb of the nearest person, or cuts the training pell, as a heavy blade would.
	UFUNCTION(Exec)
	void CutTest(const FString& Bone);

	UFUNCTION(Exec)
	void DummyCutTest(const FString& PartName);

	UFUNCTION(Exec)
	void AssetCheck(const FString& Paths);

	// The living creature in the middle of the view, if any.
	AAedirCreature* FindLookTarget(float MaxDistance) const;
	void RequestNpcAction(AAedirNpc* Npc, EAedirNpcAction Action);

	// The follower waiting to be shown where to go, and what the player is pointing out to them: a spot, and the item lying there if any.
	AAedirNpc* GetPointingNpc() const { return PointingNpc.Get(); }
	bool FindPointTarget(FVector& OutWhere, AAedirItemActor*& OutItem) const;

	UFUNCTION(Exec)
	void PointTest(float Yaw, float Pitch);

	UFUNCTION(Exec)
	void PointItemTest(const FString& Id);

	UFUNCTION(Exec)
	void AimOnlyItem(const FString& Id);

	// Looks at a person and presses the talk key, or only looks with Press 0.
	UFUNCTION(Exec)
	void LookNpcTest(const FString& Name, int32 Press);

	UFUNCTION(Exec)
	void GearInfo();

	// NpcDropTest Marra Mace 500: lays a catalog item that far in front of a person.
	UFUNCTION(Exec)
	void NpcDropTest(const FString& Name, const FString& Id, float Forward);

	UFUNCTION(Exec)
	void NpcRemoveTest(const FString& Name);

	UFUNCTION(Exec)
	void NpcDisarmTest(const FString& Name);

	UFUNCTION(Exec)
	void NpcBringTest(const FString& Name, float Forward);

	UFUNCTION(Exec)
	void PassHand();

	UFUNCTION(Exec)
	void KickNow(float HoldShare);

	UFUNCTION(Exec)
	void HandKey(int32 RightHand, int32 Release = 0);

	UFUNCTION(Exec)
	void PlaceBlock(float Forward, float Right, float Up, float SizeCm);

	UFUNCTION(Exec)
	void DrawSlot(int32 RightHand, int32 Slot);

	UFUNCTION(Exec)
	void HolsterTest(int32 RightHand);

	// Fixed movement input for diagnostics, on top of the keyboard.
	void SetDevAutoMove(const FVector2D& Input) { DevAutoMove = Input; }

	UFUNCTION(Exec)
	void ReachTest(float DelaySeconds, int32 BothHands);

	UFUNCTION(Exec)
	void CrouchTest();

	// A fixed block this far ahead (cm), to strike.
	UFUNCTION(Exec)
	void PlaceWall(float AheadCm);

	// FallTest 300: lift the body by that many cm and let it fall.
	UFUNCTION(Exec)
	void FallTest(float HeightCm);

	// Opens or closes the lower left console. Bound to the key under Esc.
	void ToggleConsole();

	// The thing clicked while the console is open.
	AActor* GetConsoleSelection() const { return SelectedEntity.Get(); }

	// Con player.additem Bread 2: a line typed into the console, without the console.
	UFUNCTION(Exec)
	void Con(const FString& Line);

	// ConPick 0.5 0.5: a click at that share of the screen, as with the console open.
	UFUNCTION(Exec)
	void ConPick(float ShareX, float ShareY);

	UFUNCTION(Exec)
	void ConOpen();

	UFUNCTION(Exec)
	void PrecipInfo();

	// Holds or lets go of a mouse button's guard, as the button would.
	UFUNCTION(Exec)
	void GuardKey(int32 RightHand, int32 Down);

	// Turns the view to a yaw from the body's facing and a pitch.
	UFUNCTION(Exec)
	void LookTo(float Yaw, float Pitch);

	// MeshSheet /Game/Pack/Meshes: stands every model of a folder in a row with its name over it and logs its size, for choosing from a pack.
	UFUNCTION(Exec)
	void MeshSheet(const FString& Folder);

	// FxTest /Game/Pack/NS_Thing.NS_Thing 0.5: plays an effect in front of the chest.
	UFUNCTION(Exec)
	void FxTest(const FString& Path, float Scale);

	UFUNCTION(Exec)
	void FoliageInfo();

	UFUNCTION(Exec)
	void LandInfo();

	UFUNCTION(Exec)
	void TalkOpenTest(const FString& Name);

	// MouthTest 1 1: holds the grab key of the right hand down, 0 lets go. T and Y and the inventory click also have test commands.
	UFUNCTION(Exec)
	void MouthTest(int32 RightHand, int32 Hold);

	UFUNCTION(Exec)
	void PackDrawTest(int32 RightHand);

	UFUNCTION(Exec)
	void NeedInfo();

	bool IsDebugBodyEnabled() const { return bDebugBody; }
	void SetDebugBodyEnabled(bool bEnabled) { bDebugBody = bEnabled; }

	// The look of the world through the camera, softer than the engine's own: colours a little faded (LookSaturation,
	// LookHighlightSaturation for the bright ones), less contrast, the shadows lifted out of black (LookShadowLift added
	// to them, a softer film toe, local exposure easing the contrast of the shadows and the highlights), as a phone camera
	// or a film graded for daylight shows the world.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookSaturation = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookHighlightSaturation = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookContrast = 0.94f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookShadowLift = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookFilmToe = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookShadowContrastScale = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float LookHighlightContrastScale = 0.85f;

	// Knocked senseless the view goes dark (to KnockoutDarkness, still faintly seen) and grey over KnockoutFadeSeconds;
	// coming round it clears over WakeDarkSeconds and its colours come back over WakeColourSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float KnockoutFadeSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look", meta=(ClampMin="0", ClampMax="1"))
	float KnockoutDarkness = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float WakeDarkSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float WakeColourSeconds = 3.f;

	// Dying, time slows to DeathTimeDilation over DeathSlowSeconds (alone only: with others it runs on), the colours go
	// over DeathGreySeconds, and the view goes dark to DeathDarkness over DeathDarkSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float DeathSlowSeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look", meta=(ClampMin="0.01", ClampMax="1"))
	float DeathTimeDilation = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float DeathGreySeconds = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look")
	float DeathDarkSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Look", meta=(ClampMin="0", ClampMax="1"))
	float DeathDarkness = 0.97f;

	// How dark the view is from being senseless or dying, 0 to 1, for the HUD to lay over it.
	float GetSenseDarkness() const { return SenseDarkness; }

	// Hurt, below HurtFromHealthShare of its health, the edges of the view go red, up to HurtEdgeMaxAlpha with no health
	// left, and the heart is heard beating, low and soft: from HeartBpmMinMax's least to its most beats a minute and
	// HeartVolumeMinMax's least to its most loud as the health goes. Dead, it stops.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Hurt", meta=(ClampMin="0.05", ClampMax="1"))
	float HurtFromHealthShare = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Hurt", meta=(ClampMin="0", ClampMax="1"))
	float HurtEdgeMaxAlpha = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Hurt")
	FVector2D HeartBpmMinMax = FVector2D(64.f, 120.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Hurt")
	FVector2D HeartVolumeMinMax = FVector2D(0.06f, 0.25f);

	// How hurt the body is for the view, 0 (above HurtFromHealthShare) to 1 (no health left), and how strong the beat the
	// heart just gave still is, 1 at the beat falling to 0.
	float GetHurtShare() const { return HurtShare; }
	float GetHeartPulse() const;

	// HeartWav: writes one beat of the heart to Saved/Heartbeat.wav, to listen to.
	UFUNCTION(Exec)
	void HeartWav();
	FString DevLookInfo() const;

private:
	void UpdateHurtFeel();
	void BuildHeartbeat();
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> HeartAudio;
	UPROPERTY(Transient)
	TObjectPtr<class USoundWaveProcedural> HeartWave;
	TArray<uint8> HeartBeatPcm;
	double NextHeartBeatSeconds = 0.0;
	double LastHeartBeatSeconds = -100.0;
	float HurtShare = 0.f;
	bool bHeartBeating = false;
	void ApplyLook();
	void UpdateSenseView();
	float SenseDarkness = 0.f;
	float SenseOutShare = 0.f;
	float SenseGreyShare = 0.f;
	float DeathRealSeconds = -1.f;

	void ApplyViewPitchLimits();
	void HandleViewTogglePressed();
	void HandleViewToggleReleased();
	void HandleHeadCameraPressed();
	bool bOrbitHeld = false;
	FRotator OrbitOffset = FRotator::ZeroRotator;
	bool bOrbitMoved = false;
	float OrbitTurnedDegrees = 0.f;
	float OrbitHeldSeconds = 0.f;
	bool bHeadCamera = false;
	// Z: the photo camera, as Half Sword's C: the camera comes off the body and flies free (mouse to look, WASD to move, E up,
	// Q down, Shift faster, wheel for the speed) until Z again. Playing alone time all but stops; with others it runs on.
	bool bPhotoCamera = false;
	bool bNoclip = false;
	FTimerHandle BladeWatchTimer;
	float BladeWatchLeft = 0.f;
	float BladeWatchClosest = 0.f;
	FTimerHandle WobbleTimer;
	float WobbleLeft = 0.f;
	int32 WobbleFrames = 0;
	int32 WobbleTurns = 0;
	float WobbleSum[4] = { 0.f, 0.f, 0.f, 0.f };
	float WobbleMost[4] = { 0.f, 0.f, 0.f, 0.f };
	FVector WobbleLastOff = FVector::ZeroVector;
	FVector WobbleLastStep = FVector::ZeroVector;
	FVector WobbleHeadSum = FVector::ZeroVector;
	FVector WobbleHeadSquares = FVector::ZeroVector;
	FVector WobbleLastHead = FVector::ZeroVector;
	FVector WobbleLastHeadStep = FVector::ZeroVector;
	int32 WobbleHeadTurns = 0;
	void SetNoclip(bool bOn);
	UFUNCTION(Server, Reliable)
	void ServerSetNoclip(bool bOn);
	float PhotoArmLengthCm = 250.f;
	FVector FreeCameraLocation = FVector::ZeroVector;
	FRotator FreeCameraRotation = FRotator::ZeroRotator;
	// Where the mouse has turned the free camera to; the camera turns after it smoothly.
	FRotator FreeCameraTargetRotation = FRotator::ZeroRotator;
	float FreeCameraSpeedCm = 300.f;
	void UpdatePhotoCamera();

	void HandleMenuTogglePressed();
	void HandleJournalPressed();
	void HandleMapPressed();
	void HandleSkillsPressed();
	void HandlePausePressed();
	void OpenMenuCategory(int32 Category);
	UAedirMenuSubsystem* GetMenuSubsystem();
	bool IsMenuOpen();
	bool IsCharacterCreationOpen();

	void EnsureStarterQuest();
	void UpdateTutorialObjectives();

	TSharedPtr<class SAedirConsole> ConsoleWidget;
	FDelegateHandle ConsoleReportHandle;
	void CloseConsole();

	FTimerHandle CharacterCreationTimer;
	bool bTutorialRotationCaptured = false;
	FRotator TutorialStartRotation = FRotator::ZeroRotator;



	void HandleMouseX(float Value);
	void HandleMouseY(float Value);
	void HandleMouseWheel(float Value);

	void HandleRunPressed();
	void HandleRunReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandleJumpPressed();
	void HandleJumpReleased();
	// An empty hand whose button is held is fought with by the rebuilt fists.
	bool UsesFistArms(EAedirHand Hand) const;
	void HandleLeftGuardPressed();
	void HandleLeftGuardReleased();
	void HandleRightGuardPressed();
	void HandleRightGuardReleased();

	void HandleLeftHandPressed();
	void HandleLeftHandReleased();
	void HandleRightHandPressed();
	void HandleRightHandReleased();
	void HandleLeftHolsterPressed();
	void HandleRightHolsterPressed();
	void UpdateRightButtonHold(float DeltaSeconds);
	bool bRightButtonDown = false;
	bool bRightButtonFreedHand = false;
	float RightButtonHeldSeconds = 0.f;
	// The right button held this long lets the off hand go; let go sooner, it was a click.
	static constexpr float RightButtonHoldSeconds = 0.25f;
	void HandleUsePressed(EAedirHand Hand);
	void HandleUseReleased(EAedirHand Hand);
	void HandleRightUsePressed() { HandleUsePressed(EAedirHand::Right); }
	void HandleRightUseReleased() { HandleUseReleased(EAedirHand::Right); }
	void HandleLeftUsePressed() { HandleUsePressed(EAedirHand::Left); }
	void HandleLeftUseReleased() { HandleUseReleased(EAedirHand::Left); }
	void HandleShovePressed();
	TWeakObjectPtr<AAedirNpc> PointingNpc;
	void AimForTest(TWeakObjectPtr<AActor> Target, bool bPress);

	TWeakObjectPtr<AActor> SelectedEntity;
	bool bConsolePaused = false;
	void HandleConsolePick(const FVector2D& ViewportPixel);
	void RefreshConsoleSelection();
	void RunConsoleLine(const FString& Line);

	UFUNCTION(Server, Reliable)
	void ServerConsoleLine(const FString& Line);
	bool bPressThenShot = false;

	UFUNCTION(Server, Reliable)
	void ServerSendNpc(AAedirNpc* Npc, const FVector& Where, AAedirItemActor* Item);
	void HandleChangeHandsPressed();
	void HandleLockOnTogglePressed();

	// Lock-on: while a guard is held the camera keeps the nearest creature the view points at in sight.
	void UpdateLockOn(float DeltaSeconds);
	// The nearest creature to where the view looks, within this far of it (the lock's own cone if none given).
	class AAedirCreature* FindLockOnTarget(float ConeDegrees = 0.f) const;
	void HandleThrustPressed();
	void HandleThrustReleased();

	// A tap of the thrust key leaves the thrust on until the next tap or the guard is let go; held, it lasts as long as the key.
	double ThrustPressedAt = 0.0;
	bool bThrustWasOn = false;
	bool bThrustLatched = false;
	void EndLatchedThrust();

	// Turns the weapon in the hand that holds one: over on its haft, reversed, or held by the blade while half-swording.
	void HandleTurnWeaponPressed();
	void HandleDynamicKickPressed();

	void HandleHandPressed(EAedirHand Hand);
	void HandleHandReleased(EAedirHand Hand);
	void UpdateHandUseHold(float DeltaSeconds);
	void UpdateReachAfterHandOver(float DeltaSeconds);
	void UpdateSwingInput(float DeltaSeconds);

	FVector2D PendingMouseDelta = FVector2D::ZeroVector;

	bool bHandKeyTracked[2] = { false, false };

	// A hand whose weapon went over into the other hand reaches as soon as it is empty (on a guest's machine, once the
	// server has said so); given up after a second.
	int32 ReachAfterHandOver = -1;
	float ReachAfterHandOverSeconds = 0.f;
	bool bHandUseTriggered[2] = { false, false };
	float HandKeyHeldSeconds[2] = { 0.f, 0.f };

	void RunDevCommand(const FString& Command, const FString& Args, bool bRunOnServer);
	void DrawDebugBody();

	UFUNCTION(Server, Reliable)
	void ServerDevCommand(const FString& Command, const FString& Args);

	FVector2D DevAutoMove = FVector2D::ZeroVector;
	bool bDebugBody = false;
};
