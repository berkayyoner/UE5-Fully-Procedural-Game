using UnrealBuildTool;
using System.Collections.Generic;

public class AedirTarget : TargetRules
{
	public AedirTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Aedir");
	}
}
