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
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/StringBuilder.h"
#include "Editor.h"                        // GEditor — needed by FBatchExecuteInsightAction

#if ENGINE_MAJOR_VERSION >= 5
#include "Trace/Detail/Channel.h"
#endif

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
		TEXT("cpu"), TEXT("gpu"), TEXT("frame"), TEXT("bookmark"),
		TEXT("counters"), TEXT("log"), TEXT("file"), TEXT("loadtime"),
		TEXT("memory"), TEXT("net"), TEXT("slate"), TEXT("rhicommands"),
		TEXT("rendercommands"), TEXT("animation"), TEXT("physics"),
		TEXT("networkprediction"), TEXT("niagara"), TEXT("tracesourcefilters"),
		TEXT("csv"), TEXT("object"),
	};
	static constexpr int32 NumKnownChannels = UE_ARRAY_COUNT(KnownChannels);
}
#endif

namespace
{
	struct FChannelInfoSnapshot
	{
		FString Name;
		FString RawName;
		FString Description;
		bool bEnabled = false;
		bool bReadOnly = false;
	};

	FString CanonicalChannelName(const FString& InName)
	{
		FString Name = InName;
		Name.TrimStartAndEndInline();
		Name.RemoveFromEnd(TEXT("Channel"), ESearchCase::IgnoreCase);
		Name.ToLowerInline();
		return Name;
	}

	void EnumerateChannelSnapshots(TArray<FChannelInfoSnapshot>& OutChannels)
	{
		OutChannels.Empty();
#if ENGINE_MAJOR_VERSION >= 5
		UE::Trace::EnumerateChannels(
			[](const UE::Trace::FChannelInfo& Info, void* UserData) -> bool
			{
				auto* Channels = static_cast<TArray<FChannelInfoSnapshot>*>(UserData);
				FChannelInfoSnapshot Snapshot;
				Snapshot.RawName = ANSI_TO_TCHAR(Info.Name);
				Snapshot.Name = CanonicalChannelName(Snapshot.RawName);
				Snapshot.Description = Info.Desc ? ANSI_TO_TCHAR(Info.Desc) : TEXT("");
				Snapshot.bEnabled = Info.bIsEnabled;
				Snapshot.bReadOnly = Info.bIsReadOnly;
				Channels->Add(MoveTemp(Snapshot));
				return true;
			},
			&OutChannels);
#else
		for (int32 Index = 0; Index < InsightMCPCompat::NumKnownChannels; ++Index)
		{
			const TCHAR* Name = InsightMCPCompat::KnownChannels[Index];
			if (Trace::IsChannel(Name))
			{
				FChannelInfoSnapshot Snapshot;
				Snapshot.Name = Name;
				Snapshot.RawName = Name;
				Snapshot.bEnabled = false;
				OutChannels.Add(MoveTemp(Snapshot));
			}
		}
#endif
	}

	const FChannelInfoSnapshot* FindChannelSnapshot(
		const TArray<FChannelInfoSnapshot>& Channels,
		const FString& RequestedName)
	{
		const FString Canonical = CanonicalChannelName(RequestedName);
		return Channels.FindByPredicate([&Canonical](const FChannelInfoSnapshot& Channel)
		{
			return Channel.Name.Equals(Canonical, ESearchCase::IgnoreCase);
		});
	}

	void ExpandChannelTokens(const FString& ChannelString, TArray<FString>& OutChannels)
	{
		TArray<FString> Tokens;
		ChannelString.ParseIntoArray(Tokens, TEXT(","), true);
		for (FString& Token : Tokens)
		{
			Token = CanonicalChannelName(Token);
			if (Token == TEXT("memory"))
			{
				OutChannels.Append({TEXT("memtag"), TEXT("memalloc"), TEXT("callstack"), TEXT("module")});
			}
			else if (Token == TEXT("default"))
			{
				OutChannels.Append({TEXT("cpu"), TEXT("gpu"), TEXT("frame"), TEXT("log"), TEXT("bookmark"), TEXT("screenshot"), TEXT("region")});
			}
			else if (!Token.IsEmpty())
			{
				OutChannels.Add(Token);
			}
		}

		TSet<FString> Seen;
		OutChannels.RemoveAll([&Seen](const FString& Channel)
		{
			if (Seen.Contains(Channel))
			{
				return true;
			}
			Seen.Add(Channel);
			return false;
		});
	}

