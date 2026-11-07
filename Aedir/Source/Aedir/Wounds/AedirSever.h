#pragma once

#include "CoreMinimal.h"

class AActor;
class USkeletalMeshComponent;

// Cuts a limb off a skinned body: the piece is rebuilt from the vertices of the moment, cut along the plane of the blow with a real cap, and falls on its own.
namespace AedirSever
{
	// The joint a blow on this bone would part, or none for what cannot come off.
	AEDIR_API FName FindSeverBone(FName HitBone);
	// How much energy a cut takes at that joint.
	AEDIR_API float GetSeverJoules(FName SeverBone);
	AEDIR_API AActor* CutLimb(USkeletalMeshComponent* Body, FName SeverBone, const FVector& PlanePoint, const FVector& PlaneNormal, const FVector& Push);
}
