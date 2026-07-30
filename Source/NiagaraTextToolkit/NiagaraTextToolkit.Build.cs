//Copyright Tranc Software, Inc. All Rights Reserved (2026)

using UnrealBuildTool;

public class NiagaraTextToolkit : ModuleRules
{
	public NiagaraTextToolkit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "Niagara",
				"NiagaraShader",
				"RHI",
				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Niagara",
                "RenderCore",
				"NiagaraCore",
				"Projects",
				// ... add private dependencies that you statically link with here ...	
			}
			);

		// TODO: Probably don't need all of these
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                "TargetPlatform",
                "DerivedDataCache",
                "EditorFramework",
                "UnrealEd",
                "SlateCore",
                "Slate"
            });
        }


        DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
