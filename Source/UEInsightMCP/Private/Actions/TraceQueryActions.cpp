// UEInsightMCP — Trace Query Actions implementation (Phase 2)
// Data query: frame_times, cpu_threads, gpu_timing, loadtime,
// loadtime_deps, memory, counters, bookmarks, hitches

#include "Actions/TraceQueryActions.h"
#include "InsightBridge.h"
#include "Misc/Paths.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Memory.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Containers/Tables.h"
#endif


// ============================================================================
// Shared utility: GetValidSessionTimeRange
// Walks backwards from last frame to find a non-inf EndTime.
// Lives here (not in header) because it depends on TraceServices types.
// ============================================================================
#if ENGINE_MAJOR_VERSION >= 5
static void GetValidSessionTimeRange(
	const TraceServices::IFrameProvider& FrameProvider,
	ETraceFrameType FrameType,
	double& OutStart,
	double& OutEnd)
{
	OutStart = 0.0;
	OutEnd = 0.0;
	uint64 FrameCount = FrameProvider.GetFrameCount(FrameType);
	if (FrameCount == 0) return;

	const TraceServices::FFrame* First = FrameProvider.GetFrame(FrameType, 0);
	if (First) OutStart = First->StartTime;

	for (int64 i = (int64)FrameCount - 1; i >= 0; --i)
	{
		const TraceServices::FFrame* F = FrameProvider.GetFrame(FrameType, i);
		if (F && FMath::IsFinite(F->EndTime) && F->EndTime > OutStart)
		{
			OutEnd = F->EndTime;
			return;
		}
	}
	// Fallback: if no valid EndTime found, use start (will result in empty range)
}
#endif


// ============================================================================
// query.frame_times — Frame time statistics
// ============================================================================

bool FQueryFrameTimesAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryFrameTimesAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;

	FString FrameTypeStr = GetOptionalString(Params, TEXT("frame_type"), TEXT("game"));
	int32 Limit = static_cast<int32>(GetOptionalNumber(Params, TEXT("limit"), 0.0));

	ETraceFrameType FrameType = FrameTypeStr == TEXT("render")
		? TraceFrameType_Rendering
		: TraceFrameType_Game;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);

	uint64 TotalFrames = FrameProvider.GetFrameCount(FrameType);
	Result->SetStringField(TEXT("frame_type"), FrameTypeStr);
	Result->SetNumberField(TEXT("total_frames"), static_cast<double>(TotalFrames));

	if (TotalFrames == 0)
	{
		Result->SetStringField(TEXT("message"), TEXT("No frames of this type found"));
		return CreateSuccessResponse(Result);
	}

	// Collect frame times
	struct FFrameStats
	{
		double Duration;
		uint64 Index;
	};
	TArray<FFrameStats> FrameTimes;
	FrameTimes.Reserve(static_cast<int32>(FMath::Min(TotalFrames, (uint64)100000)));

	double TotalTime = 0.0;
	double MinTime = DBL_MAX;
	double MaxTime = -DBL_MAX;

	FrameProvider.EnumerateFrames(FrameType, (uint64)0, TotalFrames,
		[&](const TraceServices::FFrame& Frame)
		{
			double Duration = Frame.EndTime - Frame.StartTime;
			// Filter out invalid frames: inf, NaN, negative, or unreasonably long (>60s)
			// The last frame in a trace often has EndTime=0 or inf
			if (Duration > 0.0 && FMath::IsFinite(Duration) && Duration < 60.0)
			{
				FrameTimes.Add({Duration, Frame.Index});
				TotalTime += Duration;
				MinTime = FMath::Min(MinTime, Duration);
				MaxTime = FMath::Max(MaxTime, Duration);
			}
		});

	int32 ValidFrames = FrameTimes.Num();
	if (ValidFrames == 0)
	{
		Result->SetStringField(TEXT("message"), TEXT("No valid frame times found"));
		return CreateSuccessResponse(Result);
	}

	double AvgTime = TotalTime / ValidFrames;

	// Sort for percentile calculations
	FrameTimes.Sort([](const FFrameStats& A, const FFrameStats& B)
	{
		return A.Duration < B.Duration;
	});

	double MedianTime = FrameTimes[ValidFrames / 2].Duration;
	double P95Time = FrameTimes[FMath::Min((int32)(ValidFrames * 0.95), ValidFrames - 1)].Duration;
	double P99Time = FrameTimes[FMath::Min((int32)(ValidFrames * 0.99), ValidFrames - 1)].Duration;

	// Summary stats
	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total_frames"), ValidFrames);
	Summary->SetNumberField(TEXT("total_time_seconds"), TotalTime);
	Summary->SetNumberField(TEXT("avg_ms"), AvgTime * 1000.0);
	Summary->SetNumberField(TEXT("min_ms"), MinTime * 1000.0);
	Summary->SetNumberField(TEXT("max_ms"), MaxTime * 1000.0);
	Summary->SetNumberField(TEXT("median_ms"), MedianTime * 1000.0);
	Summary->SetNumberField(TEXT("p95_ms"), P95Time * 1000.0);
	Summary->SetNumberField(TEXT("p99_ms"), P99Time * 1000.0);
	Summary->SetNumberField(TEXT("avg_fps"), 1.0 / AvgTime);
	Summary->SetNumberField(TEXT("min_fps"), 1.0 / MaxTime);  // worst frame = min FPS
	Summary->SetNumberField(TEXT("max_fps"), 1.0 / MinTime);  // best frame = max FPS
	Result->SetObjectField(TEXT("summary"), Summary);

	// FPS distribution buckets
	TArray<TSharedPtr<FJsonValue>> BucketsArray;
	struct FBucket { const TCHAR* Label; double ThresholdMs; int32 Count; };
	FBucket Buckets[] = {
		{TEXT("<16.67ms (60+fps)"), 16.67, 0},
		{TEXT("16.67-33.33ms (30-60fps)"), 33.33, 0},
		{TEXT("33.33-50ms (20-30fps)"), 50.0, 0},
		{TEXT("50-100ms (10-20fps)"), 100.0, 0},
		{TEXT(">100ms (<10fps)"), DBL_MAX, 0},
	};

	for (const FFrameStats& FS : FrameTimes)
	{
		double Ms = FS.Duration * 1000.0;
		for (int32 b = 0; b < UE_ARRAY_COUNT(Buckets); ++b)
		{
			if (Ms < Buckets[b].ThresholdMs || b == UE_ARRAY_COUNT(Buckets) - 1)
			{
				Buckets[b].Count++;
				break;
			}
		}
	}

	for (int32 b = 0; b < UE_ARRAY_COUNT(Buckets); ++b)
	{
		TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
		BObj->SetStringField(TEXT("label"), Buckets[b].Label);
		BObj->SetNumberField(TEXT("count"), Buckets[b].Count);
		BObj->SetNumberField(TEXT("percentage"), (Buckets[b].Count * 100.0) / ValidFrames);
		BucketsArray.Add(MakeShared<FJsonValueObject>(BObj));
	}
	Result->SetArrayField(TEXT("distribution"), BucketsArray);

	// Worst frames (top N)
	int32 WorstCount = Limit > 0 ? FMath::Min(Limit, ValidFrames) : FMath::Min(10, ValidFrames);
	TArray<TSharedPtr<FJsonValue>> WorstArray;
	for (int32 i = ValidFrames - 1; i >= FMath::Max(0, ValidFrames - WorstCount); --i)
	{
		TSharedPtr<FJsonObject> WObj = MakeShared<FJsonObject>();
		WObj->SetNumberField(TEXT("frame_index"), static_cast<double>(FrameTimes[i].Index));
		WObj->SetNumberField(TEXT("duration_ms"), FrameTimes[i].Duration * 1000.0);
		WObj->SetNumberField(TEXT("fps"), 1.0 / FrameTimes[i].Duration);
		WorstArray.Add(MakeShared<FJsonValueObject>(WObj));
	}
	Result->SetArrayField(TEXT("worst_frames"), WorstArray);

	UE_LOG(LogInsightMCP, Log, TEXT("query.frame_times: %d frames, avg=%.2fms, p99=%.2fms"),
		ValidFrames, AvgTime * 1000.0, P99Time * 1000.0);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.frame_times requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.cpu_threads — CPU thread timing summary
