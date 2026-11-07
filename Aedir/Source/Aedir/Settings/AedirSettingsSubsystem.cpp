#include "Settings/AedirSettingsSubsystem.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Graphics/AedirUpscalerSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Widgets/SWindow.h"

namespace
{
	const TCHAR* const SettingsIniSection = TEXT("Aedir.Settings");
	constexpr int32 SettingsMonitorWindowInset = 40;

	const TArray<FString> SettingsOffOn = { TEXT("Off"), TEXT("On") };
	const TArray<FString> SettingsQualityNames = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	const TCHAR* const SettingsPresetKey = TEXT("GraphicsPreset");
	constexpr int32 SettingsPresetCustom = 4;

	// The lines of the graphics page a preset sets on top of the quality groups, as a value for each of Low, Medium, High and Epic.
	struct FSettingsPresetLine
	{
		const TCHAR* Key;
		int32 Values[SettingsPresetCustom];
	};
	const FSettingsPresetLine SettingsPresetLines[] = {
		{ TEXT("VirtualShadows"), { 0, 0, 1, 1 } },
		{ TEXT("ShadowResolution"), { 1, 2, 2, 3 } },
		{ TEXT("ContactShadows"), { 0, 0, 1, 1 } },
		{ TEXT("VolumetricFog"), { 0, 1, 1, 1 } },
		{ TEXT("MotionBlur"), { 0, 2, 3, 4 } },
		{ TEXT("DepthOfField"), { 0, 2, 3, 4 } },
		{ TEXT("Bloom"), { 1, 2, 3, 4 } },
		{ TEXT("Anisotropy"), { 2, 3, 4, 4 } },
		{ TEXT("GrassDensity"), { 1, 2, 3, 3 } }
	};
	const TArray<FString> SettingsPercentNames = { TEXT("0%"), TEXT("10%"), TEXT("25%"), TEXT("50%"), TEXT("75%"), TEXT("100%") };
	const TArray<float> SettingsPercentValues = { 0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f };

	UGameUserSettings* SettingsGetUser()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	int32 SettingsNearestIndex(const TArray<float>& Values, float Value)
	{
		int32 Best = 0;
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			if (FMath::Abs(Values[Index] - Value) < FMath::Abs(Values[Best] - Value))
			{
				Best = Index;
			}
		}
		return Best;
	}

	TArray<FIntPoint> SettingsGetResolutions()
	{
		TArray<FIntPoint> Resolutions;
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
		if (const UGameUserSettings* User = SettingsGetUser())
		{
			Resolutions.AddUnique(User->GetScreenResolution());
		}
		Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		return Resolutions;
	}

	TSharedPtr<SWindow> SettingsGetGameWindow()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
	}

	TArray<FMonitorInfo> SettingsGetMonitors()
	{
		FDisplayMetrics Metrics;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().GetDisplayMetrics(Metrics);
		}
		return Metrics.MonitorInfo;
	}

	void SettingsMoveToMonitor(int32 Monitor)
	{
		// Inside the editor the game window is part of the editor window, which stays where it is.
		const TArray<FMonitorInfo> Monitors = SettingsGetMonitors();
		const TSharedPtr<SWindow> Window = SettingsGetGameWindow();
		UGameUserSettings* User = SettingsGetUser();
		if (GIsEditor || !Window.IsValid() || !User || !Monitors.IsValidIndex(Monitor))
		{
			return;
		}

		// A fullscreen window only moves after leaving fullscreen. Applying the settings afterwards brings the mode back.
		const FPlatformRect& Rect = Monitors[Monitor].DisplayRect;
		Window->SetWindowMode(EWindowMode::Windowed);
		Window->MoveWindowTo(FVector2D(Rect.Left + SettingsMonitorWindowInset, Rect.Top + SettingsMonitorWindowInset));
		User->ApplyResolutionSettings(false);
	}
}

FAedirGameplaySettings& UAedirSettingsSubsystem::GetValues()
{
	static FAedirGameplaySettings Values;
	return Values;
}

void UAedirSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UAedirUpscalerSubsystem>();
	BuildOptions();
	LoadSaved();
}

