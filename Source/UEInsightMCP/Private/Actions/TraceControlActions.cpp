// UEInsightMCP — Trace Control Actions implementation
// Supports both UE5 (FTraceAuxiliary::Start/Stop, UE::Trace::EnumerateChannels)
// and UE4 RDCSP (Trace::WriteTo/SendTo/Stop, hardcoded channel list)

#include "Actions/TraceControlActions.h"
#include "InsightBridge.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "ProfilingDebugging/MiscTrace.h"  // TRACE_BOOKMARK
#include "Trace/Trace.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Editor.h"                        // GEditor — needed by FBatchExecuteInsightAction

// Version compat: UE5 uses UE::Trace namespace, UE4 uses Trace namespace
#if ENGINE_MAJOR_VERSION >= 5
// UE5: full FTraceAuxiliary API + UE::Trace::EnumerateChannels
#else
// UE4 RDCSP: use Trace::WriteTo/SendTo/Stop directly
// No UE::Trace namespace — it's just Trace::
// No EnumerateChannels — use Trace::IsChannel to probe known channels
#include "Trace/Config.h"  // UE_TRACE_ENABLED

namespace InsightMCPCompat
{
	// UE4 RDCSP does not have EnumerateChannels.
	// We provide a known-channel list based on RDCSP engine source analysis.
	// These are the channels registered via UE_TRACE_CHANNEL / UE_TRACE_CHANNEL_EXTERN.
	static const TCHAR* const KnownChannels[] = {
		// Core profiling channels (Runtime/Core)
		TEXT("cpu"),
		TEXT("gpu"),
		TEXT("frame"),
		TEXT("bookmark"),
		TEXT("counters"),
		TEXT("log"),
		TEXT("file"),
		TEXT("loadtime"),
		TEXT("memory"),
		// Networking
		TEXT("net"),
		// Slate UI
		TEXT("slate"),
		// Rendering
		TEXT("rhicommands"),
		TEXT("rendercommands"),
		// Animation
		TEXT("animation"),
		// Physics
		TEXT("physics"),
		// Network Prediction
		TEXT("networkprediction"),
		// Niagara
		TEXT("niagara"),
		// Trace source filters
		TEXT("tracesourcefilters"),
		// CSV profiler
		TEXT("csv"),
		// Object trace
		TEXT("object"),
	};
	static constexpr int32 NumKnownChannels = UE_ARRAY_COUNT(KnownChannels);
}
#endif


// ============================================================================
// trace.start — Start Trace recording
// ============================================================================

bool FTraceStartAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	if (Context.bIsTracing)
	{
		OutError = TEXT("Trace is already running. Call trace.stop first.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FTraceStartAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	// 1. Parse channels
	FString ChannelStr = GetOptionalString(Params, TEXT("channels"), TEXT("cpu,gpu,frame,bookmark,loadtime"));
	TArray<FString> Channels;
	ChannelStr.ParseIntoArray(Channels, TEXT(","));

	// Build a clean comma-separated channel string
	FString CleanChannelStr;
	for (int32 i = 0; i < Channels.Num(); ++i)
	{
		if (i > 0) CleanChannelStr += TEXT(",");
		CleanChannelStr += Channels[i].TrimStartAndEnd();
	}

	// 2. Output mode
	FString OutputMode = GetOptionalString(Params, TEXT("output"), TEXT("file"));

	// 3. Start Trace
	FString TraceFile;
	bool bStarted = false;

#if ENGINE_MAJOR_VERSION >= 5
	// ── UE5 path: FTraceAuxiliary::Start (full API) ──
	if (OutputMode == TEXT("file"))
	{
		TraceFile = FInsightContext::GetDefaultTraceDir() /
			FString::Printf(TEXT("Trace_%s.utrace"),
				*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TraceFile), true);

		FTraceAuxiliary::FOptions Options;
		Options.bExcludeTail = false;
		Options.bNoWorkerThread = false;

		bStarted = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::File,
			*TraceFile, *CleanChannelStr, &Options);
	}
	else // "server" mode
	{
		FString Host = GetOptionalString(Params, TEXT("host"), TEXT("127.0.0.1"));
		bStarted = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::Network,
			*Host, *CleanChannelStr, nullptr);
	}
