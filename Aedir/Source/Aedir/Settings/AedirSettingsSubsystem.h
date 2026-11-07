#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AedirSettingsSubsystem.generated.h"

enum class EAedirSettingsPage : uint8
{
	Graphics,
	Audio,
	Gameplay
};

// One line of the settings menu: a fixed list of values to step through, applied at once.
struct FAedirSettingOption
{
	EAedirSettingsPage Page = EAedirSettingsPage::Graphics;
	// Name in the ini file. Empty for options the engine or another subsystem stores itself.
	FString Key;
	FString Name;
	FString Description;
	TArray<FString> Values;
	// Index of the value in force now, INDEX_NONE when it is none of the listed ones.
	TFunction<int32()> Read;
	TFunction<void(int32)> Apply;
};

// Values that the game code reads directly.
struct FAedirGameplaySettings
{
	float MouseSensitivity = 1.f;
	float WeaponSensitivity = 1.f;
	bool bInvertMouseY = false;
	float FieldOfView = 90.f;
	float CameraBodyMotion = 1.f;
	bool bShowCompass = true;
	bool bShowBars = true;
	float FootstepVolume = 1.f;
	float WeatherVolume = 1.f;
};

// All player settings. The options are stored in GameUserSettings.ini and applied again on the next start.
UCLASS()
class AEDIR_API UAedirSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static FAedirGameplaySettings& GetValues();

	TArray<int32> GetPageOptions(EAedirSettingsPage Page) const;
	const FAedirSettingOption* GetOption(int32 Index) const;
	FString GetValueText(int32 Index) const;
	void StepOption(int32 Index, int32 Delta);
	void SetOptionValue(int32 Index, int32 ValueIndex);

	// What needs the game window and the audio device, which do not exist yet when the subsystem starts.
	void ApplyWhenWorldReady();

private:
	void BuildOptions();
	void BuildPresetOption();
	void ApplyGraphicsPreset(int32 Preset);
	void NoteGraphicsChange(const FAedirSettingOption& Option);
	void BuildDisplayOptions();
	void BuildQualityOptions();
	void BuildAudioOptions();
	void BuildGameplayOptions();
	void AddChoice(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, TArray<FString> Labels, TFunction<int32()> Read, TFunction<void(int32)> Apply);
	void AddCVar(const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, const TCHAR* CVarName, TArray<FString> Labels, TArray<float> Values);
	void AddFloat(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, TArray<FString> Labels, TArray<float> Values, float* Target);
	void AddBool(EAedirSettingsPage Page, const TCHAR* Key, const TCHAR* Name, const TCHAR* Description, bool* Target);
	void AddQuality(const TCHAR* Name, const TCHAR* Description, int32 (*Get)(const class UGameUserSettings&), void (*Set)(class UGameUserSettings&, int32));
	void LoadSaved();
	void Store(const FAedirSettingOption& Option, int32 ValueIndex) const;

	TArray<FAedirSettingOption> Options;
	float MasterVolume = 1.f;
	int32 SavedMonitor = INDEX_NONE;
	bool bWorldReadyApplied = false;
	bool bPresetCustom = false;
	bool bLoadingSaved = false;
	bool bApplyingPreset = false;
};
