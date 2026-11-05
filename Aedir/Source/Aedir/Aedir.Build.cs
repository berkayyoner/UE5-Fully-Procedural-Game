using UnrealBuildTool;

public class Aedir : ModuleRules
{
	public Aedir(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"DeveloperSettings",
			"NetCore"
		});
	}
}
