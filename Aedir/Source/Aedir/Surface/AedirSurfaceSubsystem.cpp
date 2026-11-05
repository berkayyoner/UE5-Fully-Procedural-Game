#include "Surface/AedirSurfaceSubsystem.h"

#include "Engine/World.h"
#include "Surface/AedirPhysicalMaterial.h"

namespace
{
	constexpr float AedirSurfaceCellSize = 50.f;
	constexpr int32 AedirSurfaceMaxCells = 20000;
	constexpr float AedirSurfaceRemoveThreshold = 0.001f;
	constexpr float AedirSurfaceTraceUp = 30.f;
	constexpr float AedirSurfaceTraceDown = 80.f;
}

void UAedirSurfaceSubsystem::Tick(float DeltaTime)
{
	for (auto It = Cells.CreateIterator(); It; ++It)
	{
		FAedirSurfaceCell& Cell = It.Value();
		Cell.Wetness = FMath::Clamp(Cell.Wetness - Cell.DryRate * DeltaTime, 0.f, 1.f);
		Cell.Snow = FMath::Clamp(Cell.Snow - Cell.MeltRate * DeltaTime, 0.f, 1.f);

		if (Cell.Wetness < AedirSurfaceRemoveThreshold && Cell.Snow < AedirSurfaceRemoveThreshold)
		{
			It.RemoveCurrent();
		}
	}

	TrimToCellLimit();
}

TStatId UAedirSurfaceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAedirSurfaceSubsystem, STATGROUP_Tickables);
}

float UAedirSurfaceSubsystem::GetWetness(const FVector& WorldLocation) const
{
	const FAedirSurfaceCell* Cell = Cells.Find(GetCellCoord(WorldLocation));
	const float CellWetness = Cell ? Cell->Wetness : 0.f;

	return FMath::Max(CellWetness, GlobalWetness);
}

float UAedirSurfaceSubsystem::GetSnow(const FVector& WorldLocation) const
{
	const FAedirSurfaceCell* Cell = Cells.Find(GetCellCoord(WorldLocation));
	const float CellSnow = Cell ? Cell->Snow : 0.f;

	return FMath::Max(CellSnow, GlobalSnow);
}

void UAedirSurfaceSubsystem::AddWetness(const FVector& WorldLocation, float Amount, float DryRatePerSecond)
{
	if (Amount <= 0.f)
	{
		return;
	}

	FAedirSurfaceCell& Cell = Cells.FindOrAdd(GetCellCoord(WorldLocation));
	Cell.Wetness = FMath::Clamp(Cell.Wetness + Amount, 0.f, 1.f);
	Cell.DryRate = FMath::Max(0.f, DryRatePerSecond);
}

void UAedirSurfaceSubsystem::AddSnow(const FVector& WorldLocation, float Amount, float MeltRatePerSecond)
{
	if (Amount <= 0.f)
	{
		return;
	}

	FAedirSurfaceCell& Cell = Cells.FindOrAdd(GetCellCoord(WorldLocation));
	Cell.Snow = FMath::Clamp(Cell.Snow + Amount, 0.f, 1.f);
	Cell.MeltRate = FMath::Max(0.f, MeltRatePerSecond);
}

void UAedirSurfaceSubsystem::AddWetnessInRadius(const FVector& Center, float Radius, float Amount, float DryRate)
{
	if (Amount <= 0.f || Radius <= 0.f)
	{
		return;
	}

	const FIntPoint CenterCoord = GetCellCoord(Center);
	const FVector2D Center2D(Center.X, Center.Y);
	const int32 CellRadius = FMath::CeilToInt(Radius / AedirSurfaceCellSize);
	const float ClampedDryRate = FMath::Max(0.f, DryRate);

	for (int32 OffsetX = -CellRadius; OffsetX <= CellRadius; ++OffsetX)
	{
		for (int32 OffsetY = -CellRadius; OffsetY <= CellRadius; ++OffsetY)
		{
			const FIntPoint Coord(CenterCoord.X + OffsetX, CenterCoord.Y + OffsetY);
			const bool bContainsCenter = Coord == CenterCoord;

			const FVector2D CellCenter((Coord.X + 0.5) * AedirSurfaceCellSize, (Coord.Y + 0.5) * AedirSurfaceCellSize);
			const float Distance = static_cast<float>(FVector2D::Distance(Center2D, CellCenter));

			float Weight = FMath::Clamp(1.f - Distance / Radius, 0.f, 1.f);
			if (bContainsCenter)
			{
				Weight = FMath::Max(Weight, 0.5f);
			}

			if (Weight <= 0.f)
			{
				continue;
			}

			FAedirSurfaceCell& Cell = Cells.FindOrAdd(Coord);
			Cell.Wetness = FMath::Clamp(Cell.Wetness + Amount * Weight, 0.f, 1.f);
			Cell.DryRate = ClampedDryRate;
		}
	}
}

void UAedirSurfaceSubsystem::SetGlobalWeatherWetness(float Value01)
{
	GlobalWetness = FMath::Clamp(Value01, 0.f, 1.f);
}

void UAedirSurfaceSubsystem::SetGlobalWeatherSnow(float Value01)
{
	GlobalSnow = FMath::Clamp(Value01, 0.f, 1.f);
}

bool UAedirSurfaceSubsystem::QuerySurface(UWorld* World, const FVector& Location, const AActor* Ignore, FHitResult& OutHit)
{
	if (!World)
	{
		return false;
	}

	const FVector Start = Location + FVector::UpVector * AedirSurfaceTraceUp;
	const FVector End = Location - FVector::UpVector * AedirSurfaceTraceDown;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AedirSurfaceQuery), false, Ignore);
	Params.bReturnPhysicalMaterial = true;

	return World->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
}

const UAedirPhysicalMaterial* UAedirSurfaceSubsystem::GetAedirMaterial(const FHitResult& Hit)
{
	return Cast<UAedirPhysicalMaterial>(Hit.PhysMaterial.Get());
}

FIntPoint UAedirSurfaceSubsystem::GetCellCoord(const FVector& WorldLocation) const
{
	return FIntPoint(
		FMath::FloorToInt(WorldLocation.X / AedirSurfaceCellSize),
		FMath::FloorToInt(WorldLocation.Y / AedirSurfaceCellSize));
}

void UAedirSurfaceSubsystem::TrimToCellLimit()
{
	const int32 Excess = Cells.Num() - AedirSurfaceMaxCells;
	if (Excess <= 0)
	{
		return;
	}

	TArray<FIntPoint> RankedCoords;
	RankedCoords.Reserve(Cells.Num());
	for (const TPair<FIntPoint, FAedirSurfaceCell>& Pair : Cells)
	{
		RankedCoords.Add(Pair.Key);
	}

	RankedCoords.Sort([this](const FIntPoint& A, const FIntPoint& B)
	{
		return Cells[A].Wetness < Cells[B].Wetness;
	});

	for (int32 Index = 0; Index < Excess; ++Index)
	{
		Cells.Remove(RankedCoords[Index]);
	}
}
