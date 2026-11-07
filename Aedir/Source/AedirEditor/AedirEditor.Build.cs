using UnrealBuildTool;

public class AedirEditor : ModuleRules
{
	public AedirEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"MaterialEditor",
			"RHI",
			"AssetRegistry",
			"AssetTools",
			"EditorSubsystem",
			"Slate",
			"SlateCore",
			"InputCore",
			"Aedir"
		});
	}
}
