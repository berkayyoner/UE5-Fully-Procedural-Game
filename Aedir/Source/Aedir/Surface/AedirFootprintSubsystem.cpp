#include "Surface/AedirFootprintSubsystem.h"

#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// Prints fade so slowly that each is looked at only now and then: this many a frame.
	constexpr int32 FootprintFadePerFrame = 40;
}

void UAedirFootprintSubsystem::Add(UDecalComponent* Decal, UMaterialInstanceDynamic* Print, float Strength, float LifeSeconds, int32 MaxPrints)
{
	if (!Decal)
	{
		return;
	}
	while (Prints.Num() >= FMath::Max(MaxPrints, 1))
	{
		if (UDecalComponent* Oldest = Prints[0].Decal.Get())
		{
			Oldest->DestroyComponent();
		}
		Prints.RemoveAt(0);
		NextToFade = FMath::Max(NextToFade - 1, 0);
	}
	FPrint& Made = Prints.AddDefaulted_GetRef();
	Made.Decal = Decal;
	Made.Print = Print;
	Made.Strength = Strength;
	Made.BornSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Made.LifeSeconds = FMath::Max(LifeSeconds, 1.f);
}

float UAedirFootprintSubsystem::LastShare(const FPrint& Print, double Now) const
{
	// Slowly at first and slowly at the end, so the last of it goes unnoticed.
	return 1.f - FMath::SmoothStep(0.f, 1.f, static_cast<float>((Now - Print.BornSeconds) / Print.LifeSeconds));
}

float UAedirFootprintSubsystem::GetOldestShare() const
{
	return Prints.IsEmpty() || !GetWorld() ? 0.f : LastShare(Prints[0], GetWorld()->GetTimeSeconds());
}

void UAedirFootprintSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || Prints.IsEmpty())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// The oldest go when their time is up, or when they have gone already.
	while (!Prints.IsEmpty() && (!Prints[0].Decal.IsValid() || Now - Prints[0].BornSeconds >= Prints[0].LifeSeconds))
	{
		if (UDecalComponent* Oldest = Prints[0].Decal.Get())
		{
			Oldest->DestroyComponent();
		}
		Prints.RemoveAt(0);
		NextToFade = FMath::Max(NextToFade - 1, 0);
	}
	for (int32 Step = 0; Step < FootprintFadePerFrame && !Prints.IsEmpty(); ++Step)
	{
		NextToFade = NextToFade >= Prints.Num() ? 0 : NextToFade;
		const FPrint& Print = Prints[NextToFade++];
		if (UMaterialInstanceDynamic* Material = Print.Print.Get())
		{
			Material->SetScalarParameterValue(TEXT("Strength"), Print.Strength * LastShare(Print, Now));
		}
	}
}

TStatId UAedirFootprintSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAedirFootprintSubsystem, STATGROUP_Tickables);
}
