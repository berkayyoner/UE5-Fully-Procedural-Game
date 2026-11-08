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
	float SpineStrength = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HeadStrength = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float ArmStrength = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float LegStrength = 10.f;

	// The hips' hold on the capsule: where they are and how they are turned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipLinearStrength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipAngularStrength = 7.f;

	// How strongly the trunk and head are turned upright in the world, in Hz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float PostureStrength = 6.f;

	// How strongly each foot is pulled to where the pose sets it, in Hz.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float FootStrength = 10.f;

	// Share of its weight a body carries while standing at full strength; down it carries all of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float StandingGravity = 0.5f;

	// What is left of every muscle in a body that is down.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float LimpShare = 0.04f;

	// Damping of the muscles, 1 for no overshoot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float DampingRatio = 1.f;

	// Builds the controls on the body's bones; called again whenever the physics bodies are made anew.
	void Setup(USkeletalMeshComponent* InMesh, UCapsuleComponent* InCapsule);
	bool IsReady() const { return bReady; }

	// Every frame: how strong the body holds itself up (0..1), and how strong each arm is.
	void SetStrengths(float Upright, float LeftArm, float RightArm);

	// Jumping or falling, the body carries all its weight.
	void SetAirborne(bool bInAirborne);

	// A body down on the ground: limp, every part loose. Back up, it is tied to the capsule again.
	void SetLimp(bool bInLimp);

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

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> Control;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	TArray<FName> SpineControls;
	TArray<FName> PostureControls;
	TArray<FName> FootControls;
	TArray<FName> LegModifiers;
	TArray<FName> HeadControls;
	TArray<FName> LeftArmControls;
	TArray<FName> RightArmControls;
	TArray<FName> LegControls;
	FName HipControl;
	FPhysicsControlData HipData;
	FName HipModifier;
	FName LeftFootModifiers[2];
	FName RightFootModifiers[2];

	bool bReady = false;
	bool bLimp = false;
	bool bAirborne = false;
	bool bHipsCarried = false;
	bool bLeftPlanted = false;
	bool bRightPlanted = false;
	float Upright = 1.f;
	float LeftArmShare = 1.f;
	float RightArmShare = 1.f;
	float AppliedKey = -1.f;
};
