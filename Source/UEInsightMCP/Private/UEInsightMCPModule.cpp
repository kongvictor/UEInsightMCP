// UEInsightMCP Module — Unreal Insights MCP Plugin

#include "UEInsightMCPModule.h"
#include "InsightBridge.h"
#include "InsightLogCapture.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FUEInsightMCPModule"

void FUEInsightMCPModule::StartupModule()
{
	FInsightLogCapture::Get().Start();

	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Module starting up"));

	// The InsightBridge is an EditorSubsystem and will be automatically created
	// when the editor starts. It handles the TCP server lifecycle internally.
}

void FUEInsightMCPModule::ShutdownModule()
{
	UE_LOG(LogInsightMCP, Log, TEXT("UEInsightMCP: Module shutting down"));

	FInsightLogCapture::Get().Stop();

	// The InsightBridge will be automatically destroyed as an EditorSubsystem.
}

bool FUEInsightMCPModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("UEInsightMCP");
}

FUEInsightMCPModule& FUEInsightMCPModule::Get()
{
	return FModuleManager::LoadModuleChecked<FUEInsightMCPModule>("UEInsightMCP");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUEInsightMCPModule, UEInsightMCP)