#else
	// ── UE4 RDCSP path: Trace::WriteTo / Trace::SendTo directly ──
	// Step 1: Enable channels first (Trace::ToggleChannel)
	for (const FString& Ch : Channels)
	{
		FString TrimmedCh = Ch.TrimStartAndEnd();
		if (Trace::IsChannel(*TrimmedCh))
		{
			Trace::ToggleChannel(*TrimmedCh, true);
		}
		else
		{
			UE_LOG(LogInsightMCP, Warning, TEXT("Unknown trace channel: '%s'"), *TrimmedCh);
		}
	}

	// Step 2: Start writer
	if (OutputMode == TEXT("file"))
	{
		TraceFile = FInsightContext::GetDefaultTraceDir() /
			FString::Printf(TEXT("Trace_%s.utrace"),
				*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TraceFile), true);

		// Convert to absolute path (UE4 Trace::WriteTo requires absolute path)
		FString AbsoluteTraceFile = FPaths::ConvertRelativePathToFull(TraceFile);
		bStarted = Trace::WriteTo(*AbsoluteTraceFile);
		if (bStarted)
		{
			TraceFile = AbsoluteTraceFile;
		}
	}
	else // "server" mode
	{
		FString Host = GetOptionalString(Params, TEXT("host"), TEXT("127.0.0.1"));
		uint32 Port = static_cast<uint32>(GetOptionalNumber(Params, TEXT("port"), 0.0));
		if (Port == 0)
		{
			Port = Trace::GetConnectPort();  // RDCSP extension, default 8001
		}
		bStarted = Trace::SendTo(*Host, Port);
	}
#endif

	if (!bStarted)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to start Trace in '%s' mode"), *OutputMode),
			TEXT("trace_start_failed"));
	}

	// 4. Update context
	Context.bIsTracing = true;
	Context.ActiveTraceFile = TraceFile;
	Context.EnabledChannels.Empty();
	for (const FString& Ch : Channels)
	{
		Context.EnabledChannels.Add(Ch.TrimStartAndEnd());
	}
	Context.TraceStartTime = FPlatformTime::Seconds();
	Context.InvalidateCache();

	// 5. Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("output_mode"), OutputMode);
	Result->SetStringField(TEXT("trace_file"), TraceFile);
	Result->SetNumberField(TEXT("channel_count"), Channels.Num());

	TArray<TSharedPtr<FJsonValue>> ChannelArray;
	for (const FString& Ch : Channels)
	{
		ChannelArray.Add(MakeShared<FJsonValueString>(Ch.TrimStartAndEnd()));
	}
	Result->SetArrayField(TEXT("channels"), ChannelArray);

#if ENGINE_MAJOR_VERSION < 5
	// UE4 RDCSP extra info
	Result->SetNumberField(TEXT("rdcsp_connect_port"), Trace::GetConnectPort());
	Result->SetNumberField(TEXT("trace_memory_used"), Trace::GetMemoryUsed());