	FString JoinChannels(const TSet<FString>& Channels)
	{
		TArray<FString> Sorted = Channels.Array();
		Sorted.Sort();
		return FString::Join(Sorted, TEXT(","));
	}

	FString NormalizeTraceFilePath(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return Path;
		}
		FString Absolute = FPaths::ConvertRelativePathToFull(Path);
		FPaths::CollapseRelativeDirectories(Absolute);
		Absolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Absolute);
		FPaths::NormalizeFilename(Absolute);
		return Absolute;
	}

	bool FindUnavailableStartupChannels(const FString& ChannelString, TArray<FString>& OutMissing)
	{
		TArray<FString> Requested;
		ExpandChannelTokens(ChannelString, Requested);

		TArray<FChannelInfoSnapshot> Available;
		EnumerateChannelSnapshots(Available);
		for (const FString& Name : Requested)
		{
			const FChannelInfoSnapshot* Channel = FindChannelSnapshot(Available, Name);
			if (Channel && Channel->bReadOnly && !Channel->bEnabled)
			{
				OutMissing.Add(Channel->Name);
			}
		}
		return OutMissing.Num() > 0;
	}

	struct FFileSettlement
	{
		bool bExists = false;
		bool bSizeKnown = false;
		bool bPending = false;
		int64 Size = 0;
	};

	FFileSettlement WaitForFileSettlement(const FString& FilePath, double TimeoutSeconds = 0.5)
	{
		FFileSettlement Result;
		IFileManager& FileManager = IFileManager::Get();
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		int64 PreviousSize = -1;
		int32 StableReads = 0;

		do
		{
			Result.bExists = FileManager.FileExists(*FilePath);
			if (Result.bExists)
			{
				const int64 CurrentSize = FileManager.FileSize(*FilePath);
				if (CurrentSize >= 0 && CurrentSize == PreviousSize)
				{
					++StableReads;
					if (StableReads >= 2)
					{
						Result.bSizeKnown = true;
						Result.Size = CurrentSize;
						return Result;
					}
				}
				else
				{
					StableReads = 0;
				}
				PreviousSize = CurrentSize;
				Result.Size = FMath::Max<int64>(CurrentSize, 0);
			}
			FPlatformProcess::Sleep(0.025f);
		}
		while (FPlatformTime::Seconds() < Deadline);

		Result.bPending = true;
		return Result;
	}

#if ENGINE_MAJOR_VERSION >= 5
	bool RestorePreviousNetworkTrace(FInsightContext& Context)
	{
		if (!Context.bHasPreviousTrace ||
			Context.PreviousConnectionType != FTraceAuxiliary::EConnectionType::Network ||
			Context.PreviousTraceDestination.IsEmpty())
		{
			Context.ClearPreviousTraceState();
			return false;
		}

		const FString Destination = Context.PreviousTraceDestination;
		const FString Channels = Context.PreviousTraceChannels.IsEmpty()
			? TEXT("default")
			: Context.PreviousTraceChannels;
		Context.ClearPreviousTraceState();

		FTraceAuxiliary::FOptions Options;
		Options.bExcludeTail = false;
		const bool bRestored = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::Network,
			*Destination,
			*Channels,
			&Options);
		Context.RefreshTraceStateFromNative();
		return bRestored;
	}
#endif
}


// ============================================================================
// trace.start — Start Trace recording
// ============================================================================

