// UEInsightMCP — Trace Session Actions implementation (Phase 2)
// Session management: list/open/info/close .utrace files

#include "Actions/TraceSessionActions.h"
#include "InsightBridge.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Memory.h"
#endif

namespace
{
	FString NormalizeSessionPath(const FString& Path)
	{
		FString Absolute = FPaths::ConvertRelativePathToFull(Path);
		FPaths::CollapseRelativeDirectories(Absolute);
		Absolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Absolute);
		FPaths::NormalizeFilename(Absolute);
		return Absolute;
	}
}


// ============================================================================
// session.list — List available .utrace files
// ============================================================================

bool FSessionListAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	// session.list works on both UE4/UE5 — it just lists files
	return true;
}

TSharedPtr<FJsonObject> FSessionListAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	// 1. Determine search directory
	FString SearchDir = GetOptionalString(Params, TEXT("directory"), TEXT(""));
	if (SearchDir.IsEmpty())
	{
		SearchDir = FInsightContext::GetDefaultTraceDir();
	}
	else
	{
		SearchDir = NormalizeSessionPath(SearchDir);
	}
	FPaths::NormalizeDirectoryName(SearchDir);

	// 2. Find all .utrace files
	TArray<FString> FoundFiles;
	IFileManager& FileManager = IFileManager::Get();
	FileManager.FindFilesRecursive(FoundFiles, *SearchDir, TEXT("*.utrace"), true, false);

	// 3. Sort by modification time (newest first)
	struct FFileInfo
	{
		FString Path;
		FString Name;
		int64 Size;
		FDateTime ModTime;
	};

	TArray<FFileInfo> FileInfos;
	for (const FString& FilePath : FoundFiles)
	{
		FFileInfo Info;
		Info.Path = NormalizeSessionPath(FilePath);
		Info.Name = FPaths::GetCleanFilename(FilePath);
		Info.Size = FileManager.FileSize(*FilePath);

		FFileStatData StatData = FileManager.GetStatData(*FilePath);
		Info.ModTime = StatData.ModificationTime;

		FileInfos.Add(MoveTemp(Info));
	}

	// Sort newest first
	FileInfos.Sort([](const FFileInfo& A, const FFileInfo& B)
	{
		return A.ModTime > B.ModTime;
	});

	// 4. Apply limit
	int32 Limit = static_cast<int32>(GetOptionalNumber(Params, TEXT("limit"), 20.0));
	if (Limit > 0 && FileInfos.Num() > Limit)
	{
		FileInfos.SetNum(Limit);
	}

	// 5. Build response
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("directory"), SearchDir);
	Result->SetNumberField(TEXT("total_files"), FoundFiles.Num());
	Result->SetNumberField(TEXT("returned"), FileInfos.Num());

	// Current session info
	Result->SetBoolField(TEXT("session_loaded"), Context.bSessionLoaded);
	Result->SetStringField(TEXT("current_session_file"), Context.CurrentSessionFile);

	TArray<TSharedPtr<FJsonValue>> FilesArray;
	for (const FFileInfo& Info : FileInfos)
	{
		TSharedPtr<FJsonObject> FileObj = MakeShared<FJsonObject>();
		FileObj->SetStringField(TEXT("name"), Info.Name);
		FileObj->SetStringField(TEXT("path"), Info.Path);
		FileObj->SetNumberField(TEXT("size_bytes"), static_cast<double>(Info.Size));

		// Human-readable size
		if (Info.Size > 1024 * 1024)
		{
			FileObj->SetStringField(TEXT("size_human"),
				FString::Printf(TEXT("%.1f MB"), Info.Size / (1024.0 * 1024.0)));
		}
		else
		{
			FileObj->SetStringField(TEXT("size_human"),
				FString::Printf(TEXT("%.1f KB"), Info.Size / 1024.0));
		}

		FileObj->SetStringField(TEXT("modified"), Info.ModTime.ToString());

		// Mark if this is the currently loaded session
		FileObj->SetBoolField(TEXT("is_current"),
			Context.bSessionLoaded && FPaths::IsSamePath(Info.Path, Context.CurrentSessionFile));

		FilesArray.Add(MakeShared<FJsonValueObject>(FileObj));
	}

	Result->SetArrayField(TEXT("files"), FilesArray);

	UE_LOG(LogInsightMCP, Log, TEXT("session.list: found %d .utrace files in %s"),
		FoundFiles.Num(), *SearchDir);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// session.open — Open a .utrace file for analysis
