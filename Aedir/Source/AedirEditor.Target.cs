using UnrealBuildTool;
using System.Collections.Generic;

public class AedirEditorTarget : TargetRules
{
	public AedirEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Aedir", "AedirEditor" });
	}
}
