#include "Graphics/AedirUpscalerSubsystem.h"

#include "Aedir.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#if AEDIR_WITH_DLSSG
#include "StreamlineLibraryDLSSG.h"
#endif

namespace
{
	const TCHAR* const AedirUpscalerConfigSection = TEXT("/Script/Aedir.AedirGraphics");
	const TCHAR* const AedirUpscalerConfigUpscalerKey = TEXT("Upscaler");
	const TCHAR* const AedirUpscalerConfigQualityKey = TEXT("Quality");

	const TCHAR* const AedirUpscalerDLSSEnableCVar = TEXT("r.NGX.DLSS.Enable");
	const TCHAR* const AedirUpscalerNGXEnableCVar = TEXT("r.NGX.Enable");
	const TCHAR* const AedirUpscalerFSREnableCVar = TEXT("r.FidelityFX.FSR.Enabled");
	const TCHAR* const AedirUpscalerXeSSEnableCVar = TEXT("r.XeSS.Enabled");

	const EAedirUpscaler AedirUpscalerPreferenceOrder[] =
	{
		EAedirUpscaler::DLSS,
		EAedirUpscaler::FSR,
		EAedirUpscaler::XeSS,
		EAedirUpscaler::TSR
	};

	bool AedirUpscalerIsAvailable(EAedirUpscaler Upscaler)
	{
		IConsoleManager& ConsoleManager = IConsoleManager::Get();

		switch (Upscaler)
		{
		case EAedirUpscaler::None:
		case EAedirUpscaler::TSR:
			return true;

		case EAedirUpscaler::DLSS:
		{
			const IConsoleVariable* DLSSEnable = ConsoleManager.FindConsoleVariable(AedirUpscalerDLSSEnableCVar);
			const IConsoleVariable* NGXEnable = ConsoleManager.FindConsoleVariable(AedirUpscalerNGXEnableCVar);
			return DLSSEnable != nullptr && NGXEnable != nullptr && NGXEnable->GetInt() != 0;
		}

		case EAedirUpscaler::FSR:
			return ConsoleManager.FindConsoleVariable(AedirUpscalerFSREnableCVar) != nullptr;

		case EAedirUpscaler::XeSS:
			return ConsoleManager.FindConsoleVariable(AedirUpscalerXeSSEnableCVar) != nullptr;
		}

		return false;
	}

	void AedirUpscalerSetIntIfPresent(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// The same priority as the settings menu writes with, so the last choice made is the one in force.
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	void AedirUpscalerSetFloatIfPresent(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			// The same priority as the settings menu writes with, so the last choice made is the one in force.
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	int32 AedirUpscalerReadIntOrDefault(const TCHAR* Name, int32 DefaultValue)
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return Variable->GetInt();
		}
		return DefaultValue;
	}

	float AedirUpscalerScreenPercentage(EAedirUpscalerQuality Quality)
	{
		switch (Quality)
		{
		case EAedirUpscalerQuality::Native:           return 100.f;
		case EAedirUpscalerQuality::Quality:          return 66.7f;
		case EAedirUpscalerQuality::Balanced:         return 58.f;
		case EAedirUpscalerQuality::Performance:      return 50.f;
		case EAedirUpscalerQuality::UltraPerformance: return 33.3f;
		}

		return 100.f;
	}

	void AedirUpscalerDisableAll()
	{
		AedirUpscalerSetIntIfPresent(AedirUpscalerDLSSEnableCVar, 0);
		AedirUpscalerSetIntIfPresent(AedirUpscalerFSREnableCVar, 0);
		AedirUpscalerSetIntIfPresent(AedirUpscalerXeSSEnableCVar, 0);
	}
}

