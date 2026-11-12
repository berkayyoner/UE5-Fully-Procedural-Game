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
	float SpineStrength = 23.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HeadStrength = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float ArmStrength = 36.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float LegStrength = 19.f;

	// The hips' hold on the capsule: where they are and how they are turned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipLinearStrength = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipAngularStrength = 15.f;

	// How strongly the trunk and head are turned upright in the world, in Hz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float PostureStrength = 19.f;

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

	// Damping of the muscles, 1 for no overshoot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DampingRatio = 1.1f;

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
	TArray<FName> LegControls;
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
