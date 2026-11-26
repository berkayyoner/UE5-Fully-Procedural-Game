#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AedirDustPuff.generated.h"

// How a puff looks: how long it lasts, how many specks, how big they start and grow to (cm), how fast they are thrown
// (cm/s), how quickly the air stops them, how thick and what colour.
USTRUCT(BlueprintType)
struct FAedirDustLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0.05", Units="s"))
	float LifeSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="1"))
	int32 Specks = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0", Units="cm"))
	float StartSizeCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0", Units="cm"))
	float EndSizeCm = 26.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0"))
	float ThrowSpeed = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0"))
	float Drag = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust", meta=(ClampMin="0", ClampMax="1"))
	float Opacity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aedir|Dust")
	FLinearColor Color = FLinearColor(0.32f, 0.29f, 0.24f);
};

// A small puff of dust knocked off a body by a solid blow or off the ground by a step: a few soft specks thrown out,
// spreading, growing and fading out within a moment. Seen only, never touched.
UCLASS(NotPlaceable, Transient)
class AEDIR_API AAedirDustPuff : public AActor
{
	GENERATED_BODY()

public:
	AAedirDustPuff();

	// Throws a puff at Point, out along Normal; Strength 0 to 1 sets how much.
	static void Spawn(UWorld* World, const FVector& Point, const FVector& Normal, float Strength);
	// The same, looking as asked rather than as a blow's puff does.
	static void Spawn(UWorld* World, const FVector& Point, const FVector& Normal, float Strength, const FAedirDustLook& Look);

	virtual void Tick(float DeltaSeconds) override;

	// A blow's puff at full strength.
	UPROPERTY(EditAnywhere, Category="Aedir|Dust")
	FAedirDustLook Look;

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
