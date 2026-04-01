// UEInsightMCP Build.cs — Unreal Insights MCP Plugin
// Supports both UE5.4+ and UE4.26 RDCSP

using UnrealBuildTool;

public class UEInsightMCP : ModuleRules
{
	public UEInsightMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor framework
			"UnrealEd",
			"EditorSubsystem",
			"Json",
			"JsonUtilities",
			"Networking",
			"Sockets",
			"AssetTools",

			// ★ Trace/Insights modules (exist in both UE4 RDCSP and UE5)
			"TraceLog",             // Trace namespace, Trace emit API
			"TraceAnalysis",        // IAnalyzer, Trace event parsing
			"TraceServices",        // IAnalysisSession, analysis session management
		});

		// EditorScriptingUtilities only exists in UE5
		if (Target.Version.MajorVersion >= 5)
		{
			PrivateDependencyModuleNames.Add("EditorScriptingUtilities");
		}

		// Note: FTraceAuxiliary is in Core module (Runtime/Core/Public/ProfilingDebugging/),
		// no extra dependency needed for it.

		// RTTI and exceptions for crash protection
		bUseRTTI = true;
		bEnableExceptions = true;
	}
}
