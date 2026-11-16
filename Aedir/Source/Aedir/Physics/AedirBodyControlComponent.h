#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsControlData.h"
#include "AedirBodyControlComponent.generated.h"

class UCapsuleComponent;
class UPhysicsControlComponent;
class USkeletalMeshComponent;

// The muscles of a fully physical body, on the engine's physics control: every part is simulated and pulled towards the pose
// by its joint to the part above it, mass-independent and critically damped, so weight, blows and momentum go through the
// body as a chain. The joints alone cannot carry the weight of everything above them, so the trunk is also turned upright
// in the world, the feet are led to where the steps set them, and a standing body carries only part of its weight. The hips
// are held to the capsule by their own control and the mesh stays on the capsule. Strength follows stamina and health;
// down, everything goes limp and carries its full weight.
UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirBodyControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAedirBodyControlComponent();

	// How quickly each part is pulled back to the pose, in Hz at full strength: a higher number is a stiffer muscle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float SpineStrength = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HeadStrength = 19.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float ArmStrength = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float LegStrength = 24.f;

	// The hips' hold on the capsule: where they are and how they are turned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipLinearStrength = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipAngularStrength = 15.f;

	// How strongly the trunk and head are turned upright in the world, in Hz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float PostureStrength = 24.f;

	// How strongly each foot is pulled to where the pose sets it, in Hz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float FootStrength = 6.f;

	// Share of its weight a body carries while standing at full strength; down it carries all of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float StandingGravity = 0.4f;

	// How hard the hips are lifted while getting up, against how hard they are held standing: the legs and arms do not yet
	// carry the body, so the hips' muscles lift all of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float RiseHipShare = 2.f;

	// What is left of every muscle in a body that is down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float LimpShare = 0.04f;

	// What is left of every muscle in a dead body. At the moment of death the muscles still hold the shape the body had
	// then with DeadStartShare of their strength, and give way to DeadShare over DeadFadeSeconds: the body goes down as a
	// whole and settles, its joints still resisting being folded, rather than flowing to the ground like dough.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DeadStartShare = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DeadShare = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DeadFadeSeconds = 3.f;

	// What is left of the muscles of a limb whose bone is broken: it swings loose, still hanging together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float BrokenShare = 0.02f;

	// A broken arm's shoulder keeps this much of its strength and holds the arm hanging down at the side, a little out, so
	// the arm swings from it as a weight and does not flap about the shoulder like an empty sleeve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float BrokenShoulderShare = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float BrokenArmOutDegrees = 12.f;

	// A broken arm's joints are damped this many times more than a whole arm's, and dragged on by the body it hangs from
	// (extra damping), so it swings as a weight and does not stream out behind a running body.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float BrokenArmDampingShare = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float BrokenArmDrag = 8.f;

	// Damping of the muscles, 1 for no overshoot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DampingRatio = 1.25f;

	// Damping of the arms' muscles: well past 1, so a swung or struck arm gives and comes back without wobbling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float ArmDampingRatio = 2.2f;

	// The wrist and fingers against the rest of the arm: looser, so a blow and the weight of what the hand holds show in it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float WristShare = 0.4f;

	// The same with one thing held in both hands, which steady each other.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float TwoHandWristShare = 0.8f;

	// Every frame: how far one thing is held in both hands (0..1).
	void SetTwoHandGrip(float Alpha);

	// Builds the controls on the body's bones; called again whenever the physics bodies are made anew.
	void Setup(USkeletalMeshComponent* InMesh, UCapsuleComponent* InCapsule);
	bool IsReady() const { return bReady; }

	// Every frame: how strong the body holds itself up (0..1), and how strong each arm is.
	void SetStrengths(float Upright, float LeftArm, float RightArm);

	// The leg throwing a kick (0 left, 1 right, -1 none): its foot is driven along the kick much harder than a step.
	void SetKickLeg(int32 Leg);

	// How much harder a kicking foot is driven than a stepping one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float KickFootShare = 5.f;

	// Every frame: how much what each hand holds slows its arm (1 for an empty hand).
	void SetArmLoads(float Left, float Right);

	// Getting up off the ground: the hips stay physical and are lifted by their control, the legs push on the ground.
	void SetRising(bool bInRising);

	// Jumping or falling, the body carries all its weight.
	void SetAirborne(bool bInAirborne);

	// A body down on the ground: limp, every part loose. Back up, it is tied to the capsule again.
	void SetLimp(bool bInLimp);
	// Dead, the body keeps only DeadShare of its muscles.
	void SetDead(bool bInDead);
	// Which limbs have a broken bone: left arm, right arm, left leg, right leg.
	void SetBrokenLimbs(bool bLeftArm, bool bRightArm, bool bLeftLeg, bool bRightLeg);

	// An arm freed from where it was caught pulls back to its pose ArmFreeBoost times as hard for a moment.
	void SetArmBoost(bool bRight, bool bBoost);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float ArmFreeBoost = 3.f;

	// An arm (from the upper arm down) shown as the pose has it, its bodies simulating under it, or shown where its bodies are.
	void SetArmsPosed(bool bLeft, bool bRight);
	bool IsArmPosed(bool bRight) const { return bArmPosed[bRight ? 1 : 0]; }

	// The whole body shown as the pose has it and its bodies moved with the pose (kinematic): no physics in the look of it.
	void SetWholePosed(bool bPosed);
	bool IsWholePosed() const { return bWholePosed; }


	// While getting up the hips are carried through the moves by the pose; otherwise the control holds them.
	void SetHipsCarried(bool bCarried);

	// A foot on the ground stays where the pose plants it; lifted, it swings free.
	void SetFeetPlanted(bool bLeft, bool bRight);

	// Where the pose has a bone this frame, in the world: where its muscles are pulling it.
	FVector GetPoseLocation(FName Bone) const;

	// The capsule's physical body was made anew (its collision turned back on): the hips are tied to the new one.
	void RebindHips(UCapsuleComponent* InCapsule);

	// The body was moved at once (a teleport, a respawn): every part is put where the pose has it, at rest.
	void Teleported(const FVector& Offset);


