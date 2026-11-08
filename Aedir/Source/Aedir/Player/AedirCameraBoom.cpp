#include "Player/AedirCameraBoom.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"

void UAedirCameraBoom::UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!bDoTrace || !World)
	{
		Super::UpdateDesiredArmLocation(bDoTrace, bDoLocationLag, bDoRotationLag, DeltaTime);
		return;
	}

	// The arm is shortened to where it meets the fixed world, swept against nothing else, and the engine's own test is left out.
	const float FullLength = TargetArmLength;
	const FRotator Rotation = GetTargetRotation();
	const FVector Origin = GetComponentLocation() + TargetOffset;
	const FVector Desired = Origin - Rotation.Vector() * FullLength + FRotationMatrix(Rotation).TransformVector(SocketOffset);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirCameraBoom), false, GetOwner());
	const FCollisionObjectQueryParams FixedWorld(ECC_WorldStatic);
	if (World->SweepSingleByObjectType(Hit, Origin, Desired, FQuat::Identity, FixedWorld, FCollisionShape::MakeSphere(ProbeSize), Params))
	{
		TargetArmLength = FullLength * Hit.Time;
	}
	Super::UpdateDesiredArmLocation(false, bDoLocationLag, bDoRotationLag, DeltaTime);
	TargetArmLength = FullLength;
}
