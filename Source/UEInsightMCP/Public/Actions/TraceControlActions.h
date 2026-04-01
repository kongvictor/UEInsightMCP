// UEInsightMCP — Trace Control Actions

#pragma once

#include "InsightAction.h"

// ============================================================================
// trace.start — Start Trace recording
// ============================================================================
class UEINSIGHTMCP_API FTraceStartAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.start"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// trace.stop — Stop Trace recording
// ============================================================================
class UEINSIGHTMCP_API FTraceStopAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.stop"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// trace.status — Get current Trace status
// ============================================================================
class UEINSIGHTMCP_API FTraceStatusAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.status"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override
	{
		return true; // No preconditions
	}
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// trace.channels.list — List all available Trace channels
// ============================================================================
class UEINSIGHTMCP_API FTraceChannelsListAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.channels.list"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override
	{
		return true;
	}
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// trace.channels.toggle — Enable/disable specific Trace channels
// ============================================================================
class UEINSIGHTMCP_API FTraceChannelsToggleAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.channels.toggle"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// trace.bookmark.add — Add a named bookmark at current time
// ============================================================================
class UEINSIGHTMCP_API FTraceBookmarkAction : public FTraceControlAction
{
public:
	virtual FString GetActionName() const override { return TEXT("trace.bookmark.add"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// batch_execute — Execute multiple commands in a single TCP request
// ============================================================================
class UEINSIGHTMCP_API FBatchExecuteInsightAction : public FInsightAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("batch_execute"); }

private:
	static constexpr int32 MaxBatchSize = 50;
};
