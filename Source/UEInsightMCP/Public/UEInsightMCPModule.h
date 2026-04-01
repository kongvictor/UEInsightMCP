// UEInsightMCP Module — Unreal Insights MCP Plugin

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * FUEInsightMCPModule
 *
 * Module entry point for the UEInsightMCP plugin.
 * Starts the Insight Log Capture on startup.
 * The InsightBridge (UEditorSubsystem) handles TCP server lifecycle.
 */
class FUEInsightMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static bool IsAvailable();
	static FUEInsightMCPModule& Get();
};