// ============================================================================

bool FQueryCpuThreadsAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryCpuThreadsAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	int32 TopN = static_cast<int32>(GetOptionalNumber(Params, TEXT("top_n"), 20.0));
	FString FilterName = GetOptionalString(Params, TEXT("filter_name"), TEXT(""));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(*Session);
	const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);

	if (!TimingProvider)
	{
		return CreateErrorResponse(TEXT("Timing profiler data not available in this trace"), TEXT("no_data"));
	}

	// Get session time range from frame provider (shared utility — handles inf EndTime)
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
	double SessionStart = 0.0;
	double SessionEnd = 0.0;
	GetValidSessionTimeRange(FrameProvider, TraceFrameType_Game, SessionStart, SessionEnd);

	// Enumerate threads
	TArray<TSharedPtr<FJsonValue>> ThreadsArray;
	int32 ThreadCount = 0;

	ThreadProvider.EnumerateThreads(
		[&](const TraceServices::FThreadInfo& Thread)
		{
			// Apply name filter if specified
			if (!FilterName.IsEmpty())
			{
				FString ThreadName(Thread.Name);
				if (!ThreadName.Contains(FilterName))
				{
					return;
				}
			}

			TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
			TObj->SetNumberField(TEXT("thread_id"), static_cast<double>(Thread.Id));
			TObj->SetStringField(TEXT("name"), Thread.Name ? Thread.Name : TEXT("Unknown"));
			TObj->SetStringField(TEXT("group"), Thread.GroupName ? Thread.GroupName : TEXT(""));

			// Try to get aggregated stats for this thread
			uint32 TimelineIndex;
			if (TimingProvider->GetCpuThreadTimelineIndex(Thread.Id, TimelineIndex) && SessionEnd > SessionStart)
			{
				TraceServices::FCreateAggreationParams AggParams;
				AggParams.IntervalStart = SessionStart;
				AggParams.IntervalEnd = SessionEnd;
				AggParams.CpuThreadFilter = [Thread](uint32 TId) { return TId == Thread.Id; };
				AggParams.IncludeGpu = false;

				TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* AggTable = 
					TimingProvider->CreateAggregation(AggParams);

				if (AggTable)
				{
					// Get top N timers by total inclusive time
					struct FTimerEntry
					{
						FString Name;
						uint64 Count;
						double TotalIncMs;
						double TotalExcMs;
						double AvgIncMs;
					};
					TArray<FTimerEntry> Entries;

					TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>* Reader = AggTable->CreateReader();
					if (Reader)
					{
						for (; Reader->IsValid(); Reader->NextRow())
						{
							const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow();
							if (Row && Row->Timer && Row->Timer->Name && Row->InstanceCount > 0)
							{
								double IncMs = Row->TotalInclusiveTime * 1000.0;
								double ExcMs = Row->TotalExclusiveTime * 1000.0;
								double AvgMs = Row->AverageInclusiveTime * 1000.0;
								// Skip entries with inf/NaN values
								if (!IsValidFloat(IncMs) || !IsValidFloat(ExcMs) || !IsValidFloat(AvgMs))
								{
									continue;
								}
								FTimerEntry Entry;
								Entry.Name = Row->Timer->Name;
								Entry.Count = Row->InstanceCount;
								Entry.TotalIncMs = IncMs;
								Entry.TotalExcMs = ExcMs;
								Entry.AvgIncMs = AvgMs;
								Entries.Add(MoveTemp(Entry));
							}
						}
						delete Reader;
					}
					delete AggTable;

					// Sort by total inclusive time, descending
					Entries.Sort([](const FTimerEntry& A, const FTimerEntry& B)
					{
						return A.TotalIncMs > B.TotalIncMs;
					});

					int32 ShowCount = FMath::Min(TopN, Entries.Num());
					TArray<TSharedPtr<FJsonValue>> TopTimers;
					for (int32 i = 0; i < ShowCount; ++i)
					{
						TSharedPtr<FJsonObject> TimerObj = MakeShared<FJsonObject>();
						TimerObj->SetStringField(TEXT("name"), Entries[i].Name);
						TimerObj->SetNumberField(TEXT("count"), static_cast<double>(Entries[i].Count));
						TimerObj->SetNumberField(TEXT("total_inclusive_ms"), Entries[i].TotalIncMs);
						TimerObj->SetNumberField(TEXT("total_exclusive_ms"), Entries[i].TotalExcMs);
						TimerObj->SetNumberField(TEXT("avg_inclusive_ms"), Entries[i].AvgIncMs);
						TopTimers.Add(MakeShared<FJsonValueObject>(TimerObj));
					}
					TObj->SetArrayField(TEXT("top_timers"), TopTimers);
					TObj->SetNumberField(TEXT("total_timer_count"), Entries.Num());
				}

				TObj->SetBoolField(TEXT("has_timeline"), true);
			}
			else
			{
				TObj->SetBoolField(TEXT("has_timeline"), false);
			}

			ThreadsArray.Add(MakeShared<FJsonValueObject>(TObj));
			++ThreadCount;
		});

	Result->SetNumberField(TEXT("thread_count"), ThreadCount);
	Result->SetArrayField(TEXT("threads"), ThreadsArray);
	SafeSetNumber(Result, TEXT("session_start"), SessionStart);
	SafeSetNumber(Result, TEXT("session_end"), SessionEnd);

	UE_LOG(LogInsightMCP, Log, TEXT("query.cpu_threads: %d threads"), ThreadCount);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.cpu_threads requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.gpu_timing — GPU timing data
