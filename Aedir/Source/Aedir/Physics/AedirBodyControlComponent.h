#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AedirBodyControlComponent.generated.h"

class UCapsuleComponent;
class UPhysicsControlComponent;
class USkeletalMeshComponent;

// The muscles of a fully physical body, on the engine's physics control: every part is simulated and pulled towards the pose
// by its joint to the part above it, mass-independent and critically damped, so weight, blows and momentum go through the
// body as a chain. The hips are held to the capsule by their own control, the mesh stays on the capsule, and a foot on the
// ground is locked where the pose plants it until it lifts. Strength follows stamina and health; down, everything goes limp.
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
	float HipLinearStrength = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Body")
	float HipAngularStrength = 7.f;

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

	// A body down on the ground: limp, every part loose. Back up, it is tied to the capsule again.
	void SetLimp(bool bInLimp);

	// While getting up the hips are carried through the moves by the pose; otherwise the control holds them.
	void SetHipsCarried(bool bCarried);

	// A foot on the ground stays where the pose plants it; lifted, it swings free.
	void SetFeetPlanted(bool bLeft, bool bRight);

	// The body was moved at once (a teleport, a respawn): every part is put where the pose has it, at rest.
	void Teleported();

private:
	void ApplyMultipliers();

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> Control;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	TArray<FName> SpineControls;
	TArray<FName> HeadControls;
	TArray<FName> LeftArmControls;
	TArray<FName> RightArmControls;
	TArray<FName> LegControls;
	FName HipControl;
	FName HipModifier;
	FName LeftFootModifiers[2];
	FName RightFootModifiers[2];

	bool bReady = false;
	bool bLimp = false;
	bool bHipsCarried = false;
	bool bLeftPlanted = false;
	bool bRightPlanted = false;
	float Upright = 1.f;
	float LeftArmShare = 1.f;
	float RightArmShare = 1.f;
	float AppliedKey = -1.f;
};
