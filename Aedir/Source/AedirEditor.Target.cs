using UnrealBuildTool;
using System.Collections.Generic;

public class AedirEditorTarget : TargetRules
{
	public AedirEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Aedir", "AedirEditor" });
	}
}
