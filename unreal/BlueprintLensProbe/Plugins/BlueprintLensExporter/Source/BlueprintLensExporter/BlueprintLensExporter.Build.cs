// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BlueprintLensExporter : ModuleRules
{
	public BlueprintLensExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"BlueprintLensProbe",
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"BlueprintGraph",
				"Json",
				"JsonUtilities",
				"PlatformCrypto",
				"PlatformCryptoContext"
			}
		);
	}
}