// ============================================================================

bool FSessionOpenAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	if (!ValidateUE5(OutError))
	{
		return false;
	}

	FString FilePath;
	if (!GetRequiredString(Params, TEXT("file"), FilePath, OutError))
	{
		return false;
	}

	FilePath = NormalizeSessionPath(FilePath);
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		OutError = FString::Printf(TEXT("File not found: %s"), *FilePath);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSessionOpenAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	FString FilePath;
	FString Unused;
	GetRequiredString(Params, TEXT("file"), FilePath, Unused);
	FilePath = NormalizeSessionPath(FilePath);

	// Close existing session if any
	if (Context.bSessionLoaded)
	{
		UE_LOG(LogInsightMCP, Log, TEXT("Closing existing session before opening new one: %s"),
			*Context.CurrentSessionFile);
		Context.ResetSession();
	}

	// Get or create analysis service
	TSharedPtr<TraceServices::IAnalysisService> Service = Context.GetOrCreateAnalysisService();
	if (!Service.IsValid())
	{
		return CreateErrorResponse(TEXT("Failed to create TraceServices analysis service"), TEXT("service_error"));
	}

	// Analyze the .utrace file (synchronous — loads and parses all data)
	UE_LOG(LogInsightMCP, Log, TEXT("Opening trace file for analysis: %s"), *FilePath);

	double StartTime = FPlatformTime::Seconds();

	TSharedPtr<const TraceServices::IAnalysisSession> Session = Service->Analyze(*FilePath);

	double AnalyzeTime = FPlatformTime::Seconds() - StartTime;

	if (!Session.IsValid())
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to analyze trace file: %s"), *FilePath),
			TEXT("analysis_failed"));
	}

	// Store in context
	Context.AnalysisSession = Session;
	Context.CurrentSessionFile = FilePath;
	Context.bSessionLoaded = true;
	Context.InvalidateCache();

	// Build response with basic session info
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("file"), FilePath);
	Result->SetStringField(TEXT("file_name"), FPaths::GetCleanFilename(FilePath));
	Result->SetNumberField(TEXT("analyze_time_seconds"), AnalyzeTime);

	// File size
	int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	Result->SetNumberField(TEXT("file_size_bytes"), static_cast<double>(FileSize));
	Result->SetStringField(TEXT("file_size_human"),
		FileSize > 1024 * 1024
			? FString::Printf(TEXT("%.1f MB"), FileSize / (1024.0 * 1024.0))
			: FString::Printf(TEXT("%.1f KB"), FileSize / 1024.0));

	// Probe available providers
	TArray<TSharedPtr<FJsonValue>> ProvidersArray;
	{
		TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

		// Check each provider — use ReadProvider<T>(Name) pattern
		auto CheckProvider = [&](const TCHAR* ProviderName)
		{
			ProvidersArray.Add(MakeShared<FJsonValueString>(FString(ProviderName)));
		};

		// Try to access well-known providers
		const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
		CheckProvider(TEXT("frames"));

		const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);
		CheckProvider(TEXT("threads"));

		const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(*Session);
		if (TimingProvider)
		{
			CheckProvider(TEXT("timing_profiler"));
		}

		const TraceServices::ILoadTimeProfilerProvider* LoadTimeProvider = TraceServices::ReadLoadTimeProfilerProvider(*Session);
		if (LoadTimeProvider)
		{
			CheckProvider(TEXT("loadtime_profiler"));
		}

		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);
		CheckProvider(TEXT("counters"));

		const TraceServices::IMemoryProvider* MemoryProvider = TraceServices::ReadMemoryProvider(*Session);
		if (MemoryProvider)
		{
			CheckProvider(TEXT("memory"));
		}
	}

	Result->SetArrayField(TEXT("available_providers"), ProvidersArray);
	Result->SetNumberField(TEXT("provider_count"), ProvidersArray.Num());

	UE_LOG(LogInsightMCP, Log, TEXT("Session opened: %s (%.2fs, %d providers)"),
		*FPaths::GetCleanFilename(FilePath), AnalyzeTime, ProvidersArray.Num());

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("session.open requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// session.info — Get info about current analysis session
// ============================================================================

bool FSessionInfoAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	if (!ValidateUE5(OutError))
	{
		return false;
	}

	if (!Context.bSessionLoaded || !Context.AnalysisSession.IsValid())
	{
		OutError = TEXT("No analysis session loaded. Call session.open first.");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSessionInfoAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("file"), Context.CurrentSessionFile);
	Result->SetStringField(TEXT("file_name"), FPaths::GetCleanFilename(Context.CurrentSessionFile));

	// File info
	int64 FileSize = IFileManager::Get().FileSize(*Context.CurrentSessionFile);
	Result->SetNumberField(TEXT("file_size_bytes"), static_cast<double>(FileSize));

	// Read detailed provider info
	{
		TraceServices::FAnalysisSessionReadScope ReadScope(*Session);

		// === Frame info ===
		const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
		TSharedPtr<FJsonObject> FrameInfo = MakeShared<FJsonObject>();

		// Frame count by type (Game=0, Render=1)
		uint64 GameFrameCount = FrameProvider.GetFrameCount(TraceFrameType_Game);
		uint64 RenderFrameCount = FrameProvider.GetFrameCount(TraceFrameType_Rendering);
		FrameInfo->SetNumberField(TEXT("game_frame_count"), static_cast<double>(GameFrameCount));
		FrameInfo->SetNumberField(TEXT("render_frame_count"), static_cast<double>(RenderFrameCount));
		Result->SetObjectField(TEXT("frames"), FrameInfo);

		// === Thread info ===
		const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);
		TSharedPtr<FJsonObject> ThreadInfo = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> ThreadsArray;
		int32 ThreadCount = 0;

		ThreadProvider.EnumerateThreads(
			[&ThreadsArray, &ThreadCount](const TraceServices::FThreadInfo& Thread)
			{
				TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
				TObj->SetNumberField(TEXT("id"), static_cast<double>(Thread.Id));
				TObj->SetStringField(TEXT("name"), Thread.Name);
				TObj->SetStringField(TEXT("group_name"), Thread.GroupName);
				ThreadsArray.Add(MakeShared<FJsonValueObject>(TObj));
				++ThreadCount;
			});

		ThreadInfo->SetNumberField(TEXT("count"), ThreadCount);
		ThreadInfo->SetArrayField(TEXT("threads"), ThreadsArray);
		Result->SetObjectField(TEXT("thread_info"), ThreadInfo);

		// === Counter info ===
		const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);
		int32 CounterCount = 0;
		CounterProvider.EnumerateCounters(
			[&CounterCount](uint32 /*CounterId*/, const TraceServices::ICounter& /*Counter*/)
			{
				++CounterCount;
			});
		Result->SetNumberField(TEXT("counter_count"), CounterCount);

		// === Timing profiler check ===
		const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(*Session);
		Result->SetBoolField(TEXT("has_timing_profiler"), TimingProvider != nullptr);

		// === Load time profiler check ===
		const TraceServices::ILoadTimeProfilerProvider* LoadTimeProvider = TraceServices::ReadLoadTimeProfilerProvider(*Session);
		Result->SetBoolField(TEXT("has_loadtime_profiler"), LoadTimeProvider != nullptr);
	}

	Result->SetBoolField(TEXT("has_cached_results"),
		Context.CachedFrameTimeSummary.IsValid() || Context.CachedLoadTimeSummary.IsValid());

	UE_LOG(LogInsightMCP, Log, TEXT("session.info: %s"), *Context.CurrentSessionFile);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("session.info requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// session.close — Close the current analysis session
// ============================================================================

bool FSessionCloseAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	if (!Context.bSessionLoaded)
	{
		OutError = TEXT("No session is currently loaded.");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSessionCloseAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	FString PreviousFile = Context.CurrentSessionFile;

	Context.ResetSession();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("closed_file"), PreviousFile);
	Result->SetBoolField(TEXT("session_loaded"), false);

	UE_LOG(LogInsightMCP, Log, TEXT("Session closed: %s"), *PreviousFile);

	return CreateSuccessResponse(Result);
}
