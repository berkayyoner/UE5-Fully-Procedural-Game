using System.IO;
using UnrealBuildTool;

public class Aedir : ModuleRules
{
	public Aedir(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		// The wind plugin's public header reaches into its own internal folder.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Plugins", "Experimental", "DynamicWind", "Source", "DynamicWind", "Internal"));

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
			"DynamicWind",
			"Landscape",
			"ProceduralMeshComponent",
			"DeveloperSettings",
			"NetCore",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Sockets",
			"CoreOnline",
			"RenderCore",
			"RHI"
		});
	}
}
