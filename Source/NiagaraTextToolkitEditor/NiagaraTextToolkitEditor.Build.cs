//Copyright Tranc Software, Inc. All Rights Reserved (2026)

using UnrealBuildTool;

public class NiagaraTextToolkitEditor : ModuleRules
{
	public NiagaraTextToolkitEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"NiagaraTextToolkit",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"Niagara",
                "NiagaraEditor",
                "Slate",
				"SlateCore",
				"AssetTools",
				"AssetRegistry",
				"ContentBrowser",
				"Projects",
				"PropertyEditor",
			}
		);
	}
}


