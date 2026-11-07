#include "Creature/AedirWayFinder.h"

#include "Engine/World.h"

namespace
{
	constexpr float WayCellCm = 75.f;
	constexpr float WayMaxRiseCm = 58.f;
	constexpr float WayMinFloorZ = 0.62f;
	constexpr float WayBodyLiftCm = 75.f;
	constexpr float WayBodyRadiusCm = 26.f;
	constexpr float WayLookUpCm = 120.f;
	constexpr float WayLookDownCm = 240.f;
	constexpr float WayArriveCm = 110.f;
	constexpr float WayArriveRiseCm = 130.f;
	constexpr float WayClimbCost = 0.6f;
	constexpr float WayGoalWeight = 1.2f;
	constexpr int32 WayMaxCells = 3000;
	const FIntPoint WayNeighbours[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

	FCollisionObjectQueryParams WayObjects()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}

	bool WayBodyFits(UWorld* World, const FVector& FeetFrom, const FVector& FeetTo, const AActor* Ignored)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWayBody), false, Ignored);
		const FVector Lift(0.f, 0.f, WayBodyLiftCm);
		return !World->SweepTestByObjectType(FeetFrom + Lift, FeetTo + Lift, FQuat::Identity, WayObjects(), FCollisionShape::MakeSphere(WayBodyRadiusCm), Params);
	}
}

bool FAedirWayFinder::FindGround(UWorld* World, const FVector& Where, float UpCm, float DownCm, const AActor* Ignored, FVector& OutGround)
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirWayGround), false, Ignored);
	if (!World->LineTraceSingleByObjectType(Hit, Where + FVector(0.f, 0.f, UpCm), Where - FVector(0.f, 0.f, DownCm), WayObjects(), Params) || Hit.ImpactNormal.Z < WayMinFloorZ)
	{
		return false;
	}
	OutGround = Hit.ImpactPoint;
	return true;
}

bool FAedirWayFinder::CanWalkStraight(UWorld* World, const FVector& FeetFrom, const FVector& FeetTo, const AActor* Ignored)
{
	const int32 Steps = FMath::Max(FMath::CeilToInt(static_cast<float>(FVector::Dist2D(FeetFrom, FeetTo)) / WayCellCm), 1);
	FVector Previous = FeetFrom;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Share = static_cast<float>(Step) / Steps;
		FVector Sample = FMath::Lerp(FeetFrom, FeetTo, static_cast<double>(Share));
		Sample.Z = Previous.Z;
		FVector Ground;
		if (!FindGround(World, Sample, WayLookUpCm, WayLookDownCm, Ignored, Ground) || FMath::Abs(Ground.Z - Previous.Z) > WayMaxRiseCm || !WayBodyFits(World, Previous, Ground, Ignored))
		{
			return false;
		}
		Previous = Ground;
	}
	return true;
}

void FAedirWayFinder::Begin(UWorld* InWorld, const FVector& FeetStart, const FVector& FeetGoal, const AActor* InIgnored)
{
	World = InWorld;
	Ignored = InIgnored;
	Origin = FeetStart;
	Goal = FeetGoal;
	Cells.Reset();
	Open.Reset();
	Points.Reset();
	Visited = 0;
	bDone = false;
	Nearest = FIntPoint::ZeroValue;
	NearestLeft = static_cast<float>(FVector::Dist2D(FeetStart, FeetGoal));

	FCell& Start = Cells.Add(FIntPoint::ZeroValue);
	Start.Ground = FeetStart;
	Open.HeapPush(FOpenEntry{ NearestLeft, FIntPoint::ZeroValue });
}

bool FAedirWayFinder::Step(int32 Budget)
{
	if (bDone || !World)
	{
		return true;
	}

	for (int32 Count = 0; Count < Budget; ++Count)
	{
		if (Open.IsEmpty() || Visited >= WayMaxCells)
		{
			Finish(Nearest);
			return true;
		}

		FOpenEntry Entry;
		Open.HeapPop(Entry, EAllowShrinking::No);
		FCell* Current = Cells.Find(Entry.Key);
		if (!Current || Current->bClosed)
		{
			continue;
		}
		Current->bClosed = true;
		++Visited;

		const FVector Here = Current->Ground;
		const float HereCost = Current->Cost;
		const float Left = static_cast<float>(FVector::Dist2D(Here, Goal));
		if (Left < WayArriveCm && FMath::Abs(Here.Z - Goal.Z) < WayArriveRiseCm)
		{
			Finish(Entry.Key);
			return true;
		}
		if (Left < NearestLeft)
		{
			NearestLeft = Left;
			Nearest = Entry.Key;
		}

		for (const FIntPoint& Offset : WayNeighbours)
		{
			const FIntPoint Key = Entry.Key + Offset;
			const FCell* Known = Cells.Find(Key);
			if (Known && Known->bClosed)
			{
				continue;
			}

			const FVector Sample(Origin.X + Key.X * WayCellCm, Origin.Y + Key.Y * WayCellCm, Here.Z);
			FVector Ground;
			if (!FindGround(World, Sample, WayLookUpCm, WayLookDownCm, Ignored, Ground))
			{
				continue;
			}
			const float Rise = static_cast<float>(FMath::Abs(Ground.Z - Here.Z));
			const float Run = (Offset.X != 0 && Offset.Y != 0) ? WayCellCm * UE_SQRT_2 : WayCellCm;
			if (Rise > WayMaxRiseCm * (Run / WayCellCm) || !WayBodyFits(World, Here, Ground, Ignored))
			{
				continue;
			}

			const float NewCost = HereCost + Run + Rise * WayClimbCost;
			if (Known && Known->Cost <= NewCost)
			{
				continue;
			}
			FCell& Cell = Cells.FindOrAdd(Key);
			Cell.Ground = Ground;
			Cell.Parent = Entry.Key;
			Cell.Cost = NewCost;
			Open.HeapPush(FOpenEntry{ NewCost + static_cast<float>(FVector::Dist(Ground, Goal)) * WayGoalWeight, Key });
		}
	}
	return false;
}

void FAedirWayFinder::Finish(FIntPoint Last)
{
	bDone = true;
	TArray<FVector> Chain;
	for (const FCell* Cell = Cells.Find(Last); Cell; Cell = Cells.Find(Cell->Parent))
	{
		Chain.Add(Cell->Ground);
	}
	Algo::Reverse(Chain);

	// As few turns as the ground allows: every point goes straight to the last one it can still walk to.
	int32 From = 0;
	while (From < Chain.Num() - 1)
	{
		int32 To = From + 1;
		for (int32 Try = FMath::Min(From + 14, Chain.Num() - 1); Try > To; --Try)
		{
			if (CanWalkStraight(World, Chain[From], Chain[Try], Ignored))
			{
				To = Try;
				break;
			}
		}
		Points.Add(Chain[To]);
		From = To;
	}
	Cells.Reset();
	Open.Reset();
}
