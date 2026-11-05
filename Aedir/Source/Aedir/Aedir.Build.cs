using UnrealBuildTool;

public class Aedir : ModuleRules
{
	public Aedir(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimationCore",
			"InputCore",
			"EnhancedInput",
			"PhysicsCore",
			"Niagara",
			"DeveloperSettings",
			"NetCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Sockets",
			"CoreOnline",
			"RenderCore"
		});
	}
}