void UAedirSettingsSubsystem::BuildOptions()
{
	BuildPresetOption();
	BuildDisplayOptions();
	BuildQualityOptions();
	BuildAudioOptions();
	BuildGameplayOptions();
}

void UAedirSettingsSubsystem::AddChoice(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, TArray<FString> Labels, TFunction<int32()> Read, TFunction<void(int32)> Apply)
{
	FAedirSettingOption Option;
	Option.Page = Page;
	Option.Key = Key;
	Option.Name = Name;
	Option.Description = Description;
	Option.Values = MoveTemp(Labels);
	Option.Read = MoveTemp(Read);
	Option.Apply = MoveTemp(Apply);
	Options.Add(MoveTemp(Option));
}

void UAedirSettingsSubsystem::AddCVar(const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, const TCHAR* CVarName, TArray<FString> Labels, TArray<float> Values)
{
	// A variable that this engine build does not have gets no line in the menu.
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(CVarName);
	if (!Variable)
	{
		return;
	}

	// Set by code: renderer settings in the project configuration outrank anything lower.
	AddChoice(EAedirSettingsPage::Graphics, Key, Name, Description, MoveTemp(Labels),
		[Variable, Values]() { return SettingsNearestIndex(Values, Variable->GetFloat()); },
		[Variable, Values](int32 Index) { Variable->Set(Values[Index], ECVF_SetByCode); });
}

void UAedirSettingsSubsystem::AddFloat(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, TArray<FString> Labels, TArray<float> Values, float* Target)
{
	AddChoice(Page, Key, Name, Description, MoveTemp(Labels),
		[Target, Values]() { return SettingsNearestIndex(Values, *Target); },
		[Target, Values](int32 Index) { *Target = Values[Index]; });
}

void UAedirSettingsSubsystem::AddBool(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, bool* Target)
{
	AddChoice(Page, Key, Name, Description, SettingsOffOn,
		[Target]() { return *Target ? 1 : 0; },
		[Target](int32 Index) { *Target = Index != 0; });
}

void UAedirSettingsSubsystem::AddQuality(const TCHAR* Name, const TCHAR* Description, int32 (*Get)(const UGameUserSettings&), void (*Set)(UGameUserSettings&, int32))
{
	// The engine stores its scalability groups itself.
	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), Name, Description, SettingsQualityNames,
		[Get]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			return User ? FMath::Clamp(Get(*User), 0, SettingsQualityNames.Num() - 1) : 0;
		},
		[Set](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				Set(*User, Index);
				User->ApplyNonResolutionSettings();
				User->SaveSettings();
			}
		});
}

void UAedirSettingsSubsystem::BuildPresetOption()
{
	AddChoice(EAedirSettingsPage::Graphics, SettingsPresetKey, TEXT("Graphics preset"), TEXT("Sets the whole page at once. Changing a line below makes it Custom."), { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Custom") },
		[this]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			const int32 Level = User ? User->GetOverallScalabilityLevel() : INDEX_NONE;
			return bPresetCustom || Level < 0 || Level >= SettingsPresetCustom ? SettingsPresetCustom : Level;
		},
		[this](int32 Index)
		{
			// The engine and the lines below store what the preset set, so at the start of the game only the choice itself comes back.
			bPresetCustom = Index == SettingsPresetCustom;
			if (!bLoadingSaved && !bPresetCustom)
			{
				ApplyGraphicsPreset(Index);
			}
		});
}

void UAedirSettingsSubsystem::ApplyGraphicsPreset(int32 Preset)
{
	if (UGameUserSettings* User = SettingsGetUser())
	{
		User->SetOverallScalabilityLevel(Preset);
		User->ApplyNonResolutionSettings();
		User->SaveSettings();
	}

	bApplyingPreset = true;
	for (const FSettingsPresetLine& Line : SettingsPresetLines)
	{
		const int32 OptionIndex = Options.IndexOfByPredicate([&Line](const FAedirSettingOption& Option) { return Option.Key == Line.Key; });
		if (OptionIndex != INDEX_NONE)
		{
			SetOptionValue(OptionIndex, Line.Values[Preset]);
		}
	}
	bApplyingPreset = false;
}