namespace
{
	// Each upscaler has its own frame generation, and only that one can run with it.
	const TCHAR* AedirFrameGenerationCVar(EAedirUpscaler Upscaler)
	{
		const TCHAR* Name = Upscaler == EAedirUpscaler::DLSS ? TEXT("r.Streamline.DLSSG.Enable") : Upscaler == EAedirUpscaler::FSR ? TEXT("r.FidelityFX.FI.Enabled") : Upscaler == EAedirUpscaler::XeSS ? TEXT("r.XeFG.Enabled") : nullptr;
		return Name && IConsoleManager::Get().FindConsoleVariable(Name) ? Name : nullptr;
	}
	const TCHAR* const AedirFrameMultiplierKey = TEXT("FrameGenerationMultiplier");
	constexpr float AedirFrameGenerationRecheckSeconds = 1.f;
}

bool UAedirUpscalerSubsystem::IsFrameGenerationAvailable() const
{
	return GetFrameGenerationCVar() != nullptr;
}

const TCHAR* UAedirUpscalerSubsystem::GetFrameGenerationCVar() const
{
	return AedirFrameGenerationCVar(CurrentUpscaler);
}

bool UAedirUpscalerSubsystem::IsFrameGenerationOn() const
{
	const TCHAR* Name = GetFrameGenerationCVar();
	const IConsoleVariable* Variable = Name ? IConsoleManager::Get().FindConsoleVariable(Name) : nullptr;
	return Variable && Variable->GetInt() != 0;
}

TArray<int32> UAedirUpscalerSubsystem::GetFrameGenerationMultipliersFor(EAedirUpscaler Upscaler) const
{
	TArray<int32> Multipliers = { 1 };
	if (!AedirFrameGenerationCVar(Upscaler))
	{
		return Multipliers;
	}
	Multipliers.Add(2);
	if (Upscaler == EAedirUpscaler::DLSS)
	{
#if AEDIR_WITH_DLSSG
		// Several frames between two rendered ones need a newer card: the plugin knows which this one can draw.
		const EStreamlineDLSSGMode MoreFrames[] = { EStreamlineDLSSGMode::On3X, EStreamlineDLSSGMode::On4X, EStreamlineDLSSGMode::On5X, EStreamlineDLSSGMode::On6X };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(MoreFrames) && UStreamlineLibraryDLSSG::IsDLSSGModeSupported(MoreFrames[Index]); ++Index)
		{
			Multipliers.Add(Index + 3);
		}
#endif
	}
	else if (Upscaler == EAedirUpscaler::XeSS)
	{
		for (int32 Frames = 2; Frames <= AedirUpscalerReadIntOrDefault(TEXT("r.XeFG.MaxInterpolatedFramesSupported"), 1); ++Frames)
		{
			Multipliers.Add(Frames + 1);
		}
	}
	return Multipliers;
}

TArray<int32> UAedirUpscalerSubsystem::GetFrameGenerationMultipliers() const
{
	return GetFrameGenerationMultipliersFor(CurrentUpscaler);
}

int32 UAedirUpscalerSubsystem::GetFrameGenerationMultiplierLimit() const
{
	int32 Limit = 1;
	for (const EAedirUpscaler Upscaler : GetAvailableUpscalers())
	{
		Limit = FMath::Max(Limit, GetFrameGenerationMultipliersFor(Upscaler).Last());
	}
	return Limit;
}

int32 UAedirUpscalerSubsystem::GetFrameGenerationMultiplier() const
{
	if (!IsFrameGenerationOn())
	{
		return 1;
	}
	if (CurrentUpscaler == EAedirUpscaler::DLSS)
	{
		return AedirUpscalerReadIntOrDefault(TEXT("r.Streamline.DLSSG.FramesToGenerate"), 1) + 1;
	}
	if (CurrentUpscaler == EAedirUpscaler::XeSS)
	{
		return FMath::Max(AedirUpscalerReadIntOrDefault(TEXT("r.XeFG.MaxInterpolatedFrames"), 1), 1) + 1;
	}
	return 2;
}

void UAedirUpscalerSubsystem::SetFrameGenerationMultiplier(int32 Multiplier)
{
	FrameMultiplierWanted = FMath::Max(Multiplier, 1);
	ApplyFrameGeneration();
}

