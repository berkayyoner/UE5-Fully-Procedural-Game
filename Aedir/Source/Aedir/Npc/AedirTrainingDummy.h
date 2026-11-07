#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AedirTrainingDummy.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

// A pell of round timber on crossed feet, with a head and two arms: it rocks when struck, and a good cut takes pieces off it.
UCLASS()
class AEDIR_API AAedirTrainingDummy : public AActor
{
	GENERATED_BODY()

public:
	AAedirTrainingDummy();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Server only.
	void ReceiveStrike(UPrimitiveComponent* Part, const FVector& Location, const FVector& TipVelocity, const FVector& BladeAxis, float EnergyJoules, bool bCuts);

	float GetLastStrikeJoules() const { return LastStrikeJoules; }

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStrike(UPrimitiveComponent* Part, FVector Location, FVector Normal, FVector Push, float EnergyJoules, bool bSlice);

	void Rebuild();
	void ClearPieces();
	UProceduralMeshComponent* AddTimber(FName Name, const FVector& From, const FVector& To, float Radius);
	void SlicePart(UProceduralMeshComponent* Part, const FVector& Location, const FVector& Normal, const FVector& Push);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Base;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Rocker;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Parts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Pieces;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WoodMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CutMaterial;

	FVector2D Tilt = FVector2D::ZeroVector;
	FVector2D TiltSpeed = FVector2D::ZeroVector;
	float LastStrikeJoules = 0.f;
	float RebuildAt = 0.f;
};