private:
	void ApplyMultipliers();
	void ApplyLegContacts();
	// A broken arm hangs from the shoulder by its own weight, whether the rest is posed or held by its muscles.
	void ApplyBrokenArms();
	// Muscles that hold the shape the body has now (the parent and child bones' turn to each other as they are), not
	// the pose the animation asks; or back to the animation's pose.
	void HoldShapeNow(const TArray<FName>& Names);
	void FollowAnimation(const TArray<FName>& Names);
	// The shoulder muscle of a broken arm holds it hanging at the side.
	void HangBrokenArm(int32 Side);
	// What is left of a dead body's muscles now, fading from the moment of death.
	float GetDeadShareNow() const;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> Control;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	TArray<FName> SpineControls;
	TArray<FName> PostureControls;
	TArray<FName> FootControls;
	TArray<FName> LegModifiers;
	TArray<FName> LegBones;
	TArray<FName> HeadControls;
	TArray<FName> LeftArmControls;
	TArray<FName> RightArmControls;
	TArray<FName> LeftHandControls;
	TArray<FName> RightHandControls;
	float TwoHandGrip = 0.f;
	TArray<FName> LegControls;
	TArray<FName> LeftLegControls;
	TArray<FName> RightLegControls;
	bool bDead = false;
	double DeadTime = 0.0;
	bool bBroken[4] = { false, false, false, false };
	// For each muscle, the bone it pulls from and the one it pulls on; and each arm's shoulder muscle.
	TMap<FName, TPair<FName, FName>> MuscleBones;
	FName ShoulderControls[2];
	FName HipControl;
	FName LeftFootControl;
	FName RightFootControl;
	int32 KickLeg = -1;
	FPhysicsControlData HipData;
	FName HipModifier;
	FName LeftFootModifiers[2];
	FName RightFootModifiers[2];
	TArray<FName> ArmModifiers[2];
	bool bArmPosed[2] = { false, false };
	bool bArmLoose[2] = { false, false };
	bool bArmBoost[2] = { false, false };
	bool bWholePosed = false;

	bool bReady = false;
	bool bLimp = false;
	bool bAirborne = false;
	bool bRising = false;
	bool bHipsCarried = false;
	bool bLeftPlanted = false;
	bool bRightPlanted = false;
	float Upright = 1.f;
	float LeftArmShare = 1.f;
	float RightArmShare = 1.f;
	float LeftLoadShare = 1.f;
	float RightLoadShare = 1.f;
	double AppliedKey = -1.0;
	float AppliedTuning = 0.f;
};