// ============================================================================

bool FQueryGpuTimingAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryGpuTimingAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	int32 TopN = static_cast<int32>(GetOptionalNumber(Params, TEXT("top_n"), 20.0));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::ITimingProfilerProvider* TimingProvider = TraceServices::ReadTimingProfilerProvider(*Session);
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);

	if (!TimingProvider)
	{
		return CreateErrorResponse(TEXT("Timing profiler data not available"), TEXT("no_data"));
	}

	// Check GPU timeline availability
	uint32 GpuTimelineIndex;
	bool bHasGpu = TimingProvider->GetGpuTimelineIndex(GpuTimelineIndex);
	Result->SetBoolField(TEXT("has_gpu_data"), bHasGpu);

	if (!bHasGpu)
	{
		Result->SetStringField(TEXT("message"), TEXT("No GPU timing data in this trace. Ensure 'gpu' channel was enabled."));
		return CreateSuccessResponse(Result);
	}

	// Get time range (shared utility — handles inf EndTime)
	double SessionStart = 0.0;
	double SessionEnd = 0.0;
	GetValidSessionTimeRange(FrameProvider, TraceFrameType_Game, SessionStart, SessionEnd);

	// GPU aggregation
	TraceServices::FCreateAggreationParams AggParams;
	AggParams.IntervalStart = SessionStart;
	AggParams.IntervalEnd = SessionEnd;
	AggParams.CpuThreadFilter = [](uint32) { return false; };  // CPU excluded
	AggParams.IncludeGpu = true;

	TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>* AggTable = 
		TimingProvider->CreateAggregation(AggParams);

	if (!AggTable)
	{
		return CreateErrorResponse(TEXT("Failed to create GPU aggregation"), TEXT("aggregation_failed"));
	}

	struct FGpuEntry
	{
		FString Name;
		uint64 Count;
		double TotalIncMs;
		double TotalExcMs;
		double AvgIncMs;
		double MaxIncMs;
	};
	TArray<FGpuEntry> Entries;

	TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>* Reader = AggTable->CreateReader();
	if (Reader)
	{
		for (; Reader->IsValid(); Reader->NextRow())
		{
			const TraceServices::FTimingProfilerAggregatedStats* Row = Reader->GetCurrentRow();
			if (Row && Row->Timer && Row->Timer->Name && Row->InstanceCount > 0)
			{
				double IncMs = Row->TotalInclusiveTime * 1000.0;
				double ExcMs = Row->TotalExclusiveTime * 1000.0;
				double AvgMs = Row->AverageInclusiveTime * 1000.0;
				double MaxMs = Row->MaxInclusiveTime * 1000.0;
				// Skip entries with inf/NaN values
				if (!IsValidFloat(IncMs) || !IsValidFloat(ExcMs)) continue;
				FGpuEntry Entry;
				Entry.Name = Row->Timer->Name;
				Entry.Count = Row->InstanceCount;
				Entry.TotalIncMs = IncMs;
				Entry.TotalExcMs = ExcMs;
				Entry.AvgIncMs = IsValidFloat(AvgMs) ? AvgMs : 0.0;
				Entry.MaxIncMs = IsValidFloat(MaxMs) ? MaxMs : 0.0;
				Entries.Add(MoveTemp(Entry));
			}
		}
		delete Reader;
	}
	delete AggTable;

	// Sort by total inclusive time
	Entries.Sort([](const FGpuEntry& A, const FGpuEntry& B)
	{
		return A.TotalIncMs > B.TotalIncMs;
	});

	Result->SetNumberField(TEXT("total_gpu_timers"), Entries.Num());

	int32 ShowCount = FMath::Min(TopN, Entries.Num());
	TArray<TSharedPtr<FJsonValue>> TopTimers;
	for (int32 i = 0; i < ShowCount; ++i)
	{
		TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
		TObj->SetStringField(TEXT("name"), Entries[i].Name);
		TObj->SetNumberField(TEXT("count"), static_cast<double>(Entries[i].Count));
		TObj->SetNumberField(TEXT("total_inclusive_ms"), Entries[i].TotalIncMs);
		TObj->SetNumberField(TEXT("total_exclusive_ms"), Entries[i].TotalExcMs);
		TObj->SetNumberField(TEXT("avg_inclusive_ms"), Entries[i].AvgIncMs);
		TObj->SetNumberField(TEXT("max_inclusive_ms"), Entries[i].MaxIncMs);
		TopTimers.Add(MakeShared<FJsonValueObject>(TObj));
	}
	Result->SetArrayField(TEXT("top_gpu_timers"), TopTimers);

	UE_LOG(LogInsightMCP, Log, TEXT("query.gpu_timing: %d GPU timers"), Entries.Num());

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.gpu_timing requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.loadtime — Asset loading time data
// ============================================================================