bool FTraceStartAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	Context.RefreshTraceStateFromNative();

	const FString OutputMode = GetOptionalString(Params, TEXT("output"), TEXT("file"));
	if (OutputMode != TEXT("file") && OutputMode != TEXT("server"))
	{
		OutError = TEXT("Parameter 'output' must be 'file' or 'server'.");
		return false;
	}

	if (Context.bIsTracing)
	{
		if (Context.bTraceStartedByMCP)
		{
			OutError = TEXT("An MCP-managed Trace is already running. Call trace.stop first.");
			return false;
		}
		if (!GetOptionalBool(Params, TEXT("replace_existing"), false))
		{
			OutError = FString::Printf(
				TEXT("An external Trace is already connected (%s: %s). Set replace_existing=true to replace it temporarily."),
				*FInsightContext::ConnectionTypeToString(Context.ActiveConnectionType),
				*Context.ActiveTraceDestination);
			return false;
		}
	}

	const FString ChannelString = GetOptionalString(Params, TEXT("channels"), TEXT("cpu,gpu,frame,bookmark,loadtime"));
	TArray<FString> MissingStartupChannels;
	if (FindUnavailableStartupChannels(ChannelString, MissingStartupChannels))
	{
		OutError = FString::Printf(
			TEXT("Startup-only Trace channels are not enabled: %s. Relaunch Unreal Editor with these channels in -trace before recording."),
			*FString::Join(MissingStartupChannels, TEXT(",")));
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FTraceStartAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	Context.RefreshTraceStateFromNative();

	FString ChannelString = GetOptionalString(Params, TEXT("channels"), TEXT("cpu,gpu,frame,bookmark,loadtime"));
	TArray<FString> RequestedChannels;
	ChannelString.ParseIntoArray(RequestedChannels, TEXT(","), true);
	for (FString& Channel : RequestedChannels)
	{
		Channel.TrimStartAndEndInline();
	}
	ChannelString = FString::Join(RequestedChannels, TEXT(","));

	const FString OutputMode = GetOptionalString(Params, TEXT("output"), TEXT("file"));
	const bool bIncludeTail = GetOptionalBool(Params, TEXT("include_tail"), false);

#if ENGINE_MAJOR_VERSION >= 5
	if (Context.bIsTracing)
	{
		Context.bHasPreviousTrace = true;
		Context.PreviousConnectionType = Context.ActiveConnectionType;
		Context.PreviousTraceDestination = Context.ActiveTraceDestination;
		Context.PreviousTraceChannels = JoinChannels(Context.EnabledChannels);

		if (!FTraceAuxiliary::Stop())
		{
			Context.ClearPreviousTraceState();
			return CreateErrorResponse(TEXT("Failed to stop the existing Trace connection"), TEXT("trace_replace_failed"));
		}
		Context.ClearActiveTraceState();
		FPlatformProcess::Sleep(0.05f);
	}
#endif

	FString TraceFile;
	FString Target;
	bool bStarted = false;

#if ENGINE_MAJOR_VERSION >= 5
	FTraceAuxiliary::FOptions Options;
	Options.bExcludeTail = !bIncludeTail;
	Options.bNoWorkerThread = false;

	if (OutputMode == TEXT("file"))
	{
		TraceFile = NormalizeTraceFilePath(
			FInsightContext::GetDefaultTraceDir() /
			FString::Printf(TEXT("Trace_%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TraceFile), true);
		Target = TraceFile;
		bStarted = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::File,
			*Target,
			*ChannelString,
			&Options);
	}
	else
	{
		Target = GetOptionalString(Params, TEXT("host"), TEXT("127.0.0.1"));
		bStarted = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::Network,
			*Target,
			*ChannelString,
			&Options);
	}
#else
	for (const FString& Channel : RequestedChannels)
	{
		if (Trace::IsChannel(*Channel))
		{
			Trace::ToggleChannel(*Channel, true);
		}
	}

	if (OutputMode == TEXT("file"))
	{
		TraceFile = NormalizeTraceFilePath(
			FInsightContext::GetDefaultTraceDir() /
			FString::Printf(TEXT("Trace_%s.utrace"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		Target = TraceFile;
		bStarted = Trace::WriteTo(*TraceFile);
	}
	else
	{
		Target = GetOptionalString(Params, TEXT("host"), TEXT("127.0.0.1"));
		uint32 Port = static_cast<uint32>(GetOptionalNumber(Params, TEXT("port"), 0.0));
		if (Port == 0)
		{
			Port = Trace::GetConnectPort();
		}
		bStarted = Trace::SendTo(*Target, Port);
	}
#endif

	if (!bStarted)
	{
#if ENGINE_MAJOR_VERSION >= 5
		const bool bRestored = RestorePreviousNetworkTrace(Context);
		UE_LOG(LogInsightMCP, Warning, TEXT("Trace start failed; previous network trace restored=%s"), bRestored ? TEXT("true") : TEXT("false"));
#endif
		return CreateErrorResponse(
			FString::Printf(TEXT("Failed to start Trace in '%s' mode"), *OutputMode),
			TEXT("trace_start_failed"));
	}

#if ENGINE_MAJOR_VERSION >= 5
	FTraceAuxiliary::EConnectionType ConnectionType = OutputMode == TEXT("file")
		? FTraceAuxiliary::EConnectionType::File
		: FTraceAuxiliary::EConnectionType::Network;
	FString NativeDestination = FTraceAuxiliary::GetTraceDestinationString();
	if (ConnectionType == FTraceAuxiliary::EConnectionType::File)
	{
		NativeDestination = NormalizeTraceFilePath(NativeDestination.IsEmpty() ? TraceFile : NativeDestination);
		TraceFile = NativeDestination;
	}
	else if (NativeDestination.IsEmpty())
	{
		NativeDestination = Target;
	}
	Context.MarkMCPTraceStarted(ConnectionType, NativeDestination);
#else
	Context.bIsTracing = true;
	Context.bTraceStartedByMCP = true;
	Context.bTraceStartTimeKnown = true;
	Context.TraceStartTime = FPlatformTime::Seconds();
	Context.ActiveTraceFile = TraceFile;
	Context.ActiveTraceDestination = Target;
	Context.EnabledChannels = TSet<FString>(RequestedChannels);
#endif
	Context.InvalidateCache();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("output_mode"), OutputMode);
	Result->SetStringField(TEXT("connection_type"), FInsightContext::ConnectionTypeToString(Context.ActiveConnectionType));
	Result->SetStringField(TEXT("destination"), Context.ActiveTraceDestination);
	Result->SetStringField(TEXT("trace_file"), Context.ActiveTraceFile);
	Result->SetBoolField(TEXT("managed_by_mcp"), true);
	Result->SetBoolField(TEXT("include_tail"), bIncludeTail);
	Result->SetNumberField(TEXT("requested_channel_count"), RequestedChannels.Num());

	TArray<TSharedPtr<FJsonValue>> RequestedArray;
	for (const FString& Channel : RequestedChannels)
	{
		RequestedArray.Add(MakeShared<FJsonValueString>(Channel));
	}
	Result->SetArrayField(TEXT("requested_channels"), RequestedArray);

	TArray<TSharedPtr<FJsonValue>> EnabledArray;
	for (const FString& Channel : Context.EnabledChannels)
	{
		EnabledArray.Add(MakeShared<FJsonValueString>(Channel));
	}
	Result->SetArrayField(TEXT("enabled_channels"), EnabledArray);
	Result->SetNumberField(TEXT("enabled_channel_count"), EnabledArray.Num());

	UE_LOG(LogInsightMCP, Log, TEXT("Trace started: mode=%s, destination=%s, requested_channels=%d"),
		*OutputMode, *Context.ActiveTraceDestination, RequestedChannels.Num());

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
	Context.RefreshTraceStateFromNative();
	if (!Context.bIsTracing)
	{
		OutError = TEXT("No Trace is currently running.");
		return false;
	}
	if (!Context.bTraceStartedByMCP && !GetOptionalBool(Params, TEXT("force_external"), false))
	{
		OutError = FString::Printf(
			TEXT("Current Trace is external (%s: %s). Set force_external=true to stop it explicitly."),
			*FInsightContext::ConnectionTypeToString(Context.ActiveConnectionType),
			*Context.ActiveTraceDestination);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FTraceStopAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	Context.RefreshTraceStateFromNative();

	const bool bManagedByMCP = Context.bTraceStartedByMCP;
	const bool bDurationKnown = Context.bTraceStartTimeKnown;
	const double ElapsedSeconds = bDurationKnown
		? FPlatformTime::Seconds() - Context.TraceStartTime
		: 0.0;
	const FTraceAuxiliary::EConnectionType ConnectionType = Context.ActiveConnectionType;
	const FString Destination = Context.ActiveTraceDestination;
	const FString TraceFile = ConnectionType == FTraceAuxiliary::EConnectionType::File
		? NormalizeTraceFilePath(Destination)
		: FString();

#if ENGINE_MAJOR_VERSION >= 5
	const bool bStopAccepted = FTraceAuxiliary::Stop();
#else
	const bool bStopAccepted = Trace::Stop();
#endif
	if (!bStopAccepted)
	{
		return CreateErrorResponse(TEXT("Native Trace system rejected stop request"), TEXT("trace_stop_failed"));
	}

	Context.ClearActiveTraceState();
	Context.InvalidateCache();

	FFileSettlement FileSettlement;
	if (ConnectionType == FTraceAuxiliary::EConnectionType::File && !TraceFile.IsEmpty())
	{
		FileSettlement = WaitForFileSettlement(TraceFile);
	}

	bool bRestoredPrevious = false;
	bool bRestoreSkipped = false;
#if ENGINE_MAJOR_VERSION >= 5
	if (GetOptionalBool(Params, TEXT("restore_previous"), true) && Context.bHasPreviousTrace)
	{
		bRestoreSkipped = Context.PreviousConnectionType != FTraceAuxiliary::EConnectionType::Network;
		bRestoredPrevious = RestorePreviousNetworkTrace(Context);
	}
	else if (Context.bHasPreviousTrace)
	{
		Context.ClearPreviousTraceState();
	}
#endif

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("stop_accepted"), true);
	Result->SetBoolField(TEXT("managed_by_mcp"), bManagedByMCP);
	Result->SetBoolField(TEXT("duration_known"), bDurationKnown);
	if (bDurationKnown)
	{
		Result->SetNumberField(TEXT("duration_seconds"), ElapsedSeconds);
	}
	Result->SetStringField(TEXT("connection_type"), FInsightContext::ConnectionTypeToString(ConnectionType));
	Result->SetStringField(TEXT("destination"), Destination);
	Result->SetStringField(TEXT("trace_file"), TraceFile);
	Result->SetBoolField(TEXT("file_size_applicable"), ConnectionType == FTraceAuxiliary::EConnectionType::File);
	if (ConnectionType == FTraceAuxiliary::EConnectionType::File)
	{
		Result->SetBoolField(TEXT("file_exists"), FileSettlement.bExists);
		Result->SetBoolField(TEXT("file_size_known"), FileSettlement.bSizeKnown);
		Result->SetBoolField(TEXT("file_size_pending"), FileSettlement.bPending);
		if (FileSettlement.bSizeKnown)
		{
			Result->SetNumberField(TEXT("file_size_bytes"), static_cast<double>(FileSettlement.Size));
			Result->SetStringField(TEXT("file_size_human"),
				FileSettlement.Size > 1024 * 1024
					? FString::Printf(TEXT("%.1f MB"), FileSettlement.Size / (1024.0 * 1024.0))
					: FString::Printf(TEXT("%.1f KB"), FileSettlement.Size / 1024.0));
		}
	}
	Result->SetBoolField(TEXT("restored_previous"), bRestoredPrevious);
	Result->SetBoolField(TEXT("previous_restore_skipped"), bRestoreSkipped);

	UE_LOG(LogInsightMCP, Log, TEXT("Trace stopped: destination=%s, duration_known=%s, restored_previous=%s"),
		*Destination,
		bDurationKnown ? TEXT("true") : TEXT("false"),
		bRestoredPrevious ? TEXT("true") : TEXT("false"));

	return CreateSuccessResponse(Result);
}


// ============================================================================
// trace.status — Get current Trace status
// ============================================================================

TSharedPtr<FJsonObject> FTraceStatusAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	Context.RefreshTraceStateFromNative();

	TSharedPtr<FJsonObject> Result = Context.ToJson();
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
	Context.RefreshTraceStateFromNative();

	TArray<FChannelInfoSnapshot> ChannelList;
	EnumerateChannelSnapshots(ChannelList);
	ChannelList.Sort([](const FChannelInfoSnapshot& A, const FChannelInfoSnapshot& B)
	{
		return A.Name < B.Name;
	});

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total_channels"), ChannelList.Num());

	TArray<TSharedPtr<FJsonValue>> ChannelsJson;
	TArray<TSharedPtr<FJsonValue>> EnabledJson;
	TArray<TSharedPtr<FJsonValue>> StartupOnlyJson;

	for (const FChannelInfoSnapshot& Info : ChannelList)
	{
		TSharedPtr<FJsonObject> ChannelObject = MakeShared<FJsonObject>();
		ChannelObject->SetStringField(TEXT("name"), Info.Name);
		ChannelObject->SetStringField(TEXT("raw_name"), Info.RawName);
		ChannelObject->SetStringField(TEXT("description"), Info.Description);
		ChannelObject->SetBoolField(TEXT("enabled"), Info.bEnabled);
		ChannelObject->SetBoolField(TEXT("read_only"), Info.bReadOnly);
		ChannelObject->SetBoolField(TEXT("startup_only"), Info.bReadOnly);
		ChannelObject->SetBoolField(TEXT("runtime_toggleable"), !Info.bReadOnly);
		ChannelsJson.Add(MakeShared<FJsonValueObject>(ChannelObject));

		if (Info.bEnabled)
		{
			EnabledJson.Add(MakeShared<FJsonValueString>(Info.Name));
		}
		if (Info.bReadOnly)
		{
			StartupOnlyJson.Add(MakeShared<FJsonValueString>(Info.Name));
		}
	}

	Result->SetArrayField(TEXT("channels"), ChannelsJson);
	Result->SetArrayField(TEXT("enabled"), EnabledJson);
	Result->SetArrayField(TEXT("startup_only"), StartupOnlyJson);
	Result->SetNumberField(TEXT("enabled_count"), EnabledJson.Num());
	Result->SetStringField(TEXT("enumeration_method"),
#if ENGINE_MAJOR_VERSION >= 5
		TEXT("native_channel_info")
#else
		TEXT("known_channel_probe")
#endif
	);

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
	return GetRequiredString(Params, TEXT("channels"), Channels, OutError);
}

TSharedPtr<FJsonObject> FTraceChannelsToggleAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
	FString ChannelString;
	FString Unused;
	GetRequiredString(Params, TEXT("channels"), ChannelString, Unused);
	const bool bEnable = GetOptionalBool(Params, TEXT("enable"), true);

	TArray<FString> Requested;
	ExpandChannelTokens(ChannelString, Requested);
	TArray<FChannelInfoSnapshot> Available;
	EnumerateChannelSnapshots(Available);

	TArray<FString> Errors;
	for (const FString& Name : Requested)
	{
		const FChannelInfoSnapshot* Channel = FindChannelSnapshot(Available, Name);
		if (!Channel)
		{
			Errors.Add(FString::Printf(TEXT("Unknown Trace channel '%s'"), *Name));
		}
		else if (Channel->bReadOnly && Channel->bEnabled != bEnable)
		{
			Errors.Add(FString::Printf(
				TEXT("Trace channel '%s' is startup-only and currently %s"),
				*Channel->Name,
				Channel->bEnabled ? TEXT("enabled") : TEXT("disabled")));
		}
	}
	if (Errors.Num() > 0)
	{
		return CreateErrorResponse(FString::Join(Errors, TEXT("; ")), TEXT("channel_toggle_rejected"));
	}

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	for (const FString& Name : Requested)
	{
		const FChannelInfoSnapshot* Before = FindChannelSnapshot(Available, Name);
		const bool bBefore = Before && Before->bEnabled;
		if (Before && !Before->bReadOnly && bBefore != bEnable)
		{
#if ENGINE_MAJOR_VERSION >= 5
			UE::Trace::ToggleChannel(*Before->Name, bEnable);
#else
			Trace::ToggleChannel(*Before->Name, bEnable);
#endif
		}

		TArray<FChannelInfoSnapshot> AfterChannels;
		EnumerateChannelSnapshots(AfterChannels);
		const FChannelInfoSnapshot* After = FindChannelSnapshot(AfterChannels, Name);
		const bool bAfter = After && After->bEnabled;

		TSharedPtr<FJsonObject> ChannelResult = MakeShared<FJsonObject>();
		ChannelResult->SetStringField(TEXT("name"), Name);
		ChannelResult->SetBoolField(TEXT("read_only"), Before && Before->bReadOnly);
		ChannelResult->SetBoolField(TEXT("changed"), bBefore != bAfter);
		ChannelResult->SetBoolField(TEXT("enabled"), bAfter);
		ChannelResult->SetBoolField(TEXT("request_satisfied"), bAfter == bEnable);
		ResultsArray.Add(MakeShared<FJsonValueObject>(ChannelResult));
	}

	Context.RefreshTraceStateFromNative();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("requested_enabled"), bEnable);
	Result->SetNumberField(TEXT("count"), Requested.Num());
	Result->SetArrayField(TEXT("channels"), ResultsArray);
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
	Context.RefreshTraceStateFromNative();
	if (!Context.bIsTracing)
	{
		OutError = TEXT("Cannot add a bookmark because no Trace connection is active.");
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

	TRACE_BOOKMARK(TEXT("%s"), *Label);

	const double CurrentTime = FPlatformTime::Seconds();
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("label"), Label);
	Result->SetNumberField(TEXT("timestamp"), CurrentTime);
	Result->SetBoolField(TEXT("trace_active"), Context.bIsTracing);
	Result->SetBoolField(TEXT("trace_relative_time_known"), Context.bTraceStartTimeKnown);
	if (Context.bTraceStartTimeKnown)
	{
		Result->SetNumberField(TEXT("trace_relative_seconds"), CurrentTime - Context.TraceStartTime);
	}

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
		TSharedPtr<FJsonObject> SubResult = Bridge->ExecuteCommand(CmdType, CmdParams);

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
