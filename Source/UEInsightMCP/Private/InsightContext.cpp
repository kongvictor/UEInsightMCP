// UEInsightMCP — InsightContext implementation

#include "InsightContext.h"
#include "InsightLogCapture.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/AnalysisSession.h"
#endif

void FInsightContext::Reset()
{
	bIsTracing = false;
	ActiveTraceFile.Empty();
	EnabledChannels.Empty();
	TraceStartTime = 0.0;
	ResetSession();
}

void FInsightContext::ResetSession()
{
	AnalysisSession.Reset();
	CurrentSessionFile.Empty();
	bSessionLoaded = false;
	InvalidateCache();
}

TSharedPtr<TraceServices::IAnalysisService> FInsightContext::GetOrCreateAnalysisService()
{
#if ENGINE_MAJOR_VERSION >= 5
	if (!AnalysisService.IsValid())
	{
		ITraceServicesModule& TraceModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));

		// Ensure ALL analysis modules are enabled before creating the service.
		// In Editor builds, some modules (LoadTimeProfiler, Memory, etc.) are
		// disabled by default (ShouldBeEnabledByDefault() returns false).
		// Without enabling them, their providers won't be created during analysis,
		// and queries like query.loadtime will always report "no data".
		{
			TSharedPtr<TraceServices::IModuleService> ModuleService = TraceModule.GetModuleService();
			if (!ModuleService.IsValid())
			{
				ModuleService = TraceModule.CreateModuleService();
			}

			if (ModuleService.IsValid())
			{
				TArray<TraceServices::FModuleInfoEx> AllModules;
				ModuleService->GetAvailableModulesEx(AllModules);

				int32 EnabledCount = 0;
				for (const TraceServices::FModuleInfoEx& ModInfo : AllModules)
				{
					if (!ModInfo.bIsEnabled)
					{
						ModuleService->SetModuleEnabled(ModInfo.Info.Name, true);
						UE_LOG(LogInsightMCP, Log, TEXT("Enabled analysis module: %s"), *ModInfo.Info.Name.ToString());
						++EnabledCount;
					}
				}

				if (EnabledCount > 0)
				{
					UE_LOG(LogInsightMCP, Log, TEXT("Enabled %d additional analysis modules (total: %d)"),
						EnabledCount, AllModules.Num());
				}
			}
		}

		AnalysisService = TraceModule.GetAnalysisService();
		if (!AnalysisService.IsValid())
		{
			AnalysisService = TraceModule.CreateAnalysisService();
		}
	}
	return AnalysisService;
#else
	// UE4: TraceServices analysis not available
	return nullptr;
#endif
}

void FInsightContext::InvalidateCache()
{
	CachedFrameTimeSummary.Reset();
	CachedLoadTimeSummary.Reset();
	CacheTimestamp = 0.0;
}

TSharedPtr<FJsonObject> FInsightContext::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	// Trace control state
	Json->SetBoolField(TEXT("is_tracing"), bIsTracing);
	Json->SetStringField(TEXT("active_trace_file"), ActiveTraceFile);
	Json->SetNumberField(TEXT("trace_start_time"), TraceStartTime);

	if (bIsTracing)
	{
		double ElapsedSeconds = FPlatformTime::Seconds() - TraceStartTime;
		Json->SetNumberField(TEXT("trace_elapsed_seconds"), ElapsedSeconds);
	}

	// Enabled channels
	TArray<TSharedPtr<FJsonValue>> ChannelArray;
	for (const FString& Channel : EnabledChannels)
	{
		ChannelArray.Add(MakeShared<FJsonValueString>(Channel));
	}
	Json->SetArrayField(TEXT("enabled_channels"), ChannelArray);

	// Analysis session state
	Json->SetStringField(TEXT("current_session_file"), CurrentSessionFile);
	Json->SetBoolField(TEXT("session_loaded"), bSessionLoaded);
	Json->SetBoolField(TEXT("has_cached_results"), CachedFrameTimeSummary.IsValid() || CachedLoadTimeSummary.IsValid());

	// Default trace dir
	Json->SetStringField(TEXT("default_trace_dir"), GetDefaultTraceDir());

	return Json;
}

FString FInsightContext::GetDefaultTraceDir()
{
	FString TraceDir = FPaths::ProjectSavedDir() / TEXT("TraceSessions");

	// Ensure directory exists
	IFileManager::Get().MakeDirectory(*TraceDir, true);

	return TraceDir;
}
