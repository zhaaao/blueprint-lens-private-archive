using UnrealBuildTool;

public class BlueprintLensEditor : ModuleRules
{
	public BlueprintLensEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"BlueprintLensExporter",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Kismet",
			"BlueprintGraph",
			"DesktopPlatform",
			"GraphEditor",
			"InputCore",
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities",
			"Projects",
			"PlatformCrypto",
			"PlatformCryptoContext"
		});
	}
}
