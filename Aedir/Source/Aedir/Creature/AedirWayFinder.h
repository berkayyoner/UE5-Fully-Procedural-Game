#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

// Finds a way over whatever ground is there, with no navigation data: it feels out the world cell by cell as a person would look ahead,
// so it also works on land that was only just generated. The search is spread over frames.
class FAedirWayFinder
{
public:
	void Begin(UWorld* InWorld, const FVector& FeetStart, const FVector& FeetGoal, const AActor* InIgnored);

	// Works through up to Budget cells. True when the search is over, with OutPoints holding the way there or as near as it got.
	bool Step(int32 Budget);

	const TArray<FVector>& GetPoints() const { return Points; }

	// True if a body can walk along the straight line between two points on the ground: no wall, no drop, no slope too steep.
	static bool CanWalkStraight(UWorld* World, const FVector& FeetFrom, const FVector& FeetTo, const AActor* Ignored);

	// The ground under a point, looking from up to UpCm above it and DownCm below. False where there is none or it is too steep to stand on.
	static bool FindGround(UWorld* World, const FVector& Where, float UpCm, float DownCm, const AActor* Ignored, FVector& OutGround);

private:
	struct FCell
	{
		FVector Ground = FVector::ZeroVector;
		FIntPoint Parent = FIntPoint(MAX_int32, MAX_int32);
		float Cost = 0.f;
		bool bClosed = false;
	};

	struct FOpenEntry
	{
		float Score = 0.f;
		FIntPoint Key = FIntPoint::ZeroValue;
		bool operator<(const FOpenEntry& Other) const { return Score < Other.Score; }
	};

	void Finish(FIntPoint Last);

	UWorld* World = nullptr;
	const AActor* Ignored = nullptr;
	FVector Origin = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	TMap<FIntPoint, FCell> Cells;
	TArray<FOpenEntry> Open;
	TArray<FVector> Points;
	FIntPoint Nearest = FIntPoint::ZeroValue;
	float NearestLeft = TNumericLimits<float>::Max();
	int32 Visited = 0;
	bool bDone = true;
};