void UAedirSettingsSubsystem::NoteGraphicsChange(const FAedirSettingOption& Option)
{
	if (bApplyingPreset || bLoadingSaved)
	{
		return;
	}

	// A line of the preset changed by hand: the page no longer is what the preset says.
	for (const FSettingsPresetLine& Line : SettingsPresetLines)
	{
		if (Option.Key == Line.Key)
		{
			bPresetCustom = true;
			const FAedirSettingOption* Preset = Options.FindByPredicate([](const FAedirSettingOption& Other) { return Other.Key == SettingsPresetKey; });
			if (Preset)
			{
				Store(*Preset, SettingsPresetCustom);
			}
			return;
		}
	}
}

void UAedirSettingsSubsystem::BuildDisplayOptions()
{
	TArray<FString> MonitorNames;
	int32 MonitorNumber = 1;
	for (const FMonitorInfo& Monitor : SettingsGetMonitors())
	{
		MonitorNames.Add(FString::Printf(TEXT("%d: %dx%d%s"), MonitorNumber++, Monitor.NativeWidth, Monitor.NativeHeight, Monitor.bIsPrimary ? TEXT(" (primary)") : TEXT("")));
	}
	if (MonitorNames.IsEmpty())
	{
		MonitorNames.Add(TEXT("1"));
	}
	AddChoice(EAedirSettingsPage::Graphics, TEXT("Monitor"), TEXT("Monitor"), TEXT("The display the game runs on. Only in the packaged or standalone game, the editor window stays where it is."), MonitorNames,
		[this]()
		{
			const TSharedPtr<SWindow> Window = SettingsGetGameWindow();
			const TArray<FMonitorInfo> Monitors = SettingsGetMonitors();
			if (Window.IsValid())
			{
				const FVector2D Center = Window->GetPositionInScreen() + Window->GetSizeInScreen() * 0.5;
				for (int32 Index = 0; Index < Monitors.Num(); ++Index)
				{
					const FPlatformRect& Rect = Monitors[Index].DisplayRect;
					if (Center.X >= Rect.Left && Center.X < Rect.Right && Center.Y >= Rect.Top && Center.Y < Rect.Bottom)
					{
						return Index;
					}
				}
			}
			return FMath::Max(SavedMonitor, 0);
		},
		[this](int32 Index)
		{
			SavedMonitor = Index;
			SettingsMoveToMonitor(Index);
		});

	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Window mode"), TEXT("Fullscreen, borderless window or a normal window."), { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") },
		[]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			return User ? static_cast<int32>(User->GetFullscreenMode()) : 0;
		},
		[](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				User->SetFullscreenMode(static_cast<EWindowMode::Type>(Index));
				User->ApplyResolutionSettings(false);
				User->SaveSettings();
			}
		});

	const TArray<FIntPoint> Resolutions = SettingsGetResolutions();
	TArray<FString> ResolutionNames;
	for (const FIntPoint& Resolution : Resolutions)
	{
		ResolutionNames.Add(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
	}
	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Resolution"), TEXT("Size of the game window or the screen mode."), ResolutionNames,
		[Resolutions]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			return User ? Resolutions.IndexOfByKey(User->GetScreenResolution()) : INDEX_NONE;
		},
		[Resolutions](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				User->SetScreenResolution(Resolutions[Index]);
				User->ApplyResolutionSettings(false);
				User->SaveSettings();
			}
		});

	const TArray<float> ScaleValues = { 50.f, 60.f, 70.f, 80.f, 90.f, 100.f };
	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Render scale"), TEXT("Share of the resolution that is really rendered."), { TEXT("50%"), TEXT("60%"), TEXT("70%"), TEXT("80%"), TEXT("90%"), TEXT("100%") },
		[ScaleValues]()
		{
			float Normalized = 0.f;
			float Value = 100.f;
			float Min = 0.f;
			float Max = 100.f;
			if (const UGameUserSettings* User = SettingsGetUser())
			{
				User->GetResolutionScaleInformationEx(Normalized, Value, Min, Max);
			}
			// The engine reports nothing until a scale has been set, which means the full picture.
			return SettingsNearestIndex(ScaleValues, Value > 0.f ? Value : 100.f);
		},
		[ScaleValues](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				User->SetResolutionScaleValueEx(ScaleValues[Index]);
				User->ApplyNonResolutionSettings();
				User->SaveSettings();
			}
		});

	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Vertical sync"), TEXT("Waits for the monitor between frames, against tearing."), SettingsOffOn,
		[]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			return User && User->IsVSyncEnabled() ? 1 : 0;
		},
		[](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				User->SetVSyncEnabled(Index != 0);
				User->ApplyNonResolutionSettings();
				User->SaveSettings();
			}
		});

	const TArray<float> FrameValues = { 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f, 0.f };
	AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Frame rate limit"), TEXT("The highest number of frames per second."), { TEXT("30"), TEXT("60"), TEXT("90"), TEXT("120"), TEXT("144"), TEXT("165"), TEXT("240"), TEXT("Unlimited") },
		[FrameValues]()
		{
			const UGameUserSettings* User = SettingsGetUser();
			return SettingsNearestIndex(FrameValues, User ? User->GetFrameRateLimit() : 0.f);
		},
		[FrameValues](int32 Index)
		{
			if (UGameUserSettings* User = SettingsGetUser())
			{
				User->SetFrameRateLimit(FrameValues[Index]);
				User->ApplyNonResolutionSettings();
				User->SaveSettings();
			}
		});

	AddCVar(TEXT("Gamma"), TEXT("Brightness"), TEXT("Gamma of the final picture."), TEXT("r.TonemapperGamma"), { TEXT("Default"), TEXT("Darkest"), TEXT("Darker"), TEXT("Brighter"), TEXT("Brightest") }, { 0.f, 1.8f, 2.f, 2.4f, 2.6f });

	UAedirUpscalerSubsystem* Upscaler = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAedirUpscalerSubsystem>() : nullptr;
	if (Upscaler)
	{
		const TArray<EAedirUpscaler> Available = Upscaler->GetAvailableUpscalers();
		TArray<FString> UpscalerNames;
		for (const EAedirUpscaler Entry : Available)
		{
			UpscalerNames.Add(Upscaler->GetUpscalerDisplayName(Entry).ToString());
		}
		if (!UpscalerNames.IsEmpty())
		{
			AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Upscaler"), TEXT("Renders fewer pixels and rebuilds the full picture."), UpscalerNames,
				[Upscaler, Available]() { return Available.IndexOfByKey(Upscaler->GetCurrentUpscaler()); },
				[Upscaler, Available](int32 Index) { Upscaler->SetUpscaler(Available[Index], Upscaler->GetCurrentQuality()); });
			AddChoice(EAedirSettingsPage::Graphics, TEXT(""), TEXT("Upscaler quality"), TEXT("How much of the picture the upscaler rebuilds."), { TEXT("Native"), TEXT("Quality"), TEXT("Balanced"), TEXT("Performance"), TEXT("Ultra Performance") },
				[Upscaler]() { return static_cast<int32>(Upscaler->GetCurrentQuality()); },
				[Upscaler](int32 Index) { Upscaler->SetUpscaler(Upscaler->GetCurrentUpscaler(), static_cast<EAedirUpscalerQuality>(Index)); });
		}
	}

	AddCVar(TEXT("AntiAliasingMethod"), TEXT("Anti-aliasing method"), TEXT("How edges are smoothed. An upscaler can take this over."), TEXT("r.AntiAliasingMethod"), { TEXT("None"), TEXT("FXAA"), TEXT("TAA"), TEXT("MSAA"), TEXT("TSR") }, { 0.f, 1.f, 2.f, 3.f, 4.f });
}

