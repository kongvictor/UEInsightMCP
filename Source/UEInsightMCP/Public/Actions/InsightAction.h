// UEInsightMCP — InsightAction: base class for all Insight actions

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "../InsightContext.h"
#include "../InsightLogCapture.h"

/**
 * FInsightAction
 *
 * Base class for all Insight MCP actions. Provides a unified execution
 * pipeline with validation, crash protection.
 *
 * Subclasses override:
 * - Validate(): Check parameters and preconditions
 * - ExecuteInternal(): Perform the actual operation
 * - GetActionName(): Return action identifier
 * - RequiresSave(): Whether to auto-save on success (default: false for Trace ops)
 */
class UEINSIGHTMCP_API FInsightAction
{
public:
	virtual ~FInsightAction() = default;

	/**
	 * Execute the action with full pipeline.
	 * Handles validation, crash protection.
	 */
	TSharedPtr<FJsonObject> Execute(
		const TSharedPtr<FJsonObject>& Params,
		FInsightContext& Context);

	/**
	 * Execute the action (called after validation).
	 * Public for SEH wrapper access on Windows.
	 */
	virtual TSharedPtr<FJsonObject> ExecuteInternal(
		const TSharedPtr<FJsonObject>& Params,
		FInsightContext& Context) = 0;

	/**
	 * Whether this action must execute on the game thread.
	 * Default: true (safe for editor-touching actions).
	 * Override to false for offline analysis actions (session / query)
	 * that only read .utrace files and never touch editor/engine state.
	 */
	virtual bool RequiresGameThread() const { return true; }

protected:
	virtual bool Validate(
		const TSharedPtr<FJsonObject>& Params,
		FInsightContext& Context,
		FString& OutError) = 0;

	virtual bool PostValidate(FInsightContext& Context, FString& OutError) { return true; }
	virtual FString GetActionName() const = 0;
	virtual bool RequiresSave() const { return false; }  // Trace actions don't auto-save

	// ── Helper Methods ──
	TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData = nullptr) const;
	TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType = TEXT("error")) const;
	TSharedPtr<FJsonObject> CreateCrashPreventedResponse() const;

	bool GetRequiredString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, FString& OutValue, FString& OutError) const;
	FString GetOptionalString(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, const FString& Default = TEXT("")) const;
	const TArray<TSharedPtr<FJsonValue>>* GetOptionalArray(const TSharedPtr<FJsonObject>& Params, const FString& ParamName) const;
	double GetOptionalNumber(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, double Default = 0.0) const;
	bool GetOptionalBool(const TSharedPtr<FJsonObject>& Params, const FString& ParamName, bool Default = false) const;

private:
	TSharedPtr<FJsonObject> ExecuteWithCrashProtection(
		const TSharedPtr<FJsonObject>& Params,
		FInsightContext& Context);
};


/**
 * FTraceControlAction
 *
 * Base class for Trace control actions (start/stop/status/channels).
 * No extra state needed beyond FInsightAction + context.
 */
class UEINSIGHTMCP_API FTraceControlAction : public FInsightAction
{
protected:
	// Trace control actions don't need auto-save
	virtual bool RequiresSave() const override { return false; }
};


/**
 * FTraceSessionAction
 *
 * Base class for Phase 2 session management actions (session.list/open/info/close).
 * UE5 only — provides access to TraceServices analysis infrastructure.
 */
class UEINSIGHTMCP_API FTraceSessionAction : public FInsightAction
{
protected:
	virtual bool RequiresSave() const override { return false; }

	/** Session actions work on offline .utrace data — no game thread needed */
	virtual bool RequiresGameThread() const override { return false; }

	/** Validate that we're running on UE5 (Phase 2 features require TraceServices) */
	bool ValidateUE5(FString& OutError) const
	{
#if ENGINE_MAJOR_VERSION >= 5
		return true;
#else
		OutError = TEXT("Session/query actions require UE5. Not available on UE4.");
		return false;
#endif
	}
};


/**
 * FTraceQueryAction
 *
 * Base class for Phase 2 data query actions (query.*).
 * Requires an active analysis session. UE5 only.
 */
class UEINSIGHTMCP_API FTraceQueryAction : public FInsightAction
{
protected:
	virtual bool RequiresSave() const override { return false; }

	/** Query actions work on offline analysis data — no game thread needed */
	virtual bool RequiresGameThread() const override { return false; }

	/** Common validation: UE5 required + session must be loaded */
	bool ValidateSessionLoaded(FInsightContext& Context, FString& OutError) const
	{
#if ENGINE_MAJOR_VERSION < 5
		OutError = TEXT("Query actions require UE5. Not available on UE4.");
		return false;
#else
		if (!Context.bSessionLoaded || !Context.AnalysisSession.IsValid())
		{
			OutError = TEXT("No analysis session loaded. Call session.open first.");
			return false;
		}
		return true;
#endif
	}

	// ── Float Sanitization (trace data often contains inf/NaN) ──

	/** Sanitize a float for JSON output. Returns Fallback for inf/NaN. */
	static double SanitizeFloat(double Value, double Fallback = 0.0)
	{
		return FMath::IsFinite(Value) ? Value : Fallback;
	}

	/** Returns true if Value is finite and usable in calculations. */
	static bool IsValidFloat(double Value)
	{
		return FMath::IsFinite(Value);
	}

	/** SetNumberField with automatic inf/NaN sanitization. */
	static void SafeSetNumber(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Value, double Fallback = 0.0)
	{
		Obj->SetNumberField(Key, SanitizeFloat(Value, Fallback));
	}

};