bool FQueryLoadTimeAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryLoadTimeAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	int32 TopN = static_cast<int32>(GetOptionalNumber(Params, TEXT("top_n"), 30.0));
	FString SortBy = GetOptionalString(Params, TEXT("sort_by"), TEXT("duration"));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::ILoadTimeProfilerProvider* LoadProvider = TraceServices::ReadLoadTimeProfilerProvider(*Session);

	if (!LoadProvider)
	{
		return CreateErrorResponse(TEXT("Load time profiler data not available. Ensure 'loadtime' channel was enabled."), TEXT("no_data"));
	}

	// Get load requests
	const TraceServices::ITable<TraceServices::FLoadRequest>& RequestsTable = LoadProvider->GetRequestsTable();
	uint64 RequestCount = RequestsTable.GetRowCount();

	Result->SetNumberField(TEXT("total_requests"), static_cast<double>(RequestCount));

	struct FRequestEntry
	{
		FString Name;
		double StartTime;
		double Duration;
		int32 PackageCount;
	};
	TArray<FRequestEntry> Requests;

	TraceServices::ITableReader<TraceServices::FLoadRequest>* Reader = RequestsTable.CreateReader();
	if (Reader)
	{
		for (; Reader->IsValid(); Reader->NextRow())
		{
			const TraceServices::FLoadRequest* Row = Reader->GetCurrentRow();
			if (Row && Row->Name)
			{
				double Duration = Row->EndTime - Row->StartTime;
				// Filter out inf/NaN/negative durations (EndTime can be inf for incomplete loads)
				if (!IsValidFloat(Duration) || Duration < 0.0)
				{
					Duration = 0.0;
				}
				FRequestEntry Entry;
				Entry.Name = Row->Name;
				Entry.StartTime = IsValidFloat(Row->StartTime) ? Row->StartTime : 0.0;
				Entry.Duration = Duration;
				Entry.PackageCount = Row->Packages.Num();
				Requests.Add(MoveTemp(Entry));
			}
		}
		delete Reader;
	}

	// Sort
	if (SortBy == TEXT("start_time"))
	{
		Requests.Sort([](const FRequestEntry& A, const FRequestEntry& B)
		{
			return A.StartTime < B.StartTime;
		});
	}
	else // duration (default)
	{
		Requests.Sort([](const FRequestEntry& A, const FRequestEntry& B)
		{
			return A.Duration > B.Duration;
		});
	}

	// Summary
	double TotalLoadTime = 0.0;
	double MaxLoadTime = 0.0;
	for (const FRequestEntry& R : Requests)
	{
		TotalLoadTime += R.Duration;
		MaxLoadTime = FMath::Max(MaxLoadTime, R.Duration);
	}

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total_requests"), Requests.Num());
	Summary->SetNumberField(TEXT("total_load_time_seconds"), TotalLoadTime);
	Summary->SetNumberField(TEXT("max_load_time_ms"), MaxLoadTime * 1000.0);
	if (Requests.Num() > 0)
	{
		Summary->SetNumberField(TEXT("avg_load_time_ms"), (TotalLoadTime / Requests.Num()) * 1000.0);
	}
	Result->SetObjectField(TEXT("summary"), Summary);

	// Top requests
	int32 ShowCount = FMath::Min(TopN, Requests.Num());
	TArray<TSharedPtr<FJsonValue>> RequestsArray;
	for (int32 i = 0; i < ShowCount; ++i)
	{
		TSharedPtr<FJsonObject> RObj = MakeShared<FJsonObject>();
		RObj->SetStringField(TEXT("name"), Requests[i].Name);
		RObj->SetNumberField(TEXT("start_time_seconds"), Requests[i].StartTime);
		RObj->SetNumberField(TEXT("duration_ms"), Requests[i].Duration * 1000.0);
		RObj->SetNumberField(TEXT("package_count"), Requests[i].PackageCount);
		RequestsArray.Add(MakeShared<FJsonValueObject>(RObj));
	}
	Result->SetArrayField(TEXT("requests"), RequestsArray);
	Result->SetStringField(TEXT("sort_by"), SortBy);

	UE_LOG(LogInsightMCP, Log, TEXT("query.loadtime: %d requests, total=%.2fs"),
		Requests.Num(), TotalLoadTime);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.loadtime requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.loadtime_deps — Asset loading dependency graph