void UAedirSettingsSubsystem::BuildQualityOptions()
{
	AddQuality(TEXT("View distance"), TEXT("How far objects stay detailed."), [](const UGameUserSettings& U) { return U.GetViewDistanceQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetViewDistanceQuality(V); });
	AddQuality(TEXT("Anti-aliasing quality"), TEXT("Quality of the edge smoothing."), [](const UGameUserSettings& U) { return U.GetAntiAliasingQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetAntiAliasingQuality(V); });
	AddQuality(TEXT("Shadows"), TEXT("Resolution and reach of shadows."), [](const UGameUserSettings& U) { return U.GetShadowQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetShadowQuality(V); });
	AddQuality(TEXT("Global illumination"), TEXT("Quality of bounced light."), [](const UGameUserSettings& U) { return U.GetGlobalIlluminationQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetGlobalIlluminationQuality(V); });
	AddQuality(TEXT("Reflections"), TEXT("Quality of reflected light."), [](const UGameUserSettings& U) { return U.GetReflectionQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetReflectionQuality(V); });
	AddQuality(TEXT("Post processing"), TEXT("Quality of the screen effects."), [](const UGameUserSettings& U) { return U.GetPostProcessingQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetPostProcessingQuality(V); });
	AddQuality(TEXT("Textures"), TEXT("Detail of surfaces, uses video memory."), [](const UGameUserSettings& U) { return U.GetTextureQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetTextureQuality(V); });
	AddQuality(TEXT("Effects"), TEXT("Detail of particles and weather."), [](const UGameUserSettings& U) { return U.GetVisualEffectQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetVisualEffectQuality(V); });
	AddQuality(TEXT("Foliage"), TEXT("Amount and reach of plants."), [](const UGameUserSettings& U) { return U.GetFoliageQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetFoliageQuality(V); });
	AddQuality(TEXT("Shading"), TEXT("Quality of the materials."), [](const UGameUserSettings& U) { return U.GetShadingQuality(); }, [](UGameUserSettings& U, int32 V) { U.SetShadingQuality(V); });

	const TArray<FString> Levels = { TEXT("Off"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Highest") };
	const TArray<float> LevelValues = { 0.f, 1.f, 2.f, 3.f, 4.f };
	AddCVar(TEXT("GIMethod"), TEXT("Global illumination method"), TEXT("Lumen is real time bounced light. Off is much faster and much darker."), TEXT("r.DynamicGlobalIlluminationMethod"), { TEXT("Off"), TEXT("Lumen") }, { 0.f, 1.f });
	AddCVar(TEXT("ReflectionMethod"), TEXT("Reflection method"), TEXT("Lumen or the cheaper screen space reflections."), TEXT("r.ReflectionMethod"), { TEXT("Off"), TEXT("Lumen"), TEXT("Screen space") }, { 0.f, 1.f, 2.f });
	AddCVar(TEXT("LumenHWRT"), TEXT("Hardware ray tracing"), TEXT("Lumen uses the ray tracing of the video card, if it has any."), TEXT("r.Lumen.HardwareRayTracing"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("VirtualShadows"), TEXT("Virtual shadow maps"), TEXT("Very sharp shadows at a high cost."), TEXT("r.Shadow.Virtual.Enable"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("Nanite"), TEXT("Nanite"), TEXT("Automatic geometry detail."), TEXT("r.Nanite"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("ShadowResolution"), TEXT("Shadow resolution"), TEXT("Largest size of a shadow map."), TEXT("r.Shadow.MaxResolution"), { TEXT("512"), TEXT("1024"), TEXT("2048"), TEXT("4096") }, { 512.f, 1024.f, 2048.f, 4096.f });
	AddCVar(TEXT("ContactShadows"), TEXT("Contact shadows"), TEXT("Fine shadows where objects touch."), TEXT("r.ContactShadows"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("AmbientOcclusion"), TEXT("Ambient occlusion"), TEXT("Darkens creases and corners."), TEXT("r.AmbientOcclusionLevels"), { TEXT("Automatic"), TEXT("Off"), TEXT("Low"), TEXT("High") }, { -1.f, 0.f, 1.f, 3.f });
	AddCVar(TEXT("VolumetricFog"), TEXT("Volumetric fog"), TEXT("Light shafts in the air."), TEXT("r.VolumetricFog"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("VolumetricClouds"), TEXT("Volumetric clouds"), TEXT("Clouds with real depth."), TEXT("r.VolumetricCloud"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("MotionBlur"), TEXT("Motion blur"), TEXT("Blurs what moves fast."), TEXT("r.MotionBlurQuality"), Levels, LevelValues);
	AddCVar(TEXT("DepthOfField"), TEXT("Depth of field"), TEXT("Blurs what is out of focus."), TEXT("r.DepthOfFieldQuality"), Levels, LevelValues);
	AddCVar(TEXT("Bloom"), TEXT("Bloom"), TEXT("Glow around bright light."), TEXT("r.BloomQuality"), Levels, LevelValues);
	AddCVar(TEXT("LensFlares"), TEXT("Lens flares"), TEXT("Flares from bright lights."), TEXT("r.LensFlareQuality"), { TEXT("Off"), TEXT("Low"), TEXT("Medium"), TEXT("High") }, { 0.f, 1.f, 2.f, 3.f });
	AddCVar(TEXT("ChromaticAberration"), TEXT("Chromatic aberration"), TEXT("Colour fringes towards the screen edge."), TEXT("r.SceneColorFringeQuality"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("FilmGrain"), TEXT("Film grain"), TEXT("Noise on the picture."), TEXT("r.FilmGrain"), SettingsOffOn, { 0.f, 1.f });
	AddCVar(TEXT("Sharpen"), TEXT("Sharpening"), TEXT("Sharpens the final picture."), TEXT("r.Tonemapper.Sharpen"), { TEXT("Off"), TEXT("Light"), TEXT("Medium"), TEXT("Strong") }, { 0.f, 0.5f, 1.f, 1.5f });
	AddCVar(TEXT("Anisotropy"), TEXT("Texture filtering"), TEXT("Keeps surfaces sharp at a flat angle."), TEXT("r.MaxAnisotropy"), { TEXT("Off"), TEXT("2x"), TEXT("4x"), TEXT("8x"), TEXT("16x") }, { 0.f, 2.f, 4.f, 8.f, 16.f });
	AddCVar(TEXT("TexturePool"), TEXT("Texture memory"), TEXT("Video memory set aside for textures, in MB."), TEXT("r.Streaming.PoolSize"), { TEXT("1000 MB"), TEXT("2000 MB"), TEXT("3000 MB"), TEXT("4000 MB"), TEXT("6000 MB"), TEXT("8000 MB") }, { 1000.f, 2000.f, 3000.f, 4000.f, 6000.f, 8000.f });
	AddCVar(TEXT("ViewDistanceScale"), TEXT("View distance scale"), TEXT("Fine multiplier on the distance objects disappear at."), TEXT("r.ViewDistanceScale"), { TEXT("50%"), TEXT("75%"), TEXT("100%"), TEXT("150%"), TEXT("200%") }, { 0.5f, 0.75f, 1.f, 1.5f, 2.f });
	AddCVar(TEXT("MeshDetail"), TEXT("Mesh detail distance"), TEXT("How long meshes keep their full detail. Lower is more detail."), TEXT("r.StaticMeshLODDistanceScale"), { TEXT("50%"), TEXT("100%"), TEXT("150%"), TEXT("200%") }, { 0.5f, 1.f, 1.5f, 2.f });
	AddCVar(TEXT("CharacterDetail"), TEXT("Character detail"), TEXT("Detail steps skipped on characters."), TEXT("r.SkeletalMeshLODBias"), { TEXT("Full"), TEXT("Reduced"), TEXT("Low") }, { 0.f, 1.f, 2.f });
	AddCVar(TEXT("GrassDensity"), TEXT("Grass density"), TEXT("Amount of generated grass."), TEXT("grass.DensityScale"), { TEXT("25%"), TEXT("50%"), TEXT("75%"), TEXT("100%") }, { 0.25f, 0.5f, 0.75f, 1.f });

	// Read by the engine every frame, so a new rate counts from the next frame on.
	const TArray<float> RateValues = { 30.f, 60.f, 90.f, 120.f, 180.f, 240.f };
	AddChoice(EAedirSettingsPage::Graphics, TEXT("PhysicsRate"), TEXT("Physics simulation rate"), TEXT("Steps per second of bodies, ragdolls and loose items. Higher is steadier and costs more."), { TEXT("30 Hz"), TEXT("60 Hz"), TEXT("90 Hz"), TEXT("120 Hz"), TEXT("180 Hz"), TEXT("240 Hz") },
		[RateValues]()
		{
			const UPhysicsSettings* Physics = UPhysicsSettings::Get();
			return SettingsNearestIndex(RateValues, 1.f / FMath::Max(Physics->MaxSubstepDeltaTime, KINDA_SMALL_NUMBER));
		},
		[RateValues](int32 Index)
		{
			UPhysicsSettings* Physics = UPhysicsSettings::Get();
			Physics->bSubstepping = true;
			Physics->MaxSubstepDeltaTime = 1.f / RateValues[Index];
			Physics->MaxSubsteps = 16;
		});
	AddCVar(TEXT("ClothPhysics"), TEXT("Cloth physics"), TEXT("Simulated cloth on characters."), TEXT("p.ClothPhysics"), SettingsOffOn, { 0.f, 1.f });
}

void UAedirSettingsSubsystem::BuildAudioOptions()
{
	AddChoice(EAedirSettingsPage::Audio, TEXT("MasterVolume"), TEXT("Master volume"), TEXT("Volume of everything."), SettingsPercentNames,
		[this]() { return SettingsNearestIndex(SettingsPercentValues, MasterVolume); },
		[this](int32 Index)
		{
			MasterVolume = SettingsPercentValues[Index];
			if (FAudioDevice* Device = GEngine ? GEngine->GetMainAudioDevice().GetAudioDevice() : nullptr)
			{
				Device->SetTransientPrimaryVolume(MasterVolume);
			}
		});

	FAedirGameplaySettings& Values = GetValues();
	AddFloat(EAedirSettingsPage::Audio, TEXT("FootstepVolume"), TEXT("Footsteps"), TEXT("Volume of steps and landings."), SettingsPercentNames, SettingsPercentValues, &Values.FootstepVolume);
	AddFloat(EAedirSettingsPage::Audio, TEXT("WeatherVolume"), TEXT("Weather"), TEXT("Volume of rain, wind and thunder."), SettingsPercentNames, SettingsPercentValues, &Values.WeatherVolume);
	AddChoice(EAedirSettingsPage::Audio, TEXT("BackgroundSound"), TEXT("Sound in the background"), TEXT("Whether the game stays audible while another window has the focus."), SettingsOffOn,
		[]() { return FApp::GetUnfocusedVolumeMultiplier() > 0.f ? 1 : 0; },
		[](int32 Index) { FApp::SetUnfocusedVolumeMultiplier(Index != 0 ? 1.f : 0.f); });
}

void UAedirSettingsSubsystem::BuildGameplayOptions()
{
	FAedirGameplaySettings& Values = GetValues();
	const TArray<FString> SpeedNames = { TEXT("25%"), TEXT("50%"), TEXT("75%"), TEXT("100%"), TEXT("125%"), TEXT("150%"), TEXT("200%") };
	const TArray<float> SpeedValues = { 0.25f, 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 2.f };
	AddFloat(EAedirSettingsPage::Gameplay, TEXT("MouseSensitivity"), TEXT("Camera sensitivity"), TEXT("How fast the mouse turns the camera."), SpeedNames, SpeedValues, &Values.MouseSensitivity);
	AddFloat(EAedirSettingsPage::Gameplay, TEXT("WeaponSensitivity"), TEXT("Weapon sensitivity"), TEXT("How fast the mouse moves arms and weapons in the stance."), SpeedNames, SpeedValues, &Values.WeaponSensitivity);
	AddBool(EAedirSettingsPage::Gameplay, TEXT("InvertMouseY"), TEXT("Invert camera"), TEXT("Moving the mouse up looks down."), &Values.bInvertMouseY);
	AddFloat(EAedirSettingsPage::Gameplay, TEXT("FieldOfView"), TEXT("Field of view"), TEXT("How wide the third person camera sees."), { TEXT("70"), TEXT("80"), TEXT("90"), TEXT("100"), TEXT("110") }, { 70.f, 80.f, 90.f, 100.f, 110.f }, &Values.FieldOfView);
	AddFloat(EAedirSettingsPage::Gameplay, TEXT("CameraBodyMotion"), TEXT("Camera body motion"), TEXT("How much the camera moves with steps, jumps and landings."), { TEXT("Off"), TEXT("50%"), TEXT("100%") }, { 0.f, 0.5f, 1.f }, &Values.CameraBodyMotion);
	AddBool(EAedirSettingsPage::Gameplay, TEXT("ShowCompass"), TEXT("Compass"), TEXT("Shows the compass at the top of the screen."), &Values.bShowCompass);
	AddBool(EAedirSettingsPage::Gameplay, TEXT("ShowBars"), TEXT("Status bars"), TEXT("Shows the health, stamina and mana bars."), &Values.bShowBars);
}

void UAedirSettingsSubsystem::LoadSaved()
{
	if (!GConfig)
	{
		return;
	}

	bLoadingSaved = true;
	for (const FAedirSettingOption& Option : Options)
	{
		FString Saved;
		if (Option.Key.IsEmpty() || !GConfig->GetString(SettingsIniSection, *Option.Key, Saved, GGameUserSettingsIni))
		{
			continue;
		}

		const int32 ValueIndex = Option.Values.IndexOfByKey(Saved);
		if (ValueIndex != INDEX_NONE)
		{
			Option.Apply(ValueIndex);
		}
	}
	bLoadingSaved = false;
}

void UAedirSettingsSubsystem::Store(const FAedirSettingOption& Option, int32 ValueIndex) const
{
	if (GConfig && !Option.Key.IsEmpty())
	{
		GConfig->SetString(SettingsIniSection, *Option.Key, *Option.Values[ValueIndex], GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UAedirSettingsSubsystem::ApplyWhenWorldReady()
{
	if (bWorldReadyApplied)
	{
		return;
	}
	bWorldReadyApplied = true;

	if (SavedMonitor != INDEX_NONE)
	{
		SettingsMoveToMonitor(SavedMonitor);
	}
	if (FAudioDevice* Device = GEngine ? GEngine->GetMainAudioDevice().GetAudioDevice() : nullptr)
	{
		Device->SetTransientPrimaryVolume(MasterVolume);
	}
}

TArray<int32> UAedirSettingsSubsystem::GetPageOptions(EAedirSettingsPage Page) const
{
	TArray<int32> Result;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		if (Options[Index].Page == Page)
		{
			Result.Add(Index);
		}
	}
	return Result;
}

const FAedirSettingOption* UAedirSettingsSubsystem::GetOption(int32 Index) const
{
	return Options.IsValidIndex(Index) ? &Options[Index] : nullptr;
}

FString UAedirSettingsSubsystem::GetValueText(int32 Index) const
{
	const FAedirSettingOption* Option = GetOption(Index);
	if (!Option)
	{
		return FString();
	}

	const int32 Current = Option->Read();
	return Option->Values.IsValidIndex(Current) ? Option->Values[Current] : FString(TEXT("Custom"));
}

void UAedirSettingsSubsystem::SetOptionValue(int32 Index, int32 ValueIndex)
{
	const FAedirSettingOption* Option = GetOption(Index);
	if (Option && Option->Values.IsValidIndex(ValueIndex))
	{
		Option->Apply(ValueIndex);
		Store(*Option, ValueIndex);
		NoteGraphicsChange(*Option);
	}
}

void UAedirSettingsSubsystem::StepOption(int32 Index, int32 Delta)
{
	const FAedirSettingOption* Option = GetOption(Index);
	if (!Option || Option->Values.IsEmpty())
	{
		return;
	}

	const int32 Count = Option->Values.Num();
	const int32 Current = Option->Read();
	const int32 Next = Option->Values.IsValidIndex(Current) ? (Current + Delta + Count) % Count : 0;
	Option->Apply(Next);
	Store(*Option, Next);
	NoteGraphicsChange(*Option);
}
