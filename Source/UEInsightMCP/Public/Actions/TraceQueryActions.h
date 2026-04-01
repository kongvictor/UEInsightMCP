// UEInsightMCP — Trace Query Actions (Phase 2)
// Data query actions: frame_times, cpu_threads, gpu_timing, loadtime,
// loadtime_deps, memory, counters, bookmarks, hitches

#pragma once

#include "InsightAction.h"

// ============================================================================
// query.frame_times — Frame time statistics
// ============================================================================
class UEINSIGHTMCP_API FQueryFrameTimesAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.frame_times"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.cpu_threads — CPU thread timing summary
// ============================================================================
class UEINSIGHTMCP_API FQueryCpuThreadsAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.cpu_threads"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.gpu_timing — GPU timing data
// ============================================================================
class UEINSIGHTMCP_API FQueryGpuTimingAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.gpu_timing"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.loadtime — Asset loading time data
// ============================================================================
class UEINSIGHTMCP_API FQueryLoadTimeAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.loadtime"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.loadtime_deps — Asset loading dependency graph
// ============================================================================
class UEINSIGHTMCP_API FQueryLoadTimeDepsAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.loadtime_deps"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.memory — Memory (LLM) tag data
// ============================================================================
class UEINSIGHTMCP_API FQueryMemoryAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.memory"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.counters — Named counter data
// ============================================================================
class UEINSIGHTMCP_API FQueryCountersAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.counters"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.bookmarks — Bookmark (TRACE_BOOKMARK) data
// ============================================================================
class UEINSIGHTMCP_API FQueryBookmarksAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.bookmarks"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// query.hitches — Detected hitches (frames exceeding threshold)
// ============================================================================
class UEINSIGHTMCP_API FQueryHitchesAction : public FTraceQueryAction
{
public:
	virtual FString GetActionName() const override { return TEXT("query.hitches"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};