// ============================================================================

bool FQueryLoadTimeDepsAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryLoadTimeDepsAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	FString PackageFilter = GetOptionalString(Params, TEXT("package"), TEXT(""));
	int32 TopN = static_cast<int32>(GetOptionalNumber(Params, TEXT("top_n"), 20.0));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::ILoadTimeProfilerProvider* LoadProvider = TraceServices::ReadLoadTimeProfilerProvider(*Session);

	if (!LoadProvider)
	{
		return CreateErrorResponse(TEXT("Load time profiler data not available"), TEXT("no_data"));
	}

	// Get session time range (shared utility — handles inf EndTime)
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);
	double SessionStart = 0.0;
	double SessionEnd = DBL_MAX;
	{
		double ValidStart = 0.0, ValidEnd = 0.0;
		GetValidSessionTimeRange(FrameProvider, TraceFrameType_Game, ValidStart, ValidEnd);
		if (ValidEnd > ValidStart)
		{
			SessionStart = ValidStart;
			SessionEnd = ValidEnd;
		}
	}

	// Create package details table
	TraceServices::ITable<TraceServices::FPackagesTableRow>* PkgTable = 
		LoadProvider->CreatePackageDetailsTable(SessionStart, SessionEnd);

	if (!PkgTable)
	{
		return CreateErrorResponse(TEXT("Failed to create package details table"), TEXT("table_error"));
	}

	struct FPkgEntry
	{
		FString Name;
		uint64 TotalSerialSize;
		uint64 ExportCount;
		uint64 ExportSize;
		double MainThreadTime;
		double AsyncTime;
		TArray<FString> ImportedPackages;
	};
	TArray<FPkgEntry> Packages;

	TraceServices::ITableReader<TraceServices::FPackagesTableRow>* Reader = PkgTable->CreateReader();
	if (Reader)
	{
		for (; Reader->IsValid(); Reader->NextRow())
		{
			const TraceServices::FPackagesTableRow* Row = Reader->GetCurrentRow();
			if (Row && Row->PackageInfo && Row->PackageInfo->Name)
			{
				// Apply package name filter
				FString PkgName(Row->PackageInfo->Name);
				if (!PackageFilter.IsEmpty() && !PkgName.Contains(PackageFilter))
				{
					continue;
				}

				FPkgEntry Entry;
				Entry.Name = PkgName;
				Entry.TotalSerialSize = Row->TotalSerializedSize;
				Entry.ExportCount = Row->SerializedExportsCount;
				Entry.ExportSize = Row->SerializedExportsSize;
				Entry.MainThreadTime = IsValidFloat(Row->MainThreadTime) ? Row->MainThreadTime : 0.0;
				Entry.AsyncTime = IsValidFloat(Row->AsyncLoadingThreadTime) ? Row->AsyncLoadingThreadTime : 0.0;

				// Collect imported packages (dependencies)
				for (const TraceServices::FPackageInfo* Imported : Row->PackageInfo->ImportedPackages)
				{
					if (Imported && Imported->Name)
					{
						Entry.ImportedPackages.Add(Imported->Name);
					}
				}

				Packages.Add(MoveTemp(Entry));
			}
		}
		delete Reader;
	}
	delete PkgTable;

	// Sort by total load time (main + async)
	Packages.Sort([](const FPkgEntry& A, const FPkgEntry& B)
	{
		return (A.MainThreadTime + A.AsyncTime) > (B.MainThreadTime + B.AsyncTime);
	});

	Result->SetNumberField(TEXT("total_packages"), Packages.Num());
	if (!PackageFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("filter"), PackageFilter);
	}

	int32 ShowCount = FMath::Min(TopN, Packages.Num());
	TArray<TSharedPtr<FJsonValue>> PkgArray;
	for (int32 i = 0; i < ShowCount; ++i)
	{
		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetStringField(TEXT("name"), Packages[i].Name);
		PObj->SetNumberField(TEXT("total_serial_size_bytes"), static_cast<double>(Packages[i].TotalSerialSize));
		PObj->SetNumberField(TEXT("export_count"), static_cast<double>(Packages[i].ExportCount));
		PObj->SetNumberField(TEXT("main_thread_time_ms"), Packages[i].MainThreadTime * 1000.0);
		PObj->SetNumberField(TEXT("async_time_ms"), Packages[i].AsyncTime * 1000.0);
		PObj->SetNumberField(TEXT("total_time_ms"), (Packages[i].MainThreadTime + Packages[i].AsyncTime) * 1000.0);
		PObj->SetNumberField(TEXT("dependency_count"), Packages[i].ImportedPackages.Num());

		// Include dependency list (up to 20)
		TArray<TSharedPtr<FJsonValue>> DepsArray;
		int32 DepShowCount = FMath::Min(20, Packages[i].ImportedPackages.Num());
		for (int32 d = 0; d < DepShowCount; ++d)
		{
			DepsArray.Add(MakeShared<FJsonValueString>(Packages[i].ImportedPackages[d]));
		}
		PObj->SetArrayField(TEXT("dependencies"), DepsArray);

		PkgArray.Add(MakeShared<FJsonValueObject>(PObj));
	}
	Result->SetArrayField(TEXT("packages"), PkgArray);

	UE_LOG(LogInsightMCP, Log, TEXT("query.loadtime_deps: %d packages"), Packages.Num());

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.loadtime_deps requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.memory — Memory (LLM) tag data
// ============================================================================

