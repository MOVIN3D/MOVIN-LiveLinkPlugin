using UnrealBuildTool;

public class MOVINLiveLink : ModuleRules
{
	public MOVINLiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Networking",
				"Sockets",
				"LiveLinkInterface",
				"LiveLinkAnimationCore",
				"LiveLink",
				"LiveLinkComponents",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"LiveLink",
				"LiveLinkComponents",
			}
		);

		PrecompileForTargets = PrecompileTargetsType.Any;
	}
}
