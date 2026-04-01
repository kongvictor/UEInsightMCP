// UEInsightMCP — Trace Session Actions (Phase 2)
// Session management: list/open/info/close .utrace files for analysis

#pragma once

#include "InsightAction.h"

// ============================================================================
// session.list — List available .utrace files
// ============================================================================
class UEINSIGHTMCP_API FSessionListAction : public FTraceSessionAction
{
public:
	virtual FString GetActionName() const override { return TEXT("session.list"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// session.open — Open a .utrace file for analysis
// ============================================================================
class UEINSIGHTMCP_API FSessionOpenAction : public FTraceSessionAction
{
public:
	virtual FString GetActionName() const override { return TEXT("session.open"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// session.info — Get info about the currently loaded analysis session
// ============================================================================
class UEINSIGHTMCP_API FSessionInfoAction : public FTraceSessionAction
{
public:
	virtual FString GetActionName() const override { return TEXT("session.info"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};

// ============================================================================
// session.close — Close the current analysis session
// ============================================================================
class UEINSIGHTMCP_API FSessionCloseAction : public FTraceSessionAction
{
public:
	virtual FString GetActionName() const override { return TEXT("session.close"); }

protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context, FString& OutError) override;
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FInsightContext& Context) override;
};