bool FQueryMemoryAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryMemoryAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	FString TagFilter = GetOptionalString(Params, TEXT("tag"), TEXT(""));
	int32 TrackerId = static_cast<int32>(GetOptionalNumber(Params, TEXT("tracker_id"), 0.0));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::IMemoryProvider* MemProvider = TraceServices::ReadMemoryProvider(*Session);

	if (!MemProvider)
	{
		return CreateErrorResponse(TEXT("Memory (LLM) data not available. Ensure 'memory' channel was enabled."), TEXT("no_data"));
	}

	// Trackers info
	TArray<TSharedPtr<FJsonValue>> TrackersArray;
	MemProvider->EnumerateTrackers(
		[&TrackersArray](const TraceServices::FMemoryTrackerInfo& Tracker)
		{
			TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
			TObj->SetNumberField(TEXT("id"), Tracker.Id);
			TObj->SetStringField(TEXT("name"), Tracker.Name);
			TrackersArray.Add(MakeShared<FJsonValueObject>(TObj));
		});
	Result->SetArrayField(TEXT("trackers"), TrackersArray);
	Result->SetNumberField(TEXT("tracker_count"), TrackersArray.Num());

	// Tags info
	struct FTagInfo
	{
		TraceServices::FMemoryTagId Id;
		FString Name;
		TraceServices::FMemoryTagId ParentId;
		int64 LatestValue;
		bool bHasValue;
	};
	TArray<FTagInfo> Tags;

	MemProvider->EnumerateTags(
		[&](const TraceServices::FMemoryTagInfo& Tag)
		{
			if (!TagFilter.IsEmpty() && !Tag.Name.Contains(TagFilter))
			{
				return;
			}

			FTagInfo Info;
			Info.Id = Tag.Id;
			Info.Name = Tag.Name;
			Info.ParentId = Tag.ParentId;
			Info.LatestValue = 0;
			Info.bHasValue = false;

			// Get latest sample value for this tag
			uint64 SampleCount = MemProvider->GetTagSampleCount(
				static_cast<TraceServices::FMemoryTrackerId>(TrackerId), Tag.Id);

			if (SampleCount > 0)
			{
				// Enumerate with a very large time range to get the last sample
				MemProvider->EnumerateTagSamples(
					static_cast<TraceServices::FMemoryTrackerId>(TrackerId),
					Tag.Id,
					0.0, DBL_MAX, false,
					[&Info](double Time, double Duration, const TraceServices::FMemoryTagSample& Sample)
					{
						// Only accept finite values
						if (FMath::IsFinite(static_cast<double>(Sample.Value)))
						{
							Info.LatestValue = Sample.Value;
							Info.bHasValue = true;
						}
					});
			}

			Tags.Add(MoveTemp(Info));
		});

	// Sort by value (largest first)
	Tags.Sort([](const FTagInfo& A, const FTagInfo& B)
	{
		return FMath::Abs(A.LatestValue) > FMath::Abs(B.LatestValue);
	});

	Result->SetNumberField(TEXT("tag_count"), Tags.Num());

	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FTagInfo& Tag : Tags)
	{
		TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
		TObj->SetNumberField(TEXT("id"), static_cast<double>(Tag.Id));
		TObj->SetStringField(TEXT("name"), Tag.Name);
		TObj->SetNumberField(TEXT("parent_id"), static_cast<double>(Tag.ParentId));

		if (Tag.bHasValue)
		{
			TObj->SetNumberField(TEXT("value_bytes"), static_cast<double>(Tag.LatestValue));
			// Human-readable
			if (FMath::Abs(Tag.LatestValue) > 1024 * 1024 * 1024)
			{
				TObj->SetStringField(TEXT("value_human"),
					FString::Printf(TEXT("%.2f GB"), Tag.LatestValue / (1024.0 * 1024.0 * 1024.0)));
			}
			else if (FMath::Abs(Tag.LatestValue) > 1024 * 1024)
			{
				TObj->SetStringField(TEXT("value_human"),
					FString::Printf(TEXT("%.2f MB"), Tag.LatestValue / (1024.0 * 1024.0)));
			}
			else if (FMath::Abs(Tag.LatestValue) > 1024)
			{
				TObj->SetStringField(TEXT("value_human"),
					FString::Printf(TEXT("%.2f KB"), Tag.LatestValue / 1024.0));
			}
			else
			{
				TObj->SetStringField(TEXT("value_human"),
					FString::Printf(TEXT("%lld B"), Tag.LatestValue));
			}
		}

		TagsArray.Add(MakeShared<FJsonValueObject>(TObj));
	}
	Result->SetArrayField(TEXT("tags"), TagsArray);

	if (!TagFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("filter"), TagFilter);
	}

	UE_LOG(LogInsightMCP, Log, TEXT("query.memory: %d tags, %d trackers"),
		Tags.Num(), TrackersArray.Num());

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.memory requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.counters — Named counter data
// ============================================================================

