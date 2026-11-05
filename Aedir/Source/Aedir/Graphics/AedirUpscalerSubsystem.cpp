#include "Graphics/AedirUpscalerSubsystem.h"

#include "Aedir.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

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
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}

	void AedirUpscalerSetFloatIfPresent(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
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

void UAedirUpscalerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

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
		AedirUpscalerSetIntIfPresent(AedirUpscalerDLSSEnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;

	case EAedirUpscaler::FSR:
		AedirUpscalerSetIntIfPresent(AedirUpscalerFSREnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;

	case EAedirUpscaler::XeSS:
		AedirUpscalerSetIntIfPresent(AedirUpscalerXeSSEnableCVar, 1);
		AedirUpscalerSetFloatIfPresent(TEXT("r.ScreenPercentage"), AedirUpscalerScreenPercentage(Quality));
		break;
	}

	CurrentUpscaler = AppliedUpscaler;
	CurrentQuality = Quality;

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