#endif

	UE_LOG(LogInsightMCP, Log, TEXT("Trace started: mode=%s, file=%s, channels=%d"),
		*OutputMode, *TraceFile, Channels.Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.stop — Stop Trace recording
// ============================================================================

bool FTraceStopAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	if (!Context.bIsTracing)
	{
		OutError = TEXT("No Trace is currently running.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FTraceStopAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	double ElapsedSeconds = FPlatformTime::Seconds() - Context.TraceStartTime;
	FString TraceFile = Context.ActiveTraceFile;

	// Stop Trace
#if ENGINE_MAJOR_VERSION >= 5
	FTraceAuxiliary::Stop();
#else
	// UE4 RDCSP: Trace::Stop() is the direct API
	// Also disable channels we enabled
	for (const FString& Ch : Context.EnabledChannels)
	{
		if (Trace::IsChannel(*Ch))
		{
			Trace::ToggleChannel(*Ch, false);
		}
	}
	Trace::Stop();
#endif

	// Get file size if applicable
	int64 FileSize = 0;
	if (!TraceFile.IsEmpty() && IFileManager::Get().FileExists(*TraceFile))
	{
		FileSize = IFileManager::Get().FileSize(*TraceFile);
	}

	// Update context
	Context.bIsTracing = false;
	// Keep ActiveTraceFile for reference (user might want to analyze it)
	Context.InvalidateCache();

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("trace_file"), TraceFile);
	Result->SetNumberField(TEXT("duration_seconds"), ElapsedSeconds);
	Result->SetNumberField(TEXT("file_size_bytes"), static_cast<double>(FileSize));
	Result->SetStringField(TEXT("file_size_human"), 
		FileSize > 1024 * 1024 
			? FString::Printf(TEXT("%.1f MB"), FileSize / (1024.0 * 1024.0))
			: FString::Printf(TEXT("%.1f KB"), FileSize / 1024.0));

	UE_LOG(LogInsightMCP, Log, TEXT("Trace stopped: file=%s, duration=%.1fs, size=%lld bytes"),
		*TraceFile, ElapsedSeconds, FileSize);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.status — Get current Trace status
// ============================================================================

TSharedPtr<FJsonObject> FTraceStatusAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Use the native IsTracing() as ground truth
#if ENGINE_MAJOR_VERSION >= 5
	bool bActuallyTracing = UE::Trace::IsTracing();
#else
	bool bActuallyTracing = Trace::IsTracing();
#endif
	Result->SetBoolField(TEXT("is_tracing"), bActuallyTracing);
	Result->SetStringField(TEXT("active_trace_file"), Context.ActiveTraceFile);

	// Sync context if out of date
	if (bActuallyTracing != Context.bIsTracing)
	{
		Context.bIsTracing = bActuallyTracing;
	}

	if (Context.bIsTracing && Context.TraceStartTime > 0.0)
	{
		double ElapsedSeconds = FPlatformTime::Seconds() - Context.TraceStartTime;
		Result->SetNumberField(TEXT("elapsed_seconds"), ElapsedSeconds);
	}

	// Enabled channels
	TArray<TSharedPtr<FJsonValue>> ChannelArray;
	for (const FString& Ch : Context.EnabledChannels)
	{
		ChannelArray.Add(MakeShared<FJsonValueString>(Ch));
	}
	Result->SetArrayField(TEXT("enabled_channels"), ChannelArray);
	Result->SetNumberField(TEXT("enabled_channel_count"), Context.EnabledChannels.Num());

	// Session state
	Result->SetBoolField(TEXT("session_loaded"), Context.bSessionLoaded);
	Result->SetStringField(TEXT("current_session_file"), Context.CurrentSessionFile);

	// Default trace dir
	Result->SetStringField(TEXT("default_trace_dir"), FInsightContext::GetDefaultTraceDir());

#if ENGINE_MAJOR_VERSION < 5
	// UE4 RDCSP extra diagnostics
	Result->SetNumberField(TEXT("trace_memory_used"), Trace::GetMemoryUsed());
	Result->SetNumberField(TEXT("rdcsp_connect_port"), Trace::GetConnectPort());

	// Get trace statistics if available
	Trace::FStatistics Stats;
	Trace::GetStatistics(Stats);
	Result->SetNumberField(TEXT("bytes_sent"), static_cast<double>(Stats.BytesSent));
	Result->SetNumberField(TEXT("bytes_traced"), static_cast<double>(Stats.BytesTraced));
	Result->SetNumberField(TEXT("memory_used"), Stats.MemoryUsed);
	Result->SetNumberField(TEXT("cache_used"), Stats.CacheUsed);
#endif

	Result->SetNumberField(TEXT("engine_major_version"), ENGINE_MAJOR_VERSION);
	Result->SetNumberField(TEXT("engine_minor_version"), ENGINE_MINOR_VERSION);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.channels.list — List all available Trace channels
// ============================================================================

TSharedPtr<FJsonObject> FTraceChannelsListAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	struct FChannelInfo
	{
		FString Name;
		bool bEnabled;
	};
	TArray<FChannelInfo> ChannelList;

#if ENGINE_MAJOR_VERSION >= 5
	// UE5: use the native EnumerateChannels API
	UE::Trace::EnumerateChannels(
		[](const ANSICHAR* Name, bool bEnabled, void* UserData)
		{
			auto* List = static_cast<TArray<FChannelInfo>*>(UserData);
			List->Add({FString(ANSI_TO_TCHAR(Name)), bEnabled});
		},
		&ChannelList);
#else
	// UE4 RDCSP: no EnumerateChannels — probe known channels via Trace::IsChannel
	for (int32 i = 0; i < InsightMCPCompat::NumKnownChannels; ++i)
	{
		const TCHAR* ChannelName = InsightMCPCompat::KnownChannels[i];
		if (Trace::IsChannel(ChannelName))
		{
			// Channel exists in this build.
			// We check if it's in our context's enabled set to report status.
			bool bEnabled = Context.EnabledChannels.Contains(FString(ChannelName));
			ChannelList.Add({FString(ChannelName), bEnabled});
		}
	}
#endif

	// Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total_channels"), ChannelList.Num());

	TArray<TSharedPtr<FJsonValue>> ChannelsJson;
	TArray<TSharedPtr<FJsonValue>> EnabledJson;

	for (const FChannelInfo& Info : ChannelList)
	{
		TSharedPtr<FJsonObject> ChObj = MakeShared<FJsonObject>();
		ChObj->SetStringField(TEXT("name"), Info.Name);
		ChObj->SetBoolField(TEXT("enabled"), Info.bEnabled);
		ChannelsJson.Add(MakeShared<FJsonValueObject>(ChObj));

		if (Info.bEnabled)
		{
			EnabledJson.Add(MakeShared<FJsonValueString>(Info.Name));
		}
	}

	Result->SetArrayField(TEXT("channels"), ChannelsJson);
	Result->SetArrayField(TEXT("enabled"), EnabledJson);
	Result->SetNumberField(TEXT("enabled_count"), EnabledJson.Num());

#if ENGINE_MAJOR_VERSION < 5
	Result->SetStringField(TEXT("enumeration_method"), TEXT("known_channel_probe"));
#else
	Result->SetStringField(TEXT("enumeration_method"), TEXT("native_enumerate"));
#endif

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.channels.toggle — Enable/disable specific Trace channels
// ============================================================================

bool FTraceChannelsToggleAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	FString Channels;
	if (!GetRequiredString(Params, TEXT("channels"), Channels, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FTraceChannelsToggleAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	FString ChannelStr;
	FString Unused;
	GetRequiredString(Params, TEXT("channels"), ChannelStr, Unused);

	bool bEnable = GetOptionalBool(Params, TEXT("enable"), true);

	TArray<FString> Channels;
	ChannelStr.ParseIntoArray(Channels, TEXT(","));

	TArray<TSharedPtr<FJsonValue>> ToggledArray;
	for (const FString& Channel : Channels)
	{
		FString TrimmedChannel = Channel.TrimStartAndEnd();

		// ToggleChannel: UE5 uses UE::Trace namespace, UE4 uses Trace namespace
#if ENGINE_MAJOR_VERSION >= 5
		UE::Trace::ToggleChannel(*TrimmedChannel, bEnable);
#else
		Trace::ToggleChannel(*TrimmedChannel, bEnable);
#endif

		if (bEnable)
		{
			Context.EnabledChannels.Add(TrimmedChannel);
		}
		else
		{
			Context.EnabledChannels.Remove(TrimmedChannel);
		}

		ToggledArray.Add(MakeShared<FJsonValueString>(TrimmedChannel));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("toggled_channels"), ToggledArray);
	Result->SetBoolField(TEXT("enabled"), bEnable);
	Result->SetNumberField(TEXT("count"), Channels.Num());

	UE_LOG(LogInsightMCP, Log, TEXT("Channels toggled: %s -> %s"),
		*ChannelStr, bEnable ? TEXT("enabled") : TEXT("disabled"));

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.bookmark.add — Add a named bookmark
// ============================================================================

bool FTraceBookmarkAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	FString Label;
	if (!GetRequiredString(Params, TEXT("label"), Label, OutError))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FTraceBookmarkAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	FString Label;
	FString Unused;
	GetRequiredString(Params, TEXT("label"), Label, Unused);

	// TRACE_BOOKMARK works on both UE4 and UE5 (defined in ProfilingDebugging/MiscTrace.h)
	TRACE_BOOKMARK(TEXT("%s"), *Label);

	double CurrentTime = FPlatformTime::Seconds();
	double TraceRelativeTime = Context.bIsTracing ? (CurrentTime - Context.TraceStartTime) : 0.0;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("label"), Label);
	Result->SetNumberField(TEXT("timestamp"), CurrentTime);
	Result->SetNumberField(TEXT("trace_relative_seconds"), TraceRelativeTime);
	Result->SetBoolField(TEXT("trace_active"), Context.bIsTracing);

	UE_LOG(LogInsightMCP, Log, TEXT("Bookmark added: '%s' at %.3fs relative"),
		*Label, TraceRelativeTime);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// batch_execute — Execute multiple commands in a single TCP request
// ============================================================================

bool FBatchExecuteInsightAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	if (!Commands || Commands->Num() == 0)
	{
		OutError = TEXT("Missing or empty 'commands' array");
		return false;
	}
	if (Commands->Num() > MaxBatchSize)
	{
		OutError = FString::Printf(TEXT("Batch too large: %d commands (max %d)"), Commands->Num(), MaxBatchSize);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FBatchExecuteInsightAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	const TArray<TSharedPtr<FJsonValue>>* Commands = GetOptionalArray(Params, TEXT("commands"));
	// Python sends "continue_on_error"; also accept "stop_on_error" for compat
	const bool bContinueOnError = GetOptionalBool(Params, TEXT("continue_on_error"), false);
	const bool bStopOnError = bContinueOnError ? false : GetOptionalBool(Params, TEXT("stop_on_error"), true);

	// Get InsightBridge to dispatch sub-commands
	UInsightBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UInsightBridge>() : nullptr;
	if (!Bridge)
	{
		return CreateErrorResponse(TEXT("InsightBridge subsystem not available"));
	}

	const int32 Total = Commands->Num();
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 Succeeded = 0;
	int32 Failed = 0;
	int32 Executed = 0;

	for (int32 i = 0; i < Total; ++i)
	{
		const TSharedPtr<FJsonObject>* CmdObj = nullptr;
		if (!(*Commands)[i]->TryGetObject(CmdObj) || !CmdObj || !(*CmdObj).IsValid())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Invalid command object"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		FString CmdType;
		if (!(*CmdObj)->TryGetStringField(TEXT("type"), CmdType) || CmdType.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
			ErrResult->SetNumberField(TEXT("index"), i);
			ErrResult->SetBoolField(TEXT("success"), false);
			ErrResult->SetStringField(TEXT("error"), TEXT("Missing 'type' field"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(ErrResult));
			++Failed;
			++Executed;
			if (bStopOnError) break;
			continue;
		}

		TSharedPtr<FJsonObject> CmdParams;
		const TSharedPtr<FJsonObject>* CmdParamsPtr = nullptr;
		if ((*CmdObj)->TryGetObjectField(TEXT("params"), CmdParamsPtr) && CmdParamsPtr)
		{
			CmdParams = *CmdParamsPtr;
		}
		else
		{
			CmdParams = MakeShared<FJsonObject>();
		}

		UE_LOG(LogInsightMCP, Log, TEXT("Batch[%d/%d]: executing '%s'"), i + 1, Total, *CmdType);

		// Execute sub-command via Bridge (already on GameThread, bypasses TCP)
		TSharedPtr<FJsonObject> SubResult = Bridge->ExecuteCommand(CmdType, CmdParams);

		// Tag result with index and type
		if (SubResult.IsValid())
		{
			SubResult->SetNumberField(TEXT("index"), i);
			SubResult->SetStringField(TEXT("type"), CmdType);
		}

		bool bSubSuccess = false;
		if (SubResult.IsValid() && SubResult->TryGetBoolField(TEXT("success"), bSubSuccess) && bSubSuccess)
		{
			++Succeeded;
		}
		else
		{
			++Failed;
		}

		ResultsArray.Add(MakeShared<FJsonValueObject>(SubResult.IsValid() ? SubResult : MakeShared<FJsonObject>()));
		++Executed;

		if (!bSubSuccess && bStopOnError)
		{
			UE_LOG(LogInsightMCP, Warning, TEXT("Batch stopped at command %d/%d ('%s') due to stop_on_error"), i + 1, Total, *CmdType);
			break;
		}
	}

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
	ResultData->SetNumberField(TEXT("total"), Total);
	ResultData->SetNumberField(TEXT("executed"), Executed);
	ResultData->SetNumberField(TEXT("succeeded"), Succeeded);
	ResultData->SetNumberField(TEXT("failed"), Failed);
	ResultData->SetArrayField(TEXT("results"), ResultsArray);

	if (Failed > 0)
	{
		ResultData->SetBoolField(TEXT("has_failures"), true);
	}

	return CreateSuccessResponse(ResultData);
}