bool FQueryCountersAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryCountersAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	FString NameFilter = GetOptionalString(Params, TEXT("filter"), TEXT(""));
	FString GroupFilter = GetOptionalString(Params, TEXT("group"), TEXT(""));
	bool bIncludeValues = GetOptionalBool(Params, TEXT("include_values"), false);
	int32 MaxSamples = static_cast<int32>(GetOptionalNumber(Params, TEXT("max_samples"), 100.0));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::ICounterProvider& CounterProvider = TraceServices::ReadCounterProvider(*Session);

	uint64 TotalCounters = CounterProvider.GetCounterCount();
	Result->SetNumberField(TEXT("total_counters"), static_cast<double>(TotalCounters));

	TArray<TSharedPtr<FJsonValue>> CountersArray;
	int32 MatchedCount = 0;

	CounterProvider.EnumerateCounters(
		[&](uint32 CounterId, const TraceServices::ICounter& Counter)
		{
			FString CounterName(Counter.GetName());
			FString CounterGroup(Counter.GetGroup());

			// Apply filters
			if (!NameFilter.IsEmpty() && !CounterName.Contains(NameFilter))
			{
				return;
			}
			if (!GroupFilter.IsEmpty() && !CounterGroup.Contains(GroupFilter))
			{
				return;
			}

			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetNumberField(TEXT("id"), static_cast<double>(CounterId));
			CObj->SetStringField(TEXT("name"), CounterName);
			CObj->SetStringField(TEXT("group"), CounterGroup);
			CObj->SetStringField(TEXT("description"), Counter.GetDescription());
			CObj->SetBoolField(TEXT("is_floating_point"), Counter.IsFloatingPoint());
			CObj->SetBoolField(TEXT("is_reset_every_frame"), Counter.IsResetEveryFrame());

			FString DisplayHint;
			switch (Counter.GetDisplayHint())
			{
			case TraceServices::CounterDisplayHint_Memory: DisplayHint = TEXT("memory"); break;
			default: DisplayHint = TEXT("none"); break;
			}
			CObj->SetStringField(TEXT("display_hint"), DisplayHint);

			// Optionally include sample values
			if (bIncludeValues)
			{
				TArray<TSharedPtr<FJsonValue>> ValuesArray;
				int32 SampleIdx = 0;

				if (Counter.IsFloatingPoint())
				{
					Counter.EnumerateFloatValues(0.0, DBL_MAX, false,
						[&ValuesArray, &SampleIdx, MaxSamples](double Time, double Value)
						{
							if (SampleIdx >= MaxSamples) return;
							TSharedPtr<FJsonObject> VObj = MakeShared<FJsonObject>();
							VObj->SetNumberField(TEXT("time"), FMath::IsFinite(Time) ? Time : 0.0);
							VObj->SetNumberField(TEXT("value"), FMath::IsFinite(Value) ? Value : 0.0);
							ValuesArray.Add(MakeShared<FJsonValueObject>(VObj));
							++SampleIdx;
						});
				}
				else
				{
					Counter.EnumerateValues(0.0, DBL_MAX, false,
						[&ValuesArray, &SampleIdx, MaxSamples](double Time, int64 Value)
						{
							if (SampleIdx >= MaxSamples) return;
							TSharedPtr<FJsonObject> VObj = MakeShared<FJsonObject>();
							VObj->SetNumberField(TEXT("time"), FMath::IsFinite(Time) ? Time : 0.0);
							VObj->SetNumberField(TEXT("value"), static_cast<double>(Value));
							ValuesArray.Add(MakeShared<FJsonValueObject>(VObj));
							++SampleIdx;
						});
				}

				CObj->SetArrayField(TEXT("values"), ValuesArray);
				CObj->SetNumberField(TEXT("sample_count"), ValuesArray.Num());
			}

			CountersArray.Add(MakeShared<FJsonValueObject>(CObj));
			++MatchedCount;
		});

	Result->SetNumberField(TEXT("matched_counters"), MatchedCount);
	Result->SetArrayField(TEXT("counters"), CountersArray);

	if (!NameFilter.IsEmpty()) Result->SetStringField(TEXT("name_filter"), NameFilter);
	if (!GroupFilter.IsEmpty()) Result->SetStringField(TEXT("group_filter"), GroupFilter);

	UE_LOG(LogInsightMCP, Log, TEXT("query.counters: %d/%llu counters matched"),
		MatchedCount, TotalCounters);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.counters requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.bookmarks — Bookmark data
// ============================================================================

