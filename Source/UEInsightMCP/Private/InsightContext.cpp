// UEInsightMCP — InsightContext implementation

#include "InsightContext.h"
#include "InsightLogCapture.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/StringBuilder.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/AnalysisSession.h"
#endif

namespace
{
	bool AreDestinationsEqual(
		FTraceAuxiliary::EConnectionType Type,
		const FString& A,
		const FString& B)
	{
		if (Type == FTraceAuxiliary::EConnectionType::File)
		{
			return FPaths::IsSamePath(A, B);
		}
		return A.Equals(B, ESearchCase::IgnoreCase);
	}
}

void FInsightContext::Reset()
{
	ClearActiveTraceState();
	ClearPreviousTraceState();
	ResetSession();
}

void FInsightContext::ResetSession()
{
	AnalysisSession.Reset();
	CurrentSessionFile.Empty();
	bSessionLoaded = false;
	InvalidateCache();
}

void FInsightContext::ClearActiveTraceState()
{
	bIsTracing = false;
	bTraceStartedByMCP = false;
	bTraceStartTimeKnown = false;
	ActiveConnectionType = FTraceAuxiliary::EConnectionType::None;
	ActiveTraceDestination.Empty();
	ActiveTraceFile.Empty();
	EnabledChannels.Empty();
	TraceStartTime = 0.0;
}

void FInsightContext::ClearPreviousTraceState()
{
	bHasPreviousTrace = false;
	PreviousConnectionType = FTraceAuxiliary::EConnectionType::None;
	PreviousTraceDestination.Empty();
	PreviousTraceChannels.Empty();
}

void FInsightContext::RefreshTraceStateFromNative()
{
#if ENGINE_MAJOR_VERSION >= 5
	if (!FTraceAuxiliary::IsConnected())
	{
		ClearActiveTraceState();
		return;
	}

	const FTraceAuxiliary::EConnectionType NativeType = FTraceAuxiliary::GetConnectionType();
	FString NativeDestination = FTraceAuxiliary::GetTraceDestinationString();
	if (NativeType == FTraceAuxiliary::EConnectionType::File && !NativeDestination.IsEmpty())
	{
		NativeDestination = FPaths::ConvertRelativePathToFull(NativeDestination);
		FPaths::NormalizeFilename(NativeDestination);
	}

	const bool bSameManagedTrace =
		bTraceStartedByMCP &&
		bIsTracing &&
		ActiveConnectionType == NativeType &&
		AreDestinationsEqual(NativeType, ActiveTraceDestination, NativeDestination);

	bIsTracing = true;
	ActiveConnectionType = NativeType;
	ActiveTraceDestination = NativeDestination;
	ActiveTraceFile = NativeType == FTraceAuxiliary::EConnectionType::File
		? NativeDestination
		: FString();

	EnabledChannels.Empty();
	TStringBuilder<1024> ActiveChannels;
	FTraceAuxiliary::GetActiveChannelsString(ActiveChannels);
	TArray<FString> ChannelNames;
	FString(ActiveChannels.ToString()).ParseIntoArray(ChannelNames, TEXT(","), true);
	for (FString& ChannelName : ChannelNames)
	{
		ChannelName.TrimStartAndEndInline();
		if (!ChannelName.IsEmpty())
		{
			EnabledChannels.Add(ChannelName);
		}
	}

	if (!bSameManagedTrace)
	{
		bTraceStartedByMCP = false;
		bTraceStartTimeKnown = false;
		TraceStartTime = 0.0;
	}
#else
	bIsTracing = Trace::IsTracing();
	if (!bIsTracing)
	{
		ClearActiveTraceState();
	}
#endif
}

void FInsightContext::MarkMCPTraceStarted(
	FTraceAuxiliary::EConnectionType Type,
	const FString& Destination)
{
	bIsTracing = true;
	bTraceStartedByMCP = true;
	bTraceStartTimeKnown = true;
	ActiveConnectionType = Type;
	ActiveTraceDestination = Destination;
	ActiveTraceFile = Type == FTraceAuxiliary::EConnectionType::File
		? Destination
		: FString();
	TraceStartTime = FPlatformTime::Seconds();

	// Refresh actual channels and native destination while preserving ownership.
	RefreshTraceStateFromNative();
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

FString FInsightContext::ConnectionTypeToString(FTraceAuxiliary::EConnectionType Type)
{
	switch (Type)
	{
	case FTraceAuxiliary::EConnectionType::File:
		return TEXT("file");
	case FTraceAuxiliary::EConnectionType::Network:
		return TEXT("network");
	case FTraceAuxiliary::EConnectionType::None:
		return TEXT("none");
	default:
		return TEXT("unknown");
	}
}

TSharedPtr<FJsonObject> FInsightContext::ToJson() const
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	// Trace control state
	Json->SetBoolField(TEXT("is_tracing"), bIsTracing);
	Json->SetBoolField(TEXT("managed_by_mcp"), bTraceStartedByMCP);
	Json->SetBoolField(TEXT("start_time_known"), bTraceStartTimeKnown);
	Json->SetStringField(TEXT("connection_type"), ConnectionTypeToString(ActiveConnectionType));
	Json->SetStringField(TEXT("destination"), ActiveTraceDestination);
	Json->SetStringField(TEXT("active_trace_file"), ActiveTraceFile);

	if (bIsTracing && bTraceStartTimeKnown)
	{
		Json->SetNumberField(TEXT("trace_start_time"), TraceStartTime);
		Json->SetNumberField(TEXT("trace_elapsed_seconds"), FPlatformTime::Seconds() - TraceStartTime);
	}

	// Enabled channels
	TArray<TSharedPtr<FJsonValue>> ChannelArray;
	for (const FString& Channel : EnabledChannels)
	{
		ChannelArray.Add(MakeShared<FJsonValueString>(Channel));
	}
	Json->SetArrayField(TEXT("enabled_channels"), ChannelArray);
	Json->SetNumberField(TEXT("enabled_channel_count"), EnabledChannels.Num());

	Json->SetBoolField(TEXT("has_previous_trace"), bHasPreviousTrace);
	if (bHasPreviousTrace)
	{
		Json->SetStringField(TEXT("previous_connection_type"), ConnectionTypeToString(PreviousConnectionType));
		Json->SetStringField(TEXT("previous_destination"), PreviousTraceDestination);
	}

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
	FString TraceDir = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("TraceSessions"));
	FPaths::CollapseRelativeDirectories(TraceDir);
	FPaths::NormalizeDirectoryName(TraceDir);
	TraceDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*TraceDir);
	FPaths::NormalizeDirectoryName(TraceDir);

	// Ensure directory exists
	IFileManager::Get().MakeDirectory(*TraceDir, true);

	return TraceDir;
}