void UAedirUpscalerSubsystem::ApplyFrameGeneration()
{
	// Never two at once: every other one is off, and the one for this upscaler only if it is wanted.
	for (const TCHAR* Name : { TEXT("r.Streamline.DLSSG.Enable"), TEXT("r.FidelityFX.FI.Enabled"), TEXT("r.XeFG.Enabled") })
	{
		AedirUpscalerSetIntIfPresent(Name, 0);
	}
	// As many as wanted, or the most this upscaler and card can draw; the wish is kept for a later upscaler that can do more.
	int32 Applied = 1;
	for (const int32 Multiplier : GetFrameGenerationMultipliers())
	{
		if (Multiplier <= FrameMultiplierWanted)
		{
			Applied = FMath::Max(Applied, Multiplier);
		}
	}
	if (const TCHAR* Name = GetFrameGenerationCVar(); Name && Applied > 1)
	{
		AedirUpscalerSetIntIfPresent(TEXT("r.Streamline.DLSSG.FramesToGenerate"), Applied - 1);
		AedirUpscalerSetIntIfPresent(TEXT("r.XeFG.MaxInterpolatedFrames"), Applied - 1);
		AedirUpscalerSetIntIfPresent(TEXT("r.XeLL.Enabled"), CurrentUpscaler == EAedirUpscaler::XeSS ? 1 : 0);
		AedirUpscalerSetIntIfPresent(Name, 1);
	}
	UE_LOG(LogAedir, Log, TEXT("Frame generation %s: %dx wanted, %dx applied%s."), *GetUpscalerDisplayName(CurrentUpscaler).ToString(), FrameMultiplierWanted, Applied,
		GIsEditor && Applied > 1 ? TEXT(" (it runs in a game window only, not in the editor's viewport)") : TEXT(""));
	GConfig->SetInt(AedirUpscalerConfigSection, AedirFrameMultiplierKey, FrameMultiplierWanted, GGameUserSettingsIni);
	ApplyReflex();
}

bool UAedirUpscalerSubsystem::IsReflexAvailable() const
{
	return IConsoleManager::Get().FindConsoleVariable(TEXT("t.Streamline.Reflex.Enable")) != nullptr;
}

int32 UAedirUpscalerSubsystem::GetReflexMode() const
{
	// Frame generation cannot run without it: then it is at least on.
	return IsFrameGenerationOn() && CurrentUpscaler == EAedirUpscaler::DLSS ? FMath::Max(ReflexMode, 1) : ReflexMode;
}

void UAedirUpscalerSubsystem::SetReflexMode(int32 Mode)
{
	ReflexMode = FMath::Clamp(Mode, 0, 2);
	ApplyReflex();
}

void UAedirUpscalerSubsystem::ApplyReflex()
{
	const int32 Mode = GetReflexMode();
	AedirUpscalerSetIntIfPresent(TEXT("t.Streamline.Reflex.Enable"), Mode > 0 ? 1 : 0);
	AedirUpscalerSetIntIfPresent(TEXT("t.Streamline.Reflex.Mode"), Mode);
	GConfig->SetInt(AedirUpscalerConfigSection, TEXT("Reflex"), ReflexMode, GGameUserSettingsIni);
}

void UAedirUpscalerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Saved before the multiple could be chosen it was only on or off: on was two frames for one.
	bool bFrameGenerationWasOn = false;
	if (!GConfig->GetInt(AedirUpscalerConfigSection, AedirFrameMultiplierKey, FrameMultiplierWanted, GGameUserSettingsIni)
		&& GConfig->GetBool(AedirUpscalerConfigSection, TEXT("FrameGeneration"), bFrameGenerationWasOn, GGameUserSettingsIni))
	{
		FrameMultiplierWanted = bFrameGenerationWasOn ? 2 : 1;
	}
	GConfig->GetInt(AedirUpscalerConfigSection, TEXT("Reflex"), ReflexMode, GGameUserSettingsIni);
	Super::Initialize(Collection);

	// While the game starts NVIDIA's plugin cannot yet tell how many frames the card draws: the choice is put in again a moment
	// later, when it can.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		ApplyFrameGeneration();
		return false;
	}), AedirFrameGenerationRecheckSeconds);

	int32 SavedUpscaler = INDEX_NONE;
	int32 SavedQuality = INDEX_NONE;
	GConfig->GetInt(AedirUpscalerConfigSection, AedirUpscalerConfigUpscalerKey, SavedUpscaler, GGameUserSettingsIni);
	GConfig->GetInt(AedirUpscalerConfigSection, AedirUpscalerConfigQualityKey, SavedQuality, GGameUserSettingsIni);

	const bool bHasValidSavedSelection =
		SavedUpscaler >= static_cast<int32>(EAedirUpscaler::None) && SavedUpscaler <= static_cast<int32>(EAedirUpscaler::XeSS) &&
		SavedQuality >= static_cast<int32>(EAedirUpscalerQuality::Native) && SavedQuality <= static_cast<int32>(EAedirUpscalerQuality::UltraPerformance);

	if (bHasValidSavedSelection)
	{
		SetUpscaler(static_cast<EAedirUpscaler>(SavedUpscaler), static_cast<EAedirUpscalerQuality>(SavedQuality));
		return;
	}

	EAedirUpscaler BestUpscaler = EAedirUpscaler::TSR;
	for (const EAedirUpscaler Candidate : AedirUpscalerPreferenceOrder)
	{
		if (AedirUpscalerIsAvailable(Candidate))
		{
			BestUpscaler = Candidate;
			break;
		}
	}

	SetUpscaler(BestUpscaler, EAedirUpscalerQuality::Quality);
}

TArray<EAedirUpscaler> UAedirUpscalerSubsystem::GetAvailableUpscalers() const
{
	TArray<EAedirUpscaler> Available;
	Available.Add(EAedirUpscaler::None);
	Available.Add(EAedirUpscaler::TSR);

	for (const EAedirUpscaler Candidate : { EAedirUpscaler::DLSS, EAedirUpscaler::FSR, EAedirUpscaler::XeSS })
	{
		if (AedirUpscalerIsAvailable(Candidate))
		{
			Available.Add(Candidate);
		}
	}

	return Available;
}

