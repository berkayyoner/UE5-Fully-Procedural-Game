#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "AedirCameraBoom.generated.h"

// The third-person camera arm. It is pulled in only by the fixed world (ground, walls, rocks, buildings): bodies, weapons,
// items and anything else that moves pass through the view, so a swing or a body going by never makes the camera jump.
UCLASS(ClassGroup=(Aedir), meta=(BlueprintSpawnableComponent))
class AEDIR_API UAedirCameraBoom : public USpringArmComponent
{
	GENERATED_BODY()

protected:
	virtual void UpdateDesiredArmLocation(bool bDoTrace, bool bDoLocationLag, bool bDoRotationLag, float DeltaTime) override;
};
