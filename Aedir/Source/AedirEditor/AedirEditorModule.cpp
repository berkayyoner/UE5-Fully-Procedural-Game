#include "AedirEditor.h"

#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Editor.h"
#include "Kismet2/DebuggerCommands.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAedirEditor);

namespace
{
	const FName EditorPlayWorldContext(TEXT("PlayWorld"));
	const FName EditorStopPlayCommand(TEXT("StopPlaySession"));

	// Escape belongs to the in-game pause menu, so the editor's Stop Play shortcut moves to P.
	void EditorRebindStopPlayShortcut()
	{
		FInputBindingManager& Bindings = FInputBindingManager::Get();
		const TSharedPtr<FUICommandInfo> StopCommand = Bindings.FindCommandInContext(EditorPlayWorldContext, EditorStopPlayCommand);
		if (!StopCommand.IsValid())
		{
			UE_LOG(LogAedirEditor, Warning, TEXT("Stop Play shortcut not rebound: command %s in context %s was not found"), *EditorStopPlayCommand.ToString(), *EditorPlayWorldContext.ToString());
			return;
		}

		const FInputChord NewChord(EKeys::P);
		const FInputChord ActiveChord = *StopCommand->GetActiveChord(EMultipleKeyBindingIndex::Primary);
		if (ActiveChord == NewChord)
		{
			return;
		}

		if (ActiveChord != FInputChord(EKeys::Escape))
		{
			UE_LOG(LogAedirEditor, Log, TEXT("Stop Play shortcut left as %s (customised by the user)"), *ActiveChord.GetInputText().ToString());
			return;
		}

		if (Bindings.FindCommandInContext(EditorPlayWorldContext, NewChord, false).IsValid())
		{
			UE_LOG(LogAedirEditor, Warning, TEXT("Stop Play shortcut not rebound: P is already used in the Play World context"));
			return;
		}

		StopCommand->SetActiveChord(NewChord, EMultipleKeyBindingIndex::Primary);
		Bindings.SaveInputBindings();
		UE_LOG(LogAedirEditor, Log, TEXT("Stop Play in Editor shortcut moved from Escape to P so Escape opens the pause menu"));
	}

	class FAedirEditorModule : public FDefaultGameModuleImpl
	{
	public:
		virtual void StartupModule() override
		{
			// UnrealEd registers the Play World commands while the editor initialises, which happens after modules like this one load.
			if (FPlayWorldCommands::IsRegistered())
			{
				EditorRebindStopPlayShortcut();
			}
			else
			{
				EditorInitializedHandle = FEditorDelegates::OnEditorInitialized.AddLambda([](double)
				{
					EditorRebindStopPlayShortcut();
				});
			}
		}

		virtual void ShutdownModule() override
		{
			FEditorDelegates::OnEditorInitialized.Remove(EditorInitializedHandle);
		}

	private:
		FDelegateHandle EditorInitializedHandle;
	};
}

IMPLEMENT_MODULE(FAedirEditorModule, AedirEditor);
