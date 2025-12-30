// Copyright BULKHEAD Limited. All Rights Reserved.

using UnrealBuildTool;

public class WDEditor : ModuleRules
{
	public WDEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"OnlineSubsystemPragmaTypes",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AnimationBlueprintLibrary",
			"AnimationModifiers",
			"AssetRegistry",
			"AssetTools",
			"ApplicationCore",
			"AppFramework",
			"Blutility",
			"CommonUI",
			"ContentBrowser",
			"Core",
			"CoreUObject",
			"DataValidation",
			"DataRegistry",
			"DeveloperSettings",
			"Engine",
			"Foliage",
			"GraphEditor",
			"GameplayTags",
			"GameplayTagsEditor",
			"InputCore",
			"Json",
			"JsonUtilities",
			"Landscape",
			"MessageLog",
			"PhysicsCore",
			"PropertyEditor",
			"RenderCore",
			"RHI",
			"ScriptableEditorWidgets",
			"Slate",
			"SlateCore",
			"SourceControl",
			"ToolMenus",
			"UMG",
			"UnrealEd",
			"WorldPartitionEditor",
			"ImageCore",
			"PCG",
			"GeometryCore",
			"GeometryFramework",
			"PCGGeometryScriptInterop",
			"StaticMeshEditor",
			"EditorScriptingUtilities",

			// Project Modules
			"BHFramework",
			"BHInventory",
			"BHItems",
			"BHModularGameplay",
			"Conductor",
			"Mechanized",
			"SDFutureExtensions",
			"WDGame",
			"WDTestUtils",
			"MinimapCaptureEditor",
			"MinimapCaptureRuntime",
			"ModularVehicles",
			"ModularVehiclesEditor",
			"SDFiniteStateMachine",
			"WorkspaceMenuStructure",
			"SDOnlineEditor",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"OnlineSubsystemPragma",
			"UMGEditor",
			"WorldPartitionHLODUtilities",
			"VirtualTexturingEditor",
			"ErrantPathsRuntime",
			"BHDemolitionEngine",
			"BHDemolitionEditor"
		});
	}
}