bool FQueryBookmarksAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryBookmarksAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;
	FString TextFilter = GetOptionalString(Params, TEXT("filter"), TEXT(""));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::IBookmarkProvider& BookmarkProvider = TraceServices::ReadBookmarkProvider(*Session);

	uint64 TotalBookmarks = BookmarkProvider.GetBookmarkCount();
	Result->SetNumberField(TEXT("total_bookmarks"), static_cast<double>(TotalBookmarks));

	TArray<TSharedPtr<FJsonValue>> BookmarksArray;

	BookmarkProvider.EnumerateBookmarks(0.0, DBL_MAX,
		[&](const TraceServices::FBookmark& Bookmark)
		{
			FString Text(Bookmark.Text);

			// Apply text filter
			if (!TextFilter.IsEmpty() && !Text.Contains(TextFilter))
			{
				return;
			}

			TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
			BObj->SetStringField(TEXT("text"), Text);
			double SafeTime = FMath::IsFinite(Bookmark.Time) ? Bookmark.Time : 0.0;
			BObj->SetNumberField(TEXT("time_seconds"), SafeTime);
			BObj->SetNumberField(TEXT("time_ms"), SafeTime * 1000.0);

			BookmarksArray.Add(MakeShared<FJsonValueObject>(BObj));
		});

	Result->SetNumberField(TEXT("matched_bookmarks"), BookmarksArray.Num());
	Result->SetArrayField(TEXT("bookmarks"), BookmarksArray);

	if (!TextFilter.IsEmpty())
	{
		Result->SetStringField(TEXT("filter"), TextFilter);
	}

	UE_LOG(LogInsightMCP, Log, TEXT("query.bookmarks: %d bookmarks"), BookmarksArray.Num());

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.bookmarks requires UE5"), TEXT("unsupported"));
#endif
}


// ============================================================================
// query.hitches — Detected hitches (frames exceeding threshold)
// ============================================================================

bool FQueryHitchesAction::Validate(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context,
	FString& OutError)
{
	return ValidateSessionLoaded(Context, OutError);
}

TSharedPtr<FJsonObject> FQueryHitchesAction::ExecuteInternal(
	const TSharedPtr<FJsonObject>& Params,
	FInsightContext& Context)
{
#if ENGINE_MAJOR_VERSION >= 5
	const auto& Session = Context.AnalysisSession;

	// Hitch threshold in milliseconds (default 33.33ms = below 30fps)
	double ThresholdMs = GetOptionalNumber(Params, TEXT("threshold_ms"), 33.33);
	int32 Limit = static_cast<int32>(GetOptionalNumber(Params, TEXT("limit"), 50.0));
	FString FrameTypeStr = GetOptionalString(Params, TEXT("frame_type"), TEXT("game"));

	ETraceFrameType FrameType = FrameTypeStr == TEXT("render")
		? TraceFrameType_Rendering
		: TraceFrameType_Game;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("threshold_ms"), ThresholdMs);
	Result->SetStringField(TEXT("frame_type"), FrameTypeStr);

	TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
	const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);

	uint64 TotalFrames = FrameProvider.GetFrameCount(FrameType);

	struct FHitch
	{
		uint64 FrameIndex;
		double StartTime;
		double DurationMs;
	};
	TArray<FHitch> Hitches;

	double ThresholdSec = ThresholdMs / 1000.0;

	FrameProvider.EnumerateFrames(FrameType, (uint64)0, TotalFrames,
		[&](const TraceServices::FFrame& Frame)
		{
			double Duration = Frame.EndTime - Frame.StartTime;
			// Filter out invalid frames (inf, NaN, unreasonably long)
			if (Duration >= ThresholdSec && FMath::IsFinite(Duration) && Duration < 60.0)
			{
				Hitches.Add({Frame.Index, Frame.StartTime, Duration * 1000.0});
			}
		});

	// Sort by duration (worst first)
	Hitches.Sort([](const FHitch& A, const FHitch& B)
	{
		return A.DurationMs > B.DurationMs;
	});

	Result->SetNumberField(TEXT("total_frames"), static_cast<double>(TotalFrames));
	Result->SetNumberField(TEXT("hitch_count"), Hitches.Num());

	if (TotalFrames > 0)
	{
		Result->SetNumberField(TEXT("hitch_percentage"), (Hitches.Num() * 100.0) / TotalFrames);
	}

	// Summary stats
	if (Hitches.Num() > 0)
	{
		double TotalHitchTime = 0.0;
		for (const FHitch& H : Hitches)
		{
			TotalHitchTime += H.DurationMs;
		}

		TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("total_hitch_time_ms"), TotalHitchTime);
		Summary->SetNumberField(TEXT("worst_hitch_ms"), Hitches[0].DurationMs);
		Summary->SetNumberField(TEXT("avg_hitch_ms"), TotalHitchTime / Hitches.Num());
		Result->SetObjectField(TEXT("summary"), Summary);
	}

	// Hitch details
	int32 ShowCount = FMath::Min(Limit, Hitches.Num());
	TArray<TSharedPtr<FJsonValue>> HitchesArray;
	for (int32 i = 0; i < ShowCount; ++i)
	{
		TSharedPtr<FJsonObject> HObj = MakeShared<FJsonObject>();
		HObj->SetNumberField(TEXT("frame_index"), static_cast<double>(Hitches[i].FrameIndex));
		HObj->SetNumberField(TEXT("start_time_seconds"), Hitches[i].StartTime);
		HObj->SetNumberField(TEXT("duration_ms"), Hitches[i].DurationMs);
		HObj->SetNumberField(TEXT("fps"), 1000.0 / Hitches[i].DurationMs);
		HitchesArray.Add(MakeShared<FJsonValueObject>(HObj));
	}
	Result->SetArrayField(TEXT("hitches"), HitchesArray);

	UE_LOG(LogInsightMCP, Log, TEXT("query.hitches: %d hitches out of %llu frames (threshold=%.1fms)"),
		Hitches.Num(), TotalFrames, ThresholdMs);

	return CreateSuccessResponse(Result);

#else
	return CreateErrorResponse(TEXT("query.hitches requires UE5"), TEXT("unsupported"));
#endif
}
