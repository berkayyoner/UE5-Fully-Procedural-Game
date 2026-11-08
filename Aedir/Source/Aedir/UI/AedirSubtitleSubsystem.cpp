#include "UI/AedirSubtitleSubsystem.h"

#include "Engine/World.h"

namespace
{
	constexpr float SubtitleHearingCm = 1400.f;
	constexpr float SubtitleSecondsPerLetter = 0.055f;
	constexpr float SubtitleMinSeconds = 1.6f;
	constexpr float SubtitleMaxSeconds = 9.f;
	constexpr int32 SubtitleMaxKept = 12;
}

float UAedirSubtitleSubsystem::GetSpeakSeconds(const FText& Line)
{
	return FMath::Clamp(Line.ToString().Len() * SubtitleSecondsPerLetter, SubtitleMinSeconds, SubtitleMaxSeconds);
}

void UAedirSubtitleSubsystem::Say(const FText& Speaker, const FText& Line, const FVector& Location, float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World || Line.IsEmpty())
	{
		return;
	}

	// A speaker says one thing at a time: a new line ends what that speaker was still saying.
	const double Now = World->GetTimeSeconds();
	Lines.RemoveAll([&Speaker, Now](const FAedirSubtitle& Old) { return Old.Until <= Now || Old.Speaker.EqualTo(Speaker); });
	FAedirSubtitle& Added = Lines.AddDefaulted_GetRef();
	Added.Speaker = Speaker;
	Added.Line = Line;
	Added.Location = Location;
	Added.Until = Now + Seconds;
	if (Lines.Num() > SubtitleMaxKept)
	{
		Lines.RemoveAt(0);
	}
}

void UAedirSubtitleSubsystem::GetHeard(const FVector& Listener, TArray<FAedirSubtitle>& OutLines) const
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (const FAedirSubtitle& Line : Lines)
	{
		if (Line.Until > Now && FVector::DistSquared(Line.Location, Listener) <= FMath::Square(SubtitleHearingCm))
		{
			OutLines.Add(Line);
		}
	}
}