bool UAedirUpscalerSubsystem::SetUpscaler(EAedirUpscaler Upscaler, EAedirUpscalerQuality Quality)
{
	const bool bRequestedAvailable = AedirUpscalerIsAvailable(Upscaler);
	const EAedirUpscaler AppliedUpscaler = bRequestedAvailable ? Upscaler : EAedirUpscaler::TSR;

	AedirUpscalerDisableAll();

	switch (AppliedUpscaler)
	{
	case EAedirUpscaler::None:
		AedirUpscalerSetIntIfPresent(TEXT("r.AntiAliasingMethod"), 2);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), 100.f);
		break;

	case EAedirUpscaler::TSR:
		AedirUpscalerSetIntIfPresent(TEXT("r.AntiAliasingMethod"), 4);
		AedirUpscalerSetIntIfPresent(TEXT("r.TSR.History.UpdateQuality"), 3);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;

	case EAedirUpscaler::DLSS:
		// The upscaler takes the place of the temporal anti-aliasing pass; with any other method it would do nothing.
		AedirUpscalerSetIntIfPresent(TEXT("r.AntiAliasingMethod"), 4);
		AedirUpscalerSetIntIfPresent(AedirUpscalerDLSSEnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;

	case EAedirUpscaler::FSR:
		// The upscaler takes the place of the temporal anti-aliasing pass; with any other method it would do nothing.
		AedirUpscalerSetIntIfPresent(TEXT("r.AntiAliasingMethod"), 4);
		AedirUpscalerSetIntIfPresent(TEXT("r.FidelityFX.FSR.QualityMode"), FMath::Clamp(static_cast<int32>(Quality), 0, 4));
		AedirUpscalerSetIntIfPresent(AedirUpscalerFSREnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;

	case EAedirUpscaler::XeSS:
		// The upscaler takes the place of the temporal anti-aliasing pass; with any other method it would do nothing.
		AedirUpscalerSetIntIfPresent(TEXT("r.AntiAliasingMethod"), 4);
		AedirUpscalerSetIntIfPresent(TEXT("r.XeSS.Quality"), Quality == EAedirUpscalerQuality::Native ? 6 : FMath::Clamp(4 - static_cast<int32>(Quality), 0, 4));
		AedirUpscalerSetIntIfPresent(AedirUpscalerXeSSEnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;
	}

	CurrentUpscaler = AppliedUpscaler;
	CurrentQuality = Quality;
	ApplyFrameGeneration();

	GConfig->SetInt(AedirUpscalerConfigSection, AedirUpscalerConfigUpscalerKey, static_cast<int32>(AppliedUpscaler), GGameUserSettingsIni);
	GConfig->SetInt(AedirUpscalerConfigSection, AedirUpscalerConfigQualityKey, static_cast<int32>(Quality), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);

	if (!bRequestedAvailable)
	{
		UE_LOG(LogAedir, Warning, TEXT("Upscaler %s is unavailable, falling back to TSR."), *GetUpscalerDisplayName(Upscaler).ToString());
	}

	UE_LOG(LogAedir, Log, TEXT("Upscaler set to %s, quality %s, screen percentage %.1f."),
		*GetUpscalerDisplayName(AppliedUpscaler).ToString(),
		*UEnum::GetValueAsString(Quality),
		AedirUpscalerScreenPercentage(Quality));

	return bRequestedAvailable;
}

EAedirUpscaler UAedirUpscalerSubsystem::GetCurrentUpscaler() const
{
	return CurrentUpscaler;
}

EAedirUpscalerQuality UAedirUpscalerSubsystem::GetCurrentQuality() const
{
	return CurrentQuality;
}

FText UAedirUpscalerSubsystem::GetUpscalerDisplayName(EAedirUpscaler Upscaler) const
{
	switch (Upscaler)
	{
	case EAedirUpscaler::None:  return NSLOCTEXT("AedirUpscaler", "UpscalerNone", "Off (TAA)");
	case EAedirUpscaler::TSR:   return NSLOCTEXT("AedirUpscaler", "UpscalerTSR", "Temporal Super Resolution");
	case EAedirUpscaler::DLSS:  return NSLOCTEXT("AedirUpscaler", "UpscalerDLSS", "NVIDIA DLSS");
	case EAedirUpscaler::FSR:   return NSLOCTEXT("AedirUpscaler", "UpscalerFSR", "AMD FSR");
	case EAedirUpscaler::XeSS:  return NSLOCTEXT("AedirUpscaler", "UpscalerXeSS", "Intel XeSS");
	}

	return FText::GetEmpty();
}

void UAedirUpscalerSubsystem::ApplyOptimizedRenderDefaults()
{
	AedirUpscalerSetIntIfPresent(TEXT("r.Nanite"), 1);

	if (IConsoleVariable* PoolSize = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Streaming.PoolSize")))
	{
		if (PoolSize->GetInt() < 3000)
		{
			PoolSize->Set(3000, ECVF_SetByGameSetting);
		}
	}

	AedirUpscalerSetIntIfPresent(TEXT("r.Shadow.Virtual.Enable"), 1);
	AedirUpscalerSetIntIfPresent(TEXT("r.TextureStreaming"), 1);
	AedirUpscalerSetFloatIfPresent(TEXT("r.Streaming.MipBias"), 0.f);
	AedirUpscalerSetIntIfPresent(TEXT("r.MaxAnisotropy"), 8);

	UE_LOG(LogAedir, Log, TEXT("Optimized render defaults applied: r.Nanite=%d r.Streaming.PoolSize=%d r.Shadow.Virtual.Enable=%d r.TextureStreaming=%d r.MaxAnisotropy=%d"),
		AedirUpscalerReadIntOrDefault(TEXT("r.Nanite"), -1),
		AedirUpscalerReadIntOrDefault(TEXT("r.Streaming.PoolSize"), -1),
		AedirUpscalerReadIntOrDefault(TEXT("r.Shadow.Virtual.Enable"), -1),
		AedirUpscalerReadIntOrDefault(TEXT("r.TextureStreaming"), -1),
		AedirUpscalerReadIntOrDefault(TEXT("r.MaxAnisotropy"), -1));
}
