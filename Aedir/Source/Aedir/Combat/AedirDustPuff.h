#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AedirDustPuff.generated.h"

// A small puff of dust knocked off a body by a solid blow: a few soft specks thrown back the way the blow came, spreading,
// growing and fading out within a moment. Seen only, never touched.
UCLASS(NotPlaceable, Transient)
class AEDIR_API AAedirDustPuff : public AActor
{
	GENERATED_BODY()

public:
	AAedirDustPuff();

	// Throws a puff at Point, out along Normal; Strength 0 to 1 sets how much.
	static void Spawn(UWorld* World, const FVector& Point, const FVector& Normal, float Strength);

	virtual void Tick(float DeltaSeconds) override;

	// How long a puff lasts, how many specks, how big they start and grow to (cm), how fast they are thrown (cm/s) and how
	// quickly the air stops them; all at full strength.
	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float LifeSeconds = 0.6f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	int32 Specks = 7;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float StartSizeCm = 5.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float EndSizeCm = 26.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float ThrowSpeed = 140.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float Drag = 5.f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	float Opacity = 0.35f;

	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	FLinearColor Color = FLinearColor(0.32f, 0.29f, 0.24f);

private:
	void Begin(const FVector& Normal, float Strength);

	UPROPERTY(Transient)
	TObjectPtr<class UInstancedStaticMeshComponent> Puffs;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> Material;

	TArray<FVector> Places;
	TArray<FVector> Speeds;
	TArray<float> Sizes;
	float Age = 0.f;
	float Amount = 1.f;
};
